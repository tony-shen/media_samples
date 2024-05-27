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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/poll.h>
#include <unistd.h>

#include "sample_comm.h"

RK_S32 SAMPLE_COMM_VENC_CreateChn(SAMPLE_VENC_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_PARAM_MOD_S stModParam;
	VENC_JPEG_PARAM_S *stJpegParam =
	    (VENC_JPEG_PARAM_S *)malloc(sizeof(VENC_JPEG_PARAM_S));
	VENC_RC_PARAM_S *venc_rc_param = (VENC_RC_PARAM_S *)malloc(sizeof(VENC_RC_PARAM_S));
	memset(stJpegParam, 0, sizeof(VENC_JPEG_PARAM_S));
	memset(venc_rc_param, 0, sizeof(VENC_RC_PARAM_S));
	memset(&stModParam, 0, sizeof(VENC_PARAM_MOD_S));
	switch (ctx->enCodecType) {
	case RK_CODEC_TYPE_H265:
		ctx->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (ctx->enRcMode == VENC_RC_MODE_H265CBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			ctx->stChnAttr.stRcAttr.stH265Cbr.u32Gop = ctx->u32Gop;
			ctx->stChnAttr.stRcAttr.stH265Cbr.u32BitRate = ctx->u32BitRate;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = ctx->u32SrcFps;
		} else if (ctx->enRcMode == VENC_RC_MODE_H265VBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			ctx->stChnAttr.stRcAttr.stH265Vbr.u32Gop = ctx->u32Gop;
			ctx->stChnAttr.stRcAttr.stH265Vbr.u32BitRate = ctx->u32BitRate;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = ctx->u32SrcFps;
		}
		break;
	case RK_CODEC_TYPE_JPEG:
	case RK_CODEC_TYPE_MJPEG:

		if (ctx->enCodecType == RK_CODEC_TYPE_JPEG) {
			ctx->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_JPEG; // RK_VIDEO_ID_MJPEG
		} else if (ctx->enCodecType == RK_CODEC_TYPE_MJPEG) {
			ctx->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_MJPEG; // RK_VIDEO_ID_MJPEG
		}
		if (ctx->enRcMode == VENC_RC_MODE_MJPEGCBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = ctx->u32SrcFps;
			ctx->stChnAttr.stRcAttr.stMjpegCbr.u32BitRate =
			    ctx->u32Width * ctx->u32Height * 8;
		} else if (ctx->enRcMode == VENC_RC_MODE_MJPEGVBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGVBR;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stMjpegVbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stMjpegVbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stMjpegVbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stMjpegVbr.u32SrcFrameRateNum = ctx->u32SrcFps;
			ctx->stChnAttr.stRcAttr.stMjpegVbr.u32BitRate =
			    ctx->u32Width * ctx->u32Height * 8;
		}
		break;
	case RK_CODEC_TYPE_H264:
	default:
		ctx->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (ctx->enRcMode == VENC_RC_MODE_H264CBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			ctx->stChnAttr.stRcAttr.stH264Cbr.u32Gop = ctx->u32Gop;
			ctx->stChnAttr.stRcAttr.stH264Cbr.u32BitRate = ctx->u32BitRate;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = ctx->u32SrcFps;
		} else if (ctx->enRcMode == VENC_RC_MODE_H264VBR) {
			ctx->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			ctx->stChnAttr.stRcAttr.stH264Vbr.u32Gop = ctx->u32Gop;
			ctx->stChnAttr.stRcAttr.stH264Vbr.u32BitRate = ctx->u32BitRate;
			// frame rate: in u32Fps/1, out u32Fps/1.
			ctx->stChnAttr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = ctx->u32DstFps;
			ctx->stChnAttr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
			ctx->stChnAttr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = ctx->u32SrcFps;
		}
		break;
	}

	ctx->stChnAttr.stVencAttr.enPixelFormat = ctx->enPixelFormat;
	ctx->stChnAttr.stVencAttr.u32PicWidth = ctx->u32Width;
	ctx->stChnAttr.stVencAttr.u32PicHeight = ctx->u32Height;
	ctx->stChnAttr.stVencAttr.u32VirWidth = ctx->u32Width;
	ctx->stChnAttr.stVencAttr.u32VirHeight = ctx->u32Height;
	ctx->stChnAttr.stVencAttr.u32StreamBufCnt = 5;
	ctx->stChnAttr.stVencAttr.u32BufSize = ctx->u32Width * ctx->u32Height * 3 / 2;

	s32Ret = RK_MPI_VENC_CreateChn(ctx->s32ChnId, &ctx->stChnAttr);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VENC_CreateChn failed %x", s32Ret);
		return s32Ret;
	}
	if (ctx->bSliceSplit) {
		switch (ctx->enCodecType) {
		case RK_CODEC_TYPE_H264:
			RK_MPI_VENC_GetModParam(&stModParam);
			stModParam.enVencModType = MODTYPE_H264E;
			stModParam.stH264eModParam.u32OneStreamBuffer = ctx->u32OneStreamBuffer;
			RK_MPI_VENC_SetModParam(&stModParam);
			break;
		case RK_CODEC_TYPE_H265:
			RK_MPI_VENC_GetModParam(&stModParam);
			stModParam.enVencModType = MODTYPE_H265E;
			stModParam.stH265eModParam.u32OneStreamBuffer = ctx->u32OneStreamBuffer;
			RK_MPI_VENC_SetModParam(&stModParam);
			break;
		case RK_CODEC_TYPE_JPEG:
		case RK_CODEC_TYPE_MJPEG:
			RK_MPI_VENC_GetModParam(&stModParam);
			stModParam.enVencModType = MODTYPE_JPEGE;
			stModParam.stJpegeModParam.u32OneStreamBuffer = ctx->u32OneStreamBuffer;
			RK_MPI_VENC_SetModParam(&stModParam);
			break;
		default:
			break;
		}
	}

	VENC_CHN_PARAM_S pstChnParam;
	memset(&pstChnParam, 0, sizeof(VENC_CHN_PARAM_S));
	pstChnParam.stFrameRate.bEnable = RK_TRUE;
	pstChnParam.stFrameRate.s32DstFrmRateDen = 1;
	pstChnParam.stFrameRate.s32DstFrmRateNum = ctx->u32DstFps;
	pstChnParam.stFrameRate.s32SrcFrmRateDen = 1;
	pstChnParam.stFrameRate.s32SrcFrmRateNum = ctx->u32SrcFps;
	s32Ret = RK_MPI_VENC_SetChnParam(ctx->s32ChnId, &pstChnParam);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("create %d ch venc failed", ctx->s32ChnId);
		return s32Ret;
	}

	stRecvParam.s32RecvPicNum = -1;
	s32Ret = RK_MPI_VENC_StartRecvFrame(ctx->s32ChnId, &stRecvParam);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("create %d ch venc failed", ctx->s32ChnId);
		return s32Ret;
	}

	if (ctx->enCodecType == RK_CODEC_TYPE_JPEG) {
		stJpegParam->u32Qfactor = ctx->u32Qfactor;
		s32Ret = RK_MPI_VENC_SetJpegParam(ctx->s32ChnId, stJpegParam);
	} else if (ctx->enCodecType == RK_CODEC_TYPE_MJPEG) {
		venc_rc_param->stParamMjpeg.u32Qfactor = ctx->u32Qfactor;
		s32Ret = RK_MPI_VENC_SetRcParam(ctx->s32ChnId, venc_rc_param);
	}
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("set %d ch qfactor failed", ctx->s32ChnId);
		return s32Ret;
	}

	if (ctx->bSliceSplit) {
		VENC_SLICE_SPLIT_S stSliceSplit;
		RK_MPI_VENC_GetSliceSplit(ctx->s32ChnId, &stSliceSplit);
		stSliceSplit.bSplitEnable = RK_TRUE;
		stSliceSplit.u32SplitMode = ctx->u32SliceMode;
		stSliceSplit.u32SplitSize = ctx->u32SliceSize;
		RK_MPI_VENC_SetSliceSplit(ctx->s32ChnId, &stSliceSplit);
	}

	if (ctx->getStreamCbFunc) {
		if (!ctx->bSliceSplit) {
			ctx->stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
		} else {
			ctx->stFrame.pstPack =
			    (VENC_PACK_S *)malloc(ctx->u32SlicePacketNum * sizeof(VENC_PACK_S));
		}
		pthread_create(&ctx->getStreamThread, 0, ctx->getStreamCbFunc, (void *)(ctx));
	}

	free(stJpegParam);
	free(venc_rc_param);
	return RK_SUCCESS;
}

