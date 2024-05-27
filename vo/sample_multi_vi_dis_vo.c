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
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "sample_comm.h"

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi;
	SAMPLE_VPSS_CTX_S vpss;
	SAMPLE_VO_CTX_S vo;
} SAMPLE_MPI_CTX_S;

static bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

#define CAM_NUM_MAX 8

#define USE_PIC 0

static RK_CHAR optstr[] = "?::a::w:h:d:D:L:M:f:n:r:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"camera_num", required_argument, NULL, 'n'},
    {"disp_devid", required_argument, NULL, 'D'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"pixel_format", optional_argument, NULL, 'f'},
    {"disp_layerid", required_argument, NULL, 'L'},
    {"multictx", required_argument, NULL, 'M'},
    {"fps", required_argument, NULL, 'f'},
    {"hdr_mode", required_argument, NULL, 'h' + 'm'},
    {"vo_use_rga", optional_argument, NULL, 'r'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
* function : show usage
******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -w 2560 -h 1520 -a /etc/iqfiles/ -D 0 -L 0\n", name);
#ifdef RKAIQ
	printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a "
	       "/etc/iqfiles/, "
	       "set dirpath empty to using path by default, without this option aiq "
	       "should run in other application\n");
	printf("\t-M | --multictx: switch of multictx in isp, set 0 to disable, set "
	       "1 to enable. Default: 0\n");
#endif
	printf("\t-n | --camera_num: camera number, Default 4\n");
	printf("\t-w | --width: camera with, Default 1920\n");
	printf("\t-h | --height: camera height, Default 1080\n");
	printf("\t-f | --pixel_format: camera Format, Default nv12, "
		"Value:nv12, nv12 afbc.\n");
	printf("\t-D | --disp_devid: display DevId, Default 0\n");
	printf("\t-L | --disp_layerid: display LayerId, Default 0\n");
	printf("\t-r | --vo_use_rga: whether vo composer using RGA, Default 0:GPU\n");
}

static RK_S32 readFromPic(SAMPLE_VI_CTX_S *ctx, VIDEO_FRAME_S *buffer) {
	FILE           *fp    = NULL;
	RK_S32          s32Ret = RK_SUCCESS;
	MB_BLK          srcBlk = MB_INVALID_HANDLE;
	PIC_BUF_ATTR_S  stPicBufAttr;
	MB_PIC_CAL_S    stMbPicCalResult;

	if (!ctx->pUserPicFile) {
		return RK_ERR_NULL_PTR;
	}

	stPicBufAttr.u32Width      = ctx->stChnAttr.stSize.u32Width;
	stPicBufAttr.u32Height     = ctx->stChnAttr.stSize.u32Height;
	stPicBufAttr.enCompMode    = COMPRESS_MODE_NONE;
	stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	s32Ret = RK_MPI_CAL_VGS_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("get user picture buffer size failed %#x!", s32Ret);
		return RK_FAILURE;
	}

	s32Ret = RK_MPI_MMZ_Alloc(&srcBlk, stMbPicCalResult.u32MBSize, RK_MMZ_ALLOC_CACHEABLE);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("all user picture buffer failed %#x!", s32Ret);
		return RK_FAILURE;
	}

	fp = fopen(ctx->pUserPicFile, "rb");
	if (NULL == fp) {
		RK_LOGE("open %s fail", ctx->pUserPicFile);
		goto __FREE_USER_PIC;
	} else {
		fread(RK_MPI_MB_Handle2VirAddr(srcBlk), 1 , stMbPicCalResult.u32MBSize, fp);
		fclose(fp);
		RK_MPI_SYS_MmzFlushCache(srcBlk, RK_FALSE);
		RK_LOGD("open %s success", ctx->pUserPicFile);
	}

	buffer->u32Width       = ctx->stChnAttr.stSize.u32Width;
	buffer->u32Height      = ctx->stChnAttr.stSize.u32Height;
	buffer->u32VirWidth    = ctx->stChnAttr.stSize.u32Width;
	buffer->u32VirHeight   = ctx->stChnAttr.stSize.u32Height;
	buffer->enPixelFormat  = RK_FMT_YUV420SP;
	buffer->u32TimeRef     = 0;
	buffer->u64PTS         = 0;
	buffer->enCompressMode = COMPRESS_MODE_NONE;
	buffer->pMbBlk         = srcBlk;

	RK_LOGD("readFromPic width = %d, height = %d size = %d pixFormat = %d",
		ctx->stChnAttr.stSize.u32Width, ctx->stChnAttr.stSize.u32Height,
		stMbPicCalResult.u32MBSize, RK_FMT_YUV420SP);
	return RK_SUCCESS;

