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

#include "sample_comm.h"

#define CAM_NUM_MAX 8

typedef struct _rkMpiCtx {
	SAMPLE_VI_CTX_S vi[CAM_NUM_MAX];
	SAMPLE_VO_CTX_S vo;
	SAMPLE_AVS_CTX_S avs;
	SAMPLE_VPSS_CTX_S vpss;
	SAMPLE_VENC_CTX_S venc;
} SAMPLE_MPI_CTX_S;

static int g_loopcount = 1;
static int g_framcount = 200;
static int g_cameranum = 6;
static int g_srcfps = 25;
static int g_dstfps = 25;
static COMPRESS_MODE_E g_compressMode = COMPRESS_AFBC_16x16;

static bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	g_loopcount = 0;
	quit = true;
}

static RK_CHAR optstr[] = "?::n:l:c:f:C:";
static const struct option long_options[] = {
    {"index", required_argument, NULL, 'n'},
    {"loop_count", required_argument, NULL, 'l'},
    {"frame_count", required_argument, NULL, 'c'},
    {"pixel_format", optional_argument, NULL, 'f'},
    {"camera_num", required_argument, NULL, 'C'},
    {"src_fps", required_argument, NULL, 1},
    {"dst_fps", required_argument, NULL, 2},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("Usage : %s -C <camera num> -n <index> -l <loop count> -c <frame count> -f "
	       "<pixel format>\n",
	       name);
	printf("index:\n");
	printf("\t 0)isp stresstest(IqFileDir: /etc/iqfiles ~ /usr/share/iqfiles)\n");
	printf("\t 1)venc stresstest(H264 ~ H265)\n");
	printf("\t 2)venc stresstest(1x ~ 0.5x resolution)\n");
	printf("\t 3)vi[6]->avs->venc stresstest\n");
	printf("pixel format:\n");
	printf("\t nv12) no compress\n");
	printf("\t afbc) compress\n");
	printf("camera number:\n");
	printf("\t 6) 6X sensor \n");
	printf("\t 8) 8X sensor \n");
	printf("set venc srcFps and dstfps:\n");
	printf("\t --src_fps: default: 25\n");
	printf("\t --dst_fps: default: 25\n");
}

/******************************************************************************
 * function : vi thread
 ******************************************************************************/
