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
#define VENC_CHN_MAX 4
#define RGN_CHN_MAX 7
#define VENC_JPEG_CHN 3
#define MPI_MOD_SEND_FRAME_TIMEOUT 1000
#define MODULE_TEST_DELAY_SECOND_TIME 3 //(unit: second)

typedef enum rk_AVS_STREAM_SOURCE_TYPE_E {
	STREAM_FROM_ISP_CAMERA,
	STREAM_FROM_FILE
} AVS_STREAM_SOURCE_TYPE_E;

typedef struct _rkModeTest {
	RK_BOOL bVencThreadQuit[VENC_CHN_MAX];
	RK_BOOL bMainThreadQuit;
	RK_BOOL bModuleTestThreadQuit;
	RK_BOOL bModuleTestIfopen;
	RK_BOOL send_frame_thread_quit;
	RK_CHAR *pCAvsInputPathFolder;
	AVS_STREAM_SOURCE_TYPE_E avs_stream_source;
	COMPRESS_MODE_E source_compress_mode;
	RK_S32 s32FrameCountReferChn;
	RK_S32 s32VencGetFrameCount[VENC_CHN_MAX];
	RK_S32 s32ModuleTestType;
	RK_S32 s32ModuleTestLoop;
	RK_S32 s32TestFrameCount;
	RK_S32 s32CamId;
	RK_S32 s32AvsPipeNum;
	RK_U32 u32AvsPipeWidth;
	RK_U32 u32AvsPipeHeight;
	pthread_t send_frame_thread_id;
	VIDEO_FRAME_INFO_S pipe_frames[CAM_NUM_MAX];
	rk_aiq_working_mode_t hdr_mode;
	rk_aiq_camgroup_instance_cfg_t camgroup_cfg;
} g_pModeTest_t;

g_pModeTest_t *g_pModeTest;

RK_S32 g_exit_result = 0;
pthread_mutex_t g_rtsp_tx_mutex;
pthread_mutex_t g_frame_count_mutex[VENC_CHN_MAX];
sem_t g_sem_module_test[VENC_CHN_MAX];
rtsp_demo_handle g_rtsplive = NULL;
RK_BOOL g_rtsp_ifenbale = RK_FALSE;
static rtsp_session_handle g_rtsp_session[VENC_CHN_MAX];

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi[CAM_NUM_MAX];
	SAMPLE_AVS_CTX_S avs;
	SAMPLE_VENC_CTX_S venc[VENC_CHN_MAX];
	SAMPLE_RGN_CTX_S rgn[RGN_CHN_MAX];
	SAMPLE_VPSS_CTX_S vpss_gpu;
	SAMPLE_VPSS_CTX_S vpss_rga;
} SAMPLE_MPI_CTX_S;

SAMPLE_MPI_CTX_S *ctx;

static void program_handle_error(const char *func, RK_U32 line) {
	RK_LOGE("func: <%s> line: <%d> error exit!", func, line);
	g_exit_result = RK_FAILURE;
	g_pModeTest->bMainThreadQuit = RK_TRUE;
}

static void program_normal_exit(const char *func, RK_U32 line) {
	RK_LOGE("func: <%s> line: <%d> normal exit!", func, line);
	g_pModeTest->bMainThreadQuit = RK_TRUE;
}

static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	program_normal_exit(__func__, __LINE__);
}

static RK_CHAR optstr[] = "?::a::b:i:I:l:o:n:e:C:x:y:X:Y:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"bitrate", required_argument, NULL, 'b'},
    {"bmpFilePathForVpssRgn", required_argument, NULL, 'i'},
    {"bmpFilePathForVencRgn", required_argument, NULL, 'I'},
    {"loopCount", required_argument, NULL, 'l'},
    {"outputPath", required_argument, NULL, 'o'},
    {"camNum", required_argument, NULL, 'n'},
    {"encode", required_argument, NULL, 'e'},
    {"streamCompressMode", required_argument, NULL, 'C'},
    {"avsCenterX", required_argument, NULL, 'x'},
    {"avsCenterY", required_argument, NULL, 'y'},
    {"avsFovX", required_argument, NULL, 'X'},
    {"avsFoxY", required_argument, NULL, 'Y'},
    {"avsWidth", required_argument, NULL, 'A' + 'W'},
    {"avsHeight", required_argument, NULL, 'A' + 'H'},
    {"avsCalibFilePath", required_argument, NULL, 'A' + 'C'},
    {"avsLutFilePath", required_argument, NULL, 'A' + 'L'},
    {"avsStitchMode", required_argument, NULL, 'A' + 'M'},
    {"avsSyncPipe", required_argument, NULL, 'A' + 'S'},
    {"avsStreamSource", required_argument, NULL, 'a' + 's'},
    {"avsStreamFilePath", required_argument, NULL, 'f' + 'p'},
    {"avsStreamFileCompressFormat", required_argument, NULL, 'f' + 'f'},
    {"mainStreamWidth", required_argument, NULL, 'm' + 'w'},
    {"mainStreamHeight", required_argument, NULL, 'm' + 'h'},
    {"mainVencSrcFps", required_argument, NULL, 'm' + 's'},
    {"mainVencDstFps", required_argument, NULL, 'm' + 'd'},
    {"jpegWidth", required_argument, NULL, 'j' + 'w'},
    {"jpegHeight", required_argument, NULL, 'j' + 'h'},
    {"jpegFps", required_argument, NULL, 'j' + 'f'},
    {"subVencSrcFps", required_argument, NULL, 's' + 's'},
    {"subVencDstFps", required_argument, NULL, 's' + 'd'},
    {"viWidth", required_argument, NULL, 'v' + 'w'},
    {"viHeight", required_argument, NULL, 'v' + 'h'},
    {"moduleTestType", required_argument, NULL, 't' + 't'},
    {"moduleTestLoop", required_argument, NULL, 'm' + 'l'},
    {"testFrameCount", required_argument, NULL, 'f' + 'c'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf(
	    "\t%s --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e h265cbr -n 6 -b 4096 "
	    "-o /data/\n",
	    name);
	printf("\trtsp://xx.xx.xx.xx/live/0, Default OPEN\n");
#ifdef RKAIQ
	printf("\t-a | --aiq : enable aiq with dirpath provided, eg:-a "
	       "/etc/iqfiles/, "
	       "set dirpath empty to using path by default, without this option aiq "
	       "should run in other application\n");
#endif
	printf("\t-b | --bitrate : encode bitrate, Default 4096\n");
	printf("\t-i | --bmpFilePathForVpssRgn : input file path of bmp for rgn overlay on "
	       "Vpss chn 1, require "
	       "size:160x96, Default: "
	       "NULL\n");
	printf("\t-I | --bmpFilePathForVencRgn : input file path of bmp for rgn overlay on "
	       "venc chn0 and Venc chn2, require "
	       "size:608x288, Default: "
	       "NULL\n");
	printf("\t-l | --loopCount : main venc stream gets frame count, Default: -1\n");
	printf("\t-o | --outputPath : encode output path, Default: NULL\n");
	printf("\t-n | --camNum : camera num, Default: 6\n");
	printf(
	    "\t-e | --encode : encode type, Default:h265cbr, Value:h264cbr, "
	    "h264vbr, h264avbr "
	    "h265cbr, h265vbr, h265avbr, mjpegcbr, mjpegvbr, h264cbr_slice, h265cbr_slice\n");
	printf("\t-C | --streamCompressMode : data compress mode for each mod 0:afbc "
	       "1:none, default: afbc\n");
	printf("\t-x | --avsCenterX : avs Center X value, Default: 4196\n");
	printf("\t-y | --avsCenterY : avs Center Y value, Default: 2080\n");
	printf("\t-X | --avsFovX : avs Fox X value, Default: 28000\n");
	printf("\t-Y | --avsFoxY : avs Fox Y value, Default: 9500\n");
	printf("\t--avsWidth : avs output Width, Default: 8192\n");
	printf("\t--avsHeight : avs output Hight, Default: 2700\n");
	printf("\t--avsCalibFilePath : avs calib file path, Default: "
	       "/oem/usr/share/avs_calib/calib_file.pto \n");
	printf("\t--avsLutFilePath : avs lut folder path, Default: "
	       "NULL\n");
	printf("\t--avsStitchMode : avs stitch mode, 0:calib 1: middle lut, Default: 0\n");
	printf("\t--avsSyncPipe : avs pipe mode, 0:nonsync 1:sync Default: 0\n");
	printf("\t--avsStreamSource : avs stream data source, 0: camera, 1: specified nv12 "
	       "format's file. default: 0\n");
	printf("\t--avsStreamFilePath : input the yuv file folder path, the yuv's file "
	       "format require: vi_x.bin. default: NULL\n");
	printf("\t--avsStreamFileCompressFormat : stream file compress format. 0: none, 1: "
	       "afbc. default: 0\n");
	printf("\t--mainStreamWidth : main venc stream width, Default: 8192\n");
	printf("\t--mainStreamHeight : main venc stream height, Default: 2700\n");
	printf("\t--mainVencSrcFps : main Venc input fps, Default: 25\n");
	printf("\t--mainVencDstFps : main venc output fps, Default: 25\n");
	printf("\t--jpegWidth : jpeg encode width, Default: 8192\n");
	printf("\t--jpegHeight : jpeg encode height, Default: 2700\n");
	printf("\t--jpegFps : jpeg output fps, Default: 1\n");
	printf("\t--subVencSrcFps : sub venc input fps, Default: 25\n");
	printf("\t--subVencDstFps : sub venc out fps, Default: 25\n");
	printf("\t--viWidth : vi mod width, Default: 2560\n");
	printf("\t--viHeight : vi mod height, Default: 1520\n");
	printf(
	    "\t--moduleTestType : stress test module, 0: ordinary stream 1: PN mode switch 2: avs resolution switch \n \
			3: sub stream resolution switch 4: encode type switch (264 cbr/vbr/slice) (265 cbr/vbr/slice) \n \
			5: bind and unbind test, 6: avs_vpss resolution switch, 7: main stream resolution switch \n \
			8: rgn create and destroy switch default: 0\n");
	printf("\t--moduleTestLoop : module test loop, default: -1\n");
	printf("\t--testFrameCount : set the venc reveive frame count for every test "
	       "loop, default: 500\n");
}

/******************************************************************************
 * function : venc thread
 ******************************************************************************/