__FREE_USER_PIC:
	RK_MPI_SYS_MmzFree(srcBlk);

	return RK_FAILURE;
}

/******************************************************************************
* function    : main()
* Description : main
******************************************************************************/
int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	SAMPLE_MPI_CTX_S *ctx;
	RK_S32 video_width  = 1920;
	RK_S32 video_height = 1080;
	RK_S32 disp_width   = 1080;
	RK_S32 disp_height  = 1920;
	RK_S32 i;
	RK_S32 s32CamNum = 4;
	RK_S32 spilt_disp_width = 0;
	RK_S32 spilt_disp_height = 0;
	RK_S32 spilt_disp_colum_cnt = 1;

	RK_S32 s32ChnId = 0;
	RK_S32 s32DisId = 0;
	RK_S32 s32DisLayerId = 0;
	RK_BOOL bUseRga = RK_TRUE;
	PIXEL_FORMAT_E PixelFormat = RK_FMT_YUV420SP;
	COMPRESS_MODE_E CompressMode = COMPRESS_MODE_NONE;
	MPP_CHN_S stSrcChn, stDestChn;
	DIS_CONFIG_S pstDisConfig;
	DIS_ATTR_S   pstDisAttr;

	if (argc < 2) {
		print_usage(argv[0]);
		return 0;
	}

	ctx = (SAMPLE_MPI_CTX_S *)(malloc(sizeof(SAMPLE_MPI_CTX_S)));
	memset(ctx, 0, sizeof(SAMPLE_MPI_CTX_S));
	memset(&pstDisConfig, 0, sizeof(DIS_CONFIG_S));
	memset(&pstDisAttr, 0, sizeof(DIS_ATTR_S));

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
		case 'n':
			s32CamNum = atoi(optarg);
			break;
		case 'f':
			if (!strcmp(optarg, "nv12")) {
				s32ChnId = 0;
				PixelFormat = RK_FMT_YUV420SP;
				CompressMode = COMPRESS_MODE_NONE;
			} else if (!strcmp(optarg, "afbc")) {
				s32ChnId = 2;
				PixelFormat = RK_FMT_YUV420SP;
				CompressMode = COMPRESS_AFBC_16x16;
			}
			break;
		case 'D':
			s32DisId = atoi(optarg);
			if (s32DisId == 3) { // MIPI
				disp_width = 1080;
				disp_height = 1920;
			} else {
				disp_width = 1920;
				disp_height = 1080;
			}
			break;
		case 'w':
			video_width = atoi(optarg);
			break;
		case 'h':
			video_height = atoi(optarg);
			break;
		case 'L':
			s32DisLayerId = atoi(optarg);
			break;
		case 'r':
			bUseRga = atoi(optarg);
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

	printf("#CameraNum: %d\n", s32CamNum);
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
			printf("%s : Isp Cam Group Init Failure\n", __func__);
			goto __FAILED2;
		}