static void *vi_get_stream(void *pArgs) {
	SAMPLE_VI_CTX_S *ctx = (SAMPLE_VI_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	char name[256] = {0};
	FILE *fp = RK_NULL;
	void *pData = RK_NULL;
	RK_S32 loopCount = 0;
	RK_U32 frameSize;

	if (ctx->dstFilePath) {
		snprintf(name, sizeof(name), "/%s/vi_%d.bin", ctx->dstFilePath, ctx->s32DevId);
		fp = fopen(name, "wb");
		if (fp == RK_NULL) {
			printf("chn %d can't open %s file !\n", ctx->s32DevId, ctx->dstFilePath);
			quit = true;
			return RK_NULL;
		}
	}

	while (!quit) {
		s32Ret = SAMPLE_COMM_VI_GetChnFrame(ctx, &pData);
		if (s32Ret == RK_SUCCESS) {
			if (ctx->stViFrame.stVFrame.u64PrivateData <= 0) {
				// SAMPLE_COMM_VI_ReleaseChnFrame(ctx);
				// continue;
			}

			// exit when complete
			if (ctx->s32loopCount > 0) {
				if (loopCount >= ctx->s32loopCount) {
					SAMPLE_COMM_VI_ReleaseChnFrame(ctx);
					quit = true;
					break;
				}
			}

			frameSize = ctx->stViFrame.stVFrame.u32Height *
			            ctx->stViFrame.stVFrame.u32Width * 3 / 2;
			if (fp) {
				fwrite(pData, 1, frameSize, fp);
				fflush(fp);
			}

			printf(
			    "SAMPLE_COMM_VI_GetChnFrame DevId %d ok:data %p size:%d loop:%d seq:%d "
			    "pts:%ld ms\n",
			    ctx->s32DevId, pData, frameSize, loopCount,
			    ctx->stViFrame.stVFrame.u32TimeRef,
			    ctx->stViFrame.stVFrame.u64PTS / 1000);

			SAMPLE_COMM_VI_ReleaseChnFrame(ctx);
			loopCount++;
		}
		usleep(1000);
	}

	if (fp)
		fclose(fp);

	return RK_NULL;
}

/******************************************************************************
 * function : venc thread
 ******************************************************************************/
static void *venc_get_stream(void *pArgs) {
	SAMPLE_VENC_CTX_S *ctx = (SAMPLE_VENC_CTX_S *)(pArgs);
	RK_S32 s32Ret = RK_FAILURE;
	char name[256] = {0};
	FILE *fp = RK_NULL;
	void *pData = RK_NULL;
	RK_S32 loopCount = 0;

	if (ctx->dstFilePath) {
		snprintf(name, sizeof(name), "/%s/venc_%d.bin", ctx->dstFilePath, ctx->s32ChnId);
		fp = fopen(name, "wb");
		if (fp == RK_NULL) {
			printf("chn %d can't open %s file !\n", ctx->s32ChnId, ctx->dstFilePath);
			quit = true;
			return RK_NULL;
		}
	}

	while (!quit) {
		s32Ret = SAMPLE_COMM_VENC_GetStream(ctx, &pData);
		if (s32Ret == RK_SUCCESS) {
			// exit when complete
			if (ctx->s32loopCount > 0) {
				if (loopCount >= ctx->s32loopCount) {
					SAMPLE_COMM_VENC_ReleaseStream(ctx);
					quit = true;
					break;
				}

				if (fp) {
					fwrite(pData, 1, ctx->stFrame.pstPack->u32Len, fp);
					fflush(fp);
				}
			}

			PrintStreamDetails(ctx->s32ChnId, ctx->stFrame.pstPack->u32Len);

			RK_LOGD("chn:%d, loopCount:%d wd:%d\n", ctx->s32ChnId, loopCount,
			        ctx->stFrame.pstPack->u32Len);

			SAMPLE_COMM_VENC_ReleaseStream(ctx);
			loopCount++;
		}
		usleep(1000);
	}

	if (fp)
		fclose(fp);

	return RK_NULL;
}

/******************************************************************************
 * function    : SAMPLE_COMM_ISP_Stresstest
 ******************************************************************************/
int SAMPLE_CAMERA_ISP_Stresstest(SAMPLE_MPI_CTX_S *ctx, char *pIqFileDir) {
	RK_S32 s32Ret = RK_FAILURE;
	int video_width = 2560;
	int video_height = 1520;
	RK_CHAR *pDeviceName = NULL;
	RK_CHAR *pOutPath = NULL;
	char *iq_file_dir = pIqFileDir;
	RK_S32 i, s32CamNum = g_cameranum;
	RK_S32 s32loopCnt = g_framcount;
	RK_BOOL bMultictx = RK_FALSE;
	pthread_t vi_thread_id[CAM_NUM_MAX];
	quit = false;

	printf("#CameraNum: %d\n", s32CamNum);
	printf("#pDeviceName: %s\n", pDeviceName);
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
			return RK_FAILURE;
		}
#endif
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		printf("RK_MPI_SYS_Init failed!\n");
		goto __FAILED;
	}

	// Init VI
	for (i = 0; i < s32CamNum; i++) {
		ctx->vi[i].u32Width = video_width;
		ctx->vi[i].u32Height = video_height;
		ctx->vi[i].s32DevId = i;
		ctx->vi[i].u32PipeId = ctx->vi[i].s32DevId;
		ctx->vi[i].s32ChnId = 2;
		ctx->vi[i].stChnAttr.stIspOpt.u32BufCount = 3;
		ctx->vi[i].stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
		ctx->vi[i].stChnAttr.u32Depth = 2;
		ctx->vi[i].stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
		ctx->vi[i].stChnAttr.enCompressMode = g_compressMode;
		ctx->vi[i].stChnAttr.stFrameRate.s32SrcFrameRate = -1;
		ctx->vi[i].stChnAttr.stFrameRate.s32DstFrameRate = -1;
		ctx->vi[i].dstFilePath = pOutPath;
		ctx->vi[i].s32loopCount = s32loopCnt;
		if (g_compressMode == COMPRESS_MODE_NONE) {
			ctx->vi[i].s32ChnId = 0;
		}
		SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);

		pthread_create(&vi_thread_id[i], 0, vi_get_stream, (void *)(&ctx->vi[i]));
	}
	printf("%s initial finish\n", __func__);

	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);

	for (i = 0; i < s32CamNum; i++) {
		pthread_join(vi_thread_id[i], NULL);
	}

	// Destroy VI[0]
	for (i = 0; i < s32CamNum; i++) {
		SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
	}