static void *venc_get_stream(void *pArgs) {
	SAMPLE_VENC_CTX_S *ctx = (SAMPLE_VENC_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	FILE *fp = RK_NULL;
	void *pData = RK_NULL;
	RK_S32 loopCount = 0;
	RK_S32 i = 0;

	if (ctx->s32ChnId != VENC_JPEG_CHN) {
		if (ctx->dstFilePath) {
			SAMPLE_COMM_TEST_CreateFile(ctx->dstFilePath, &fp, "venc", ctx->s32ChnId, 0);
		}
	}

	RK_LOGD("fp:%p  chnid %d path:%s", fp, ctx->s32ChnId, ctx->dstFilePath);
	while (!g_pModeTest->bVencThreadQuit[ctx->s32ChnId]) {
		if (ctx->bSliceSplit) {
			ctx->stFrame.u32PackCount = ctx->u32SlicePacketNum;
		}
		s32Ret = SAMPLE_COMM_VENC_GetStream(ctx, &pData);
		if (s32Ret == RK_SUCCESS) {
			// exit when complete
			if (ctx->s32loopCount > 0) {
				if (loopCount >= ctx->s32loopCount) {
					SAMPLE_COMM_VENC_ReleaseStream(ctx);
					ctx->s32loopCount = -1;
					program_normal_exit(__func__, __LINE__);
					continue;
				}
			}

			if (ctx->dstFilePath != RK_NULL && ctx->s32ChnId == VENC_JPEG_CHN) {
				SAMPLE_COMM_TEST_CreateFile(ctx->dstFilePath, &fp, "venc", ctx->s32ChnId,
				                            loopCount % 10);
			}
			RK_LOGD("u32PackCount: %d", ctx->stFrame.u32PackCount);
			for (i = 0; i < ctx->stFrame.u32PackCount; i++) {
				pData = (char *)RK_MPI_MB_Handle2VirAddr(ctx->stFrame.pstPack[i].pMbBlk);

				if (fp) {
					fwrite(pData + ctx->stFrame.pstPack[i].u32Offset, 1,
					       ctx->stFrame.pstPack[i].u32Len, fp);
					fflush(fp);
					if (ctx->s32ChnId == VENC_JPEG_CHN) {
						fclose(fp);
						fp = RK_NULL;
					}
				}

				if (g_rtsp_ifenbale && ctx->s32ChnId != VENC_JPEG_CHN) {
					pthread_mutex_lock(&g_rtsp_tx_mutex);
					rtsp_tx_video(g_rtsp_session[ctx->s32ChnId],
					              pData + ctx->stFrame.pstPack[i].u32Offset,
					              ctx->stFrame.pstPack[i].u32Len,
					              ctx->stFrame.pstPack[i].u64PTS);
					rtsp_do_event(g_rtsplive);
					RK_LOGD("chn:%d, loopCount:%d wd:%d offset:%d type: %d\n",
					        ctx->s32ChnId, loopCount, ctx->stFrame.pstPack[i].u32Len,
					        ctx->stFrame.pstPack[i].u32Offset,
					        ctx->stFrame.pstPack[i].DataType.enH265EType);
					pthread_mutex_unlock(&g_rtsp_tx_mutex);
				} else if (ctx->s32ChnId == 0) {
					RK_LOGE("venc chnid:%d loopcount:%d datalen:%d", ctx->s32ChnId,
					        loopCount, ctx->stFrame.pstPack[i].u32Len);
				}
			}

			SAMPLE_COMM_VENC_ReleaseStream(ctx);
			loopCount++;
			if (ctx->s32ChnId != VENC_JPEG_CHN && g_pModeTest->bModuleTestIfopen) {
				pthread_mutex_lock(&g_frame_count_mutex[ctx->s32ChnId]);
				g_pModeTest->s32VencGetFrameCount[ctx->s32ChnId]++;
				pthread_mutex_unlock(&g_frame_count_mutex[ctx->s32ChnId]);

				if (g_pModeTest->s32VencGetFrameCount[ctx->s32ChnId] ==
				    g_pModeTest->s32TestFrameCount) {
					sem_post(&g_sem_module_test[ctx->s32ChnId]);
				}
			}
		}
	}

	if (fp)
		fclose(fp);

	RK_LOGE("venc chnid %d exit", ctx->s32ChnId);

	return RK_NULL;
}

RK_S32 encode_destroy_and_restart(CODEC_TYPE_E enCodecType, VENC_RC_MODE_E enRcMode,
                                  RK_U32 u32Profile, RK_BOOL bIfSliceSplit) {
	RK_S32 s32Ret = RK_FAILURE;
	MPP_CHN_S stSrcChn, stDestChn;
	RK_LOGD("the loopcount: %d", ctx->venc[0].s32loopCount);

	g_pModeTest->bVencThreadQuit[ctx->venc[0].s32ChnId] = RK_TRUE;
	if (ctx->venc[0].getStreamCbFunc) {
		pthread_join(ctx->venc[0].getStreamThread, RK_NULL);
	}

	// Bind VPSS[0,0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[0].s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, ctx->vpss_gpu.s32ChnId, ctx->venc[0].s32ChnId);
		program_handle_error(__func__, __LINE__);
		return s32Ret;
	}
	// Destroy VENC[0]
	s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[0]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn 0 Failure s32Ret:%#X", s32Ret);
		program_handle_error(__func__, __LINE__);
		return s32Ret;
	}

	ctx->venc[0].enCodecType = enCodecType;
	ctx->venc[0].enRcMode = enRcMode;
	ctx->venc[0].stChnAttr.stVencAttr.u32Profile = u32Profile;
	g_pModeTest->bVencThreadQuit[ctx->venc[0].s32ChnId] = RK_FALSE;

	if (bIfSliceSplit) {
		ctx->venc[0].bSliceSplit = RK_TRUE;
		ctx->venc[0].u32SliceMode = 2;
		ctx->venc[0].u32SliceSize = 1;
		ctx->venc[0].u32SlicePacketNum = 16;
		ctx->venc[0].u32OneStreamBuffer = 0;
	} else {
		ctx->venc[0].bSliceSplit = RK_FALSE;
		ctx->venc[0].u32SliceMode = 0;
		ctx->venc[0].u32SliceSize = 0;
		ctx->venc[0].u32SlicePacketNum = 1;
		ctx->venc[0].u32OneStreamBuffer = 1;
	}
	// Init VENC[0]
	s32Ret = SAMPLE_COMM_VENC_CreateChn(&ctx->venc[0]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn 0 Failure s32Ret:%#X", s32Ret);
		program_handle_error(__func__, __LINE__);
		return s32Ret;
	}

	// Bind VPSS[0,0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[0].s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, ctx->vpss_gpu.s32ChnId, ctx->venc[0].s32ChnId);
		program_handle_error(__func__, __LINE__);
		return s32Ret;
	}
	return s32Ret;
}

static void wait_module_test_switch_success(void) {
	RK_S32 i = 0;
	for (i = 0; i < VENC_CHN_MAX; i++) {
		if (i == VENC_JPEG_CHN)
			continue;
		pthread_mutex_lock(&g_frame_count_mutex[i]);
		g_pModeTest->s32VencGetFrameCount[i] = 0;
		pthread_mutex_unlock(&g_frame_count_mutex[i]);
		sem_wait(&g_sem_module_test[i]);
	}
}

static RK_S32 venc_rcmode_switch(VENC_RC_MODE_E eRcMode, RK_S32 chnid) {
	VENC_CHN_ATTR_S pstChnAttr;
	RK_S32 s32Ret = RK_FAILURE;

	memset(&pstChnAttr, 0, sizeof(VENC_CHN_ATTR_S));
	s32Ret = RK_MPI_VENC_GetChnAttr(ctx->venc[chnid].s32ChnId, &pstChnAttr);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VENC_GetChnAttr chn:%d Failure Ret:%#X",
		        ctx->venc[chnid].s32ChnId, s32Ret);
		return RK_FAILURE;
	}
	switch (eRcMode) {
	case VENC_RC_MODE_H264CBR:
		pstChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		pstChnAttr.stRcAttr.stH264Cbr.u32Gop = ctx->venc[chnid].u32Gop;
		pstChnAttr.stRcAttr.stH264Cbr.u32BitRate = ctx->venc[chnid].u32BitRate;
		pstChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = ctx->venc[chnid].u32DstFps;
		pstChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = ctx->venc[chnid].u32SrcFps;
		break;
	case VENC_RC_MODE_H264VBR:
		pstChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
		pstChnAttr.stRcAttr.stH264Vbr.u32Gop = ctx->venc[chnid].u32Gop;
		pstChnAttr.stRcAttr.stH264Vbr.u32BitRate = ctx->venc[chnid].u32BitRate;
		pstChnAttr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = ctx->venc[chnid].u32DstFps;
		pstChnAttr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = ctx->venc[chnid].u32SrcFps;
		break;
	case VENC_RC_MODE_H265CBR:
		pstChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		pstChnAttr.stRcAttr.stH265Cbr.u32Gop = ctx->venc[chnid].u32Gop;
		pstChnAttr.stRcAttr.stH265Cbr.u32BitRate = ctx->venc[chnid].u32BitRate;
		pstChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = ctx->venc[chnid].u32DstFps;
		pstChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = ctx->venc[chnid].u32SrcFps;
		break;
	case VENC_RC_MODE_H265VBR:
		pstChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
		pstChnAttr.stRcAttr.stH265Vbr.u32Gop = ctx->venc[chnid].u32Gop;
		pstChnAttr.stRcAttr.stH265Vbr.u32BitRate = ctx->venc[chnid].u32BitRate;
		pstChnAttr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = ctx->venc[chnid].u32DstFps;
		pstChnAttr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
		pstChnAttr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = ctx->venc[chnid].u32SrcFps;
		break;
	default:
		RK_LOGE("now only support h264cbr/var h265cbr/vbr");
		break;
	}

	s32Ret = RK_MPI_VENC_SetChnAttr(ctx->venc[chnid].s32ChnId, &pstChnAttr);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VENC_SetChnAttr chn:%d Failure Ret:%#X",
		        ctx->venc[chnid].s32ChnId, s32Ret);
		return RK_FAILURE;
	}
	return s32Ret;
}

RK_S32 sample_encode_type_switch(RK_S32 test_loop) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 now_test_loop = 0;
	g_rtsp_ifenbale = RK_FALSE;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		switch (now_test_loop % 6) {
		case 0: // 264 cbr
			RK_LOGE("-------------Switch To H264CBR---------------");
			if (RK_CODEC_TYPE_H264 == ctx->venc[0].enCodecType) {
				s32Ret = venc_rcmode_switch(VENC_RC_MODE_H264CBR, 0);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("switch to 264_cbr failure");
					program_handle_error(__func__, __LINE__);
					return RK_FAILURE;
				}
				break;
			}

			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H264, VENC_RC_MODE_H264CBR,
			                                    100, RK_FALSE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 264_cbr failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		case 1: // 264 vbr
			RK_LOGE("-------------Switch To H264VBR---------------");

			if (RK_CODEC_TYPE_H264 == ctx->venc[0].enCodecType) {
				s32Ret = venc_rcmode_switch(VENC_RC_MODE_H264VBR, 0);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("switch to 264_vbr failure");
					program_handle_error(__func__, __LINE__);
					return RK_FAILURE;
				}
				break;
			}

			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H264, VENC_RC_MODE_H264VBR,
			                                    100, RK_FALSE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 264_vbr failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		case 2: // 265 cbr
			RK_LOGE("-------------Switch To H265CBR---------------");
			if (RK_CODEC_TYPE_H265 == ctx->venc[0].enCodecType) {
				s32Ret = venc_rcmode_switch(VENC_RC_MODE_H265CBR, 0);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("switch to 265_cbr failure");
					program_handle_error(__func__, __LINE__);
					return RK_FAILURE;
				}
				break;
			}

			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H265, VENC_RC_MODE_H265CBR,
			                                    0, RK_FALSE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 265_cbr failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		case 3: // 265 vbr
			RK_LOGE("-------------Switch To H265VBR---------------");

			if (RK_CODEC_TYPE_H265 == ctx->venc[0].enCodecType) {
				s32Ret = venc_rcmode_switch(VENC_RC_MODE_H265VBR, 0);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("switch to 265_vbr failure");
					program_handle_error(__func__, __LINE__);
					return RK_FAILURE;
				}
				break;
			}

			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H265, VENC_RC_MODE_H265VBR,
			                                    0, RK_FALSE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 265_vbr failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		case 4: // 264 cbr slice split
			RK_LOGE("-------------Switch To H264CBR Slice Split---------------");
			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H264, VENC_RC_MODE_H264CBR,
			                                    100, RK_TRUE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 264_slice failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		case 5: // 265 cbr slice split
			RK_LOGE("-------------Switch To H265CBR Slice Split---------------");
			s32Ret = encode_destroy_and_restart(RK_CODEC_TYPE_H265, VENC_RC_MODE_H265CBR,
			                                    0, RK_TRUE);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("switch to 265_slice failure");
				program_handle_error(__func__, __LINE__);
				return RK_FAILURE;
			}
			break;
		default:
			break;
		}

		wait_module_test_switch_success();

		now_test_loop++;
		RK_LOGE("---------------------------------------switch success total:%d "
		        "time:%d--------------------------------",
		        test_loop, now_test_loop);
		if (test_loop > 0 && now_test_loop > test_loop) {
			RK_LOGE("encode_type_switch_test end and passed(success)!!!");
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}

	RK_LOGE("sample_encode_type_switch exit");
	return s32Ret;
}

