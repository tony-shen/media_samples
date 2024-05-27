/*
 * Copyright 2021 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "rtsp_demo.h"
#include "sample_comm.h"

#define CAM_NUM_MAX 8
#define VENC_CHN_MAX 7
#define VENC_MAIN_STREAM_NUM 2
#define VENC_SUB_STREAM_NUM 5
#define RGN_NUM_MAX 5
#define TDE_NUM_MAX 5
#define MPI_MOD_GET_FRAME_TIMEOUT 2000
#define MPI_MOD_SEND_FRAME_TIMEOUT 1000
#define MAIN_VENC_CHNID 0
#define JPEG_VENC_CHNID 1
#define MODULE_TEST_DELAY_SECOND_TIME 3 //(unit: second)

typedef struct _rkThreadManage {
	RK_BOOL main_thread_quit;
	RK_BOOL tde_thread_quit;
	RK_BOOL vpss_thread_quit;
	RK_BOOL avs_thread_quit;
	RK_BOOL vi_thread_quit;
	RK_BOOL avs_post_sem_ifexit;
	RK_BOOL module_test_quit;
	RK_BOOL send_frame_thread_quit;
	RK_BOOL venc_thread_quit[VENC_CHN_MAX];
} g_thread_manage_t;

g_thread_manage_t *g_thread_manage;

typedef enum rk_AVS_STREAM_SOURCE_TYPE_E {
	STREAM_FROM_ISP_CAMERA,
	STREAM_FROM_FILE
} AVS_STREAM_SOURCE_TYPE_E;

typedef struct _rkModeTest {
	RK_BOOL module_test_ifopen;
	AVS_STREAM_SOURCE_TYPE_E avs_stream_source;
	COMPRESS_MODE_E source_compress_mode;
	RK_CHAR *pCAvsInputPathFolder;
	RK_S32 module_test_type;
	RK_S32 module_test_loop;
	RK_S32 venc_getframe_count[VENC_CHN_MAX];
	RK_S32 test_frame_count;
	RK_S32 s32CamId;
	RK_S32 avs_pipe_num;
	RK_U32 u32AvsPipeWidth;
	RK_U32 u32AvsPipeHeight;
	VIDEO_FRAME_INFO_S pipe_frames[CAM_NUM_MAX];
	rk_aiq_working_mode_t hdr_mode;
	rk_aiq_camgroup_instance_cfg_t camgroup_cfg;
} g_mode_test_t;

g_mode_test_t *g_mode_test;

RK_S32 g_exit_result = 0;
pthread_mutex_t g_rtsp_tx_mutex;
pthread_mutex_t g_frame_count_mutex[VENC_CHN_MAX];
sem_t g_sem_test_switch_start[VENC_CHN_MAX];
sem_t g_sem_tde_wait_task[TDE_NUM_MAX];
sem_t g_sem_tde_handle_taskdone[TDE_NUM_MAX];
RK_BOOL g_rtsp_ifopen = RK_FALSE;
rtsp_demo_handle g_rtsplive = RK_NULL;
rtsp_session_handle g_rtsp_session[VENC_CHN_MAX] = {RK_NULL};

typedef struct _rkMpiTDECtx {
	RK_U32 s32ChnId;
	RK_U32 buffersize;
	TDE_HANDLE hHandle;
	TDE_SURFACE_S pstSrc;
	TDE_RECT_S pstSrcRect;
	TDE_SURFACE_S pstDst;
	TDE_RECT_S pstDstRect;
	MB_POOL tde_frame_pool;
	VIDEO_FRAME_INFO_S stVideoFrames;
	pthread_t tde_task_handle_id;
} SAMPLE_TDE_CTX_S;

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi[CAM_NUM_MAX];
	SAMPLE_AVS_CTX_S avs;
	SAMPLE_RGN_CTX_S rgn[RGN_NUM_MAX];
	SAMPLE_VPSS_CTX_S vpss;
	SAMPLE_VENC_CTX_S main_venc[VENC_MAIN_STREAM_NUM];
	SAMPLE_VENC_CTX_S sub_venc[VENC_SUB_STREAM_NUM];
	SAMPLE_TDE_CTX_S tde[TDE_NUM_MAX];
} SAMPLE_MPI_CTX_S;

SAMPLE_MPI_CTX_S *ctx;

static void program_error_handle(const char *pdata) {
	RK_LOGE("func: %s sponsor exit", pdata);
	g_thread_manage->main_thread_quit = RK_TRUE;
	g_exit_result = RK_FAILURE;
}

static void program_normal_exit(const char *pdata) {
	RK_LOGE("func: %s sponsor normal exit", pdata);
	g_thread_manage->main_thread_quit = RK_TRUE;
}

static void sigterm_handler(RK_S32 sig) {
	fprintf(stderr, "signal %d\n", sig);
	program_normal_exit(__func__);
}

static RK_CHAR optstr[] = "?::a::A:n:b:w:h:l:o:e:i:M:x:y:X:Y:s:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"avs_stitch_file_path", required_argument, NULL, 'A'},
    {"camera_num", required_argument, NULL, 'n'},
    {"bitrate", required_argument, NULL, 'b'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"loop_count", required_argument, NULL, 'l'},
    {"output_path", required_argument, NULL, 'o'},
    {"encode", required_argument, NULL, 'e'},
    {"input_bmp_file", required_argument, NULL, 'i'},
    {"avs_stitch_mode", required_argument, NULL, 'M'},
    {"avs_centerx", required_argument, NULL, 'x'},
    {"avs_centery", required_argument, NULL, 'y'},
    {"avs_fovx", required_argument, NULL, 'X'},
    {"avs_fovy", required_argument, NULL, 'Y'},
    {"avs_bsync_pipe", required_argument, NULL, 's'},
    {"avs_stream_source", required_argument, NULL, 'a' + 's'},
    {"avs_stream_file_path", required_argument, NULL, 'f' + 'p'},
    {"avs_stream_file_compress_format", required_argument, NULL, 'f' + 'f'},
    {"vpss_proc_devtype", required_argument, NULL, 'v' + 'p'},
    {"main_venc_width", required_argument, NULL, 'm' + 'w'},
    {"main_venc_hight", required_argument, NULL, 'm' + 'h'},
    {"main_venc_src_fps", required_argument, NULL, 'm' + 's'},
    {"main_venc_dst_fps", required_argument, NULL, 'm' + 'd'},
    {"sub_venc_width", required_argument, NULL, 's' + 'w'},
    {"sub_venc_hight", required_argument, NULL, 's' + 'h'},
    {"sub_venc_dst_fps", required_argument, NULL, 's' + 'd'},
    {"sub_venc_src_fps", required_argument, NULL, 's' + 'f'},
    {"jpeg_venc_width", required_argument, NULL, 'j' + 'w'},
    {"jpeg_venc_height", required_argument, NULL, 'j' + 'h'},
    {"jpeg_fps", required_argument, NULL, 'j' + 'f'},
    {"module_test_type", required_argument, NULL, 't' + 't'},
    {"module_test_loop", required_argument, NULL, 't' + 'o'},
    {"test_frame_count", required_argument, NULL, 'f' + 'c'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -w 8160 -h 3616 -a /etc/iqfiles/ -n 6 -e h265cbr\n", name);
	printf("\trtsp://xx.xx.xx.xx/live/0, Default OPEN\n");
#ifdef RKAIQ
	printf("\t-a | --aiq : enable aiq with dirpath provided, eg:-a "
	       "/etc/iqfiles/ <Require Parameter>\n");
#endif
	printf(
	    "\t-A | --avs_stitch_file_path : input file path of avs_stitch_file xxx.pto or "
	    "xxx.bin "
	    "Default /oem/usr/share/avs_calib/calib_file.pto\n \
		           if stitch file is middle_lut file, please input folder like /xxx/  <Require Parameter>\n");
	printf("\t-n | --camera_num : camera number, Default 6  <Require Parameter>\n");
	printf("\t-b | --bitrate : encode bitrate, Default 4096\n");
	printf("\t-w | --width : avs stitch output, Default 8160\n");
	printf("\t-h | --height : avs stitch output, Default 3616\n");
	printf("\t-l | --loop_count : venc get frame loop count, Default -1\n");
	printf("\t-o | --output_path : avs output file path, Default:NULL\n");
	printf("\t-e | --encode : encode type, Default:h264cbr, Value:h264cbr, "
	       "h264vbr, h264avbr "
	       "h265cbr, h265vbr, h265avbr, mjpegcbr, mjpegvbr\n");
	printf("\t-i | --input_bmp_file : input bmp file path, size require 608x288, "
	       "Default: NULL\n");
	printf("\t-M | --avs_stitch_mode : 0: calib file; 1: lut file, Default: 0  <Require "
	       "Parameter>\n");
	printf("\t--avs_centerx : avs Center X value, Default:4196\n");
	printf("\t--avs_centery : avs Center Y value, Default:2080\n");
	printf("\t--avs_fovx : avs Fox X value, Default:28000\n");
	printf("\t--avs_fovy : avs Fox Y value, Default:9500\n");
	printf("\t--avs_bsync_pipe : avs pipe mode: 0: nonsync; 1: sync; Default:0\n");
	printf("\t--avs_stream_source : avs stream data source, 0: camera, 1: specified nv12 "
	       "format's file. default: 0\n");
	printf("\t--avs_stream_file_path : input the yuv file folder path, the yuv's file "
	       "format require: vi_x.bin. default: NULL\n");
	printf("\t--avs_stream_file_compress_format : stream file compress format. 0: none, "
	       "1: afbc. default: 0\n");
	printf("\t--vpss_proc_devtype : vpss dev type; 0: RGA; 1: GPU, Default:0\n");
	printf("\t--main_venc_width : Main Venc Width value, Default:3840\n");
	printf("\t--main_venc_hight : Main Venc hight value, Default:2160\n");
	printf("\t--main_venc_src_fps : Main Venc Src Fps, Default: 25\n");
	printf("\t--main_venc_dst_fps : Main Venc Dst Fps, Default: 25\n");
	printf("\t--sub_venc_width : Sub Venc Width value, Default:1920\n");
	printf("\t--sub_venc_hight : Sub Venc Height value, Default:1080\n");
	printf("\t--sub_venc_dst_fps : Sub Venc Dst Fps, Default: 25\n");
	printf("\t--sub_venc_src_fps : Sub Venc Src Fps, Default: 25 \n");
	printf("\t--jpeg_venc_width : jpeg Venc Width value, Default:1920\n");
	printf("\t--jpeg_venc_height : jpeg Venc Height value, Default:1080\n");
	printf("\t--jpeg_fps : Jpeg Fps value, Default:1\n");
	printf("\t--module_test_type : module test type 0: ordinary stream 1: P/N mode "
	       "switch 2: avs dpi switch 3: main stream dpi switch default: 0\n");
	printf("\t--module_test_loop : module test loop, default: -1\n");
	printf("\t--test_frame_count : set the venc reveive frame count for every test loop, "
	       "Default: 500\n");
}

static void *vi_avs_thread(void *pArgs) {
	RK_S32 s32Ret = RK_SUCCESS;
	RK_S32 i = 0;
	prctl(PR_SET_NAME, "sample_vi_avs");

	while (!g_thread_manage->vi_thread_quit) {
		s32Ret = RK_SUCCESS;
		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			s32Ret |=
			    RK_MPI_VI_GetChnFrame(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId,
			                          &ctx->vi[i].stViFrame, MPI_MOD_GET_FRAME_TIMEOUT);
			RK_LOGD("vi dev %d   seq: %d ", ctx->vi[i].s32DevId,
			        ctx->vi[i].stViFrame.stVFrame.u32TimeRef);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_GetChnFrame failure dev %d s32Ret "
				        "%#X",
				        ctx->vi[i].s32DevId, s32Ret);
				break;
			}
		}
		if (s32Ret == RK_SUCCESS) {
			for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
				s32Ret =
				    RK_MPI_AVS_SendPipeFrame(ctx->avs.s32GrpId, i, &ctx->vi[i].stViFrame,
				                             MPI_MOD_SEND_FRAME_TIMEOUT);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("RK_MPI_AVS_SendPipeFrame failure dev %d "
					        "s32Ret %#X",
					        ctx->vi[i].s32DevId, s32Ret);
					i = ctx->avs.stAvsGrpAttr.u32PipeNum;
					break;
				}
			}
		}

		for (i = i - 1; i >= 0; i--) {
			s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId,
			                                   &ctx->vi[i].stViFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_ReleaseChnFrame failure dev %d s32Ret %#X",
				        ctx->vi[i].s32DevId, s32Ret);
				program_error_handle(__func__);
				continue;
			}
		}
	}

	RK_LOGE("vi_avs_thread exit");
	return NULL;
}

static void tde_error_handle(const char *func, RK_S32 tde_chnid, MB_BLK mb,
                             RK_BOOL ifrelease) {
	sem_post(&g_sem_tde_handle_taskdone[tde_chnid]);
	if (ifrelease && mb) {
		RK_MPI_MB_ReleaseMB(mb);
	}
	program_error_handle(func);
}

static void *tde_task_hangle(void *pArgs) {
	SAMPLE_TDE_CTX_S *tde_ctx = (SAMPLE_TDE_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	prctl(PR_SET_NAME, "sample_tde_handle");
	RK_LOGE("tde_task_hangle init finish %d", tde_ctx->s32ChnId);
	while (!g_thread_manage->tde_thread_quit) {
		sem_wait(&g_sem_tde_wait_task[tde_ctx->s32ChnId]);
		if (g_thread_manage->avs_post_sem_ifexit) {
			continue;
		}
		tde_ctx->stVideoFrames.stVFrame.pMbBlk =
		    RK_MPI_MB_GetMB(tde_ctx->tde_frame_pool, tde_ctx->buffersize, RK_TRUE);
		if (tde_ctx->stVideoFrames.stVFrame.pMbBlk == MB_INVALID_HANDLE) {
			RK_LOGE("RK_MPI_MB_GetMB Failure pMbBlk is MB_INVALID_HANDLE tde:%d",
			        tde_ctx->s32ChnId);
			tde_error_handle(__func__, tde_ctx->s32ChnId, RK_NULL, RK_FALSE);
			continue;
		}
		tde_ctx->hHandle = RK_TDE_BeginJob();
		if (RK_ERR_TDE_INVALID_HANDLE == tde_ctx->hHandle) {
			RK_LOGE("RK_TDE_BeginJob Failure  chnid%d", tde_ctx->s32ChnId);
			tde_error_handle(__func__, tde_ctx->s32ChnId,
			                 tde_ctx->stVideoFrames.stVFrame.pMbBlk, RK_TRUE);
			continue;
		}

		tde_ctx->pstSrc.pMbBlk = ctx->avs.stVideoFrame.stVFrame.pMbBlk;
		tde_ctx->pstDst.pMbBlk = tde_ctx->stVideoFrames.stVFrame.pMbBlk;
		tde_ctx->stVideoFrames.stVFrame.u32TimeRef =
		    ctx->avs.stVideoFrame.stVFrame.u32TimeRef;
		tde_ctx->stVideoFrames.stVFrame.u64PTS = ctx->avs.stVideoFrame.stVFrame.u64PTS;

		s32Ret =
		    RK_TDE_QuickCopy(tde_ctx->hHandle, &tde_ctx->pstSrc, &tde_ctx->pstSrcRect,
		                     &tde_ctx->pstDst, &tde_ctx->pstDstRect);

		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_TDE_QuickCopy Failure %#X chnid: %d", s32Ret, tde_ctx->s32ChnId);
			RK_TDE_CancelJob(tde_ctx->hHandle);
			tde_error_handle(__func__, tde_ctx->s32ChnId,
			                 tde_ctx->stVideoFrames.stVFrame.pMbBlk, RK_TRUE);
			continue;
		}
		s32Ret = RK_TDE_EndJob(tde_ctx->hHandle, RK_FALSE, RK_TRUE, -1);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_TDE_EndJob Failure %#X  chnid: %d", s32Ret, tde_ctx->s32ChnId);
			RK_TDE_CancelJob(tde_ctx->hHandle);
			tde_error_handle(__func__, tde_ctx->s32ChnId,
			                 tde_ctx->stVideoFrames.stVFrame.pMbBlk, RK_TRUE);
			continue;
		}
		s32Ret = RK_TDE_WaitForDone(tde_ctx->hHandle);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_TDE_WaitForDone Failure s32Ret: %#X  chnid:%d", s32Ret,
			        tde_ctx->s32ChnId);
			tde_error_handle(__func__, tde_ctx->s32ChnId,
			                 tde_ctx->stVideoFrames.stVFrame.pMbBlk, RK_TRUE);
			continue;
		}

		s32Ret =
		    RK_MPI_VENC_SendFrame(ctx->sub_venc[tde_ctx->s32ChnId].s32ChnId,
		                          &tde_ctx->stVideoFrames, MPI_MOD_SEND_FRAME_TIMEOUT);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VENC_SendFrame Failure venc chnid: %d s32Ret: %#X",
			        ctx->sub_venc[tde_ctx->s32ChnId].s32ChnId, s32Ret);
		}

		s32Ret = RK_MPI_MB_ReleaseMB(tde_ctx->stVideoFrames.stVFrame.pMbBlk);
		if (s32Ret != RK_SUCCESS) {
			program_error_handle(__func__);
			RK_LOGE("RK_MPI_MB_ReleaseMB Failure tde:%d s32Ret: %#X", tde_ctx->s32ChnId,
			        s32Ret);
		}
		sem_post(&g_sem_tde_handle_taskdone[tde_ctx->s32ChnId]);
	}

	s32Ret = RK_MPI_MB_DestroyPool(tde_ctx->tde_frame_pool);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_MB_DestroyPool Failure s32Ret: %#X pool:%d", s32Ret,
		        tde_ctx->tde_frame_pool);
	}
	RK_LOGE("tde_task_hangle exit  tde chnid: %d", tde_ctx->s32ChnId);
	return NULL;
}

static void *avs_vpss_thread(void *pArgs) {
	RK_S32 s32Ret = RK_SUCCESS;
	RK_U32 i = 0;
	prctl(PR_SET_NAME, "sample_avs_vpss");
	while (!g_thread_manage->avs_thread_quit) {
		s32Ret =
		    RK_MPI_AVS_GetChnFrame(ctx->avs.s32GrpId, ctx->avs.s32ChnId,
		                           &ctx->avs.stVideoFrame, MPI_MOD_GET_FRAME_TIMEOUT);
		if (s32Ret == RK_SUCCESS) {
			if (!g_thread_manage->tde_thread_quit) {
				g_thread_manage->avs_post_sem_ifexit = RK_FALSE;
			} else {
				g_thread_manage->avs_post_sem_ifexit = RK_TRUE;
			}
			for (i = 0; i < TDE_NUM_MAX; i++) {
				sem_post(&g_sem_tde_wait_task[i]);
			}
			if (!g_thread_manage->avs_post_sem_ifexit) {
				for (i = 0; i < TDE_NUM_MAX; i++) {
					sem_wait(&g_sem_tde_handle_taskdone[i]);
				}
			}
			s32Ret = RK_MPI_VPSS_SendFrame(ctx->vpss.s32GrpId, 0, &ctx->avs.stVideoFrame,
			                               MPI_MOD_SEND_FRAME_TIMEOUT);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VPSS_SendFrame Failure s32Ret: %#X", s32Ret);
			}
			s32Ret = RK_MPI_AVS_ReleaseChnFrame(ctx->avs.s32GrpId, ctx->avs.s32ChnId,
			                                    &ctx->avs.stVideoFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_AVS_ReleaseChnFrame Failure s32Ret: %#X  avs_grp_id:%d "
				        "avs_chnid: %d",
				        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
				program_error_handle(__func__);
				continue;
			}
		} else {
			RK_LOGE("RK_MPI_AVS_GetChnFrame s32Ret: %#X", s32Ret);
		}
	}
	RK_LOGE("avs_vpss_thread exit");
	return RK_NULL;
}

static void *vpss_venc_thread(void *pArgs) {
	RK_S32 s32Ret = RK_SUCCESS;
	VIDEO_FRAME_INFO_S stChnFrameInfos[VENC_MAIN_STREAM_NUM] = {0};
	prctl(PR_SET_NAME, "sample_vpss_venc");
	RK_S32 i = 0;

	while (!g_thread_manage->vpss_thread_quit) {
		for (i = 0; i < VENC_MAIN_STREAM_NUM; i++) {
			s32Ret = RK_MPI_VPSS_GetChnFrame(ctx->vpss.s32GrpId, i, &stChnFrameInfos[i],
			                                 MPI_MOD_GET_FRAME_TIMEOUT);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VPSS_GetChnFrame failure ch_id: %d "
				        "s32Ret: %#X",
				        ctx->main_venc[i].s32ChnId, s32Ret);
				continue;
			}
			s32Ret =
			    RK_MPI_VENC_SendFrame(ctx->main_venc[i].s32ChnId, &stChnFrameInfos[i],
			                          MPI_MOD_SEND_FRAME_TIMEOUT);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_SendFrame failure ch_id: %d "
				        "s32Ret: %#X",
				        ctx->main_venc[i].s32ChnId, s32Ret);
			}

			s32Ret =
			    RK_MPI_VPSS_ReleaseChnFrame(ctx->vpss.s32GrpId, i, &stChnFrameInfos[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VPSS_ReleaseChnFrame failure ch_id: %d "
				        "s32Ret: %#X",
				        ctx->main_venc[i].s32ChnId, s32Ret);
				program_error_handle(__func__);
				continue;
			}
		}
	}
	RK_LOGE("vpss_venc_thread exit");
	return NULL;
}

static void *venc_get_stream(void *pArgs) {
	SAMPLE_VENC_CTX_S *ctx = (SAMPLE_VENC_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	void *pData = RK_NULL;
	FILE *fp = RK_NULL;
	RK_S32 loopCount = 0;

	if (ctx->s32ChnId != 1) {
		if (ctx->dstFilePath) {
			SAMPLE_COMM_TEST_CreateFile(ctx->dstFilePath, &fp, "venc", ctx->s32ChnId, 0);
		}
	}

	while (!g_thread_manage->venc_thread_quit[ctx->s32ChnId]) {

		s32Ret = SAMPLE_COMM_VENC_GetStream(ctx, &pData);
		if (s32Ret == RK_SUCCESS) {
			// exit when complete
			if (ctx->s32loopCount > 0) {
				if (loopCount >= ctx->s32loopCount) {
					s32Ret = SAMPLE_COMM_VENC_ReleaseStream(ctx);
					if (s32Ret != RK_SUCCESS) {
						RK_LOGE("SAMPLE_COMM_VENC_ReleaseStream Failure s32Ret: %#X "
						        "chnid: %d",
						        s32Ret, ctx->s32ChnId);
					}
					RK_LOGE("Venc test LoopCount is end");
					program_normal_exit(__func__);
					ctx->s32loopCount = -1;
					continue;
				}
			}

			if (ctx->dstFilePath != RK_NULL && ctx->s32ChnId == JPEG_VENC_CHNID) {
				SAMPLE_COMM_TEST_CreateFile(ctx->dstFilePath, &fp, "venc", ctx->s32ChnId,
				                            loopCount % 10);
			}

			if (fp) {
				fwrite(pData, 1, ctx->stFrame.pstPack->u32Len, fp);
				fflush(fp);
				if (ctx->s32ChnId == 1) {
					fclose(fp);
					fp = RK_NULL;
				}
			}

			if (g_rtsp_ifopen && ctx->s32ChnId != JPEG_VENC_CHNID) {
				pthread_mutex_lock(&g_rtsp_tx_mutex);
				rtsp_tx_video(g_rtsp_session[ctx->s32ChnId], pData,
				              ctx->stFrame.pstPack->u32Len, ctx->stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
				pthread_mutex_unlock(&g_rtsp_tx_mutex);
			} else if (ctx->s32ChnId == MAIN_VENC_CHNID) {
				RK_LOGE("chn:%d, loopCount:%d wd:%d\n", ctx->s32ChnId, loopCount,
				        ctx->stFrame.pstPack->u32Len);
			}

			s32Ret = SAMPLE_COMM_VENC_ReleaseStream(ctx);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_VENC_ReleaseStream Failure s32Ret: %#X chnid: %d",
				        s32Ret, ctx->s32ChnId);
				program_error_handle(__func__);
				continue;
			}
			loopCount++;
			if (ctx->s32ChnId != JPEG_VENC_CHNID && g_mode_test->module_test_ifopen) {
				pthread_mutex_lock(&g_frame_count_mutex[ctx->s32ChnId]);
				g_mode_test->venc_getframe_count[ctx->s32ChnId]++;
				pthread_mutex_unlock(&g_frame_count_mutex[ctx->s32ChnId]);

				if (g_mode_test->venc_getframe_count[ctx->s32ChnId] ==
				    g_mode_test->test_frame_count) {
					sem_post(&g_sem_test_switch_start[ctx->s32ChnId]);
				}
			}
		}
	}
	if (fp) {
		fclose(fp);
	}
	RK_LOGE("venc_get_stream chn: %d exit", ctx->s32ChnId);
	return RK_NULL;
}

static void wait_module_test_switch_success(void) {
	RK_S32 i = 0;
	for (i = 0; i < VENC_CHN_MAX; i++) {
		if (i == JPEG_VENC_CHNID)
			continue;
		pthread_mutex_lock(&g_frame_count_mutex[i]);
		g_mode_test->venc_getframe_count[i] = 0;
		pthread_mutex_unlock(&g_frame_count_mutex[i]);
		sem_wait(&g_sem_test_switch_start[i]);
	}
}

static void pn_mode_switch(RK_U32 test_loop) {
	RK_S32 i = 0;
	RK_U32 test_count = 0;
	RK_S32 s32Ret = RK_FAILURE;
	prctl(PR_SET_NAME, "sample_pn_switch");
	RK_LOGE("s32CamId: %d hdr: %d  camnum: %d", g_mode_test->s32CamId,
	        g_mode_test->hdr_mode, g_mode_test->camgroup_cfg.sns_num);

	while (!g_thread_manage->module_test_quit) {

		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			RK_MPI_VI_PauseChn(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId);
		}
		SAMPLE_COMM_ISP_CamGroup_Stop(g_mode_test->s32CamId);
		s32Ret = SAMPLE_COMM_ISP_CamGroup_Init(
		    g_mode_test->s32CamId, g_mode_test->hdr_mode, 1, &g_mode_test->camgroup_cfg);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_ISP_CamGroup_Init failure\n");
			program_error_handle(__func__);
			break;
		}
		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			RK_MPI_VI_ResumeChn(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId);
		}

		wait_module_test_switch_success();

		test_count++;
		RK_LOGE("-------------------PN Switch success, Test Total: %d Now Count: "
		        "%d-------------------",
		        test_loop, test_count);
		if (test_loop > 0 && test_count >= test_loop) {
			RK_LOGE("------------------PN test pass(success) count: %d-----------------",
			        test_count);
			g_mode_test->module_test_ifopen = RK_FALSE;
			program_normal_exit(__func__);
			break;
		}
	}
	RK_LOGE("pn_mode_switch_thread exit");
}

static RK_S32 avs_dynamic_switch(RK_U32 test_loop) {
	AVS_CHN_ATTR_S avsChn;
	RK_U32 test_count = 0;
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;

	while (!g_thread_manage->module_test_quit) {

		memset(&avsChn, 0, sizeof(AVS_CHN_ATTR_S));
		s32Ret = RK_MPI_AVS_GetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_error_handle(__func__);
			return s32Ret;
		}

		if (avsChn.u32Width == ctx->avs.stAvsChnAttr[0].u32Width) {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width - 480;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height - 16;
		} else {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height;
		}
		for (i = 0; i < TDE_NUM_MAX; i++) {
			ctx->tde[i].pstSrc.u32Width = avsChn.u32Width;
			ctx->tde[i].pstSrc.u32Height = avsChn.u32Height;
		}

		s32Ret = RK_MPI_AVS_SetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_error_handle(__func__);
			return s32Ret;
		}

		wait_module_test_switch_success();
		test_count++;
		RK_LOGE("Avs dpi switch to <%d x %d> success total: %d now: %d", avsChn.u32Width,
		        avsChn.u32Height, test_loop, test_count);

		if (test_loop > 0 && test_count >= test_loop) {
			RK_LOGE("Avs dpi switch Pass(success) total:%d", test_loop);
			g_mode_test->module_test_ifopen = RK_FALSE;
			program_normal_exit(__func__);
			return s32Ret;
		}
	}

	return s32Ret;
}

static RK_S32 Tde_Task_Init(RK_S32 tde_handle_width, RK_S32 tde_handle_height) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;
	MB_POOL_CONFIG_S stMbPoolCfg;
	PIC_BUF_ATTR_S stPicBufAttr;
	MB_PIC_CAL_S stMbPicCalResult;

	memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
	memset(&stPicBufAttr, 0, sizeof(PIC_BUF_ATTR_S));
	memset(&stMbPicCalResult, 0, sizeof(MB_PIC_CAL_S));

	stPicBufAttr.u32Width = tde_handle_width;
	stPicBufAttr.u32Height = tde_handle_height;
	stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
	s32Ret = RK_MPI_CAL_TDE_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
	RK_LOGD("u32MBSize is %d", stMbPicCalResult.u32MBSize);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_CAL_TDE_GetPicBufferSize Failure s32Ret:%#X", s32Ret);
		return s32Ret;
	}

	memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
	stMbPoolCfg.u64MBSize = stMbPicCalResult.u32MBSize;
	stMbPoolCfg.u32MBCnt = 2;
	stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
	stMbPoolCfg.bPreAlloc = RK_TRUE;

	for (i = 0; i < TDE_NUM_MAX; i++) {
		ctx->tde[i].s32ChnId = i;
		ctx->tde[i].buffersize = stMbPicCalResult.u32MBSize;
		ctx->tde[i].stVideoFrames.stVFrame.u32Width = tde_handle_width;
		ctx->tde[i].stVideoFrames.stVFrame.u32Height = tde_handle_height;
		ctx->tde[i].stVideoFrames.stVFrame.u32VirWidth = tde_handle_width;
		ctx->tde[i].stVideoFrames.stVFrame.u32VirHeight = tde_handle_height;
		ctx->tde[i].stVideoFrames.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
		ctx->tde[i].stVideoFrames.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
		ctx->tde[i].tde_frame_pool = RK_MPI_MB_CreatePool(&stMbPoolCfg);
		if (ctx->tde[i].tde_frame_pool == MB_INVALID_POOLID) {
			RK_LOGE("RK_MPI_MB_CreatePool Failure, the pool is MB_INVALID_POOLID");
			return RK_FAILURE;
		}

		ctx->tde[i].pstSrc.u32Width = ctx->avs.stAvsChnAttr[0].u32Width;
		ctx->tde[i].pstSrc.u32Height = ctx->avs.stAvsChnAttr[0].u32Height;
		ctx->tde[i].pstSrc.enColorFmt = RK_FMT_YUV420SP;
		ctx->tde[i].pstSrc.enComprocessMode = ctx->avs.stAvsChnAttr[0].enCompressMode;
		ctx->tde[i].pstSrcRect.s32Xpos = 200 * i;
		ctx->tde[i].pstSrcRect.s32Ypos = 200 * i;
		ctx->tde[i].pstSrcRect.u32Width = tde_handle_width;
		ctx->tde[i].pstSrcRect.u32Height = tde_handle_height;

		ctx->tde[i].pstDst.u32Width = tde_handle_width;
		ctx->tde[i].pstDst.u32Height = tde_handle_height;
		ctx->tde[i].pstDst.enColorFmt = RK_FMT_YUV420SP;
		ctx->tde[i].pstDst.enComprocessMode = COMPRESS_MODE_NONE;
		ctx->tde[i].pstDstRect.s32Xpos = 0;
		ctx->tde[i].pstDstRect.s32Ypos = 0;
		ctx->tde[i].pstDstRect.u32Width = tde_handle_width;
		ctx->tde[i].pstDstRect.u32Height = tde_handle_height;
	}

	s32Ret = RK_TDE_Open();
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_TDE_Open Failure s32Ret:%#X", s32Ret);
		return s32Ret;
	}

	return s32Ret;
}

static void main_venc_switch_dpi(RK_U32 test_loop) {
	RK_S32 s32Ret = RK_FAILURE;
	VPSS_CHN_ATTR_S pstChnAttr;
	RK_S32 switch_count = 0;

	while (!g_thread_manage->module_test_quit) {

		memset(&pstChnAttr, 0, sizeof(VPSS_CHN_ATTR_S));

		s32Ret =
		    RK_MPI_VPSS_GetChnAttr(ctx->vpss.s32GrpId, ctx->vpss.s32ChnId, &pstChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_GetChnAttr Failure s32Ret: %#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss.s32GrpId, ctx->vpss.s32ChnId);
			program_error_handle(__func__);
			break;
		}
		if (pstChnAttr.u32Width == ctx->vpss.stVpssChnAttr[0].u32Width) {
			pstChnAttr.u32Width = ctx->vpss.stVpssChnAttr[0].u32Width / 2;
			pstChnAttr.u32Height = ctx->vpss.stVpssChnAttr[0].u32Height / 2;
		} else {
			pstChnAttr.u32Width = ctx->vpss.stVpssChnAttr[0].u32Width;
			pstChnAttr.u32Height = ctx->vpss.stVpssChnAttr[0].u32Height;
		}
		s32Ret =
		    RK_MPI_VPSS_SetChnAttr(ctx->vpss.s32GrpId, ctx->vpss.s32ChnId, &pstChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_SetChnAttr Failure s32Ret:%#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss.s32GrpId, ctx->vpss.s32ChnId);
			program_error_handle(__func__);
			break;
		}

		wait_module_test_switch_success();
		switch_count++;
		RK_LOGE("-------Main Venc Dpi Switch to <%d x %d> success  Toatl: %d   Now "
		        "Count: %d---------",
		        pstChnAttr.u32Width, pstChnAttr.u32Height, test_loop, switch_count);

		if (test_loop > 0 && switch_count >= test_loop) {
			RK_LOGE("-------Main Venc Dpi Switch Test Toatl: %d   Now Count: %d---------",
			        test_loop, switch_count);
			g_mode_test->module_test_ifopen = RK_FALSE;
			program_normal_exit(__func__);
			break;
		}
	}

	RK_LOGE("main_venc_switch_dpi exit");
}

static void *sample_module_test(void *pArgs) {
	prctl(PR_SET_NAME, "sample_module_test");
	sleep(MODULE_TEST_DELAY_SECOND_TIME);

	switch (g_mode_test->module_test_type) {
	case 1: // P/N mode swithc
		if (g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
			pn_mode_switch(g_mode_test->module_test_loop);
		} else if (g_mode_test->avs_stream_source == STREAM_FROM_FILE) {
			RK_LOGE("isp and vi mod not running, this mode dosen't support PN mode "
			        "switch test");
			g_mode_test->module_test_ifopen = RK_FALSE;
			program_normal_exit(__func__);
		}
		break;
	case 2: // avs resolution switch
		avs_dynamic_switch(g_mode_test->module_test_loop);
		break;
	case 3: // main resolution switch
		main_venc_switch_dpi(g_mode_test->module_test_loop);
		break;
	default:
		RK_LOGE("test module is unexist!!!");
		break;
	}
	return RK_NULL;
}

static void *send_frame_thread(void *pArgs) {
	prctl(PR_SET_NAME, "send_frame_thread");
	SAMPLE_MPI_CTX_S *ctx = (SAMPLE_MPI_CTX_S *)(pArgs);
	char name[256] = {0};
	FILE *fp[CAM_NUM_MAX] = {RK_NULL};
	RK_S32 loopCount = 0;
	RK_U32 u32Datalen = 0;
	RK_S32 s32Ret = RK_FAILURE;
	VIDEO_FRAME_INFO_S stVideoFrames[CAM_NUM_MAX] = {0};
	MB_POOL mbPipePool[CAM_NUM_MAX] = {0};
	MB_BLK mbAppBlk = RK_NULL;
	PIC_BUF_ATTR_S pstBufAttr = {0};
	MB_PIC_CAL_S pstPicCal = {0};
	MB_POOL_CONFIG_S stMbPoolCfg = {0};
	RK_S64 pts = SAMPLE_COMM_TEST_GetNowUs();

	pstBufAttr.u32Width = g_mode_test->u32AvsPipeWidth;
	pstBufAttr.u32Height = g_mode_test->u32AvsPipeHeight;
	pstBufAttr.enCompMode = COMPRESS_MODE_NONE;
	pstBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	s32Ret = RK_MPI_CAL_VGS_GetPicBufferSize(&pstBufAttr, &pstPicCal);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_CAL_VGS_GetPicBufferSize failure:%#X", s32Ret);
		return RK_NULL;
	}

	/* mb pool create */
	for (RK_S32 i = 0; i < g_mode_test->avs_pipe_num; i++) {
		memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
		stMbPoolCfg.u64MBSize = pstPicCal.u32MBSize;
		stMbPoolCfg.u32MBCnt = 5;
		stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
		stMbPoolCfg.bPreAlloc = RK_TRUE;
		mbPipePool[i] = RK_MPI_MB_CreatePool(&stMbPoolCfg);
		if (MB_INVALID_POOLID == mbPipePool[i]) {
			RK_LOGE("RK_MPI_MB_CreatePool failure, PipeNum:%d", i);
			goto __INIT_FAILURE;
		}
	}

	if (g_mode_test->pCAvsInputPathFolder) {
		for (RK_S32 i = 0; i < g_mode_test->avs_pipe_num; i++) {
			snprintf(name, sizeof(name), "%s/%d.yuv", g_mode_test->pCAvsInputPathFolder,
			         i);
			fp[i] = fopen(name, "rb");
			if (!fp[i]) {
				RK_LOGE("fopen %s failed, error: %s", name, strerror(errno));
				g_mode_test->pCAvsInputPathFolder = RK_NULL;
			}
		}
	}

	while (!g_thread_manage->send_frame_thread_quit) {

		for (RK_S32 i = 0; i < g_mode_test->avs_pipe_num; i++) {

			mbAppBlk = RK_MPI_MB_GetMB(mbPipePool[i], pstPicCal.u32MBSize, RK_TRUE);
			if (!mbAppBlk) {
				i--;
				RK_LOGE("RK_MPI_MB_GetMB get blk is Null");
				continue;
			}

			stVideoFrames[i].stVFrame.pMbBlk = mbAppBlk;
			stVideoFrames[i].stVFrame.u32Width = pstBufAttr.u32Width;
			stVideoFrames[i].stVFrame.u32Height = pstBufAttr.u32Height;
			stVideoFrames[i].stVFrame.u32VirWidth = pstPicCal.u32VirWidth;
			stVideoFrames[i].stVFrame.u32VirHeight = pstPicCal.u32VirHeight;
			stVideoFrames[i].stVFrame.enPixelFormat = RK_FMT_YUV420SP;
			stVideoFrames[i].stVFrame.enCompressMode = COMPRESS_MODE_NONE;

			if (g_mode_test->pCAvsInputPathFolder) {
				fseek(fp[i], 0, SEEK_SET);
				u32Datalen =
				    fread(RK_MPI_MB_Handle2VirAddr(stVideoFrames[i].stVFrame.pMbBlk), 1,
				          pstPicCal.u32MBSize, fp[i]);
				if (u32Datalen != pstPicCal.u32MBSize) {
					RK_LOGE("failure_len:%d, Size:%d, mb:%p, fp:%p", u32Datalen,
					        pstPicCal.u32MBSize,
					        RK_MPI_MB_Handle2VirAddr(stVideoFrames[i].stVFrame.pMbBlk),
					        fp[i]);
				}

			} else {
				s32Ret = SAMPLE_COMM_FillImage(
				    (RK_U8 *)RK_MPI_MB_Handle2VirAddr(stVideoFrames[i].stVFrame.pMbBlk),
				    pstBufAttr.u32Width, pstBufAttr.u32Height,
				    RK_MPI_CAL_COMM_GetHorStride(stVideoFrames[i].stVFrame.u32Width,
				                                 stVideoFrames[i].stVFrame.enPixelFormat),
				    stVideoFrames[i].stVFrame.u32VirHeight,
				    stVideoFrames[i].stVFrame.enPixelFormat, loopCount + i);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("TEST_COMM_FillImage failure:%#X", s32Ret);
					program_error_handle(__func__);
				}
			}

			stVideoFrames[i].stVFrame.u32TimeRef = loopCount;
			stVideoFrames[i].stVFrame.u64PTS = SAMPLE_COMM_TEST_GetNowUs();
			RK_MPI_SYS_MmzFlushCache(stVideoFrames[i].stVFrame.pMbBlk, RK_FALSE);
			s32Ret =
			    RK_MPI_AVS_SendPipeFrame(ctx->avs.s32GrpId, i, &stVideoFrames[i], 1000);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_AVS_SendPipeFrame %#X", s32Ret);
				program_error_handle(__func__);
			}
			RK_MPI_MB_ReleaseMB(stVideoFrames[i].stVFrame.pMbBlk);
			mbAppBlk = RK_NULL;
		}

		loopCount++;
		pts = SAMPLE_COMM_TEST_GetNowUs();
		usleep(33 * 1000);
	}