__FAILED:
	RK_MPI_SYS_Exit();
	if (iq_file_dir) {
#ifdef RKAIQ
		SAMPLE_COMM_ISP_CamGroup_Stop(0);
#endif
	}

	return 0;
}

/******************************************************************************
 * function    : SAMPLE_COMM_VENC_Stresstest
 ******************************************************************************/
int SAMPLE_CAMERA_VENC_SetConfig(SAMPLE_MPI_CTX_S *ctx, MPP_CHN_S *pSrc,
                                 MPP_CHN_S *pDest) {
	quit = false;

	printf("%s entering!\n", __func__);

	// UnBind VPSS[0] and VENC[0]
	SAMPLE_COMM_UnBind(pSrc, pDest);

	// Destroy VENC[0]
	SAMPLE_COMM_VENC_DestroyChn(&ctx->venc);

	// Init VENC[0]
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc);

	// Bind VPSS[0] and VENC[0]
	SAMPLE_COMM_Bind(pSrc, pDest);

	while (!quit) {
		sleep(1);
	}

	if (ctx->venc.getStreamCbFunc) {
		pthread_join(ctx->venc.getStreamThread, NULL);
	}

	printf("%s exit!\n", __func__);

	return 0;
}

int SAMPLE_CAMERA_VENC_Stresstest(SAMPLE_MPI_CTX_S *ctx, RK_S32 mode) {
	RK_S32 s32Ret = RK_FAILURE;
	int video_width = 2560;
	int video_height = 1520;
	int avs_width = 8192;
	int avs_height = 2700;
	int venc_width = 8192;
	int venc_height = 2700;
	RK_CHAR *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pAvsMeshAlphaPath = "/tmp/";
	RK_CHAR *pAvsLutFilePath = NULL;
	RK_CHAR *pOutPathVenc = NULL;
	RK_CHAR *iq_file_dir = "/etc/iqfiles";
	RK_CHAR *pCodecName = "H264";
	CODEC_TYPE_E enCodecType = RK_CODEC_TYPE_H264;
	VENC_RC_MODE_E enRcMode = VENC_RC_MODE_H264CBR;
	RK_S32 s32BitRate = 4 * 1024;
	RK_S32 s32CamId = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	RK_S32 s32CamNum = g_cameranum;
	RK_S32 s32loopCnt = g_framcount;
	RK_S32 i;
	RK_BOOL bMultictx = RK_FALSE;
	quit = false;

	if (mode == 1) {
		s32loopCnt = -1;
	}

	printf("#CameraIdx: %d\n", s32CamId);
	printf("#AvsCalibFile: %s\n", pAvsCalibFilePath);
	printf("#AvsMeshPath: %s\n", pAvsMeshAlphaPath);
	printf("#pAvsLutFilePath: %s\n", pAvsLutFilePath);
	printf("#CodecName:%s\n", pCodecName);
	printf("#Output Path: %s\n", pOutPathVenc);

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
			printf("%s : isp canm group init failure\n", __func__);
			return RK_FAILURE;
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
		ctx->vi[i].stChnAttr.enCompressMode = g_compressMode;
		ctx->vi[i].stChnAttr.stFrameRate.s32SrcFrameRate = -1;
		ctx->vi[i].stChnAttr.stFrameRate.s32DstFrameRate = -1;
		if (g_compressMode == COMPRESS_MODE_NONE) {
			ctx->vi[i].s32ChnId = 0;
		}
		SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
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
	if (s32CamNum == 6) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4196;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 2080;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 28000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 9500;
		avs_width = 8192;
		avs_height = 2700;
		venc_width = 8192;
		venc_height = 2700;
	} else if (s32CamNum == 8) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4096;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 1800;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 36000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 8500;
		avs_width = 8192;
		avs_height = 3840;
		venc_width = 8192;
		venc_height = 3840;
	}
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Roll = 9000;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Pitch = 9000;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Width = avs_width;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Height = avs_height;
	ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
	ctx->avs.stAvsGrpAttr.bSyncPipe = RK_TRUE;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].enCompressMode = g_compressMode;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].u32Depth = 1;
	ctx->avs.stAvsChnAttr[0].u32Width = avs_width;
	ctx->avs.stAvsChnAttr[0].u32Height = avs_height;
	ctx->avs.stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->avs.stAvsChnAttr[0].u32FrameBufCnt = 15;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = pAvsCalibFilePath;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pMeshAlphaPath = pAvsMeshAlphaPath;
	s32Ret = SAMPLE_COMM_AVS_CreateChn(&ctx->avs);
	if (s32Ret != RK_SUCCESS) {
		printf("func: %s line: %d Create avs chn failure\n", __func__, __LINE__);
		goto __AVS_FAILED;
	}

	// Init VPSS[0]
	ctx->vpss.s32GrpId = 0;
	ctx->vpss.s32ChnId = 0;
	// RGA_device: VIDEO_PROC_DEV_RGA GPU_device: VIDEO_PROC_DEV_GPU
	ctx->vpss.enVProcDevType = VIDEO_PROC_DEV_GPU;
	ctx->vpss.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stGrpVpssAttr.enCompressMode = g_compressMode;

	ctx->vpss.stCropInfo.bEnable = RK_FALSE;
	ctx->vpss.stCropInfo.enCropCoordinate = VPSS_CROP_RATIO_COOR;
	ctx->vpss.stCropInfo.stCropRect.s32X = 0;
	ctx->vpss.stCropInfo.stCropRect.s32Y = 0;
	ctx->vpss.stCropInfo.stCropRect.u32Width = avs_width;
	ctx->vpss.stCropInfo.stCropRect.u32Height = avs_height;

	ctx->vpss.stChnCropInfo[0].bEnable = RK_TRUE;
	ctx->vpss.stChnCropInfo[0].enCropCoordinate = VPSS_CROP_RATIO_COOR;
	ctx->vpss.stChnCropInfo[0].stCropRect.s32X = 0;
	ctx->vpss.stChnCropInfo[0].stCropRect.s32Y = 0;
	ctx->vpss.stChnCropInfo[0].stCropRect.u32Width = venc_width * 1000 / 8192;
	ctx->vpss.stChnCropInfo[0].stCropRect.u32Height = venc_height * 1000 / 2700;
	ctx->vpss.s32ChnRotation[0] = ROTATION_0;
	ctx->vpss.stRotationEx[0].bEnable = RK_FALSE;
	ctx->vpss.stRotationEx[0].stRotationEx.u32Angle = 60;
	ctx->vpss.stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
	ctx->vpss.stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->vpss.stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
	ctx->vpss.stVpssChnAttr[0].enCompressMode = g_compressMode;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->vpss.stVpssChnAttr[0].u32Width = venc_width;
	ctx->vpss.stVpssChnAttr[0].u32Height = venc_height;
	ctx->vpss.stVpssChnAttr[0].u32FrameBufCnt = 5;
	SAMPLE_COMM_VPSS_CreateChn(&ctx->vpss);

	// Init VENC[0]
	ctx->venc.s32ChnId = 0;
	ctx->venc.u32Width = venc_width;
	ctx->venc.u32Height = venc_height;
	ctx->venc.u32SrcFps = g_srcfps;
	ctx->venc.u32DstFps = g_dstfps;
	ctx->venc.u32Gop = 50;
	ctx->venc.u32BitRate = s32BitRate;
	ctx->venc.enCodecType = enCodecType;
	ctx->venc.enRcMode = enRcMode;
	ctx->venc.getStreamCbFunc = venc_get_stream;
	ctx->venc.s32loopCount = s32loopCnt;
	ctx->venc.dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	ctx->venc.stChnAttr.stVencAttr.u32Profile = 100;
	ctx->venc.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc);

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

	// Bind AVS[0] and VPSS[0]
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss.s32GrpId;
	stDestChn.s32ChnId = ctx->vpss.s32ChnId;
	SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);

	// Bind VPSS[0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss.s32GrpId;
	stSrcChn.s32ChnId = ctx->vpss.s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc.s32ChnId;
	SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);

	printf("%s initial finish\n", __func__);

	if (mode == 0) {
		while (!quit) {
			sleep(1);
		}

		if (ctx->venc.getStreamCbFunc) {
			pthread_join(ctx->venc.getStreamThread, NULL);
		}

		if (g_loopcount > 0) {
			g_loopcount--;
			printf("sample_camera_stresstest: g_loopcount(%d)\n", g_loopcount);
		}
	}

	while (g_loopcount) {
		if (g_loopcount % 2 == 0) {
			if (mode == 0) {
				printf("Switch to h265cbr coding!\n");
				ctx->venc.enCodecType = RK_CODEC_TYPE_H265;
				ctx->venc.enRcMode = VENC_RC_MODE_H265CBR;
				ctx->venc.stChnAttr.stVencAttr.u32Profile = 0;
				s32Ret = SAMPLE_CAMERA_VENC_SetConfig(ctx, &stSrcChn, &stDestChn);
			} else if (mode == 1) {
				printf("Switch to %dx%d coding!\n", venc_width / 2, venc_height / 2);
				ctx->vpss.s32GrpId = 0;
				ctx->vpss.s32ChnId = 0;
				ctx->vpss.stVpssChnAttr[0].u32Width = venc_width / 2;
				ctx->vpss.stVpssChnAttr[0].u32Height = venc_height / 2;
				SAMPLE_COMM_VPSS_SetChnAttr(&ctx->vpss);
			}
		} else {
			if (mode == 0) {
				printf("Switch to h264cbr coding!\n");
				ctx->venc.enCodecType = RK_CODEC_TYPE_H264;
				ctx->venc.enRcMode = VENC_RC_MODE_H264CBR;
				ctx->venc.stChnAttr.stVencAttr.u32Profile = 66;
				s32Ret = SAMPLE_CAMERA_VENC_SetConfig(ctx, &stSrcChn, &stDestChn);
			} else if (mode == 1) {
				printf("Switch to %dx%d coding!\n", venc_width, venc_height);
				ctx->vpss.s32GrpId = 0;
				ctx->vpss.s32ChnId = 0;
				ctx->vpss.stVpssChnAttr[0].u32Width = venc_width;
				ctx->vpss.stVpssChnAttr[0].u32Height = venc_height;
				SAMPLE_COMM_VPSS_SetChnAttr(&ctx->vpss);
			}
		}

		if (g_loopcount > 0) {
			g_loopcount--;
			printf("sample_camera_stresstest: g_loopcount(%d)\n", g_loopcount);
		} else {
			quit = true;
			break;
		}

		sleep(2);
	}

	printf("%s exit!\n", __func__);

	if (mode == 1) {
		quit = true;
		if (ctx->venc.getStreamCbFunc) {
			pthread_join(ctx->venc.getStreamThread, NULL);
		}
	}

	// UnBind VPSS[0] and VENC[0]
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = ctx->vpss.s32GrpId;
	stSrcChn.s32ChnId = ctx->vpss.s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc.s32ChnId;
	SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	printf("%s Unbind VPSS[0] - VENC[0]!\n", __func__);

	// UnBind AVS[0] and VPSS[0]
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = ctx->vpss.s32GrpId;
	stDestChn.s32ChnId = ctx->vpss.s32ChnId;
	SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	printf("%s Unbind AVS[0] - VPSS[0]!\n", __func__);

	// UnBind Bind VI[0]~VI[5]
	for (i = 0; i < s32CamNum; i++) {
		stSrcChn.enModId = RK_ID_VI;
		stSrcChn.s32DevId = ctx->vi[i].s32DevId;
		stSrcChn.s32ChnId = ctx->vi[i].s32ChnId;
		stDestChn.enModId = RK_ID_AVS;
		stDestChn.s32DevId = ctx->avs.s32GrpId;
		stDestChn.s32ChnId = i;
		SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	}
	printf("%s Unbind VI - AVS !\n", __func__);

	// Destroy VENC[0]
	SAMPLE_COMM_VENC_DestroyChn(&ctx->venc);
	// Destroy VPSS[0]
	SAMPLE_COMM_VPSS_DestroyChn(&ctx->vpss);
	// Destroy AVS[0]
	SAMPLE_COMM_AVS_DestroyChn(&ctx->avs);
	// Destroy VI[0]~VI[5]