RK_S32 sample_all_mod_bind(void) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// // Bind VI[0]~VI[5] and avs[0]
		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			stSrcChn.enModId = RK_ID_VI;
			stSrcChn.s32DevId = ctx->vi[i].s32DevId;
			stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
			stDestChn.enModId = RK_ID_AVS;
			stDestChn.s32DevId = ctx->avs.s32GrpId;
			stDestChn.s32ChnId = i;
			s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("vi dev:%d band to avs:%d failure", ctx->vi[i].s32DevId, i);
				return RK_FAILURE;
			}
		}
	}
	// Bind AVS[0] and VPSS[0,0]
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stDestChn.s32ChnId = ctx->vpss_gpu.s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("avs grpid:%d band to vpss grpid:%d failure", ctx->avs.s32GrpId,
		        ctx->vpss_gpu.s32GrpId);
		return RK_FAILURE;
	}

	// Bind VPSS[0,3] and VPSS[1,0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 3;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stDestChn.s32ChnId = ctx->vpss_rga.s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to vpss grpid:%d chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, 3, ctx->vpss_rga.s32GrpId,
		        ctx->vpss_rga.s32ChnId);
		return RK_FAILURE;
	}

	// Bind VPSS[0,0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[0].s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, ctx->vpss_gpu.s32ChnId, ctx->venc[0].s32ChnId);
		return RK_FAILURE;
	}

	// Bind VPSS[0,2] and VENC[3]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 2;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[3].s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, 2, ctx->venc[3].s32ChnId);
		return RK_FAILURE;
	}

	// // Bind VPSS[1,0] and VENC[2]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[2].s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to venc chnid:%d failure",
		        ctx->vpss_rga.s32GrpId, 0, ctx->venc[2].s32ChnId);
		return RK_FAILURE;
	}

	// // Bind VPSS[1,1] and VENC[1]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stSrcChn.s32ChnId = 1;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[1].s32ChnId;
	s32Ret = SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d band to venc chnid:%d failure",
		        ctx->vpss_rga.s32GrpId, 1, ctx->venc[1].s32ChnId);
		return RK_FAILURE;
	}
	return s32Ret;
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

	pstBufAttr.u32Width = g_pModeTest->u32AvsPipeWidth;
	pstBufAttr.u32Height = g_pModeTest->u32AvsPipeHeight;
	pstBufAttr.enCompMode = COMPRESS_MODE_NONE;
	pstBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	s32Ret = RK_MPI_CAL_VGS_GetPicBufferSize(&pstBufAttr, &pstPicCal);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_CAL_VGS_GetPicBufferSize failure:%#X", s32Ret);
		return RK_NULL;
	}

	/* mb pool create */
	for (RK_S32 i = 0; i < g_pModeTest->s32AvsPipeNum; i++) {
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

	if (g_pModeTest->pCAvsInputPathFolder) {
		for (RK_S32 i = 0; i < g_pModeTest->s32AvsPipeNum; i++) {
			snprintf(name, sizeof(name), "%s/%d.yuv", g_pModeTest->pCAvsInputPathFolder,
			         i);
			fp[i] = fopen(name, "rb");
			if (!fp[i]) {
				RK_LOGE("fopen %s failed, error: %s", name, strerror(errno));
				g_pModeTest->pCAvsInputPathFolder = RK_NULL;
			}
		}
	}

	while (!g_pModeTest->send_frame_thread_quit) {

		for (RK_S32 i = 0; i < g_pModeTest->s32AvsPipeNum; i++) {

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

			if (g_pModeTest->pCAvsInputPathFolder) {
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
					program_handle_error(__func__, __LINE__);
				}
			}

			stVideoFrames[i].stVFrame.u32TimeRef = loopCount;
			stVideoFrames[i].stVFrame.u64PTS = SAMPLE_COMM_TEST_GetNowUs();
			RK_MPI_SYS_MmzFlushCache(stVideoFrames[i].stVFrame.pMbBlk, RK_FALSE);
			s32Ret =
			    RK_MPI_AVS_SendPipeFrame(ctx->avs.s32GrpId, i, &stVideoFrames[i], 1000);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_AVS_SendPipeFrame %#X", s32Ret);
				program_handle_error(__func__, __LINE__);
			}
			RK_MPI_MB_ReleaseMB(stVideoFrames[i].stVFrame.pMbBlk);
			mbAppBlk = RK_NULL;
		}

		loopCount++;
		pts = SAMPLE_COMM_TEST_GetNowUs();
		usleep(33 * 1000);
	}

__INIT_FAILURE:
	for (RK_S32 i = 0; i < g_pModeTest->s32AvsPipeNum; i++) {
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

static RK_S32 mod_bind_unbind_test(RK_S32 test_loop) {
	RK_S32 s32TestCount = 0;
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;
	RK_BOOL bIfCompress = RK_FALSE;
	MPP_CHN_S stSrcChn, stDestChn;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		// rgn Deinit failure
		for (i = 0; i < RGN_CHN_MAX; i++) {
			s32Ret = SAMPLE_COMM_RGN_DestroyChn(&ctx->rgn[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_RGN_DestroyChn Failure s32Ret:%#X rgn handle:%d",
				        s32Ret, ctx->rgn[i].rgnHandle);
			}
		}
		// venc[3] ubind and destroy
		g_pModeTest->bVencThreadQuit[3] = RK_TRUE;
		if (ctx->venc[3].getStreamCbFunc) {
			pthread_join(ctx->venc[3].getStreamThread, RK_NULL);
		}
		// UNBind VPSS[0,2] and VENC[3]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
		stSrcChn.s32ChnId = 2;
		stDestChn.enModId = RK_ID_VENC;
		stDestChn.s32DevId = 0;
		stDestChn.s32ChnId = ctx->venc[3].s32ChnId;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
			        ctx->vpss_gpu.s32GrpId, 2, ctx->venc[3].s32ChnId);
			program_handle_error(__func__, __LINE__);
		}
		s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[3]);
		if (s32Ret == RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[3].s32ChnId);
		} else {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[3].s32ChnId,
			        s32Ret);
			program_handle_error(__func__, __LINE__);
		}

		// venc[2] ubind and destroy
		g_pModeTest->bVencThreadQuit[2] = RK_TRUE;
		if (ctx->venc[2].getStreamCbFunc) {
			pthread_join(ctx->venc[2].getStreamThread, RK_NULL);
		}
		// UNBind VPSS[1,0] and VENC[2]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
		stSrcChn.s32ChnId = 0;
		stDestChn.enModId = RK_ID_VENC;
		stDestChn.s32DevId = 0;
		stDestChn.s32ChnId = ctx->venc[2].s32ChnId;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
			        ctx->vpss_rga.s32GrpId, ctx->vpss_rga.s32ChnId,
			        ctx->venc[2].s32ChnId);
			program_handle_error(__func__, __LINE__);
		}
		s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[2]);
		if (s32Ret == RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[2].s32ChnId);
		} else {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[2].s32ChnId,
			        s32Ret);
			program_handle_error(__func__, __LINE__);
		}

		// venc[1] ubind and destroy
		g_pModeTest->bVencThreadQuit[1] = RK_TRUE;
		if (ctx->venc[1].getStreamCbFunc) {
			pthread_join(ctx->venc[1].getStreamThread, RK_NULL);
		}
		// UNBind VPSS[1,1] and VENC[1]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
		stSrcChn.s32ChnId = 1;
		stDestChn.enModId = RK_ID_VENC;
		stDestChn.s32DevId = 0;
		stDestChn.s32ChnId = ctx->venc[1].s32ChnId;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
			        ctx->vpss_rga.s32GrpId, 1, ctx->venc[1].s32ChnId);
			program_handle_error(__func__, __LINE__);
		}
		s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[1]);
		if (s32Ret == RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[1].s32ChnId);
		} else {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[1].s32ChnId,
			        s32Ret);
			program_handle_error(__func__, __LINE__);
		}

		// venc[0] ubind and destroy
		g_pModeTest->bVencThreadQuit[0] = RK_TRUE;
		if (ctx->venc[0].getStreamCbFunc) {
			pthread_join(ctx->venc[0].getStreamThread, RK_NULL);
		}
		// UNBind VPSS[0,0] and VENC[0]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
		stSrcChn.s32ChnId = 0;
		stDestChn.enModId = RK_ID_VENC;
		stDestChn.s32DevId = 0;
		stDestChn.s32ChnId = ctx->venc[0].s32ChnId;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
			        ctx->vpss_gpu.s32GrpId, ctx->vpss_gpu.s32ChnId,
			        ctx->venc[0].s32ChnId);
			program_handle_error(__func__, __LINE__);
		}
		s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[0]);
		if (s32Ret == RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[0].s32ChnId);
		} else {
			RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[0].s32ChnId,
			        s32Ret);
			program_handle_error(__func__, __LINE__);
		}

		// UNBind VPSS[0,3] and VPSS[1,0], destroy vpss_rga
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
		stSrcChn.s32ChnId = 3;
		stDestChn.enModId = RK_ID_VPSS;
		stDestChn.s32DevId = ctx->vpss_rga.s32GrpId;
		stDestChn.s32ChnId = 0;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vpss grpid:%d chnid:%d unband to vpss grpid:%d chnid:%d failure",
			        ctx->vpss_gpu.s32GrpId, 3, ctx->vpss_rga.s32GrpId, 0);
			program_handle_error(__func__, __LINE__);
		}
		SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss_rga);

		// UNBind AVS[0] and VPSS[0,0], destroy vpss_gpu
		stSrcChn.enModId = RK_ID_AVS;
		stSrcChn.s32DevId = ctx->avs.s32GrpId;
		stSrcChn.s32ChnId = ctx->avs.s32ChnId;
		stDestChn.enModId = RK_ID_VPSS;
		stDestChn.s32DevId = ctx->vpss_gpu.s32GrpId;
		stDestChn.s32ChnId = ctx->vpss_gpu.s32ChnId;
		s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("avs grpid:%d unband to vpss grpid:%d failure", ctx->avs.s32GrpId,
			        ctx->vpss_gpu.s32GrpId);
			program_handle_error(__func__, __LINE__);
		}
		SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss_gpu);
		if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
			// UNBind VI[0]~VI[5] and avs[0]
			for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
				stSrcChn.enModId = RK_ID_VI;
				stSrcChn.s32DevId = ctx->vi[i].s32DevId;
				stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
				stDestChn.enModId = RK_ID_AVS;
				stDestChn.s32DevId = ctx->avs.s32GrpId;
				stDestChn.s32ChnId = i;
				s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("vi dev:%d unband to avs:%d failure", ctx->vi[i].s32DevId, i);
					program_handle_error(__func__, __LINE__);
				}
			}
		}

		// Destroy AVS[0]
		SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
		if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
			// Destroy VI[0]
			for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
				SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
			}
			RK_MPI_SYS_Exit();
			SAMPLE_COMM_ISP_CamGroup_Stop(g_pModeTest->s32CamId);

			SAMPLE_COMM_ISP_CamGroup_Init(g_pModeTest->s32CamId, g_pModeTest->hdr_mode, 1,
			                              &g_pModeTest->camgroup_cfg);
			RK_MPI_SYS_Init();
			// Init VI[0] ~ VI[5]
			for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
				if (bIfCompress) {           // compress mode afbc
					ctx->vi[i].s32ChnId = 2; // rk3588 mainpath:0 selfpath:1 fbcpath:2
					ctx->vi[i].stChnAttr.enCompressMode = COMPRESS_AFBC_16x16;
				} else {                     // compress mode none
					ctx->vi[i].s32ChnId = 0; // rk3588 mainpath:0 selfpath:1 fbcpath:2
					ctx->vi[i].stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
				}
				SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
			}
		} else if (g_pModeTest->avs_stream_source == STREAM_FROM_FILE) {
			g_pModeTest->send_frame_thread_quit = RK_TRUE;
			pthread_join(g_pModeTest->send_frame_thread_id, NULL);
		}

		if (bIfCompress) {
			ctx->avs.stAvsChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
		} else {
			ctx->avs.stAvsChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
		}
		SAMPLE_COMM_AVS_CreateChn(&ctx->avs);

		if (bIfCompress) {
			ctx->vpss_gpu.stGrpVpssAttr.enCompressMode = COMPRESS_AFBC_16x16;
			ctx->vpss_gpu.stVpssChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
			ctx->vpss_gpu.stVpssChnAttr[3].enCompressMode = COMPRESS_AFBC_16x16;
		} else {
			ctx->vpss_gpu.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
			ctx->vpss_gpu.stVpssChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
			ctx->vpss_gpu.stVpssChnAttr[3].enCompressMode = COMPRESS_MODE_NONE;
		}
		SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss_gpu);

		if (bIfCompress) {
			ctx->vpss_rga.stGrpVpssAttr.enCompressMode = COMPRESS_AFBC_16x16;
			ctx->vpss_rga.stVpssChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
		} else {
			ctx->vpss_rga.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
			ctx->vpss_rga.stVpssChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
		}
		SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss_rga);

		for (i = 0; i < VENC_CHN_MAX; i++) {
			g_pModeTest->bVencThreadQuit[i] = RK_FALSE;
			SAMPLE_COMM_VENC_CreateChn(&ctx->venc[i]);
		}
		// rgn init
		for (i = 0; i < RGN_CHN_MAX; i++) {
			s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_RGN_CreateChn Handle:%d Failure Ret:%#X",
				        ctx->rgn[i].rgnHandle, s32Ret);
			}
		}
		s32Ret = sample_all_mod_bind();
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("all mod bind Failure !!!");
			program_handle_error(__func__, __LINE__);
		}
		if (g_pModeTest->avs_stream_source == STREAM_FROM_FILE) {
			g_pModeTest->send_frame_thread_quit = RK_FALSE;
			pthread_create(&g_pModeTest->send_frame_thread_id, 0, send_frame_thread,
			               (void *)ctx);
		}
		wait_module_test_switch_success();

		s32TestCount++;
		RK_LOGE("unbind and bind success------------total_loop: %d now_count: %d "
		        "bIfCompress:%d",
		        test_loop, s32TestCount, bIfCompress);
		if (test_loop > 0 && s32TestCount >= test_loop) {
			RK_LOGE("--------------------all mod bind and unbind test "
			        "Pass(success)!-------------------");
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
		bIfCompress = !bIfCompress;
	}
	RK_LOGE("mod_bind_unbind_test exit");
	return RK_SUCCESS;
}