__INIT_FAILURE:
	for (RK_S32 i = 0; i < g_mode_test->avs_pipe_num; i++) {
		if (fp[i]) {
			fclose(fp[i]);
			fp[i] = RK_NULL;
		}
		if (MB_INVALID_POOLID != mbPipePool[i]) {
			RK_MPI_MB_DestroyPool(mbPipePool[i]);
		}
	}
	RK_LOGE("avs_send_stream thread eixt !!!");
	return RK_NULL;
}

static RK_S32 Rgn_Init(RK_CHAR *bmp_file_path) {

	RK_S32 s32Ret = RK_FAILURE;
	// Init RGN[0]
	ctx->rgn[0].rgnHandle = 0;
	ctx->rgn[0].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[0].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[0].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[0].stMppChn.s32DevId = ctx->vpss.s32ChnId;
	ctx->rgn[0].stRegion.s32X = 0;        // must be 16 aligned
	ctx->rgn[0].stRegion.s32Y = 0;        // must be 16 aligned
	ctx->rgn[0].stRegion.u32Width = 640;  // must be 16 aligned
	ctx->rgn[0].stRegion.u32Height = 640; // must be 16 aligned
	ctx->rgn[0].u32Color = 0xFF0000;
	ctx->rgn[0].u32BgAlpha = 128;
	ctx->rgn[0].u32FgAlpha = 128;
	ctx->rgn[0].u32Layer = 1;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[0]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[0].rgnHandle);
		return s32Ret;
	}

	// Init RGN[1]
	ctx->rgn[1].rgnHandle = 1;
	ctx->rgn[1].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[1].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[1].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[1].stMppChn.s32DevId = ctx->vpss.s32ChnId;
	ctx->rgn[1].stRegion.s32X = 0; // must be 16 aligned
	ctx->rgn[1].stRegion.s32Y = RK_ALIGN(ctx->avs.stAvsChnAttr->u32Height - 640, 16);
	// must be 16 aligned
	ctx->rgn[1].stRegion.u32Width = 640;  // must be 16 aligned
	ctx->rgn[1].stRegion.u32Height = 640; // must be 16 aligned
	ctx->rgn[1].u32Color = 0xFFFF00;
	ctx->rgn[1].u32BgAlpha = 128;
	ctx->rgn[1].u32FgAlpha = 128;
	ctx->rgn[1].u32Layer = 2;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[1]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[1].rgnHandle);
		return s32Ret;
	}
	// Init RGN[2]
	ctx->rgn[2].rgnHandle = 2;
	ctx->rgn[2].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[2].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[2].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[2].stMppChn.s32DevId = ctx->vpss.s32ChnId;
	ctx->rgn[2].stRegion.s32X = RK_ALIGN(ctx->avs.stAvsChnAttr->u32Width - 640, 16);
	// must be 16 aligned
	ctx->rgn[2].stRegion.s32Y = 0;        // must be 16 aligned
	ctx->rgn[2].stRegion.u32Width = 640;  // must be 16 aligned
	ctx->rgn[2].stRegion.u32Height = 640; // must be 16 aligned
	ctx->rgn[2].u32Color = 0x00FF00;
	ctx->rgn[2].u32BgAlpha = 128;
	ctx->rgn[2].u32FgAlpha = 128;
	ctx->rgn[2].u32Layer = 3;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[2]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[2].rgnHandle);
		return s32Ret;
	}
	// Init RGN[3]
	ctx->rgn[3].rgnHandle = 3;
	ctx->rgn[3].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[3].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[3].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[3].stMppChn.s32DevId = ctx->vpss.s32ChnId;
	ctx->rgn[3].stRegion.s32X =
	    RK_ALIGN(ctx->avs.stAvsChnAttr->u32Width - 640, 16); // must be 16 aligned
	ctx->rgn[3].stRegion.s32Y =
	    RK_ALIGN(ctx->avs.stAvsChnAttr->u32Height - 640, 16); // must be 16 aligned
	ctx->rgn[3].stRegion.u32Width = 640;                      // must be 16 aligned
	ctx->rgn[3].stRegion.u32Height = 640;                     // must be 16 aligned
	ctx->rgn[3].u32Color = 0x0000FF;
	ctx->rgn[3].u32BgAlpha = 128;
	ctx->rgn[3].u32FgAlpha = 128;
	ctx->rgn[3].u32Layer = 5;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[3]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[3].rgnHandle);
		return s32Ret;
	}

	// Init RGN[4]
	ctx->rgn[4].rgnHandle = 4;
	ctx->rgn[4].stRgnAttr.enType = OVERLAY_RGN;
	ctx->rgn[4].stMppChn.enModId = RK_ID_VENC;
	ctx->rgn[4].stMppChn.s32ChnId = ctx->main_venc[0].s32ChnId;
	ctx->rgn[4].stMppChn.s32DevId = 0;
	ctx->rgn[4].stRegion.s32X =
	    RK_ALIGN(ctx->vpss.stVpssChnAttr[ctx->vpss.s32ChnId].u32Width / 2,
	             16); // must be 16 aligned
	ctx->rgn[4].stRegion.s32Y =
	    RK_ALIGN(ctx->vpss.stVpssChnAttr[ctx->vpss.s32ChnId].u32Height / 2,
	             16);                     // must be 16 aligned
	ctx->rgn[4].stRegion.u32Width = 608;  // must be 16 aligned
	ctx->rgn[4].stRegion.u32Height = 288; // must be 16 aligned
	ctx->rgn[4].u32BmpFormat = RK_FMT_BGRA5551;
	ctx->rgn[4].u32BgAlpha = 128;
	ctx->rgn[4].u32FgAlpha = 128;
	ctx->rgn[4].u32Layer = 6;
	ctx->rgn[4].srcFileBmpName = bmp_file_path;
	if (bmp_file_path) {
		s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[4]);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
			        ctx->rgn[4].rgnHandle);
			return s32Ret;
		}
	} else {
		RK_LOGE("the bmp file is NULL, overlay rgn skips");
	}

	return s32Ret;
}