__AVS_FAILED:
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

	return 0;
}

/******************************************************************************
 * function    : SAMPLE_COMM_VI_AVS_VENC_Stresstest
 ******************************************************************************/
int SAMPLE_CAMERA_VI_AVS_VENC_Stresstest(SAMPLE_MPI_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;
	int video_width = 2560;
	int video_height = 1520;
	int avs_width = 8192;
	int avs_height = 2700;
	int venc_width = 8192;
	int venc_height = 2700;
	RK_CHAR *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.pto";
	RK_CHAR *pAvsMeshAlphaPath = "/tmp/";
	RK_CHAR *pOutPathVenc = "/data/";
	RK_CHAR *iq_file_dir = "/etc/iqfiles";
	RK_CHAR *pCodecName = "H265";
	CODEC_TYPE_E enCodecType = RK_CODEC_TYPE_H265;
	VENC_RC_MODE_E enRcMode = VENC_RC_MODE_H265CBR;
	RK_S32 s32BitRate = 4 * 1024;
	RK_S32 s32CamId = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	RK_S32 s32CamNum = g_cameranum;
	RK_S32 s32loopCnt = g_framcount;
	RK_S32 i;
	RK_BOOL bMultictx = RK_FALSE;
	quit = false;

	printf("#CameraIdx: %d\n", s32CamId);
	printf("#AvsCalibFile: %s\n", pAvsCalibFilePath);
	printf("#AvsMeshPath: %s\n", pAvsMeshAlphaPath);
	printf("#CodecName:%s\n", pCodecName);
	printf("#Output Path: %s\n", pOutPathVenc);
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
			printf("%s : isp cam group init failure\n", __func__);
			return RK_FAILURE;
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
		ctx->vi[i].stChnAttr.u32Depth = 0;
		ctx->vi[i].stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
		ctx->vi[i].stChnAttr.enCompressMode = g_compressMode;
		ctx->vi[i].stChnAttr.stFrameRate.s32SrcFrameRate = -1;
		ctx->vi[i].stChnAttr.stFrameRate.s32DstFrameRate = -1;
		if (g_compressMode == COMPRESS_MODE_NONE) {
			ctx->vi[i].s32ChnId = 0;
		}
		SAMPLE_COMM_VI_CreateChn(&ctx->vi[i]);
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
	if (s32CamNum == 6) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4196;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 2080;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 28000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 9500;
		avs_width = 8192;
		avs_height = 2700;
		venc_width = 8192;
		venc_height = 2700;
	} else if (s32CamNum == 8) {
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32X = 4096;
		ctx->avs.stAvsGrpAttr.stOutAttr.stCenter.s32Y = 1800;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 36000;
		ctx->avs.stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 8500;
		avs_width = 8192;
		avs_height = 3840;
		venc_width = 8192;
		venc_height = 2700;
	}
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Roll = 9000;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Pitch = 9000;
	ctx->avs.stAvsGrpAttr.stOutAttr.stORIRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Roll = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Pitch = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stRotation.s32Yaw = 0;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Width = avs_width;
	ctx->avs.stAvsGrpAttr.stOutAttr.stSize.u32Height = avs_height;
	ctx->avs.stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
	ctx->avs.stAvsGrpAttr.bSyncPipe = RK_TRUE;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].enCompressMode = g_compressMode;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	ctx->avs.stAvsChnAttr[0].u32Depth = 1;
	ctx->avs.stAvsChnAttr[0].u32Width = avs_width;
	ctx->avs.stAvsChnAttr[0].u32Height = avs_height;
	ctx->avs.stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = pAvsCalibFilePath;
	ctx->avs.stAvsGrpAttr.stInAttr.stCalib.pMeshAlphaPath = pAvsMeshAlphaPath;
	s32Ret = SAMPLE_COMM_AVS_CreateChn(&ctx->avs);
	if (s32Ret != RK_SUCCESS) {
		printf("func: %s line: %d Create avs chn failure\n", __func__, __LINE__);
		goto __AVS_FAILED;
	}

	// Init VENC[0]
	ctx->venc.s32ChnId = 0;
	ctx->venc.u32Width = venc_width;
	ctx->venc.u32Height = venc_height;
	ctx->venc.u32SrcFps = g_srcfps;
	ctx->venc.u32DstFps = g_dstfps;
	ctx->venc.u32Gop = 50;
	ctx->venc.u32BitRate = s32BitRate;
	ctx->venc.enCodecType = enCodecType;
	ctx->venc.enRcMode = enRcMode;
	ctx->venc.getStreamCbFunc = venc_get_stream;
	ctx->venc.s32loopCount = s32loopCnt;
	ctx->venc.dstFilePath = pOutPathVenc;
	// H264  66：Baseline  77：Main Profile 100：High Profile
	// H265  0：Main Profile  1：Main 10 Profile
	// MJPEG 0：Baseline
	ctx->venc.stChnAttr.stVencAttr.u32Profile = 0;
	ctx->venc.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP; // VENC_GOPMODE_SMARTP
	SAMPLE_COMM_VENC_CreateChn(&ctx->venc);

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

	// Bind AVS[0] and VENC[0]
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc.s32ChnId;
	SAMPLE_COMM_Bind(&stSrcChn, &stDestChn);

	printf("%s initial finish\n", __func__);

	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);

	if (ctx->venc.getStreamCbFunc) {
		pthread_join(ctx->venc.getStreamThread, NULL);
	}

	// UnBind avs[0] and VENC[0]
	stSrcChn.enModId = RK_ID_AVS;
	stSrcChn.s32DevId = ctx->avs.s32GrpId;
	stSrcChn.s32ChnId = ctx->avs.s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = ctx->venc.s32ChnId;
	SAMPLE_COMM_UnBind(&stSrcChn, &stDestChn);
	// Destroy VENC[0]
	SAMPLE_COMM_VENC_DestroyChn(&ctx->venc);

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
	// Destroy VI[0]~VI[5]
	for (i = 0; i < s32CamNum; i++) {
		SAMPLE_COMM_VI_DestroyChn(&ctx->vi[i]);
	}