static void pn_mode_switch(RK_U32 test_loop) {
	RK_S32 i = 0;
	RK_U32 s32TestCount = 0;
	RK_S32 s32Ret = RK_FAILURE;
	RK_LOGE("s32CamId: %d hdr: %d  camnum: %d", g_pModeTest->s32CamId,
	        g_pModeTest->hdr_mode, g_pModeTest->camgroup_cfg.sns_num);
	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			RK_MPI_VI_PauseChn(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId);
		}
		SAMPLE_COMM_ISP_CamGroup_Stop(g_pModeTest->s32CamId);
		s32Ret = SAMPLE_COMM_ISP_CamGroup_Init(
		    g_pModeTest->s32CamId, g_pModeTest->hdr_mode, 1, &g_pModeTest->camgroup_cfg);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_ISP_CamGroup_Init failure\n");
			program_handle_error(__func__, __LINE__);
			break;
		}
		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			RK_MPI_VI_ResumeChn(ctx->vi[i].u32PipeId, ctx->vi[i].s32ChnId);
		}

		wait_module_test_switch_success();

		s32TestCount++;
		RK_LOGE("-------------------PN Switch Test success Total: %d Now Count: "
		        "%d-------------------",
		        test_loop, s32TestCount);
		if (test_loop > 0 && s32TestCount >= test_loop) {
			RK_LOGE(
			    "------------------PN test end(pass/success) count: %d-----------------",
			    s32TestCount);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("pn_mode_switch_thread exit");
	return;
}

static RK_S32 avs_dynamic_switch(RK_S32 test_loop) {
	AVS_CHN_ATTR_S avsChn;
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 s32TestCount = 0;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		memset(&avsChn, 0, sizeof(AVS_CHN_ATTR_S));
		s32Ret = RK_MPI_AVS_GetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_handle_error(__func__, __LINE__);
			return s32Ret;
		}

		if (avsChn.u32Width == ctx->avs.stAvsChnAttr[0].u32Width) {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width / 2;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height / 2;
		} else {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height;
		}

		s32Ret = RK_MPI_AVS_SetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_handle_error(__func__, __LINE__);
			return s32Ret;
		}

		wait_module_test_switch_success();

		s32TestCount++;
		RK_LOGE("-------Avs Dpi Switch to <%d x %d> success Toatl: %d   Now Count: "
		        "%d---------",
		        avsChn.u32Width, avsChn.u32Height, test_loop, s32TestCount);

		if (test_loop > 0 && s32TestCount >= test_loop) {
			RK_LOGE("Avs Dpi Switch Test End Toatl: %d", test_loop);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("avs_dynamic_switch exit");
	return s32Ret;
}

static void sub_venc_switch_dpi(RK_S32 test_loop) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 sub_venc_1_width = ctx->vpss_rga.stVpssChnAttr[1].u32Width;
	RK_S32 switch_count = 0;

	// Frame count refer chn is venc[1]
	g_pModeTest->s32FrameCountReferChn = 1;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		s32Ret = RK_MPI_VPSS_GetChnAttr(ctx->vpss_rga.s32GrpId, 1,
		                                &ctx->vpss_rga.stVpssChnAttr[1]);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_GetChnAttr Failure s32Ret: %#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss_rga.s32GrpId, 1);
			program_handle_error(__func__, __LINE__);
			break;
		}
		if (sub_venc_1_width == ctx->vpss_rga.stVpssChnAttr[1].u32Width) {
			ctx->vpss_rga.stVpssChnAttr[1].u32Width = 3840;
			ctx->vpss_rga.stVpssChnAttr[1].u32Height = 1248;
			RK_LOGE("sub venc resolution switch to %dx%d",
			        ctx->vpss_rga.stVpssChnAttr[1].u32Width,
			        ctx->vpss_rga.stVpssChnAttr[1].u32Height);
		} else if (3840 == ctx->vpss_rga.stVpssChnAttr[1].u32Width) {
			ctx->vpss_rga.stVpssChnAttr[1].u32Width = 2048;
			ctx->vpss_rga.stVpssChnAttr[1].u32Height = 680;
			RK_LOGE("sub venc resolution switch to %dx%d",
			        ctx->vpss_rga.stVpssChnAttr[1].u32Width,
			        ctx->vpss_rga.stVpssChnAttr[1].u32Height);
		}
		s32Ret = RK_MPI_VPSS_SetChnAttr(ctx->vpss_rga.s32GrpId, 1,
		                                &ctx->vpss_rga.stVpssChnAttr[1]);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_SetChnAttr Failure s32Ret:%#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss_rga.s32GrpId, 1);
			program_handle_error(__func__, __LINE__);
			break;
		}

		wait_module_test_switch_success();

		switch_count++;
		RK_LOGE("-------sub Venc Dpi Switch <%d x %d> Toatl: %d   Now Count: %d---------",
		        ctx->vpss_rga.stVpssChnAttr[1].u32Width,
		        ctx->vpss_rga.stVpssChnAttr[1].u32Height, test_loop, switch_count);

		if (test_loop > 0 && switch_count >= test_loop) {
			RK_LOGE("sub Venc Dpi Switch Test End Toatl: %d", test_loop);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("sub_venc_switch_dpi exit");
	return;
}

