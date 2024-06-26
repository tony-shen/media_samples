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
#ifdef AVS_ENABLE
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

RK_S32 SAMPLE_COMM_AVS_CreateChn(SAMPLE_AVS_CTX_S *ctx) {
	RK_S32 chnIndex;
	RK_S32 s32Ret = RK_SUCCESS;

	if (ctx->s32GrpId >= AVS_MAX_GRP_NUM) {
		RK_LOGE("s32GrpNum is less than the maximum channel: %d", AVS_MAX_GRP_NUM);
		return RK_FAILURE;
	}

	if (ctx->s32ChnId >= AVS_MAX_CHN_NUM) {
		RK_LOGE("s32ChnNum is less than the maximum channel: %d", AVS_MAX_CHN_NUM);
		return RK_FAILURE;
	}

	s32Ret = RK_MPI_AVS_SetModParam(&ctx->stAvsModParam);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_AVS_SetModParam failed with %#x!\n", s32Ret);
		return s32Ret;
	}
	if (ctx->stAvsGrpAttr.stInAttr.enParamSource == AVS_PARAM_SOURCE_LUT) {
		RK_U32 i = 0;
		RK_CHAR pLutname[CAM_NUM_MAX][255] = {0};
		FILE *fp = RK_NULL;
		RK_U32 file_size = 0;
		for (i = 0; i < ctx->stAvsGrpAttr.u32PipeNum; i++) {
			snprintf(pLutname[i], sizeof(pLutname[i]), "%s/rk_ps_lut_%d.bin",
			         ctx->pLutFilePath, i);
			fp = fopen(pLutname[i], "rb");
			if (fp == NULL) {
				RK_LOGE("the %s file opened failure ", pLutname[i]);
				return RK_FAILURE;
			}
			fseek(fp, 0, SEEK_END);
			file_size = ftell(fp);
			file_size = RK_ALIGN_256(file_size);
			fseek(fp, 0, SEEK_SET);
			ctx->pLUTVirAddr[i] = malloc(file_size);
			if (ctx->pLUTVirAddr[i] == RK_NULL) {
				RK_LOGE("malloc pLUTVirAddr[%d] failure", i);
				return RK_FAILURE;
			}
			fread(ctx->pLUTVirAddr[i], 1, file_size, fp);
			ctx->stAvsGrpAttr.stInAttr.stLUT.pVirAddr[i] = ctx->pLUTVirAddr[i];
			if (fp) {
				fclose(fp);
				fp = RK_NULL;
			}
		}
	}

	s32Ret = RK_MPI_AVS_CreateGrp(ctx->s32GrpId, &ctx->stAvsGrpAttr);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_AVS_CreateGrp failed with %#x!\n", s32Ret);
		return s32Ret;
	}

	for (chnIndex = 0; chnIndex < AVS_MAX_CHN_NUM; chnIndex++) {
		if (ctx->stAvsChnAttr[chnIndex].u32Width &&
		    ctx->stAvsChnAttr[chnIndex].u32Height) {
			s32Ret = RK_MPI_AVS_SetChnAttr(ctx->s32GrpId, chnIndex,
			                               &ctx->stAvsChnAttr[chnIndex]);
			if (RK_SUCCESS != s32Ret) {
				RK_LOGE("RK_MPI_AVS_SetChnAttr failed with %#x!\n", s32Ret);
				return s32Ret;
			}

			s32Ret = RK_MPI_AVS_EnableChn(ctx->s32GrpId, chnIndex);
			if (RK_SUCCESS != s32Ret) {
				RK_LOGE("RK_MPI_AVS_EnableChn failed with %#x!\n", s32Ret);
				return s32Ret;
			}
		}
	}

	s32Ret = RK_MPI_AVS_StartGrp(ctx->s32GrpId);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_AVS_StartGrp failed with %#x!\n", s32Ret);
		return s32Ret;
	}

	return RK_SUCCESS;
}

RK_S32 SAMPLE_COMM_AVS_GetChnFrame(SAMPLE_AVS_CTX_S *ctx, void **pdata) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 waitTime = -1;

	s32Ret = RK_MPI_AVS_GetChnFrame(ctx->s32GrpId, ctx->s32ChnId, &ctx->stVideoFrame,
	                                waitTime);
	if (s32Ret == RK_SUCCESS) {
		*pdata = RK_MPI_MB_Handle2VirAddr(ctx->stVideoFrame.stVFrame.pMbBlk);
	} else {
		RK_LOGE("RK_MPI_AVS_GetChnFrame GrpId %d timeout %x", ctx->s32GrpId, s32Ret);
	}

	return RK_SUCCESS;
}

RK_S32 SAMPLE_COMM_AVS_ReleaseChnFrame(SAMPLE_AVS_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_AVS_ReleaseChnFrame(ctx->s32GrpId, ctx->s32ChnId, &ctx->stVideoFrame);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_AVS_ReleaseChnFrame fail %x", s32Ret);
	}
	return RK_SUCCESS;
}

RK_S32 SAMPLE_COMM_AVS_DestroyChn(SAMPLE_AVS_CTX_S *ctx) {
	RK_S32 chnIndex;
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_AVS_StopGrp(ctx->s32GrpId);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_AVS_StopGrp failed with %#x!\n", s32Ret);
		return s32Ret;
	}

	for (chnIndex = 0; chnIndex < AVS_MAX_CHN_NUM; chnIndex++) {
		if (ctx->stAvsChnAttr[chnIndex].u32Width &&
		    ctx->stAvsChnAttr[chnIndex].u32Height) {
			s32Ret = RK_MPI_AVS_DisableChn(ctx->s32GrpId, chnIndex);
			if (RK_SUCCESS != s32Ret) {
				RK_LOGE("RK_MPI_AVS_DisableChn failed with %#x!\n", s32Ret);
				return s32Ret;
			}
		}
	}

	s32Ret = RK_MPI_AVS_DestroyGrp(ctx->s32GrpId);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_AVS_DestroyGrp failed with %#x!\n", s32Ret);
		return s32Ret;
	}
	if (ctx->stAvsGrpAttr.stInAttr.enParamSource == AVS_PARAM_SOURCE_LUT) {
		for (RK_S32 i = 0; i < ctx->stAvsGrpAttr.u32PipeNum; i++) {
			if (ctx->pLUTVirAddr[i]) {
				free(ctx->pLUTVirAddr[i]);
				ctx->pLUTVirAddr[i] = RK_NULL;
			}
		}
	}

	return RK_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
#endif