__AVS_FAILED:
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

	return 0;
}

/******************************************************************************
 * function    : main()
 * Description : main
 ******************************************************************************/
int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 s32Index;
	SAMPLE_MPI_CTX_S ctx;

	if (argc < 2) {
		print_usage(argv[0]);
		return 0;
	}

	signal(SIGINT, sigterm_handler);

	int c;
	while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
		switch (c) {
		case 'n':
			s32Index = atoi(optarg);
			break;
		case 'l':
			g_loopcount = atoi(optarg);
			break;
		case 'c':
			g_framcount = atoi(optarg);
			break;
		case 'C':
			g_cameranum = atoi(optarg);
			break;
		case 'f':
			if (!strcmp(optarg, "nv12")) {
				g_compressMode = COMPRESS_MODE_NONE;
			} else if (!strcmp(optarg, "afbc")) {
				g_compressMode = COMPRESS_AFBC_16x16;
			}
			break;
		case 1:
			g_srcfps = atoi(optarg);
			break;
		case 2:
			g_dstfps = atoi(optarg);
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return 0;
		}
	}

	memset(&ctx, 0, sizeof(SAMPLE_MPI_CTX_S));

	while (g_loopcount) {
		switch (s32Index) {
		case 0:
			if (g_loopcount % 2 == 0) {
				s32Ret = SAMPLE_CAMERA_ISP_Stresstest(&ctx, "/etc/iqfiles");
				if (s32Ret != RK_SUCCESS) {
					printf("%s : ISP stresstest depend on /etc/iqfiles failure\n",
					       __func__);
				}
			} else {
				s32Ret = SAMPLE_CAMERA_ISP_Stresstest(&ctx, "/oem/usr/share/iqfiles");
				if (s32Ret != RK_SUCCESS) {
					printf("%s : ISP stresstest depend on /usr/share/iqfile failure\n",
					       __func__);
				}
			}
			break;
		case 1:
			s32Ret = SAMPLE_CAMERA_VENC_Stresstest(&ctx, 0);
			if (s32Ret != RK_SUCCESS) {
				printf("%s : Venc stresstest failure for long time\n", __func__);
			}
			g_loopcount = 0;
			break;
		case 2:
			s32Ret = SAMPLE_CAMERA_VENC_Stresstest(&ctx, 1);
			if (s32Ret != RK_SUCCESS) {
				printf("%s : Venc stresstest failure\n", __func__);
			}
			g_loopcount = 0;
			break;
		case 3:
			s32Ret = SAMPLE_CAMERA_VI_AVS_VENC_Stresstest(&ctx);
			if (s32Ret != RK_SUCCESS) {
				printf("%s : stresstest include vi,avs and venc failure\n", __func__);
			}
			break;
		default:
			printf("the index %d is invaild!\n", s32Index);
			print_usage(argv[0]);
			return RK_FAILURE;
		}

		if (g_loopcount > 0) {
			g_loopcount--;
			printf("sample_camera_stresstest: g_loopcount(%d)\n", g_loopcount);
		} else {
			break;
		}

		sleep(2);
	}

	printf("%s finish!\n", __func__);

	return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