static RK_S32 Rgn_Init(RK_CHAR *pBmpFilePathForVpssRgn, RK_CHAR *pBmpFilePathForVencRgn) {

	RK_S32 s32Ret = RK_FAILURE;
	// Init RGN[0] for vpss group
	ctx->rgn[0].rgnHandle = 0;
	ctx->rgn[0].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[0].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[0].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[0].stMppChn.s32DevId = ctx->vpss_gpu.s32GrpId;
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

	// Init RGN[1] for vpss group
	ctx->rgn[1].rgnHandle = 1;
	ctx->rgn[1].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[1].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[1].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[1].stMppChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	ctx->rgn[1].stRegion.s32X = 0; // must be 16 aligned
	ctx->rgn[1].stRegion.s32Y = RK_ALIGN_16(ctx->avs.stAvsChnAttr->u32Height - 640);
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

	// Init RGN[2] for vpss group
	ctx->rgn[2].rgnHandle = 2;
	ctx->rgn[2].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[2].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[2].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[2].stMppChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	ctx->rgn[2].stRegion.s32X = RK_ALIGN_16(ctx->avs.stAvsChnAttr->u32Width - 640);
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

	// Init RGN[3] for vpss group
	ctx->rgn[3].rgnHandle = 3;
	ctx->rgn[3].stRgnAttr.enType = COVER_RGN;
	ctx->rgn[3].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[3].stMppChn.s32ChnId = VPSS_MAX_CHN_NUM;
	ctx->rgn[3].stMppChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	ctx->rgn[3].stRegion.s32X =
	    RK_ALIGN_16(ctx->avs.stAvsChnAttr->u32Width - 640); // must be 16 aligned
	ctx->rgn[3].stRegion.s32Y =
	    RK_ALIGN_16(ctx->avs.stAvsChnAttr->u32Height - 640); // must be 16 aligned
	ctx->rgn[3].stRegion.u32Width = 640;                     // must be 16 aligned
	ctx->rgn[3].stRegion.u32Height = 640;                    // must be 16 aligned
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

	// Init RGN[4] for venc[0]
	ctx->rgn[4].rgnHandle = 4;
	ctx->rgn[4].stRgnAttr.enType = OVERLAY_RGN;
	ctx->rgn[4].stMppChn.enModId = RK_ID_VENC;
	ctx->rgn[4].stMppChn.s32ChnId = ctx->venc[0].s32ChnId;
	ctx->rgn[4].stMppChn.s32DevId = 0;
	ctx->rgn[4].stRegion.s32X =
	    RK_ALIGN_16(ctx->vpss_gpu.stVpssChnAttr[0].u32Width / 2); // must be 16 aligned
	ctx->rgn[4].stRegion.s32Y =
	    RK_ALIGN_16(ctx->vpss_gpu.stVpssChnAttr[0].u32Height / 2); // must be 16 aligned
	ctx->rgn[4].stRegion.u32Width = 608;                           // must be 16 aligned
	ctx->rgn[4].stRegion.u32Height = 288;                          // must be 16 aligned
	ctx->rgn[4].u32BmpFormat = RK_FMT_BGRA5551;
	ctx->rgn[4].u32BgAlpha = 128;
	ctx->rgn[4].u32FgAlpha = 128;
	ctx->rgn[4].u32Layer = 6;
	ctx->rgn[4].srcFileBmpName = pBmpFilePathForVencRgn;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[4]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[4].rgnHandle);
		return s32Ret;
	}

	// Init RGN[5] for venc[2]
	ctx->rgn[5].rgnHandle = 5;
	ctx->rgn[5].stRgnAttr.enType = OVERLAY_RGN;
	ctx->rgn[5].stMppChn.enModId = RK_ID_VENC;
	ctx->rgn[5].stMppChn.s32ChnId = ctx->venc[2].s32ChnId;
	ctx->rgn[5].stMppChn.s32DevId = 0;
	ctx->rgn[5].stRegion.s32X =
	    RK_ALIGN_16(ctx->venc[2].u32Width / 2); // must be 16 aligned
	ctx->rgn[5].stRegion.s32Y =
	    RK_ALIGN_16(ctx->venc[2].u32Height / 2); // must be 16 aligned
	ctx->rgn[5].stRegion.u32Width = 608;         // must be 16 aligned
	ctx->rgn[5].stRegion.u32Height = 288;        // must be 16 aligned
	ctx->rgn[5].u32BmpFormat = RK_FMT_BGRA5551;
	ctx->rgn[5].u32BgAlpha = 128;
	ctx->rgn[5].u32FgAlpha = 128;
	ctx->rgn[5].u32Layer = 6;
	ctx->rgn[5].srcFileBmpName = pBmpFilePathForVencRgn;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[5]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[5].rgnHandle);
		return s32Ret;
	}

	// Init RGN[6] for vpss chn1
	ctx->rgn[6].rgnHandle = 6;
	ctx->rgn[6].stRgnAttr.enType = OVERLAY_RGN;
	ctx->rgn[6].stMppChn.enModId = RK_ID_VPSS;
	ctx->rgn[6].stMppChn.s32ChnId = 1;
	ctx->rgn[6].stMppChn.s32DevId = ctx->vpss_rga.s32GrpId;
	ctx->rgn[6].stRegion.s32X =
	    RK_ALIGN_16(ctx->vpss_rga.stVpssChnAttr[1].u32Width / 2); // must be 16 aligned
	ctx->rgn[6].stRegion.s32Y =
	    RK_ALIGN_16(ctx->vpss_rga.stVpssChnAttr[1].u32Height / 2); // must be 16 aligned
	ctx->rgn[6].stRegion.u32Width = 160;                           // must be 16 aligned
	ctx->rgn[6].stRegion.u32Height = 96;                           // must be 16 aligned
	ctx->rgn[6].u32BmpFormat = RK_FMT_BGRA5551;
	ctx->rgn[6].u32BgAlpha = 128;
	ctx->rgn[6].u32FgAlpha = 128;
	ctx->rgn[6].u32Layer = 6;
	ctx->rgn[6].srcFileBmpName = pBmpFilePathForVpssRgn;
	s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[6]);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_RGN_CreateChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
		        ctx->rgn[6].rgnHandle);
		return s32Ret;
	}

	return s32Ret;
}

static RK_S32 Rgn_Deinit(void) {
	RK_S32 s32Ret = RK_SUCCESS;
	for (RK_S32 i = 0; i < RGN_CHN_MAX; i++) {
		s32Ret = SAMPLE_COMM_RGN_DestroyChn(&ctx->rgn[i]);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("SAMPLE_COMM_RGN_DestroyChn Failure s32Ret:%#X rgn handle:%d", s32Ret,
			        ctx->rgn[i].rgnHandle);
		}
	}
	return s32Ret;
}

static RK_S32 avs_vpss_switch_dpi(RK_S32 test_loop) {
	AVS_CHN_ATTR_S avsChn;
	VPSS_CHN_ATTR_S vpssChnAttr;
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 s32TestCount = 0;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		memset(&avsChn, 0, sizeof(AVS_CHN_ATTR_S));
		memset(&vpssChnAttr, 0, sizeof(VPSS_CHN_ATTR_S));
		s32Ret = RK_MPI_AVS_GetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_handle_error(__func__, __LINE__);
			break;
		}
		s32Ret = RK_MPI_VPSS_GetChnAttr(ctx->vpss_gpu.s32GrpId, 0, &vpssChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE(
			    "RK_MPI_VPSS_GetChnAttr Failure s32Ret:%#X  vpssGroup:%d  vpssChnid:%d",
			    s32Ret, ctx->vpss_gpu.s32GrpId, 0);
			program_handle_error(__func__, __LINE__);
			break;
		}

		if (avsChn.u32Width == ctx->avs.stAvsChnAttr[0].u32Width) {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width / 2;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height / 2;
		} else {
			avsChn.u32Width = ctx->avs.stAvsChnAttr[0].u32Width;
			avsChn.u32Height = ctx->avs.stAvsChnAttr[0].u32Height;
		}

		vpssChnAttr.u32Width = avsChn.u32Width;
		vpssChnAttr.u32Height = avsChn.u32Height;

		s32Ret = RK_MPI_AVS_SetChnAttr(ctx->avs.s32GrpId, ctx->avs.s32ChnId, &avsChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_AVS_GetChnAttr Failure s32Ret:%#X  avsGroup:%d  avsChnid:%d",
			        s32Ret, ctx->avs.s32GrpId, ctx->avs.s32ChnId);
			program_handle_error(__func__, __LINE__);
			break;
		}

		s32Ret = RK_MPI_VPSS_SetChnAttr(ctx->vpss_gpu.s32GrpId, 0, &vpssChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_SetChnAttr Failure s32Ret:%#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss_gpu.s32GrpId, 0);
			program_handle_error(__func__, __LINE__);
			break;
		}

		wait_module_test_switch_success();

		s32TestCount++;
		RK_LOGE(
		    "-------Avs and Vpss Dpi Switch to <%d x %d> success Toatl: %d   Now Count: "
		    "%d---------",
		    avsChn.u32Width, avsChn.u32Height, test_loop, s32TestCount);

		if (test_loop > 0 && s32TestCount >= test_loop) {
			RK_LOGE("Avs and Vpss Dpi Switch Test End Toatl: %d", test_loop);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("avs_vpss_switch_dpi exit");
	return s32Ret;
}

static RK_S32 main_venc_switch_dpi(RK_S32 test_loop) {
	RK_S32 s32Ret = RK_FAILURE;
	VPSS_CHN_ATTR_S vencChnAttr;
	RK_S32 switch_count = 0;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		memset(&vencChnAttr, 0, sizeof(VPSS_CHN_ATTR_S));
		s32Ret = RK_MPI_VPSS_GetChnAttr(ctx->vpss_gpu.s32GrpId, 0, &vencChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_GetChnAttr Failure s32Ret: %#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss_gpu.s32GrpId, 0);
			program_handle_error(__func__, __LINE__);
			break;
		}
		if (vencChnAttr.u32Width == ctx->vpss_gpu.stVpssChnAttr[0].u32Width) {
			vencChnAttr.u32Width = ctx->vpss_gpu.stVpssChnAttr[0].u32Width / 2;
			vencChnAttr.u32Height = ctx->vpss_gpu.stVpssChnAttr[0].u32Height / 2;
		} else {
			vencChnAttr.u32Width = ctx->vpss_gpu.stVpssChnAttr[0].u32Width;
			vencChnAttr.u32Height = ctx->vpss_gpu.stVpssChnAttr[0].u32Height;
		}
		s32Ret = RK_MPI_VPSS_SetChnAttr(ctx->vpss_gpu.s32GrpId, 0, &vencChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_SetChnAttr Failure s32Ret:%#X  VpssGrp:%d VpssChnid:%d",
			        s32Ret, ctx->vpss_gpu.s32GrpId, 0);
			program_handle_error(__func__, __LINE__);
			break;
		}

		wait_module_test_switch_success();

		switch_count++;
		RK_LOGE("-------Main Venc Dpi Switch success <%d x %d> Toatl: %d   Now Count: "
		        "%d---------",
		        vencChnAttr.u32Width, vencChnAttr.u32Height, test_loop, switch_count);

		if (test_loop > 0 && switch_count >= test_loop) {
			RK_LOGE("Main Venc Dpi Switch Test End Toatl: %d", test_loop);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("Main Venc Dpi Switch exit");
	return s32Ret;
}

static RK_S32 rgn_create_and_destroy(RK_S32 test_loop) {
	RK_S32 s32NowLoopCount = 0;
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 i = 0;

	// Frame count refer chn is venc[0]
	g_pModeTest->s32FrameCountReferChn = 0;
	while (!g_pModeTest->bModuleTestThreadQuit) {

		// rgn Deinit failure
		for (i = 0; i < RGN_CHN_MAX; i++) {
			s32Ret = SAMPLE_COMM_RGN_DestroyChn(&ctx->rgn[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_RGN_DestroyChn Failure s32Ret:%#X rgn handle:%d",
				        s32Ret, ctx->rgn[i].rgnHandle);
			}
		}

		// rgn init
		for (i = 0; i < RGN_CHN_MAX; i++) {
			s32Ret = SAMPLE_COMM_RGN_CreateChn(&ctx->rgn[i]);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("SAMPLE_COMM_RGN_CreateChn Handle:%d Failure Ret:%#X",
				        ctx->rgn[i].rgnHandle, s32Ret);
			}
		}

		wait_module_test_switch_success();

		s32NowLoopCount++;
		RK_LOGE("-------rgn create and destroy test total: %d   Now Count: %d---------",
		        test_loop, s32NowLoopCount);

		if (test_loop > 0 && s32NowLoopCount >= test_loop) {
			RK_LOGE("rgn create and destroy test success, total:%d", test_loop);
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
			break;
		}
	}
	RK_LOGE("rgn_create_and_destroy exit");
	return s32Ret;
}

static void *sample_module_test(void *pArgs) {
	prctl(PR_SET_NAME, "sample_module_test");
	sleep(MODULE_TEST_DELAY_SECOND_TIME);

	switch (g_pModeTest->s32ModuleTestType) {
	case 1: // P/N mode swithc
		if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
			pn_mode_switch(g_pModeTest->s32ModuleTestLoop);
		} else {
			RK_LOGE("isp and vi mod not running, this mode dosen't support PN mode "
			        "switch test");
			g_pModeTest->bModuleTestIfopen = RK_FALSE;
			program_normal_exit(__func__, __LINE__);
		}
		break;
	case 2: // avs resolution switch
		avs_dynamic_switch(g_pModeTest->s32ModuleTestLoop);
		break;
	case 3: // sub resolution switch
		sub_venc_switch_dpi(g_pModeTest->s32ModuleTestLoop);
		break;
	case 4: // encode type switch  264(cbr/vbr/slice) 265(cbr/vbr/slice)
		sample_encode_type_switch(g_pModeTest->s32ModuleTestLoop);
		break;
	case 5: // bind and unbind switch( NV12/AFBC)
		mod_bind_unbind_test(g_pModeTest->s32ModuleTestLoop);
		break;
	case 6: // avs vpss resolution switch test
		avs_vpss_switch_dpi(g_pModeTest->s32ModuleTestLoop);
		break;
	case 7: // main stream resolution switch
		main_venc_switch_dpi(g_pModeTest->s32ModuleTestLoop);
		break;
	case 8: // rgn create and destroy switch
		rgn_create_and_destroy(g_pModeTest->s32ModuleTestLoop);
		break;
	default:
		RK_LOGE("test module is unexist!!!");
		break;
	}
	return RK_NULL;
}