static RK_S32 Rgn_Deinit(RK_CHAR *bmp_file_path) {
	RK_S32 s32Ret = RK_SUCCESS;
	for (int i = 0; i < RGN_NUM_MAX; i++) {
		if (!ctx->rgn[i].srcFileBmpName && ctx->rgn[i].stRgnAttr.enType == OVERLAY_RGN)
			continue;
		s32Ret = SAMPLE_COMM_RGN_DestroyChn(&ctx->rgn[i]);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_RGN_DestroyChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
			        ctx->rgn[i].rgnHandle);
		}
	}
	return s32Ret;
}

static RK_S32 global_param_init(void) {
	RK_S32 i = 0;
	ctx = (SAMPLE_MPI_CTX_S *)(malloc(sizeof(SAMPLE_MPI_CTX_S)));
	if (ctx == RK_NULL) {
		RK_LOGE("malloc mem to ctx failure");
		return RK_FAILURE;
	}
	memset(ctx, 0, sizeof(SAMPLE_MPI_CTX_S));

	g_thread_manage = (g_thread_manage_t *)malloc(sizeof(g_thread_manage_t));
	if (ctx == RK_NULL) {
		RK_LOGE("malloc mem to g_thread_manage failure");
		return RK_FAILURE;
	}
	memset(g_thread_manage, 0, sizeof(g_thread_manage_t));

	g_mode_test = (g_mode_test_t *)malloc(sizeof(g_mode_test_t));
	if (ctx == RK_NULL) {
		RK_LOGE("malloc mem to g_mode_test failure");
		return RK_FAILURE;
	}
	memset(g_mode_test, 0, sizeof(g_thread_manage_t));

	for (i = 0; i < VENC_CHN_MAX; i++) {
		g_thread_manage->venc_thread_quit[i] = RK_FALSE;
	}

	g_mode_test->test_frame_count = 500;
	g_mode_test->module_test_loop = -1;
	g_mode_test->avs_pipe_num = 6;
	g_mode_test->u32AvsPipeWidth = 2560;
	g_mode_test->u32AvsPipeHeight = 1520;
	g_mode_test->avs_stream_source = STREAM_FROM_ISP_CAMERA;

	for (i = 0; i < TDE_NUM_MAX; i++) {
		sem_init(&g_sem_tde_wait_task[i], 0, 0);
		sem_init(&g_sem_tde_handle_taskdone[i], 0, 0);
	}
	for (i = 0; i < VENC_CHN_MAX; i++) {
		sem_init(&g_sem_test_switch_start[i], 0, 0);
		if (pthread_mutex_init(&g_frame_count_mutex[i], NULL) != 0) {
			RK_LOGE("mutex %d init failure \n", i);
			return RK_FAILURE;
		}
	}
	if (pthread_mutex_init(&g_rtsp_tx_mutex, NULL) != 0) {
		RK_LOGE("mutex init failure \n");
		return RK_FAILURE;
	}

	return RK_SUCCESS;
}

