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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "rtsp_demo.h"
#include "sample_comm.h"

#define CAM_NUM_MAX 8

int video_width = 2560;
int video_height = 1520;
RK_CHAR *input_yuv_path = "/userdata";

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi[CAM_NUM_MAX];
	SAMPLE_AVS_CTX_S avs;
} SAMPLE_MPI_CTX_S;

static bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

static RK_CHAR optstr[] = "?::a::A:w:h:W:H:l:o:d:D:I:i:n:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"calib_file_path", required_argument, NULL, 'A'},
    {"camera_num", required_argument, NULL, 'n'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"avs_width", required_argument, NULL, 'W'},
    {"avs_height", required_argument, NULL, 'H'},
    {"input_yuv_path", required_argument, NULL, 'i'},
    {"loop_count", required_argument, NULL, 'l'},
    {"output_path", required_argument, NULL, 'o'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -w 2560 -h 1520 -a /etc/iqfiles/ -n 6\n", name);
#ifdef RKAIQ
	printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a "
	       "/etc/iqfiles/, "
	       "set dirpath empty to using path by default, without this option aiq "
	       "should run in other application\n");
#endif
	printf("\t-A | --calib_file_path: input file path of calib_file_xxx.pto "
	       "Default /oem/usr/share/avs_calib/calib_file.pto\n");
	printf("\t-n | --camera_num: camera number, Default 6\n");
	printf("\t-w | --width: camera with, Default 1920\n");
	printf("\t-h | --height: camera height, Default 1080\n");
	printf("\t-W | --width: avs out with, Default 8192\n");
	printf("\t-H | --height: avs out height, Default 2700\n");
	printf("\t-l | --loop_count: loop count, Default -1\n");
	printf("\t-i | --input_path: avs input file path, Default /userdata, need 0~5.yuv\n");
	printf("\t-o | --output_path: avs output file path, Default NULL\n");
}

/******************************************************************************
 * function : avs send thread
 ******************************************************************************/
static void *avs_send_stream(void *pArgs) {
	SAMPLE_AVS_CTX_S *ctx = (SAMPLE_AVS_CTX_S *)(pArgs);
	char name[256] = {0};
	FILE *fp[CAM_NUM_MAX];
	RK_S32 loopCount = 0;
	VIDEO_FRAME_INFO_S stVideoFrames[CAM_NUM_MAX] = {0};
	RK_S32 s32Ret = RK_FAILURE;
	for (int i = 0; i < ctx->stAvsGrpAttr.u32PipeNum; i++) {

		stVideoFrames[i].stVFrame.u32Width = video_width;
		stVideoFrames[i].stVFrame.u32Height = video_height;
		stVideoFrames[i].stVFrame.u32VirWidth = video_width;
		stVideoFrames[i].stVFrame.u32VirHeight = video_height;
		stVideoFrames[i].stVFrame.enPixelFormat = RK_FMT_YUV420SP;
		stVideoFrames[i].stVFrame.enCompressMode = COMPRESS_MODE_NONE;
		RK_MPI_SYS_MmzAlloc_Cached(&(stVideoFrames[i].stVFrame.pMbBlk), RK_NULL, RK_NULL,
		                           video_width * video_height * 3 / 2);

		snprintf(name, sizeof(name), "%s/%d.yuv", input_yuv_path, i);
		fp[i] = fopen(name, "rb");
		if (fp[i] == RK_NULL) {
			RK_LOGE("fopen %s failed, error: %s", name, strerror(errno));
			quit = true;
			return RK_NULL;
		}
	}
	while (!quit) {
		// exit when complete
		if (ctx->s32loopCount > 0) {
			if (loopCount >= ctx->s32loopCount) {
				break;
			}
		}
		for (int i = 0; i < ctx->stAvsGrpAttr.u32PipeNum; i++) {
			fread(RK_MPI_MB_Handle2VirAddr(stVideoFrames[i].stVFrame.pMbBlk), 1,
			      video_width * video_height * 3 / 2, fp[i]);
			RK_MPI_SYS_MmzFlushCache(stVideoFrames[i].stVFrame.pMbBlk, RK_FALSE);
			s32Ret = RK_MPI_AVS_SendPipeFrame(ctx->s32GrpId, i, &stVideoFrames[i], 1000);
			if (s32Ret != RK_SUCCESS) {
				printf("func: %s line: %d AVS_SendPipeFrame failure!\n", __func__,
				       __LINE__);
			}
		}
		loopCount++;
		usleep(33 * 1000);
	}
	for (int i = 0; i < ctx->stAvsGrpAttr.u32PipeNum; i++) {
		fclose(fp[i]);
		RK_MPI_MB_ReleaseMB(stVideoFrames[i].stVFrame.pMbBlk);
	}

	return RK_NULL;
}