RK_S32 SAMPLE_COMM_VENC_SendStream(SAMPLE_VENC_CTX_S *ctx, void *pdata, RK_S32 width,
                                   RK_S32 height, RK_S32 size,
                                   COMPRESS_MODE_E enCompressMode) {
	RK_S32 s32Ret = RK_FAILURE;
	MB_BLK blk = RK_NULL;
	RK_U8 *pVirAddr = RK_NULL;
	RK_S32 s32ReachEOS = 0;
	VIDEO_FRAME_INFO_S stFrame;

__RETRY0:
	blk = RK_MPI_MB_GetMB(ctx->pool, size, RK_TRUE);
	if (RK_NULL == blk) {
		RK_LOGE("RK_MPI_MB_GetMB fail %x", blk);
		usleep(2000llu);
		goto __RETRY0;
	}

	pVirAddr = (RK_U8 *)(RK_MPI_MB_Handle2VirAddr(blk));

	pVirAddr = pdata;

	RK_MPI_SYS_MmzFlushCache(blk, RK_FALSE);

	stFrame.stVFrame.pMbBlk = blk;
	stFrame.stVFrame.u32Width = width;
	stFrame.stVFrame.u32Height = height;
	stFrame.stVFrame.u32VirWidth = width;
	stFrame.stVFrame.u32VirHeight = height;
	stFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
	stFrame.stVFrame.u32FrameFlag |= s32ReachEOS ? FRAME_FLAG_SNAP_END : 0;
	stFrame.stVFrame.enCompressMode = enCompressMode;

__RETRY1:
	s32Ret = RK_MPI_VENC_SendFrame(ctx->s32ChnId, &stFrame, -1);
	if (s32Ret == RK_SUCCESS) {
		RK_MPI_MB_ReleaseMB(blk);
	} else {
		RK_LOGE("RK_MPI_VENC_SendFrame fail %x", s32Ret);
		usleep(10000llu);
		goto __RETRY1;
	}

	return s32Ret;
}

RK_S32 SAMPLE_COMM_VENC_GetStream(SAMPLE_VENC_CTX_S *ctx, void **pdata) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_VENC_GetStream(ctx->s32ChnId, &ctx->stFrame, -1);
	if (s32Ret == RK_SUCCESS) {
		*pdata = RK_MPI_MB_Handle2VirAddr(ctx->stFrame.pstPack->pMbBlk);
	} else {
		RK_LOGE("CHN_ID %d RK_MPI_VENC_GetStream fail %#X", ctx->s32ChnId, s32Ret);
	}

	return s32Ret;
}

RK_S32 SAMPLE_COMM_VENC_ReleaseStream(SAMPLE_VENC_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_VENC_ReleaseStream(ctx->s32ChnId, &ctx->stFrame);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
	}

	return s32Ret;
}

RK_S32 SAMPLE_COMM_VENC_DestroyChn(SAMPLE_VENC_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_VENC_StopRecvFrame(ctx->s32ChnId);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	RK_LOGE("destroy enc chn:%d", ctx->s32ChnId);
	s32Ret = RK_MPI_VENC_DestroyChn(ctx->s32ChnId);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
	}

	if (ctx->stFrame.pstPack) {
		free(ctx->stFrame.pstPack);
	}

	return RK_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