static RK_S32 rtsp_init(CODEC_TYPE_E enCodecType) {
	RK_S32 i = 0;
	char buffer[BUFFER_BYTE_SIZE] = {0};
	g_rtsplive = create_rtsp_demo(554);
	for (i = 0; i < VENC_CHN_MAX; i++) {
		if (i == VENC_JPEG_CHN) {
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
			g_rtsp_ifenbale = RK_FALSE;
			return RK_SUCCESS;
		}
		rtsp_sync_video_ts(g_rtsp_session[i], rtsp_get_reltime(), rtsp_get_ntptime());
	}
	g_rtsp_ifenbale = RK_TRUE;
	return RK_SUCCESS;
}

static RK_S32 rtsp_deinit(void) {
	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	return RK_SUCCESS;
}

static RK_S32 global_param_init(void) {
	RK_S32 i = 0;

	ctx = (SAMPLE_MPI_CTX_S *)(malloc(sizeof(SAMPLE_MPI_CTX_S)));
	if (ctx == RK_NULL) {
		return RK_FAILURE;
	}
	memset(ctx, 0, sizeof(SAMPLE_MPI_CTX_S));

	g_pModeTest = (g_pModeTest_t *)malloc(sizeof(g_pModeTest_t));
	if (g_pModeTest == RK_NULL) {
		return RK_FAILURE;
	}
	memset(g_pModeTest, 0, sizeof(g_pModeTest_t));

	g_pModeTest->s32ModuleTestLoop = -1;
	g_pModeTest->s32TestFrameCount = 500;
	g_pModeTest->s32ModuleTestType = 0;
	g_pModeTest->s32AvsPipeNum = 6;
	g_pModeTest->u32AvsPipeWidth = 2560;
	g_pModeTest->u32AvsPipeHeight = 1520;
	g_pModeTest->avs_stream_source = STREAM_FROM_ISP_CAMERA;

	// init mutex
	if (pthread_mutex_init(&g_rtsp_tx_mutex, NULL) != 0) {
		RK_LOGE("mutex init failure \n");
		return RK_FAILURE;
	}
	for (i = 0; i < VENC_CHN_MAX; i++) {
		if (pthread_mutex_init(&g_frame_count_mutex[i], NULL) != 0) {
			RK_LOGE("mutex init failure \n");
			return RK_FAILURE;
		}
		// init sem
		sem_init(&g_sem_module_test[i], 0, 0);
	}
	return RK_SUCCESS;
}

static RK_S32 global_param_deinit(void) {
	RK_S32 i = 0;

	for (i = 0; i < VENC_CHN_MAX; i++) {
		// destroy sem
		sem_destroy(&g_sem_module_test[i]);
		pthread_mutex_destroy(&g_frame_count_mutex[i]);
	}

	// destroy mutex
	pthread_mutex_destroy(&g_rtsp_tx_mutex);

	if (g_pModeTest) {
		free(g_pModeTest);
		g_pModeTest = RK_NULL;
	}
	if (ctx) {
		free(ctx);
		ctx = RK_NULL;
	}

	return RK_SUCCESS;
}

/******************************************************************************
 * function    : main()
 * Description : main
 ******************************************************************************/
