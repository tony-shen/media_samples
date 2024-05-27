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
#ifndef __SAMPLE_COMM_H__
#define __SAMPLE_COMM_H__

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include "loadbmp.h"
#include "rk_comm_dis.h"
#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_adec.h"
#include "rk_mpi_aenc.h"
#include "rk_mpi_ai.h"
#include "rk_mpi_ao.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_tde.h"
#include "rk_mpi_vdec.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vgs.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"
#include "sample_comm_isp.h"

/*******************************************************
    macro define
*******************************************************/
#define RKAIQ
#define CAM_NUM_MAX 8
#define BUFFER_BYTE_SIZE 255
#define RK_ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define RK_ALIGN_16(x) RK_ALIGN(x, 16)
#define RK_ALIGN_64(x) RK_ALIGN(x, 64)
#define RK_ALIGN_256(x) RK_ALIGN(x, 256)
typedef void *(*Thread_Func)(void *param);

/*******************************************************
    enum define
*******************************************************/
typedef enum rk_CODEC_TYPE_E {
	RK_CODEC_TYPE_NONE = -1,
	// Audio
	RK_CODEC_TYPE_MP3,
	RK_CODEC_TYPE_MP2,
	RK_CODEC_TYPE_G711A,
	RK_CODEC_TYPE_G711U,
	RK_CODEC_TYPE_G726,
	// Video
	RK_CODEC_TYPE_H264,
	RK_CODEC_TYPE_H265,
	RK_CODEC_TYPE_JPEG,
	RK_CODEC_TYPE_MJPEG,
	RK_CODEC_TYPE_NB
} CODEC_TYPE_E;

typedef enum rkVGS_TASK_TYPE_E {
	VGS_TASK_TYPE_NONE = 0,
	VGS_TASK_TYPE_SCALE,
	VGS_TASK_TYPE_ROTATE,
	VGS_TASK_TYPE_DRAW_LINE,
	VGS_TASK_TYPE_COVER,
	VGS_TASK_TYPE_OSD,
	VGS_TASK_TYPE_MOSAIC,
	VGS_TASK_TYPE_CROP,
	VGS_TASK_TYPE_BUTT
} VGS_TASK_TYPE_E;

/*******************************************************
    structure define
*******************************************************/
typedef struct _rkMpiAICtx {
	RK_S32 s32loopCount;
	RK_CHAR *dstFilePath;
	AUDIO_DEV s32DevId;
	AI_CHN s32ChnId;
	RK_S32 s32ChnSampleRate;
	AIO_ATTR_S stAiAttr;
	AUDIO_FRAME_S stFrame;
} SAMPLE_AI_CTX_S;

typedef struct _rkMpiAOCtx {
	AUDIO_DEV s32DevId;
	AO_CHN s32ChnId;
	RK_S32 s32ChnSampleRate;
	AIO_ATTR_S stAoAttr;
} SAMPLE_AO_CTX_S;

typedef struct _rkMpiVICtx {
	RK_U32 u32Width;
	RK_U32 u32Height;
	RK_S32 s32loopCount;
	VI_DEV s32DevId;
	VI_PIPE u32PipeId;
	VI_CHN s32ChnId;
	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	VI_CHN_ATTR_S stChnAttr;
	VI_SAVE_FILE_INFO_S stDebugFile;
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	RK_CHAR *dstFilePath;
	VI_USERPIC_ATTR_S stUsrPic;
	RK_BOOL bUserPicEnabled;
	RK_CHAR *pUserPicFile;
} SAMPLE_VI_CTX_S;

typedef struct _rkMpiDISCtx {
	VI_DEV s32DevId;
	VI_PIPE u32PipeId;
	VI_CHN s32ChnId;
	DIS_CONFIG_S stDisConfig;
	DIS_ATTR_S stDisAttr;
	RK_CHAR *dstFilePath;
} SAMPLE_DIS_CTX_S;

typedef struct _rkMpiVOCtx {
	RK_U32 u32DispBufLen;
	VO_CHN s32ChnId;
	VO_DEV s32DevId;
	VO_LAYER s32LayerId;
	VO_LAYER_MODE_E Volayer_mode;
	VO_PUB_ATTR_S stVoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CHN_ATTR_S stChnAttr;
	VO_SPLICE_MODE_E enSpliceMode;
} SAMPLE_VO_CTX_S;