#endif
	}

	if (s32CamNum > CAM_NUM_MAX) {
		RK_LOGE("unsupport camera number %d large than max number %d", s32CamNum, CAM_NUM_MAX);
		goto __FAILED;
	}

	if (s32CamNum <= 4) {
		spilt_disp_colum_cnt = 2;
	} else if (s32CamNum <= CAM_NUM_MAX) {
		spilt_disp_colum_cnt = 3;
	}
	spilt_disp_height = disp_height / spilt_disp_colum_cnt;
	spilt_disp_width  = disp_width  / spilt_disp_colum_cnt;
	RK_LOGE("spilt disp colum cnt %d s32CamNum %d", spilt_disp_colum_cnt, s32CamNum);

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		goto __FAILED;
	}

	// Init VI[0]
	ctx->vi.u32Width = video_width;
	ctx->vi.u32Height = video_height;

	ctx->vi.s32ChnId = s32ChnId;
	ctx->vi.stChnAttr.stIspOpt.u32BufCount = 8;
	ctx->vi.stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	ctx->vi.stChnAttr.u32Depth = 0;
	ctx->vi.stChnAttr.enPixelFormat = PixelFormat;
	ctx->vi.stChnAttr.enCompressMode = CompressMode;
	ctx->vi.stChnAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->vi.stChnAttr.stFrameRate.s32DstFrameRate = -1;

	pstDisConfig.enMode              = DIS_MODE_4_DOF_GME;
	pstDisConfig.enMotionLevel       = DIS_MOTION_LEVEL_NORMAL;
	pstDisConfig.u32CropRatio        = 80;
	pstDisConfig.u32BufNum           = 10;
	pstDisConfig.u32FrameRate        = 25;
	pstDisConfig.enPdtType           = DIS_PDT_TYPE_IPC;
	pstDisConfig.u32GyroOutputRange  = 0;
	pstDisConfig.bCameraSteady       = RK_TRUE;
	pstDisConfig.u32GyroDataBitWidth = 0;

	pstDisAttr.bEnable               = RK_TRUE;
	pstDisAttr.u32MovingSubjectLevel = 0;
	pstDisAttr.s32RollingShutterCoef = 0;
	pstDisAttr.u32Timelag            = 33333;
	pstDisAttr.u32ViewAngle          = 1000;
	pstDisAttr.bStillCrop            = RK_FALSE;
	pstDisAttr.u32HorizontalLimit    = 512;
	pstDisAttr.u32VerticalLimit      = 512;

	for (i = 0; i < s32CamNum; i++) {
		ctx->vi.s32DevId = i;
		ctx->vi.u32PipeId = ctx->vi.s32DevId;

		SAMPLE_COMM_VI_CreateChn(&ctx->vi);
#if USE_PIC
		ctx->vi.stUsrPic.enUsrPicMode = VI_USERPIC_MODE_PIC;
		ctx->vi.pUserPicFile = "/data/test/avs/input_image/image_data/camera0_3840x2160_nv12.yuv";

		if (ctx->vi.stUsrPic.enUsrPicMode == VI_USERPIC_MODE_PIC) {
			s32Ret = readFromPic(&ctx->vi, &ctx->vi.stUsrPic.unUsrPic.stUsrPicFrm.stVFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("vi [%d, %d] readFromPic fail:%x",
					ctx->vi.s32DevId, ctx->vi.s32ChnId, s32Ret);
				// goto __FAILED;
			}
		} else if (ctx->vi.stUsrPic.enUsrPicMode == VI_USERPIC_MODE_BGC) {
			/* set background color */
			// ctx->vi.stUsrPic.unUsrPic.stUsrPicBg.u32BgColor = RGB(0, 0, 128);
		}

		s32Ret = RK_MPI_VI_SetUserPic(ctx->vi.u32PipeId, ctx->vi.s32ChnId, &ctx->vi.stUsrPic);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vi [%d, %d, %d] RK_MPI_VI_SetUserPic failed: %#x!",
				ctx->vi.s32DevId, ctx->vi.u32PipeId, ctx->vi.s32ChnId, s32Ret);
			// goto __FREE_USER_PIC;
		}
		RK_LOGV("vi [%d, %d, %d] RK_MPI_VI_SetUserPic already.",
			ctx->vi.s32DevId, ctx->vi.u32PipeId, ctx->vi.s32ChnId);

		s32Ret = RK_MPI_VI_EnableUserPic(ctx->vi.u32PipeId, ctx->vi.s32ChnId);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vi [%d, %d, %d] RK_MPI_VI_EnableUserPic failed: %#x!",
				ctx->vi.s32DevId, ctx->vi.u32PipeId, ctx->vi.s32ChnId, s32Ret);
			// goto __FREE_USER_PIC;
		}
		RK_LOGV("vi [%d, %d, %d] RK_MPI_VI_EnableUserPic already.",
			ctx->vi.s32DevId, ctx->vi.u32PipeId, ctx->vi.s32ChnId);