int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 u32ViWidth = 2560;
	RK_U32 u32ViHeight = 1520;
	RK_U32 u32AvsWidth = 8192;
	RK_U32 u32AvsHeight = 2700;
	RK_U32 u32MainVencWidth = 8192;
	RK_U32 u32MainVencHeight = 2700;
	RK_U32 u32VencJpegWidth = 8192;
	RK_U32 u32VencJpegHeight = 2700;
	RK_CHAR *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pAvsMeshAlphaPath = "/tmp/";
	RK_CHAR *pAvsLutFilePath = RK_NULL;
	RK_CHAR *pBmpFilePathForVpssRgn = RK_NULL;
	RK_CHAR *pBmpFilePathForVencRgn = RK_NULL;
	RK_CHAR *pOutPathVenc = NULL;
	RK_CHAR *pAvsStreamFilePath = RK_NULL;
	CODEC_TYPE_E enCodecType = RK_CODEC_TYPE_H265;
	VENC_RC_MODE_E enRcMode = VENC_RC_MODE_H265CBR;
	CODEC_TYPE_E enJpegCodecType = RK_CODEC_TYPE_JPEG;
	VENC_RC_MODE_E enJpegRcMode = VENC_RC_MODE_MJPEGCBR;
	RK_CHAR *pCodecName = "H265";
	RK_S32 s32JpegFps = 1;
	RK_S32 s32MainVencSrcFps = 25;
	RK_S32 s32MainVencDstFps = 25;
	RK_S32 s32SubVencSrcFps = 25;
	RK_S32 s32SubVencDstFps = 25;
	RK_S32 s32AvsCenterX = 4196;
	RK_S32 s32avsCenterY = 2080;
	RK_S32 s32avsFovX = 28000;
	RK_S32 s32avsFoxY = 9500;
	RK_BOOL bAvsSyncPipe = RK_TRUE;
	RK_BOOL bIfOpenSliceSplit = RK_FALSE;
	RK_S32 s32CamId = 0;
	RK_S32 s32CamNum = 6;
	RK_S32 s32LoopCnt = -1;
	RK_S32 s32BitRate = 4 * 1024;
	RK_S32 i;
	RK_S32 s32AvsStitchMode = 0;
	COMPRESS_MODE_E enStreamCompressMode = COMPRESS_AFBC_16x16;
	MPP_CHN_S stSrcChn, stDestChn;
	RK_S32 s32RevLongoptInput = 0;

	pthread_t module_test_thread_id;

	if (argc < 2) {
		print_usage(argv[0]);
		return RK_FAILURE;
	}

	s32Ret = global_param_init();
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("param_init fail");
		return RK_FAILURE;
	}

	signal(SIGINT, sigterm_handler);

	int c;
	char *iq_file_dir = NULL;
	while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
		const char *tmp_optarg = optarg;
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
		case 'b':
			s32BitRate = atoi(optarg);
			break;
		case 'i':
			pBmpFilePathForVpssRgn = optarg;
			break;
		case 'I':
			pBmpFilePathForVencRgn = optarg;
			break;
		case 'l':
			s32LoopCnt = atoi(optarg);
			break;
		case 'o':
			pOutPathVenc = optarg;
			break;
		case 'n':
			s32CamNum = atoi(optarg);
			g_pModeTest->s32AvsPipeNum = s32CamNum;
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
			} else if (!strcmp(optarg, "h264cbr_slice")) {
				enCodecType = RK_CODEC_TYPE_H264;
				enRcMode = VENC_RC_MODE_H264CBR;
				pCodecName = "H264";
				bIfOpenSliceSplit = RK_TRUE;
			} else if (!strcmp(optarg, "h265cbr_slice")) {
				enCodecType = RK_CODEC_TYPE_H265;
				enRcMode = VENC_RC_MODE_H265CBR;
				pCodecName = "H265";
				bIfOpenSliceSplit = RK_TRUE;
			} else {
				printf("ERROR: Invalid encoder type.\n");
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'C':
			if (0 == atoi(optarg)) {
				enStreamCompressMode = COMPRESS_AFBC_16x16;
			} else if (1 == atoi(optarg)) {
				enStreamCompressMode = COMPRESS_MODE_NONE;
			} else {
				RK_LOGE("input the enStreamCompressMode param invaild, input (0--1) for "
				        "(afbc/none)");
				print_usage(argv[0]);
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'x':
			s32AvsCenterX = atoi(optarg);
			break;
		case 'y':
			s32avsCenterY = atoi(optarg);
			break;
		case 'X':
			s32avsFovX = atoi(optarg);
			break;
		case 'Y':
			s32avsFoxY = atoi(optarg);
			break;
		case 'A' + 'W':
			u32AvsWidth = atoi(optarg);
			break;
		case 'A' + 'H':
			u32AvsHeight = atoi(optarg);
			break;
		case 'A' + 'C':
			pAvsCalibFilePath = optarg;
			if (!pAvsCalibFilePath) {
				RK_LOGE("avs stitch file is null, please check input!");
				print_usage(argv[0]);
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'A' + 'L':
			pAvsLutFilePath = optarg;
			if (!pAvsLutFilePath) {
				RK_LOGE("avs stitch file is null, please check input!");
				print_usage(argv[0]);
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'A' + 'M':
			s32AvsStitchMode = atoi(optarg);
			break;
		case 'A' + 'S':
			if (0 == atoi(optarg)) {
				bAvsSyncPipe = RK_FALSE;
			} else if (1 == atoi(optarg)) {
				bAvsSyncPipe = RK_TRUE;
			} else {
				RK_LOGE("input the bAvsSyncPipe param invaild, input '0' or '1' ");
				print_usage(argv[0]);
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'a' + 's':
			s32RevLongoptInput = atoi(optarg);
			if (s32RevLongoptInput == 0) {
				g_pModeTest->avs_stream_source = STREAM_FROM_ISP_CAMERA;
			} else if (s32RevLongoptInput == 1) {
				g_pModeTest->avs_stream_source = STREAM_FROM_FILE;
			} else {
				RK_LOGE("avs_stream_source Value is invaild, please check input");
				goto __INVAILD_INPUT;
			}
			break;
		case 'f' + 'p':
			pAvsStreamFilePath = optarg;
			break;
		case 'f' + 'f':
			s32RevLongoptInput = atoi(optarg);
			if (s32RevLongoptInput == 0) {
				g_pModeTest->source_compress_mode = COMPRESS_MODE_NONE;
			} else if (s32RevLongoptInput == 1) {
				g_pModeTest->source_compress_mode = COMPRESS_AFBC_16x16;
			} else {
				RK_LOGE("avs_stream_file_compress_format Value is invaild, please check "
				        "input");
				goto __INVAILD_INPUT;
			}
			break;
		case 'm' + 'w':
			u32MainVencWidth = atoi(optarg);
			break;
		case 'm' + 'h':
			u32MainVencHeight = atoi(optarg);
			break;
		case 'm' + 's':
			s32MainVencSrcFps = atoi(optarg);
			break;
		case 'm' + 'd':
			s32MainVencDstFps = atoi(optarg);
			break;
		case 'j' + 'w':
			u32VencJpegWidth = atoi(optarg);
			break;
		case 'j' + 'h':
			u32VencJpegHeight = atoi(optarg);
			break;
		case 'j' + 'f':
			s32JpegFps = atoi(optarg);
			break;
		case 's' + 's':
			s32SubVencSrcFps = atoi(optarg);
			break;
		case 's' + 'd':
			s32SubVencDstFps = atoi(optarg);
			break;
		case 'v' + 'w':
			u32ViWidth = atoi(optarg);
			g_pModeTest->u32AvsPipeWidth = u32ViWidth;
			break;
		case 'v' + 'h':
			u32ViHeight = atoi(optarg);
			g_pModeTest->u32AvsPipeHeight = u32ViHeight;
			break;
		case 't' + 't':
			g_pModeTest->s32ModuleTestType = atoi(optarg);
			if (g_pModeTest->s32ModuleTestType < 0 ||
			    g_pModeTest->s32ModuleTestType > 8) {
				RK_LOGE("input the s32ModuleTestType param invaild, input (0--8)");
				print_usage(argv[0]);
				g_exit_result = RK_FAILURE;
				goto __FAILED2;
			}
			break;
		case 'm' + 'l':
			g_pModeTest->s32ModuleTestLoop = atoi(optarg);
			break;
		case 'f' + 'c':
			g_pModeTest->s32TestFrameCount = atoi(optarg);
			break;
		__INVAILD_INPUT:
		case '?':
		default:
			print_usage(argv[0]);
			g_exit_result = RK_FAILURE;
			goto __FAILED2;
		}
	}

	printf("#CameraIdx: %d\n", s32CamId);
	printf("#pAvsCalibFilePath: %s\n", pAvsCalibFilePath);
	printf("#pAvsMeshAlphaPath: %s\n", pAvsMeshAlphaPath);
	printf("#pAvsLutFilePath: %s\n", pAvsLutFilePath);
	printf("#CodecName:%s\n", pCodecName);
	printf("#Output Path: %s\n", pOutPathVenc);
	printf("#IQ Path: %s\n", iq_file_dir);
	if (iq_file_dir && g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
#ifdef RKAIQ
		printf("#Rkaiq XML DirPath: %s\n", iq_file_dir);
		g_pModeTest->s32CamId = 0;
		g_pModeTest->hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
		g_pModeTest->camgroup_cfg.sns_num = s32CamNum;
		g_pModeTest->camgroup_cfg.config_file_dir = iq_file_dir;

		SAMPLE_COMM_ISP_CamGroup_Init(g_pModeTest->s32CamId, g_pModeTest->hdr_mode, 1,
		                              &g_pModeTest->camgroup_cfg);
#endif
	}

	// init rtsp
	if (rtsp_init(enCodecType) != RK_SUCCESS) {
		RK_LOGE("rtsp_init failure");
		g_exit_result = RK_FAILURE;
		goto __FAILED2;
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		g_exit_result = RK_FAILURE;
		goto __FAILED;
	}
	if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// Init VI[0] ~ VI[5]
		for (i = 0; i < s32CamNum; i++) {
			ctx->vi[i].u32Width = u32ViWidth;
			ctx->vi[i].u32Height = u32ViHeight;
			ctx->vi[i].s32DevId = i;
			ctx->vi[i].u32PipeId = i;
			ctx->vi[i].s32ChnId = 2; // rk3588 mainpath:0 selfpath:1 fbcpath:2
			ctx->vi[i].stChnAttr.stIspOpt.u32BufCount = 8;
			ctx->vi[i].stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
			ctx->vi[i].stChnAttr.u32Depth = 2;
			ctx->vi[i].stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
			ctx->vi[i].stChnAttr.enCompressMode = COMPRESS_AFBC_16x16;
			ctx->vi[i].stChnAttr.stFrameRate.s32SrcFrameRate = -1;
			ctx->vi[i].stChnAttr.stFrameRate.s32DstFrameRate = -1;
			SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
		}
	} else if (g_pModeTest->avs_stream_source == STREAM_FROM_FILE) {
		g_pModeTest->pCAvsInputPathFolder = pAvsStreamFilePath;
	}
	// Init avs[0]
	ctx->avs.s32GrpId = 0;
	ctx->avs.s32ChnId = 0;
	ctx->avs.stAvsModParam.enMBSource = MB_SOURCE_PRIVATE;
	ctx->avs.stAvsModParam.u32WorkingSetSize = 67 * 1024;
	ctx->avs.stAvsGrpAttr.enMode = 0; // 0: blend 1: no blend
	ctx->avs.stAvsGrpAttr.u32PipeNum = s32CamNum;
	ctx->avs.stAvsGrpAttr.stGainAttr.enMode = AVS_GAIN_MODE_AUTO;
	ctx->avs.stAvsGrpAttr.stOutAttr.enPrjMode = AVS_PROJECTION_EQUIRECTANGULAR;
	ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = s32AvsCenterX;
	ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = s32avsCenterY;
	ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = s32avsFovX;
	ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = s32avsFoxY;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Width = u32AvsWidth;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Height = u32AvsHeight;
	ctx->avs.stAvsGrpAttr.bSyncPipe = bAvsSyncPipe;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].enCompressMode = enStreamCompressMode;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].u32Depth = 2;
	ctx->avs.stAvsChnAttr[0].u32FrameBufCnt = 8;
	ctx->avs.stAvsChnAttr[0].u32Width = u32AvsWidth;
	ctx->avs.stAvsChnAttr[0].u32Height = u32AvsHeight;
	ctx->avs.stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	if (s32AvsStitchMode == 0) { // stitch file is calib file
		ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
		ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = pAvsCalibFilePath;
		ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pMeshAlphaPath = pAvsMeshAlphaPath;
	} else if (s32AvsStitchMode == 1) { // stitch file is lut file
		ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_LUT;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.enAccuracy = AVS_LUT_ACCURACY_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.enFuseWidth = AVS_FUSE_WIDTH_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.stLutStep.enStepX = AVS_LUT_STEP_HIGH;
		ctx->avs.stAvsGrpAttr.stInAttr.stLUT.stLutStep.enStepY = AVS_LUT_STEP_HIGH;
		ctx->avs.pLutFilePath = pAvsLutFilePath;
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

	// vpss init
	ctx->vpss_gpu.s32GrpId = 0;
	ctx->vpss_gpu.s32ChnId = 0;
	ctx->vpss_gpu.enVProcDevType = VIDEO_PROC_DEV_GPU;
	ctx->vpss_gpu.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_gpu.stGrpVpssAttr.enCompressMode = enStreamCompressMode;
	// chnid 0
	ctx->vpss_gpu.stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_gpu.stVpssChnAttr[0].u32Width = u32MainVencWidth;
	ctx->vpss_gpu.stVpssChnAttr[0].u32Height = u32MainVencHeight;
	if (enCodecType == RK_CODEC_TYPE_MJPEG) {
		ctx->vpss_gpu.stVpssChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
		ctx->vpss_gpu.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420P;
	} else {
		ctx->vpss_gpu.stVpssChnAttr[0].enCompressMode = enStreamCompressMode;
		ctx->vpss_gpu.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
	}
	ctx->vpss_gpu.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[0].u32Depth = 1;
	ctx->vpss_gpu.stVpssChnAttr[0].u32FrameBufCnt = 8;
	// chnid 1
	ctx->vpss_gpu.stVpssChnAttr[1].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_gpu.stVpssChnAttr[1].u32Width = 4096;
	ctx->vpss_gpu.stVpssChnAttr[1].u32Height = 1800;
	ctx->vpss_gpu.stVpssChnAttr[1].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_gpu.stVpssChnAttr[1].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss_gpu.stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[1].u32Depth = 1;
	ctx->vpss_gpu.stVpssChnAttr[1].u32FrameBufCnt = 8;
	// chnid 2
	ctx->vpss_gpu.stVpssChnAttr[2].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_gpu.stVpssChnAttr[2].u32Width = u32VencJpegWidth;
	ctx->vpss_gpu.stVpssChnAttr[2].u32Height = u32VencJpegHeight;
	ctx->vpss_gpu.stVpssChnAttr[2].enPixelFormat = RK_FMT_YUV420P;
	ctx->vpss_gpu.stVpssChnAttr[2].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss_gpu.stVpssChnAttr[2].stFrameRate.s32SrcFrameRate = s32SubVencSrcFps;
	ctx->vpss_gpu.stVpssChnAttr[2].stFrameRate.s32DstFrameRate = 1;
	ctx->vpss_gpu.stVpssChnAttr[2].u32Depth = 1;
	ctx->vpss_gpu.stVpssChnAttr[2].u32FrameBufCnt = 8;
	// chnid 3
	ctx->vpss_gpu.stVpssChnAttr[3].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_gpu.stVpssChnAttr[3].u32Width = 3840;
	ctx->vpss_gpu.stVpssChnAttr[3].u32Height = 1248;
	ctx->vpss_gpu.stVpssChnAttr[3].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_gpu.stVpssChnAttr[3].enCompressMode = enStreamCompressMode;
	ctx->vpss_gpu.stVpssChnAttr[3].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[3].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_gpu.stVpssChnAttr[3].u32Depth = 1;
	ctx->vpss_gpu.stVpssChnAttr[3].u32FrameBufCnt = 8;
	SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss_gpu);

	// init vpss_rga
	ctx->vpss_rga.s32GrpId = 1;
	ctx->vpss_rga.s32ChnId = 0;
	ctx->vpss_rga.enVProcDevType = VIDEO_PROC_DEV_GPU;
	ctx->vpss_rga.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_rga.stGrpVpssAttr.enCompressMode = enStreamCompressMode;
	// chnid 0
	ctx->vpss_rga.stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_rga.stVpssChnAttr[0].u32Width = 3840;
	ctx->vpss_rga.stVpssChnAttr[0].u32Height = 1248;
	ctx->vpss_rga.stVpssChnAttr[0].enCompressMode = enStreamCompressMode;
	ctx->vpss_rga.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_rga.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[0].u32Depth = 1;
	ctx->vpss_rga.stVpssChnAttr[0].u32FrameBufCnt = 8;
	// chnid 1
	ctx->vpss_rga.stVpssChnAttr[1].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_rga.stVpssChnAttr[1].u32Width = 2048;
	ctx->vpss_rga.stVpssChnAttr[1].u32Height = 680;
	ctx->vpss_rga.stVpssChnAttr[1].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_rga.stVpssChnAttr[1].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss_rga.stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[1].u32Depth = 1;
	ctx->vpss_rga.stVpssChnAttr[1].u32FrameBufCnt = 8;
	// chnid 2
	ctx->vpss_rga.stVpssChnAttr[2].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_rga.stVpssChnAttr[2].u32Width = 704;
	ctx->vpss_rga.stVpssChnAttr[2].u32Height = 576;
	ctx->vpss_rga.stVpssChnAttr[2].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss_rga.stVpssChnAttr[2].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss_rga.stVpssChnAttr[2].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[2].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[2].u32Depth = 1;
	ctx->vpss_rga.stVpssChnAttr[2].u32FrameBufCnt = 8;
	// chnid 3
	ctx->vpss_rga.stVpssChnAttr[3].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss_rga.stVpssChnAttr[3].u32Width = 720;
	ctx->vpss_rga.stVpssChnAttr[3].u32Height = 480;
	ctx->vpss_rga.stVpssChnAttr[3].enPixelFormat = RK_FMT_YUV422SP;
	ctx->vpss_rga.stVpssChnAttr[3].enCompressMode = COMPRESS_MODE_NONE;
	ctx->vpss_rga.stVpssChnAttr[3].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[3].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss_rga.stVpssChnAttr[3].u32Depth = 1;
	ctx->vpss_rga.stVpssChnAttr[3].u32FrameBufCnt = 8;
	SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss_rga);

	// Init VENC[0]
	ctx->venc[0].s32ChnId = 0;
	if (bIfOpenSliceSplit) {
		ctx->venc[0].bSliceSplit = RK_TRUE;
		ctx->venc[0].u32SliceMode = 2;
		ctx->venc[0].u32SlicePacketNum = 16;
		ctx->venc[0].u32SliceSize = 1;
		ctx->venc[0].u32OneStreamBuffer = 0;
	} else {
		ctx->venc[0].u32OneStreamBuffer = 1;
		ctx->venc[0].bSliceSplit = RK_FALSE;
	}
	if (enCodecType == RK_CODEC_TYPE_MJPEG) {
		ctx->venc[0].enPixelFormat = RK_FMT_YUV420P;
	} else {
		ctx->venc[0].enPixelFormat = RK_FMT_YUV420SP;
	}
	ctx->venc[0].u32Width = u32MainVencWidth;
	ctx->venc[0].u32Height = u32MainVencHeight;
	ctx->venc[0].u32SrcFps = s32MainVencSrcFps;
	ctx->venc[0].u32DstFps = s32MainVencDstFps;
	ctx->venc[0].u32Gop = 50;
	ctx->venc[0].u32BitRate = s32BitRate;
	ctx->venc[0].enCodecType = enCodecType;
	ctx->venc[0].enRcMode = enRcMode;
	ctx->venc[0].getStreamCbFunc = venc_get_stream;
	ctx->venc[0].s32loopCount = s32LoopCnt;
	ctx->venc[0].dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	if (RK_CODEC_TYPE_H264 != enCodecType) {
		ctx->venc[0].stChnAttr.stVencAttr.u32Profile = 0;
	} else {
		ctx->venc[0].stChnAttr.stVencAttr.u32Profile = 77;
	}
	ctx->venc[0].stChnAttr.stGopAttr.enGopMode =
	    VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc[0]);

	// Init VENC[1]
	ctx->venc[1].s32ChnId = 1;
	ctx->venc[1].u32OneStreamBuffer = 1;
	ctx->venc[1].bSliceSplit = RK_FALSE;
	ctx->venc[1].enPixelFormat = RK_FMT_YUV420SP;
	ctx->venc[1].u32Width = 2048;
	ctx->venc[1].u32Height = 680;
	ctx->venc[1].u32SrcFps = s32SubVencSrcFps;
	ctx->venc[1].u32DstFps = s32SubVencDstFps;
	ctx->venc[1].u32Gop = 50;
	ctx->venc[1].u32BitRate = s32BitRate;
	ctx->venc[1].enCodecType = enCodecType;
	ctx->venc[1].enRcMode = enRcMode;
	ctx->venc[1].getStreamCbFunc = venc_get_stream;
	ctx->venc[1].s32loopCount = s32LoopCnt;
	ctx->venc[1].dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	if (RK_CODEC_TYPE_H264 != enCodecType) {
		ctx->venc[1].stChnAttr.stVencAttr.u32Profile = 0;
	} else {
		ctx->venc[1].stChnAttr.stVencAttr.u32Profile = 77;
	}
	ctx->venc[1].stChnAttr.stGopAttr.enGopMode =
	    VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc[1]);

	// Init VENC[2]
	ctx->venc[2].s32ChnId = 2;
	ctx->venc[2].u32OneStreamBuffer = 1;
	ctx->venc[2].bSliceSplit = RK_FALSE;
	ctx->venc[2].enPixelFormat = RK_FMT_YUV420SP;
	ctx->venc[2].u32Width = 3840;
	ctx->venc[2].u32Height = 1248;
	ctx->venc[2].u32SrcFps = s32SubVencSrcFps;
	ctx->venc[2].u32DstFps = s32SubVencDstFps;
	ctx->venc[2].u32Gop = 50;
	ctx->venc[2].u32BitRate = s32BitRate;
	ctx->venc[2].enCodecType = enCodecType;
	ctx->venc[2].enRcMode = enRcMode;
	ctx->venc[2].getStreamCbFunc = venc_get_stream;
	ctx->venc[2].s32loopCount = s32LoopCnt;
	ctx->venc[2].dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	if (RK_CODEC_TYPE_H264 != enCodecType) {
		ctx->venc[2].stChnAttr.stVencAttr.u32Profile = 0;
	} else {
		ctx->venc[2].stChnAttr.stVencAttr.u32Profile = 77;
	}
	ctx->venc[2].stChnAttr.stGopAttr.enGopMode =
	    VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc[2]);

	// Init VENC[3]
	ctx->venc[3].s32ChnId = VENC_JPEG_CHN;
	ctx->venc[3].u32OneStreamBuffer = 1;
	ctx->venc[3].bSliceSplit = RK_FALSE;
	ctx->venc[3].enPixelFormat = RK_FMT_YUV420P;
	ctx->venc[3].u32Width = u32VencJpegWidth;
	ctx->venc[3].u32Height = u32VencJpegHeight;
	ctx->venc[3].u32SrcFps = 1;
	ctx->venc[3].u32DstFps = s32JpegFps;
	ctx->venc[3].u32Gop = 1;
	ctx->venc[3].u32Qfactor = 50;
	ctx->venc[3].u32BitRate = s32BitRate;
	ctx->venc[3].enCodecType = enJpegCodecType;
	ctx->venc[3].enRcMode = enJpegRcMode;
	ctx->venc[3].getStreamCbFunc = venc_get_stream;
	ctx->venc[3].s32loopCount = s32LoopCnt;
	ctx->venc[3].dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	if (RK_CODEC_TYPE_H264 != enCodecType) {
		ctx->venc[3].stChnAttr.stVencAttr.u32Profile = 0;
	} else {
		ctx->venc[3].stChnAttr.stVencAttr.u32Profile = 77;
	}
	ctx->venc[3].stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_INIT; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc[3]);

	s32Ret = Rgn_Init(pBmpFilePathForVpssRgn, pBmpFilePathForVencRgn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("Rgn Init Failure !!!");
		g_exit_result = RK_FAILURE;
		goto __FAILED;
	}
	s32Ret = sample_all_mod_bind();
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("all mod bind Failure !!!");
		g_exit_result = RK_FAILURE;
		goto __FAILED;
	}

	if (g_pModeTest->avs_stream_source == STREAM_FROM_FILE) {
		pthread_create(&g_pModeTest->send_frame_thread_id, 0, send_frame_thread,
		               (void *)ctx);
	}
	if (g_pModeTest->s32ModuleTestType) {
		g_pModeTest->bModuleTestIfopen = RK_TRUE;
		pthread_create(&module_test_thread_id, 0, sample_module_test, RK_NULL);
	}

	RK_LOGE("main initial finish");

	while (!g_pModeTest->bMainThreadQuit) {
		sleep(1);
	}

	RK_LOGE("main exit!");

	if (g_pModeTest->s32ModuleTestType) {
		g_pModeTest->bModuleTestThreadQuit = RK_TRUE;
		pthread_join(module_test_thread_id, RK_NULL);
	}
	g_pModeTest->s32ModuleTestType = 0;
	// rgn deinit
	Rgn_Deinit();

	// venc[3] ubind and destroy
	g_pModeTest->bVencThreadQuit[3] = RK_TRUE;
	if (ctx->venc[3].getStreamCbFunc) {
		pthread_join(ctx->venc[3].getStreamThread, RK_NULL);
	}
	// UNBind VPSS[0,2] and VENC[3]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 2;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[3].s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, 2, ctx->venc[3].s32ChnId);
		g_exit_result = RK_FAILURE;
	}
	s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[3]);
	if (s32Ret == RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[3].s32ChnId);
	} else {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[3].s32ChnId,
		        s32Ret);
		g_exit_result = RK_FAILURE;
	}

	// venc[2] ubind and destroy
	g_pModeTest->bVencThreadQuit[2] = RK_TRUE;
	if (ctx->venc[2].getStreamCbFunc) {
		pthread_join(ctx->venc[2].getStreamThread, RK_NULL);
	}
	// UNBind VPSS[1,0] and VENC[2]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[2].s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
		        ctx->vpss_rga.s32GrpId, ctx->vpss_rga.s32ChnId, ctx->venc[2].s32ChnId);
		g_exit_result = RK_FAILURE;
	}
	s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[2]);
	if (s32Ret == RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[2].s32ChnId);
	} else {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[2].s32ChnId,
		        s32Ret);
		g_exit_result = RK_FAILURE;
	}

	// venc[1] ubind and destroy
	g_pModeTest->bVencThreadQuit[1] = RK_TRUE;
	if (ctx->venc[1].getStreamCbFunc) {
		pthread_join(ctx->venc[1].getStreamThread, RK_NULL);
	}
	// UNBind VPSS[1,1] and VENC[1]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stSrcChn.s32ChnId = 1;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[1].s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
		        ctx->vpss_rga.s32GrpId, 1, ctx->venc[1].s32ChnId);
		g_exit_result = RK_FAILURE;
	}
	s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[1]);
	if (s32Ret == RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[1].s32ChnId);
	} else {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[1].s32ChnId,
		        s32Ret);
		g_exit_result = RK_FAILURE;
	}

	// venc[0] ubind and destroy
	g_pModeTest->bVencThreadQuit[0] = RK_TRUE;
	if (ctx->venc[0].getStreamCbFunc) {
		pthread_join(ctx->venc[0].getStreamThread, RK_NULL);
	}
	// UNBind VPSS[0,0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc[0].s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to venc chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, ctx->vpss_gpu.s32ChnId, ctx->venc[0].s32ChnId);
		g_exit_result = RK_FAILURE;
	}
	s32Ret = SAMPLE_COMM_VENC_DestroyChn(&ctx->venc[0]);
	if (s32Ret == RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Success", ctx->venc[0].s32ChnId);
	} else {
		RK_LOGE("SAMPLE_COMM_VENC_DestroyChn:%d Failure:%#X", ctx->venc[0].s32ChnId,
		        s32Ret);
		g_exit_result = RK_FAILURE;
	}

	// rtsp deinit
	rtsp_deinit();

	// UNBind VPSS[0,3] and VPSS[1,0], destroy vpss_rga
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stSrcChn.s32ChnId = 3;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss_rga.s32GrpId;
	stDestChn.s32ChnId = 0;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("vpss grpid:%d chnid:%d unband to vpss grpid:%d chnid:%d failure",
		        ctx->vpss_gpu.s32GrpId, 3, ctx->vpss_rga.s32GrpId, 0);
		g_exit_result = RK_FAILURE;
	}
	SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss_rga);

	// UNBind AVS[0] and VPSS[0,0], destroy vpss_gpu
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss_gpu.s32GrpId;
	stDestChn.s32ChnId = ctx->vpss_gpu.s32ChnId;
	s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("avs grpid:%d unband to vpss grpid:%d failure", ctx->avs.s32GrpId,
		        ctx->vpss_gpu.s32GrpId);
		g_exit_result = RK_FAILURE;
	}
	SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss_gpu);
	if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// UNBind VI[0]~VI[5] and avs[0]
		for (i = 0; i < ctx->avs.stAvsGrpAttr.u32PipeNum; i++) {
			stSrcChn.enModId = RK_ID_VI;
			stSrcChn.s32DevId = ctx->vi[i].s32DevId;
			stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
			stDestChn.enModId = RK_ID_AVS;
			stDestChn.s32DevId = ctx->avs.s32GrpId;
			stDestChn.s32ChnId = i;
			s32Ret = SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("vi dev:%d unband to avs:%d failure", ctx->vi[i].s32DevId, i);
				return RK_NULL;
			}
		}
	}

	// Destroy AVS[0]
	SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);

__AVS_FAILED:
	if (g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
		// Destroy VI[0]
		for (i = 0; i < s32CamNum; i++) {
			SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
		}
	} else if (g_pModeTest->avs_stream_source == STREAM_FROM_FILE) {
		g_pModeTest->send_frame_thread_quit = RK_TRUE;
		pthread_join(g_pModeTest->send_frame_thread_id, NULL);
		for (i = 0; i < s32CamNum; i++) {
			if (g_pModeTest->pipe_frames[i].stVFrame.pMbBlk) {
				RK_MPI_MB_ReleaseMB(g_pModeTest->pipe_frames[i].stVFrame.pMbBlk);
				g_pModeTest->pipe_frames[i].stVFrame.pMbBlk = RK_NULL;
			}
		}
	}

__FAILED:
	RK_MPI_SYS_Exit();
	if (iq_file_dir && g_pModeTest->avs_stream_source == STREAM_FROM_ISP_CAMERA) {
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
