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
#include <time.h>
#include <unistd.h>

#include "rtsp_demo.h"
#include "sample_comm.h"

#define CAM_NUM_MAX 8

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi[CAM_NUM_MAX];
	SAMPLE_AVS_CTX_S avs;
} SAMPLE_MPI_CTX_S;

static bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

static RK_CHAR optstr[] = "?::a::A:b:w:h:l:o:e:d:D:I:i:L:M:n:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"calib_file_path", required_argument, NULL, 'A'},
    {"camera_num", required_argument, NULL, 'n'},
    {"bitrate", required_argument, NULL, 'b'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"input_bmp_name", required_argument, NULL, 'i'},
    {"loop_count", required_argument, NULL, 'l'},
    {"output_path", required_argument, NULL, 'o'},
    {"encode", required_argument, NULL, 'e'},
    {"multictx", required_argument, NULL, 'M'},
    {"fps", required_argument, NULL, 'f'},
    {"hdr_mode", required_argument, NULL, 'h' + 'm'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -w 2560 -h 1520 -a /etc/iqfiles/ -n 6 -l 10 -o /data/\n", name);
	printf("\trtsp://xx.xx.xx.xx/live/0, Default OPEN\n");
#ifdef RKAIQ
	printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a "
	       "/etc/iqfiles/, "
	       "set dirpath empty to using path by default, without this option aiq "
	       "should run in other application\n");
	printf("\t-M | --multictx: switch of multictx in isp, set 0 to disable, set "
	       "1 to enable. Default: 0\n");
#endif
	printf("\t-A | --calib_file_path: input file path of calib_file_xxx.pto "
	       "Default /oem/usr/share/avs_calib/calib_file.pto\n");
	printf("\t-n | --camera_num: camera number, Default 6\n");
	printf("\t-w | --width: camera with, Default 1920\n");
	printf("\t-h | --height: camera height, Default 1080\n");
	printf("\t-l | --loop_count: loop count, Default -1\n");
	printf("\t-o | --output_path: avs output file path, Default NULL\n");
}

/******************************************************************************
 * function : avs thread
 ******************************************************************************/
static void *avs_get_stream(void *pArgs) {
	SAMPLE_AVS_CTX_S *ctx = (SAMPLE_AVS_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	char name[256] = {0};
	FILE *fp = RK_NULL;
	void *pData = RK_NULL;
	RK_S32 loopCount = 0;
	RK_S32 frameLength;

	if (ctx->dstFilePath) {
		snprintf(name, sizeof(name), "/%s/avs_%d.bin", ctx->dstFilePath, ctx->s32GrpId);
		fp = fopen(name, "wb");
		if (fp == RK_NULL) {
			printf("chn %d can't open %s file !\n", ctx->s32GrpId, ctx->dstFilePath);
			quit = true;
			return RK_NULL;
		}
	}

	while (!quit) {
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
	int video_width = 2560;
	int video_height = 1520;
	int avs_width = 8192;
	int avs_height = 2700;
	RK_CHAR *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pAvsMeshAlphaPath = "/tmp/";
	RK_CHAR *pAvsLutFilePath = NULL;
	RK_CHAR *pOutPath = NULL;
	RK_S32 s32CamId = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	RK_S32 s32CamNum = 6;
	RK_S32 s32loopCnt = -1;
	RK_S32 i;
	pthread_t avs_thread_id;

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
		case 'n':
			s32CamNum = atoi(optarg);
			break;
		case 'l':
			s32loopCnt = atoi(optarg);
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

	printf("#CameraIdx: %d\n", s32CamId);
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

		s32Ret =
		    SAMPLE_COMM_ISP_CamGroup_Init(s32CamId, hdr_mode, bMultictx, &camgroup_cfg);
		if (s32Ret != RK_SUCCESS) {
			printf("%s : Isp cam group init failure\n", __func__);
			goto __FAILED2;
		}
#endif
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		goto __FAILED;
	}

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
		SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
	}

	// Init avs[0]
	ctx->avs.s32GrpId = 0;
	ctx->avs.s32ChnId = 0;
	ctx->avs.dstFilePath = pOutPath;
	ctx->avs.s32loopCount = s32loopCnt;
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

	// Bind VI[0]~VI[5] and avs[0]
	for (i = 0; i < s32CamNum; i++) {
		stSrcChn.enModId = RK_ID_VI;
		stSrcChn.s32DevId = ctx->vi[i].s32DevId;
		stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
		stDestChn.enModId = RK_ID_AVS;
		stDestChn.s32DevId = ctx->avs.s32GrpId;
		stDestChn.s32ChnId = i;
		SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	}

	pthread_create(&avs_thread_id, 0, avs_get_stream, (void *)(&ctx->avs));

	printf("%s initial finish\n", __func__);

	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);

	pthread_join(avs_thread_id, NULL);

	// UnBind Bind VI[0]~VI[5] and AVS[0]
	for (i = 0; i < s32CamNum; i++) {
		stSrcChn.enModId = RK_ID_VI;
		stSrcChn.s32DevId = ctx->vi[i].s32DevId;
		stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
		stDestChn.enModId = RK_ID_AVS;
		stDestChn.s32DevId = ctx->avs.s32GrpId;
		stDestChn.s32ChnId = i;
		SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	}

	// Destroy AVS[0]
	SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
	// Destroy VI[0]
	for (i = 0; i < s32CamNum; i++) {
		SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
	}

__FAILED:
	RK_MPI_SYS_Exit();
	if (iq_file_dir) {
#ifdef RKAIQ
		SAMPLE_COMM_ISP_CamGroup_Stop(s32CamId);
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