static RK_S32 global_param_deinit(void) {
	RK_S32 i = 0;

	// sem and mutex destroy
	for (i = 0; i < VENC_CHN_MAX; i++) {
		sem_destroy(&g_sem_test_switch_start[i]);
		pthread_mutex_destroy(&g_frame_count_mutex[i]);
	}
	for (i = 0; i < TDE_NUM_MAX; i++) {
		sem_destroy(&g_sem_tde_wait_task[i]);
		sem_destroy(&g_sem_tde_handle_taskdone[i]);
	}
	pthread_mutex_destroy(&g_rtsp_tx_mutex);

	if (g_thread_manage) {
		free(g_thread_manage);
		g_thread_manage = RK_NULL;
	}
	if (g_mode_test) {
		free(g_mode_test);
		g_mode_test = RK_NULL;
	}

	if (ctx) {
		free(ctx);
		ctx = RK_NULL;
	}
	return RK_SUCCESS;
}

static RK_S32 rtsp_init(CODEC_TYPE_E enCodecType) {
	RK_S32 i = 0;
	char buffer[BUFFER_BYTE_SIZE] = {0};
	g_rtsplive = create_rtsp_demo(554);
	for (i = 0; i < VENC_CHN_MAX; i++) {
		if (i == JPEG_VENC_CHNID) {
			continue;
		}
		sprintf(buffer, "/live/%d", i);

		g_rtsp_session[i] = rtsp_new_session(g_rtsplive, buffer);
		if (enCodecType == RK_CODEC_TYPE_H264) {
			rtsp_set_video(g_rtsp_session[i], RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
		} else if (enCodecType == RK_CODEC_TYPE_H265) {
			rtsp_set_video(g_rtsp_session[i], RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
		} else {
			RK_LOGE("not support other type\n");
			g_rtsp_ifopen = RK_FALSE;
			return RK_SUCCESS;
		}
		rtsp_sync_video_ts(g_rtsp_session[i], rtsp_get_reltime(), rtsp_get_ntptime());
	}
	g_rtsp_ifopen = RK_TRUE;
	return RK_SUCCESS;
}

static RK_S32 rtsp_deinit(void) {
	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	return RK_SUCCESS;
}

/******************************************************************************
 * function    : main()
 * Description : main
 ******************************************************************************/
RK_S32 main(RK_S32 argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;
	RK_S32 video_width = 2560;
	RK_S32 video_height = 1520;
	RK_S32 avs_width = 8160;
	RK_S32 avs_height = 3616;
	RK_CHAR *pavs_stitch_filepath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pavs_mesh_alphapath = "/tmp/";
	RK_CHAR *pout_path = RK_NULL;
	RK_CHAR *pbmp_file_path = RK_NULL;
	RK_CHAR *pavs_stream_file_path = RK_NULL;
	RK_S32 s32CamId = 0;
	RK_S32 s32CamNum = 6;
	RK_S32 s32loopCnt = -1;
	RK_S32 main_venc_width = 3840;
	RK_S32 main_venc_height = 2160;
	RK_S32 sub_venc_width = 1920;
	RK_S32 sub_venc_height = 1080;
	RK_S32 jpeg_venc_width = 1920;
	RK_S32 jpeg_venc_height = 1080;
	RK_S32 main_venc_src_fps = 25;
	RK_S32 main_venc_dst_fps = 25;
	RK_S32 sub_venc_src_fps = 25;
	RK_S32 sub_venc_dst_fps = 25;
	CODEC_TYPE_E enCodecType = RK_CODEC_TYPE_H265;
	VENC_RC_MODE_E enRcMode = VENC_RC_MODE_H265CBR;
	RK_CHAR *pCodecName = "H265";
	RK_S32 s32BitRate = 4 * 1024;
	RK_S32 avs_stitch_mode = 0;
	RK_S32 avs_centerx = 4196;
	RK_S32 avs_centery = 2080;
	RK_S32 avs_fovx = 28000;
	RK_S32 avs_fovy = 9500;
	RK_BOOL avs_bsync_pipe = RK_FALSE;
	RK_BOOL vpss_proc_devtype = RK_FALSE;
	RK_S32 jpeg_fps = 1;
	RK_S32 receive_longopt_input = 0;

	pthread_t vpss_venc_thread_id, avs_vpss_thread_id, vi_avs_thread_id,
	    module_test_thread_id, send_frame_thread_id;

	if (argc < 2) {
		print_usage(argv[0]);
		return RK_FAILURE;
	}

	signal(SIGINT, sigterm_handler);
	if (global_param_init() != RK_SUCCESS) {
		RK_LOGE("global_param_init failure");
		return RK_FAILURE;
	}

#ifdef RKAIQ
	RK_BOOL bMultictx = RK_FALSE;
#endif
	RK_S32 c;
	char *iq_file_dir = "/etc/iqfiles/";
	int index = 0;
	while ((c = getopt_long(argc, argv, optstr, long_options, &index)) != -1) {
		const char *tmp_optarg = optarg;
		RK_LOGD("c: %d optarg: %s \n", c, optarg);
		switch (c) {
		case 'a':
			if (!optarg && NULL != argv[optind] && '-' != argv[optind][0]) {
				tmp_optarg = argv[optind++];
			}
			if (tmp_optarg) {
				iq_file_dir = (char *)tmp_optarg;
			} else {
				iq_file_dir = NULL;
			}
			break;
		case 'A':
			pavs_stitch_filepath = optarg;
			if (!pavs_stitch_filepath) {
				RK_LOGE("avs_stitch_file_path Value is invaild, please check input");
				goto __INVAILD_INPUT;
			}
			break;
		case 'n':
			s32CamNum = atoi(optarg);
			if (s32CamNum <= 0) {
				RK_LOGE("camera_num Value is invaild, please check input");
				goto __INVAILD_INPUT;
			}
			g_mode_test->avs_pipe_num = s32CamNum;
			break;
		case 'b':
			s32BitRate = atoi(optarg);
			break;
		case 'w':
			avs_width = atoi(optarg);
			break;
		case 'h':
			avs_height = atoi(optarg);
			break;
		case 'l':
			s32loopCnt = atoi(optarg);
			break;
		case 'o':
			pout_path = optarg;
			break;
		case 'e':
			if (!strcmp(optarg, "h264cbr")) {
				enCodecType = RK_CODEC_TYPE_H264;
				enRcMode = VENC_RC_MODE_H264CBR;
				pCodecName = "H264";
			} else if (!strcmp(optarg, "h264vbr")) {
				enCodecType = RK_CODEC_TYPE_H264;
				enRcMode = VENC_RC_MODE_H264VBR;
				pCodecName = "H264";
			} else if (!strcmp(optarg, "h264avbr")) {
				enCodecType = RK_CODEC_TYPE_H264;
				enRcMode = VENC_RC_MODE_H264AVBR;
				pCodecName = "H264";
			} else if (!strcmp(optarg, "h265cbr")) {
				enCodecType = RK_CODEC_TYPE_H265;
				enRcMode = VENC_RC_MODE_H265CBR;
				pCodecName = "H265";
			} else if (!strcmp(optarg, "h265vbr")) {
				enCodecType = RK_CODEC_TYPE_H265;
				enRcMode = VENC_RC_MODE_H265VBR;
				pCodecName = "H265";
			} else if (!strcmp(optarg, "h265avbr")) {
				enCodecType = RK_CODEC_TYPE_H265;
				enRcMode = VENC_RC_MODE_H265AVBR;
				pCodecName = "H265";
			} else if (!strcmp(optarg, "mjpegcbr")) {
				enCodecType = RK_CODEC_TYPE_MJPEG;
				enRcMode = VENC_RC_MODE_MJPEGCBR;
				pCodecName = "MJPEG";
			} else if (!strcmp(optarg, "mjpegvbr")) {
				enCodecType = RK_CODEC_TYPE_MJPEG;
				enRcMode = VENC_RC_MODE_MJPEGVBR;
				pCodecName = "MJPEG";
			} else {
				printf("ERROR: Invalid encoder type.\n");
				goto __INVAILD_INPUT;
			}
			break;
		case 'i':
			pbmp_file_path = optarg;
			break;
		case 'M':
			avs_stitch_mode = atoi(optarg);
			break;
		case 'x':
			avs_centerx = atoi(optarg);
			break;
		case 'y':
			avs_centery = atoi(optarg);
			break;
		case 'X':
			avs_fovx = atoi(optarg);
			break;
		case 'Y':
			avs_fovy = atoi(optarg);
			break;
		case 's':
			avs_bsync_pipe = atoi(optarg);
			break;
		case 'a' + 's':
			receive_longopt_input = atoi(optarg);
			if (receive_longopt_input == 0) {
				g_mode_test->avs_stream_source = STREAM_FROM_ISP_CAMERA;
			} else if (receive_longopt_input == 1) {
				g_mode_test->avs_stream_source = STREAM_FROM_FILE;
			} else {
				RK_LOGE("avs_stream_source Value is invaild, please check input");
				goto __INVAILD_INPUT;
			}
			break;
		case 'f' + 'p':
			pavs_stream_file_path = optarg;
			break;
		case 'f' + 'f':
			receive_longopt_input = atoi(optarg);
			if (receive_longopt_input == 0) {
				g_mode_test->source_compress_mode = COMPRESS_MODE_NONE;
			} else if (receive_longopt_input == 1) {
				g_mode_test->source_compress_mode = COMPRESS_AFBC_16x16;
			} else {
				RK_LOGE("avs_stream_file_compress_format Value is invaild, please check "
				        "input");
				goto __INVAILD_INPUT;
			}
			break;
		case 'v' + 'p':
			vpss_proc_devtype = atoi(optarg);
			if (vpss_proc_devtype == 0) {
				vpss_proc_devtype = RK_FALSE;
			} else if (vpss_proc_devtype >= 0) {
				vpss_proc_devtype = RK_TRUE;
			} else {
				RK_LOGE("vpss_proc_devtype Value is invaild");
				goto __INVAILD_INPUT;
			}
			break;
		case 'm' + 'w':
			main_venc_width = atoi(optarg);
			break;
		case 'm' + 'h':
			main_venc_height = atoi(optarg);
			break;
		case 'm' + 's':
			main_venc_src_fps = atoi(optarg);
			break;
		case 'm' + 'd':
			main_venc_dst_fps = atoi(optarg);
			break;
		case 's' + 'w':
			sub_venc_width = atoi(optarg);
			break;
		case 's' + 'h':
			sub_venc_height = atoi(optarg);
			break;
		case 's' + 'd':
			sub_venc_dst_fps = atoi(optarg);
			break;
		case 's' + 'f':
			sub_venc_src_fps = atoi(optarg);
			break;
		case 'j' + 'w':
			jpeg_venc_width = atoi(optarg);
			break;
		case 'j' + 'h':
			jpeg_venc_height = atoi(optarg);
			break;
		case 'j' + 'f':
			jpeg_fps = atoi(optarg);
			break;
		case 't' + 't':
			g_mode_test->module_test_type = atoi(optarg);
			if (g_mode_test->module_test_type < 0 || g_mode_test->module_test_type > 3) {
				RK_LOGE("input the s32ModuleTestType param invaild, input (0--3)");
				goto __INVAILD_INPUT;
			}
			break;
		case 't' + 'o':
			g_mode_test->module_test_loop = atoi(optarg);
			break;
		case 'f' + 'c':
			g_mode_test->test_frame_count = atoi(optarg);
			break;
		case '?':
		__INVAILD_INPUT:
		default:
			print_usage(argv[0]);
			g_exit_result = RK_FAILURE;
			goto __FAILED2;
		}
	}

	RK_LOGE("#CameraIdx: %d", s32CamId);
	RK_LOGE("#pavs_stitch_filepath: %s", pavs_stitch_filepath);
	RK_LOGE("#pavs_mesh_alphapath: %s", pavs_mesh_alphapath);
	RK_LOGE("#CodecName:%s", pCodecName);
	RK_LOGE("#Output Path: %s", pout_path);
	RK_LOGE("#IQ Path: %s", iq_file_dir);
	if (iq_file_dir && g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
#ifdef RKAIQ
		RK_LOGE("#Rkaiq XML DirPath: %s\n", iq_file_dir);
		RK_LOGE("#bMultictx: %d\n\n", bMultictx);

		g_mode_test->s32CamId = s32CamId;
		g_mode_test->hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
		g_mode_test->camgroup_cfg.sns_num = s32CamNum;
		g_mode_test->camgroup_cfg.config_file_dir = iq_file_dir;
		bMultictx = 1;

		s32Ret =
		    SAMPLE_COMM_ISP_CamGroup_Init(g_mode_test->s32CamId, g_mode_test->hdr_mode,
		                                  bMultictx, &g_mode_test->camgroup_cfg);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_ISP_CamGroup_Init failure\n");
			g_exit_result = RK_FAILURE;
			goto __FAILED2;
		}
#endif
	}

	if (rtsp_init(enCodecType) != RK_SUCCESS) {
		RK_LOGE("rtsp_init failure");
		g_exit_result = RK_FAILURE;
		goto __FAILED;
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		g_exit_result = RK_FAILURE;
		goto __FAILED;
	}

	if (g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// Init VI[0] ~ VI[5]
		for (i = 0; i < s32CamNum; i++) {
			ctx->vi[i].u32Width = video_width;
			ctx->vi[i].u32Height = video_height;
			ctx->vi[i].s32DevId = i;
			ctx->vi[i].u32PipeId = i;
			ctx->vi[i].s32ChnId = 2; // rk3588 mainpath:0 selfpath:1 fbcpath:2
			ctx->vi[i].stChnAttr.stIspOpt.u32BufCount = 6;
			ctx->vi[i].stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
			ctx->vi[i].stChnAttr.u32Depth = 2;
			ctx->vi[i].stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
			ctx->vi[i].stChnAttr.enCompressMode = COMPRESS_AFBC_16x16;
			ctx->vi[i].stChnAttr.stFrameRate.s32SrcFrameRate = -1;
			ctx->vi[i].stChnAttr.stFrameRate.s32DstFrameRate = -1;
			s32Ret = SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_VI_CreateChn Failure s32Ret:%#X vi_chnid: %d",
				        s32Ret, ctx->vi[i].s32DevId);
				break;
			}
		}
		if (s32Ret != RK_SUCCESS) {
			for (i = i - 1; i >= 0; i--) {
				s32Ret = SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("SAMPLE_COMM_VI_DestroyChn Failure s32Ret:%#X vi_chnid: %d",
					        s32Ret, ctx->vi[i].s32DevId);
					continue;
				}
			}
			g_exit_result = RK_FAILURE;
			goto __FAILED;
		}
	} else if (g_mode_test->avs_stream_source == STREAM_FROM_FILE) {
		g_mode_test->pCAvsInputPathFolder = pavs_stream_file_path;
	}

	// Init avs[0]
	ctx->avs.s32GrpId = 0;
	ctx->avs.s32ChnId = 0;
	ctx->avs.s32loopCount = s32loopCnt;
	ctx->avs.stAvsModParam.enMBSource = MB_SOURCE_PRIVATE;
	ctx->avs.stAvsModParam.u32WorkingSetSize = 67 * 1024;
	ctx->avs.stAvsGrpAttr.enMode = 0; // 0: blend 1: no blend
	ctx->avs.stAvsGrpAttr.u32PipeNum = s32CamNum;
	ctx->avs.stAvsGrpAttr.stGainAttr.enMode = AVS_GAIN_MODE_AUTO;
	ctx->avs.stAvsGrpAttr.stOutAttr.enPrjMode = AVS_PROJECTION_EQUIRECTANGULAR;
	ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = avs_centerx;
	ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = avs_centery;
	ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = avs_fovx;
	ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = avs_fovy;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Width = avs_width;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Height = avs_height;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.bSyncPipe = avs_bsync_pipe;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].u32Depth = 2;
	ctx->avs.stAvsChnAttr[0].u32Width = avs_width;
	ctx->avs.stAvsChnAttr[0].u32Height = avs_height;
	ctx->avs.stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->avs.stAvsChnAttr[0].u32FrameBufCnt = 6;
	if (avs_stitch_mode == 0) { // stitch file is calib file
		ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
		ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = pavs_stitch_filepath;
		ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pMeshAlphaPath = pavs_mesh_alphapath;
	} else if (avs_stitch_mode == 1) { // stitch file is middle_lut file
		ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_LUT;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.enAccuracy = AVS_LUT_ACCURACY_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.enFuseWidth = AVS_FUSE_WIDTH_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.stLutStep.enStepX = AVS_LUT_STEP_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.stLutStep.enStepY = AVS_LUT_STEP_HIGH;
		ctx->avs.pLutFilePath = pavs_stitch_filepath;
	} else {
		RK_LOGE("avs stitch mode set error");
		g_exit_result = RK_FAILURE;
		goto __AVS_FAILED;
	}

	s32Ret = SAMPLE_COMM_AVS_CreateChn(&ctx->avs);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_AVS_CreateChn Failure s32Ret:%#X", s32Ret);
		g_exit_result = RK_FAILURE;
		goto __AVS_FAILED;
	}

	// Init VPSS[0]
	ctx->vpss.s32GrpId = 0;
	ctx->vpss.s32ChnId = 0;
	// RGA_device: VIDEO_PROC_DEV_RGA GPU_device: VIDEO_PROC_DEV_GPU
	if (vpss_proc_devtype == RK_FALSE) {
		ctx->vpss.enVProcDevType = VIDEO_PROC_DEV_RGA;
	} else if (vpss_proc_devtype == RK_TRUE) {
		ctx->vpss.enVProcDevType = VIDEO_PROC_DEV_GPU;
	}

	ctx->vpss.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stGrpVpssAttr.enCompressMode = COMPRESS_AFBC_16x16;
	ctx->vpss.s32ChnRotation[0] = ROTATION_0;
	ctx->vpss.stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss.stVpssChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
	ctx->vpss.stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->vpss.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].u32Width = main_venc_width;
	ctx->vpss.stVpssChnAttr[0].u32Height = main_venc_height;
	ctx->vpss.stVpssChnAttr[0].u32Depth = 2;
	ctx->vpss.stVpssChnAttr[0].u32FrameBufCnt = 4;
	ctx->vpss.s32ChnRotation[1] = ROTATION_0;
	ctx->vpss.stVpssChnAttr[1].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss.stVpssChnAttr[1].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss.stVpssChnAttr[1].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->vpss.stVpssChnAttr[1].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss.stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss.stVpssChnAttr[1].u32Width = jpeg_venc_width;
	ctx->vpss.stVpssChnAttr[1].u32Height = jpeg_venc_height;
	ctx->vpss.stVpssChnAttr[1].u32Depth = 2;
	ctx->vpss.stVpssChnAttr[1].u32FrameBufCnt = 4;
	s32Ret = SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VPSS_CreateChn Failure s32Ret:%#X", s32Ret);
		SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
		g_exit_result = RK_FAILURE;
		goto __AVS_FAILED;
	}

	// Init VENC[0]
	ctx->main_venc[0].s32ChnId = 0;
	ctx->main_venc[0].u32Width = main_venc_width;
	ctx->main_venc[0].u32Height = main_venc_height;
	ctx->main_venc[0].u32SrcFps = main_venc_src_fps;
	ctx->main_venc[0].u32DstFps = main_venc_dst_fps;
	ctx->main_venc[0].u32Gop = 50;
	ctx->main_venc[0].u32BitRate = s32BitRate;
	ctx->main_venc[0].enCodecType = enCodecType;
	ctx->main_venc[0].enRcMode = enRcMode;
	ctx->main_venc[0].getStreamCbFunc = venc_get_stream;
	ctx->main_venc[0].s32loopCount = s32loopCnt;
	ctx->main_venc[0].dstFilePath = pout_path;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	ctx->main_venc[0].stChnAttr.stVencAttr.u32Profile = 0;
	ctx->main_venc[0].stChnAttr.stGopAttr.enGopMode =
	    VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	s32Ret = SAMPLE_COMM_VENC_CreateChn(&ctx->main_venc[0]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_CreateChn Failure s32Ret:%#X", s32Ret);
		SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
		SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss);
		g_exit_result = RK_FAILURE;
		goto __AVS_FAILED;
	}

	// Init VENC[1]
	ctx->main_venc[1].s32ChnId = 1;
	ctx->main_venc[1].u32Width = jpeg_venc_width;
	ctx->main_venc[1].u32Height = jpeg_venc_height;
	ctx->main_venc[1].u32SrcFps = main_venc_src_fps;
	ctx->main_venc[1].u32DstFps = jpeg_fps;
	ctx->main_venc[1].u32Gop = 1;
	ctx->main_venc[1].u32Qfactor = 50;
	ctx->main_venc[1].u32BitRate = 4096;
	ctx->main_venc[1].enCodecType = RK_CODEC_TYPE_JPEG;
	ctx->main_venc[1].enRcMode = VENC_RC_MODE_MJPEGCBR;
	ctx->main_venc[1].getStreamCbFunc = venc_get_stream;
	ctx->main_venc[1].s32loopCount = s32loopCnt;
	ctx->main_venc[1].dstFilePath = pout_path;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	ctx->main_venc[1].stChnAttr.stVencAttr.u32Profile = 0;
	ctx->main_venc[1].stChnAttr.stGopAttr.enGopMode =
	    VENC_GOPMODE_INIT; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->main_venc[1]);

	for (i = 0; i < VENC_SUB_STREAM_NUM; i++) {
		ctx->sub_venc[i].s32ChnId = i + VENC_MAIN_STREAM_NUM;
		ctx->sub_venc[i].u32Width = sub_venc_width;
		ctx->sub_venc[i].u32Height = sub_venc_height;
		ctx->sub_venc[i].u32SrcFps = sub_venc_src_fps;
		ctx->sub_venc[i].u32DstFps = sub_venc_dst_fps;
		ctx->sub_venc[i].u32Gop = 50;
		ctx->sub_venc[i].u32BitRate = s32BitRate;
		ctx->sub_venc[i].enCodecType = enCodecType;
		ctx->sub_venc[i].enRcMode = enRcMode;
		ctx->sub_venc[i].getStreamCbFunc = venc_get_stream;
		ctx->sub_venc[i].s32loopCount = s32loopCnt;
		ctx->sub_venc[i].dstFilePath = pout_path;
		// H264  66：Baseline  77：Main Profile 100：High Profile
		// H265  0：Main Profile  1：Main 10 Profile
		// MJPEG 0：Baseline
		ctx->sub_venc[i].stChnAttr.stVencAttr.u32Profile = 0;
		ctx->sub_venc[i].stChnAttr.stGopAttr.enGopMode =
		    VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
		SAMPLE_COMM_VENC_CreateChn(&ctx->sub_venc[i]);
	}

	s32Ret = Tde_Task_Init(sub_venc_width, sub_venc_height);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("Tde_Task_Init Failure");
		g_exit_result = RK_FAILURE;
		goto __FAILED2;
	}
	s32Ret = Rgn_Init(pbmp_file_path);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("Rgn_Init Failure");
		g_exit_result = RK_FAILURE;
		goto __FAILED2;
	}
	if (g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		pthread_create(&vi_avs_thread_id, 0, vi_avs_thread, (void *)ctx);
	} else if (g_mode_test->avs_stream_source == STREAM_FROM_FILE) {
		pthread_create(&send_frame_thread_id, 0, send_frame_thread, (void *)ctx);
	}
	pthread_create(&avs_vpss_thread_id, 0, avs_vpss_thread, (void *)ctx);
	pthread_create(&vpss_venc_thread_id, 0, vpss_venc_thread, (void *)ctx);

	for (i = 0; i < TDE_NUM_MAX; i++) {
		pthread_create(&ctx->tde[i].tde_task_handle_id, 0, tde_task_hangle,
		               (void *)&ctx->tde[i]);
	}

	if (g_mode_test->module_test_type) {
		g_mode_test->module_test_ifopen = RK_TRUE;
		pthread_create(&module_test_thread_id, 0, sample_module_test, RK_NULL);
	}

	RK_LOGE("%s initial finish  !main_thread_quit %d\n", __func__,
	        !g_thread_manage->main_thread_quit);

	while (!g_thread_manage->main_thread_quit) {
		sleep(1);
	}

	RK_LOGE("%s exit!\n", __func__);

	if (g_mode_test->module_test_type) {
		g_thread_manage->module_test_quit = RK_TRUE;
		pthread_join(module_test_thread_id, RK_NULL);
	}

	// rgn deinit
	Rgn_Deinit(pbmp_file_path);

	// Venc main stream thread exit and Destroy chn
	for (i = 0; i < VENC_MAIN_STREAM_NUM; i++) {
		g_thread_manage->venc_thread_quit[i] = RK_TRUE;
		pthread_join(ctx->main_venc[i].getStreamThread, NULL);
		SAMPLE_COMM_VENC_DestroyChn(&ctx->main_venc[i]);
	}

	// venc sub stream thread exit and destroy chn
	for (i = 0; i < VENC_SUB_STREAM_NUM; i++) {
		g_thread_manage->venc_thread_quit[ctx->sub_venc[i].s32ChnId] = RK_TRUE;
		pthread_join(ctx->sub_venc[i].getStreamThread, NULL);
		SAMPLE_COMM_VENC_DestroyChn(&ctx->sub_venc[i]);
	}

	// deinit rtsp
	rtsp_deinit();

	// Tde thread exit
	g_thread_manage->tde_thread_quit = RK_TRUE;
	for (i = 0; i < TDE_NUM_MAX; i++) {
		pthread_join(ctx->tde[i].tde_task_handle_id, NULL);
	}

	// Vpss thread exit and destroy
	g_thread_manage->vpss_thread_quit = RK_TRUE;
	pthread_join(vpss_venc_thread_id, NULL);
	SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss);

	// Avs thread exit and destroy
	g_thread_manage->avs_thread_quit = RK_TRUE;
	pthread_join(avs_vpss_thread_id, NULL);
	SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
	if (g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// vi thread exit
		g_thread_manage->vi_thread_quit = RK_TRUE;
		pthread_join(vi_avs_thread_id, NULL);
	} else if (g_mode_test->avs_stream_source == STREAM_FROM_FILE) {
		g_thread_manage->send_frame_thread_quit = RK_TRUE;
		pthread_join(send_frame_thread_id, NULL);
		for (i = 0; i < s32CamNum; i++) {
			if (g_mode_test->pipe_frames[i].stVFrame.pMbBlk) {
				RK_MPI_MB_ReleaseMB(g_mode_test->pipe_frames[i].stVFrame.pMbBlk);
				g_mode_test->pipe_frames[i].stVFrame.pMbBlk = RK_NULL;
			}
		}
	}
__AVS_FAILED:
	if (g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// Destroy VI[0]
		for (i = 0; i < s32CamNum; i++) {
			SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
		}
	}
__FAILED:
	RK_MPI_SYS_Exit();
	if (iq_file_dir && g_mode_test->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
#ifdef RKAIQ
		SAMPLE_COMM_ISP_CamGroup_Stop(s32CamId);
#endif
	}
__FAILED2:
	global_param_deinit();
	return g_exit_result;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
