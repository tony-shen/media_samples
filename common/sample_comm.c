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
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "sample_comm.h"

#define RUN_FRAME_NUM (100)
void PrintStreamDetails(int chnId, int framesize) {
	static int strmfrmCnt = 0;
	static int sumframesize = 0;
	static struct timeval startTime, endTime, passTime;
	double calcTime;

	sumframesize += framesize;

	if (strmfrmCnt == 0)
		gettimeofday(&startTime, NULL);
	if (strmfrmCnt == RUN_FRAME_NUM) {
		gettimeofday(&endTime, NULL);
		printf("\n================= CH%d STREAMING DETAILS ==================\n", chnId);
		printf("Start Time : %ldsec %06ldusec\n", (long)startTime.tv_sec,
		       (long)startTime.tv_usec);
		printf("End Time   : %ldsec %06ldusec\n", (long)endTime.tv_sec,
		       (long)endTime.tv_usec);
		timersub(&endTime, &startTime, &passTime);
		calcTime = (double)passTime.tv_sec * 1000.0 + (double)passTime.tv_usec / 1000.0;
		printf("Total Time to stream %d frames: %.3f msec TotalBytes/sec: %.3f "
		       "Mbps\n",
		       RUN_FRAME_NUM, calcTime,
		       ((float)sumframesize * 8 * 1000) / calcTime / 1024 / 1024);
		printf("Time per frame: %3.4f msec\n", calcTime / RUN_FRAME_NUM);
		printf("Streaming Performance in FPS: %3.4f\n",
		       RUN_FRAME_NUM / (calcTime / 1000));
		// if(RUN_FRAME_NUM/(calcTime/1000) > 31) {
		//    printf("FPS error!!!\n");
		//}
		printf("===========================================================\n");
		strmfrmCnt = 0;
		sumframesize = 0;
	} else {
		strmfrmCnt++;
	}
}

RK_S32 SAMPLE_COMM_Bind(const MPP_CHN_S *pstSrcChn, const MPP_CHN_S *pstDestChn) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_SYS_Bind(pstSrcChn, pstDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_Bind failed with %#x!\n", s32Ret);
		return s32Ret;
	}

	return RK_SUCCESS;
}

RK_S32 SAMPLE_COMM_UnBind(const MPP_CHN_S *pstSrcChn, const MPP_CHN_S *pstDestChn) {
	RK_S32 s32Ret = RK_FAILURE;

	s32Ret = RK_MPI_SYS_UnBind(pstSrcChn, pstDestChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind failed with %#x!\n", s32Ret);
		return s32Ret;
	}

	return RK_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