typedef struct _rkMpiVENCCtx {
	RK_BOOL bSliceSplit;
	RK_U32 u32Width;
	RK_U32 u32Height;
	RK_U32 u32SrcFps;
	RK_U32 u32DstFps;
	RK_U32 u32Gop;
	RK_U32 u32BitRate;
	RK_S32 s32loopCount;
	RK_U32 u32Qfactor;
	RK_U32 u32OneStreamBuffer;
	RK_S32 u32SliceMode;
	RK_S32 u32SliceSize;
	RK_S32 u32SlicePacketNum;
	PIXEL_FORMAT_E enPixelFormat;
	CODEC_TYPE_E enCodecType;
	VENC_RC_MODE_E enRcMode;
	VENC_CHN s32ChnId;
	VENC_CHN_ATTR_S stChnAttr;
	VENC_STREAM_S stFrame;
	pthread_t getStreamThread;
	Thread_Func getStreamCbFunc;
	MB_POOL pool;
	RK_CHAR *srcFilePath;
	RK_CHAR *dstFilePath;
} SAMPLE_VENC_CTX_S;

typedef struct _rkMpiAENCCtx {
	RK_S32 s32loopCount;
	RK_CHAR *dstFilePath;
	AENC_CHN s32ChnId;
	AUDIO_STREAM_S stFrame;
	AENC_CHN_ATTR_S stChnAttr;
} SAMPLE_AENC_CTX_S;

typedef struct _rkMpiRGNCtx {
	const char *srcFileBmpName;
	RK_U32 u32BmpFormat;
	RK_U32 u32BgAlpha;
	RK_U32 u32FgAlpha;
	BITMAP_S stBitmap;
	MPP_CHN_S stMppChn;
	RECT_S stRegion;
	RK_U32 u32Color;
	RK_U32 u32Layer;
	RGN_HANDLE rgnHandle;
	RGN_ATTR_S stRgnAttr;
	RGN_CHN_ATTR_S stRgnChnAttr;
} SAMPLE_RGN_CTX_S;

typedef struct _rkMpiVPSSCtx {
	RK_S32 s32loopCount;
	VPSS_GRP s32GrpId;
	VPSS_CHN s32ChnId;
	RK_S32 s32RotationEx;
	RK_S32 s32GrpCropRatio;
	RK_S32 s32ChnCropRatio;
	VPSS_GRP_ATTR_S stGrpVpssAttr;
	VPSS_CROP_INFO_S stCropInfo;
	VIDEO_PROC_DEV_TYPE_E enVProcDevType;
	RK_S32 s32ChnRotation[VPSS_MAX_CHN_NUM];
	VPSS_CROP_INFO_S stChnCropInfo[VPSS_MAX_CHN_NUM];
	VPSS_ROTATION_EX_ATTR_S stRotationEx[VPSS_MAX_CHN_NUM];
	VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
	MB_POOL pool;
	RK_CHAR *srcFilePath;
	RK_CHAR *dstFilePath;
	VIDEO_FRAME_INFO_S stChnFrameInfos;
} SAMPLE_VPSS_CTX_S;

typedef struct _rkMpiAVSCtx {
	RK_VOID *pLUTVirAddr[CAM_NUM_MAX];
	RK_CHAR *pLutFilePath;
	RK_S32 s32loopCount;
	AVS_GRP s32GrpId;
	AVS_CHN s32ChnId;
	AVS_MOD_PARAM_S stAvsModParam;
	AVS_GRP_ATTR_S stAvsGrpAttr;
	AVS_OUTPUT_ATTR_S stAvsOutAttr;
	AVS_CHN_ATTR_S stAvsChnAttr[AVS_MAX_CHN_NUM];
	VIDEO_FRAME_INFO_S stVideoFrame;
	RK_CHAR *dstFilePath;
} SAMPLE_AVS_CTX_S;

typedef struct _rkReadOneFrameCtx {
	RK_S32 s32FrameWidth;
	RK_S32 s32FrameHeight;
	COMPRESS_MODE_E enCompMode;
	PIXEL_FORMAT_E enPixelFormat;
	RK_CHAR pFilePath[BUFFER_BYTE_SIZE];
	VIDEO_FRAME_INFO_S *pipe_frames;
} SAMPLE_READ_FRAME_CTX_S;