/******************************************************************************
 * function : avs get thread
 ******************************************************************************/
static void *avs_get_stream(void *pArgs) {
	SAMPLE_AVS_CTX_S *ctx = (SAMPLE_AVS_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	char name[256] = {0};
	FILE *fp = RK_NULL;
	void *pData = RK_NULL;
	RK_S32 loopCount = 0;
	RK_S32 frameLength;

	while (!quit) {
		if (ctx->dstFilePath) {
			snprintf(name, sizeof(name), "/%s/avs_%d.bin", ctx->dstFilePath, loopCount);
			fp = fopen(name, "wb");
			if (fp == RK_NULL) {
				printf("can't open %s file !\n", name);
				quit = true;
				return RK_NULL;
			}
		}

		s32Ret = SAMPLE_COMM_AVS_GetChnFrame(ctx, &pData);
		if (s32Ret == RK_SUCCESS) {
			// exit when complete
			if (ctx->s32loopCount > 0) {
				if (loopCount >= ctx->s32loopCount) {
					SAMPLE_COMM_AVS_ReleaseChnFrame(ctx);
					quit = true;
					break;
				}
			}

			frameLength = ctx->stVideoFrame.stVFrame.u32Width *
			              ctx->stVideoFrame.stVFrame.u32Height * 1.5;
			if (fp) {
				fwrite(pData, 1, frameLength, fp);
				fflush(fp);
			}

			printf("SAMPLE_COMM_AVS_GetChnFrame DevId %d ok:data %p loop:%d pts:%ld ms\n",
			       ctx->s32GrpId, pData, loopCount,
			       ctx->stVideoFrame.stVFrame.u64PTS / 1000);
			SAMPLE_COMM_AVS_ReleaseChnFrame(ctx);
			loopCount++;
		}
		usleep(1000);
	}

	if (fp)
		fclose(fp);

	return RK_NULL;
}

/******************************************************************************
 * function    : main()
 * Description : main
 ******************************************************************************/
int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	SAMPLE_MPI_CTX_S *ctx;
	int avs_width = 8192;
	int avs_height = 2700;
	RK_CHAR *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pAvsMeshAlphaPath = "/tmp/";
	RK_CHAR *pAvsLutFilePath = NULL;
	RK_CHAR *pOutPath = NULL;
	RK_S32 s32CamNum = 6;
	pthread_t avs_send_thread_id, avs_get_thread_id;

	if (argc < 2) {
		print_usage(argv[0]);
		return 0;
	}

	ctx = (SAMPLE_MPI_CTX_S *)(malloc(sizeof(SAMPLE_MPI_CTX_S)));
	memset(ctx, 0, sizeof(SAMPLE_MPI_CTX_S));

	signal(SIGINT, sigterm_handler);

#ifdef RKAIQ
	RK_BOOL bMultictx = RK_FALSE;
#endif
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
		case 'A':
			pAvsCalibFilePath = optarg;
			break;
		case 'w':
			video_width = atoi(optarg);
			break;
		case 'h':
			video_height = atoi(optarg);
			break;
		case 'W':
			avs_width = atoi(optarg);
			break;
		case 'H':
			avs_height = atoi(optarg);
			break;
		case 'n':
			s32CamNum = atoi(optarg);
		case 'i':
			input_yuv_path = optarg;
			break;
		case 'l':
			ctx->avs.s32loopCount = atoi(optarg);
			break;
		case 'o':
			pOutPath = optarg;
			break;
#ifdef RKAIQ
		case 'M':
			if (atoi(optarg)) {
				bMultictx = RK_TRUE;
			}
			break;
#endif
		case '?':
		default:
			print_usage(argv[0]);
			return 0;
		}
	}

	printf("#input_yuv_path: %s\n", input_yuv_path);
	printf("#pAvsLutFilePath: %s\n", pAvsLutFilePath);
	printf("#pAvsCalibFilePath: %s\n", pAvsCalibFilePath);
	printf("#pAvsMeshAlphaPath: %s\n", pAvsMeshAlphaPath);
	printf("#Output Path: %s\n", pOutPath);
	printf("#IQ Path: %s\n", iq_file_dir);
	if (iq_file_dir) {
#ifdef RKAIQ
		printf("#Rkaiq XML DirPath: %s\n", iq_file_dir);
		printf("#bMultictx: %d\n\n", bMultictx);
		rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
		rk_aiq_camgroup_instance_cfg_t camgroup_cfg;

		memset(&camgroup_cfg, 0, sizeof(camgroup_cfg));
		camgroup_cfg.sns_num = s32CamNum;
		camgroup_cfg.config_file_dir = iq_file_dir;

		s32Ret = SAMPLE_COMM_ISP_CamGroup_Init(0, hdr_mode, bMultictx, &camgroup_cfg);
		if (s32Ret != RK_SUCCESS) {
			printf("%s : isp cam group init failure\n", __func__);
			goto __FAILED2;
		}
#endif
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		goto __FAILED;
	}

	// Init avs[0]
	ctx->avs.s32GrpId = 0;
	ctx->avs.s32ChnId = 0;
	ctx->avs.dstFilePath = pOutPath;
	ctx->avs.stAvsModParam.enMBSource = MB_SOURCE_PRIVATE;
	ctx->avs.stAvsModParam.u32WorkingSetSize = 67 * 1024;
	ctx->avs.stAvsGrpAttr.enMode = 0; // 0: blend 1: no blend
	ctx->avs.stAvsGrpAttr.u32PipeNum = s32CamNum;
	ctx->avs.stAvsGrpAttr.stGainAttr.enMode = AVS_GAIN_MODE_AUTO;
	ctx->avs.stAvsGrpAttr.stOutAttr.enPrjMode = AVS_PROJECTION_EQUIRECTANGULAR;
	if (s32CamNum == 6) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4196;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 2080;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 28000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 9500;
		avs_width = 8192;
		avs_height = 2700;
	} else if (s32CamNum == 8) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4096;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 1800;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 36000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 8500;
		avs_width = 8192;
		avs_height = 3840;
	}
	ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Width = avs_width;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Height = avs_height;
	ctx->avs.stAvsGrpAttr.bSyncPipe = RK_TRUE;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].u32Depth = 2;
	ctx->avs.stAvsChnAttr[0].u32Width = avs_width;
	ctx->avs.stAvsChnAttr[0].u32Height = avs_height;
	ctx->avs.stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = pAvsCalibFilePath;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pMeshAlphaPath = pAvsMeshAlphaPath;
	SAMPLE_COMM_AVS_CreateChn(&ctx->avs);

	pthread_create(&avs_send_thread_id, 0, avs_send_stream, (void *)(&ctx->avs));
	pthread_create(&avs_get_thread_id, 0, avs_get_stream, (void *)(&ctx->avs));

	printf("%s initial finish\n", __func__);

	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);

	pthread_join(avs_send_thread_id, NULL);
	pthread_join(avs_get_thread_id, NULL);

	// Destroy AVS[0]
	SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
	// Destroy VI[0]

__FAILED:
	RK_MPI_SYS_Exit();
	if (iq_file_dir) {
#ifdef RKAIQ
		SAMPLE_COMM_ISP_CamGroup_Stop(0);
#endif
	}
__FAILED2:
	if (ctx) {
		free(ctx);
		ctx = RK_NULL;
	}

	return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