#endif

		s32Ret = RK_MPI_VI_SetChnDISConfig(ctx->vi.s32DevId, ctx->vi.s32ChnId, &pstDisConfig);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vi [%d, %d, %d] RK_MPI_VI_SetChnDISConfig failed: %#x!",
				ctx->vi.s32DevId, ctx->vi.u32PipeId,
				ctx->vi.s32ChnId, s32Ret);
		}
		RK_LOGV("vi [%d, %d, %d] RK_MPI_VI_SetChnDISConfig already.",
			ctx->vi.s32DevId, ctx->vi.u32PipeId,
			ctx->vi.s32ChnId);

		s32Ret = RK_MPI_VI_SetChnDISAttr(ctx->vi.s32DevId, ctx->vi.s32ChnId, &pstDisAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vi [%d, %d, %d] RK_MPI_VI_SetChnDISAttr failed: %#x!",
				ctx->vi.s32DevId, ctx->vi.u32PipeId,
				ctx->vi.s32ChnId, s32Ret);
		}
		RK_LOGV("vi [%d, %d, %d] RK_MPI_VI_SetChnDISAttr already.",
			ctx->vi.s32DevId, ctx->vi.u32PipeId,
			ctx->vi.s32ChnId);
	}

	// Init VPSS[0]
	// RGA_device: VIDEO_PROC_DEV_RGA GPU_device: VIDEO_PROC_DEV_GPU
	ctx->vpss.enVProcDevType = VIDEO_PROC_DEV_RGA;
	ctx->vpss.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE; // no compress
	ctx->vpss.s32ChnRotation[0] = ROTATION_0;
	ctx->vpss.stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss.stVpssChnAttr[0].enCompressMode = CompressMode;
	ctx->vpss.stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->vpss.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].u32Width = video_width;
	ctx->vpss.stVpssChnAttr[0].u32Height = video_height;
	ctx->vpss.stVpssChnAttr[0].u32FrameBufCnt = 5;
	if (s32DisId == 3) { // MIPI
		ctx->vpss.s32ChnRotation[0] = ROTATION_90;
		ctx->vpss.stVpssChnAttr[0].u32Width = video_height;
		ctx->vpss.stVpssChnAttr[0].u32Height = video_width;
	}

	ctx->vpss.s32ChnId = 0;
	for (i = 0; i < s32CamNum; i++) {
		ctx->vpss.s32GrpId = i;
		SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss);
	}

	// Init VO[0]
	ctx->vo.s32DevId = s32DisId;
	ctx->vo.s32ChnId = 0;
	ctx->vo.s32LayerId = s32DisLayerId;
	ctx->vo.Volayer_mode = VO_LAYER_MODE_GRAPHIC;
	ctx->vo.u32DispBufLen = 4;
	ctx->vo.stVoPubAttr.enIntfType = VO_INTF_MIPI;
	ctx->vo.stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	ctx->vo.stLayerAttr.stDispRect.s32X = 0;
	ctx->vo.stLayerAttr.stDispRect.s32Y = 0;
	ctx->vo.stLayerAttr.stDispRect.u32Width = disp_width;
	ctx->vo.stLayerAttr.stDispRect.u32Height = disp_height;
	ctx->vo.stLayerAttr.stImageSize.u32Width = disp_width;
	ctx->vo.stLayerAttr.stImageSize.u32Height = disp_height;
	ctx->vo.stLayerAttr.u32DispFrmRt = 30;
	ctx->vo.stLayerAttr.enPixFormat = RK_FMT_RGB888;
	ctx->vo.stLayerAttr.bBypassFrame = RK_FALSE;
	ctx->vo.stChnAttr.stRect.s32X = 0;
	ctx->vo.stChnAttr.stRect.s32Y = 0;
	ctx->vo.stChnAttr.stRect.u32Width = spilt_disp_width;
	ctx->vo.stChnAttr.stRect.u32Height = spilt_disp_height;
	ctx->vo.stChnAttr.u32Priority = 1;
	if (s32DisId == 3) { // MIPI
		ctx->vo.stVoPubAttr.enIntfType = VO_INTF_MIPI;
		ctx->vo.stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	} else {
		ctx->vo.stVoPubAttr.enIntfType = VO_INTF_HDMI;
		ctx->vo.stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	}
	if (bUseRga) {
		ctx->vo.enSpliceMode = VO_SPLICE_MODE_RGA;
	}

	SAMPLE_COMM_VO_CreateChn(&ctx->vo);
	for (i = 1; i < s32CamNum; i++) {
		ctx->vo.s32ChnId = i;
		ctx->vo.stChnAttr.u32Priority = i;

		ctx->vo.stChnAttr.stRect.s32X = spilt_disp_width * (i % spilt_disp_colum_cnt);
		ctx->vo.stChnAttr.stRect.s32Y = spilt_disp_height * (i / spilt_disp_colum_cnt);
		RK_LOGE("vo %d rect %d %d", i, ctx->vo.stChnAttr.stRect.s32X, ctx->vo.stChnAttr.stRect.s32Y);

		ctx->vo.stChnAttr.stRect.u32Width = spilt_disp_width;
		ctx->vo.stChnAttr.stRect.u32Height = spilt_disp_height;
		s32Ret = RK_MPI_VO_SetChnAttr(ctx->vo.s32LayerId, ctx->vo.s32ChnId, &ctx->vo.stChnAttr);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vo [%d, %d, %d] RK_MPI_VO_SetChnAttr failed with %#x!",
				ctx->vo.s32DevId, ctx->vo.s32LayerId,
				ctx->vo.s32ChnId, s32Ret);
			return RK_FAILURE;
		}
		RK_LOGV("vo [%d, %d, %d] RK_MPI_VO_SetChnAttr already.",
			ctx->vo.s32DevId, ctx->vo.s32LayerId,
			ctx->vo.s32ChnId);

		s32Ret = RK_MPI_VO_EnableChn(ctx->vo.s32LayerId, ctx->vo.s32ChnId);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("vo [%d, %d, %d] RK_MPI_VO_EnableChn failed with %#x!",
				ctx->vo.s32DevId, ctx->vo.s32LayerId,
				ctx->vo.s32ChnId, s32Ret);
			return RK_FAILURE;
		}
		RK_LOGV("vo [%d, %d, %d] RK_MPI_VO_EnableChn already.",
			ctx->vo.s32DevId, ctx->vo.s32LayerId,
			ctx->vo.s32ChnId);
	}

	for (i = 0; i < s32CamNum; i++) {
		// Bind VI[0] and VPSS[0]
		ctx->vi.s32DevId = i;
		ctx->vpss.s32GrpId = i;
		ctx->vo.s32ChnId = i;

		stSrcChn.enModId = RK_ID_VI;
		stSrcChn.s32DevId = ctx->vi.s32DevId;
		stSrcChn.s32ChnId = ctx->vi.s32ChnId;
		stDestChn.enModId = RK_ID_VPSS;
		stDestChn.s32DevId = ctx->vpss.s32GrpId;
		stDestChn.s32ChnId = ctx->vpss.s32ChnId;
		SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);

		// Bind VPSS[0] and VO[0]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss.s32GrpId;
		stSrcChn.s32ChnId = ctx->vpss.s32ChnId;
		stDestChn.enModId = RK_ID_VO;
		stDestChn.s32DevId = ctx->vo.s32LayerId;
		stDestChn.s32ChnId = ctx->vo.s32ChnId;
		// SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);
	}

	printf("%s initial finish\n", __func__);

	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);

	for (i = 0; i < s32CamNum; i++) {
		ctx->vi.s32DevId = i;
		ctx->vpss.s32GrpId = i;
		ctx->vo.s32ChnId = i;

		// UnBind VPSS[0] and VO[0]
		stSrcChn.enModId = RK_ID_VPSS;
		stSrcChn.s32DevId = ctx->vpss.s32GrpId;
		stSrcChn.s32ChnId = ctx->vpss.s32ChnId;
		stDestChn.enModId = RK_ID_VO;
		stDestChn.s32DevId = ctx->vo.s32LayerId;
		stDestChn.s32ChnId = ctx->vo.s32ChnId;
		// SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);

		// UnBind VI[0] and VPSS[0]
		stSrcChn.enModId = RK_ID_VI;
		stSrcChn.s32DevId = ctx->vi.s32DevId;
		stSrcChn.s32ChnId = ctx->vi.s32ChnId;
		stDestChn.enModId = RK_ID_VPSS;
		stDestChn.s32DevId = ctx->vpss.s32GrpId;
		stDestChn.s32ChnId = ctx->vpss.s32ChnId;
		SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	}

	// Destroy VO[0]
	for (i = 1; i < s32CamNum; i++) {
		ctx->vo.s32ChnId = i;
		RK_MPI_VO_DisableChn(ctx->vo.s32LayerId, ctx->vo.s32ChnId);
	}

	ctx->vo.s32ChnId = 0;
	SAMPLE_COMM_VO_DestroyChn(&ctx->vo);
	// Destroy VPSS[0]
	for (i = 0; i < s32CamNum; i++) {
		ctx->vpss.s32GrpId = i;
		SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss);
	}
	// Destroy VI[0]
	for (i = 0; i < s32CamNum; i++) {
		ctx->vi.s32DevId = i;
		ctx->vi.u32PipeId = i;
		SAMPLE_COMM_VI_DestroyChn(&ctx->vi);
	}

#if USE_PIC
__FREE_USER_PIC:
	if (VI_USERPIC_MODE_PIC == ctx->vi.stUsrPic.enUsrPicMode &&
		ctx->vi.stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk) {
		RK_MPI_MMZ_Free(ctx->vi.stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk);
		ctx->vi.stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk = RK_NULL;
	}
#endif

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