/*******************************************************
    function announce
*******************************************************/
void PrintStreamDetails(int chnId, int framesize);
void SIGSEGV_handler(int signum, siginfo_t *sig_info, void *context);

RK_S32 SAMPLE_COMM_VI_CreateChn(SAMPLE_VI_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VI_DestroyChn(SAMPLE_VI_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VI_GetChnFrame(SAMPLE_VI_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_VI_ReleaseChnFrame(SAMPLE_VI_CTX_S *ctx);

RK_S32 SAMPLE_COMM_DIS_ChangeStillCrop(SAMPLE_DIS_CTX_S *ctx);

RK_S32 SAMPLE_COMM_AI_CreateChn(SAMPLE_AI_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AI_DestroyChn(SAMPLE_AI_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AI_GetFrame(SAMPLE_AI_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_AI_ReleaseFrame(SAMPLE_AI_CTX_S *ctx);

RK_S32 SAMPLE_COMM_VO_CreateChn(SAMPLE_VO_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VO_DestroyChn(SAMPLE_VO_CTX_S *ctx);

RK_S32 SAMPLE_COMM_AO_CreateChn(SAMPLE_AO_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AO_DestroyChn(SAMPLE_AO_CTX_S *ctx);

RK_S32 SAMPLE_COMM_VPSS_CreateChn(SAMPLE_VPSS_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VPSS_SendStream(SAMPLE_VPSS_CTX_S *ctx, void *pdata, RK_S32 width,
                                   RK_S32 height, RK_S32 size,
                                   COMPRESS_MODE_E enCompressMode);
RK_S32 SAMPLE_COMM_VPSS_GetChnFrame(SAMPLE_VPSS_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_VPSS_ReleaseChnFrame(SAMPLE_VPSS_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VPSS_DestroyChn(SAMPLE_VPSS_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VPSS_SetChnAttr(SAMPLE_VPSS_CTX_S *ctx);

RK_S32 SAMPLE_COMM_AVS_CreateChn(SAMPLE_AVS_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AVS_DestroyChn(SAMPLE_AVS_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AVS_GetChnFrame(SAMPLE_AVS_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_AVS_ReleaseChnFrame(SAMPLE_AVS_CTX_S *ctx);

RK_S32 SAMPLE_COMM_VENC_CreateChn(SAMPLE_VENC_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VENC_GetStream(SAMPLE_VENC_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_VENC_ReleaseStream(SAMPLE_VENC_CTX_S *ctx);
RK_S32 SAMPLE_COMM_VENC_DestroyChn(SAMPLE_VENC_CTX_S *ctx);

RK_S32 SAMPLE_COMM_AENC_CreateChn(SAMPLE_AENC_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AENC_GetStream(SAMPLE_AENC_CTX_S *ctx, void **pdata);
RK_S32 SAMPLE_COMM_AENC_ReleaseStream(SAMPLE_AENC_CTX_S *ctx);
RK_S32 SAMPLE_COMM_AENC_DestroyChn(SAMPLE_AENC_CTX_S *ctx);

RK_S32 SAMPLE_COMM_RGN_CreateChn(SAMPLE_RGN_CTX_S *ctx);
RK_S32 SAMPLE_COMM_RGN_DestroyChn(SAMPLE_RGN_CTX_S *ctx);

RK_S32 SAMPLE_COMM_Bind(const MPP_CHN_S *pstSrcChn, const MPP_CHN_S *pstDestChn);
RK_S32 SAMPLE_COMM_UnBind(const MPP_CHN_S *pstSrcChn, const MPP_CHN_S *pstDestChn);

RK_S32 SAMPLE_COMM_TEST_ReadFrameFromFile(SAMPLE_READ_FRAME_CTX_S *pReadFrame);
RK_U64 SAMPLE_COMM_TEST_GetNowUs(void);
void SAMPLE_COMM_TEST_CreateFile(RK_CHAR *path, FILE **fp, RK_CHAR *mod, RK_S32 chnid,
                                 RK_S32 file_index);

RK_S32 SAMPLE_COMM_FillImage(RK_U8 *buf, RK_U32 width, RK_U32 height, RK_U32 hor_stride,
                             RK_U32 ver_stride, PIXEL_FORMAT_E fmt, RK_U32 frame_count);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef __SAMPLE_COMMON_H__ */
