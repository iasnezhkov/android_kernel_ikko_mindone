/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#ifndef __GED_BRIDGE_ID_H__
#define __GED_BRIDGE_ID_H__

#include "ged_type.h"

struct GED_BRIDGE_PACKAGE {
	uint32_t ui32FunctionID;
	int32_t i32Size;
	void *pvParamIn;
	int32_t i32InBufferSize;
	void *pvParamOut;
	int32_t i32OutBufferSize;
};

struct GED_BRIDGE_PACKAGE_32 {
	uint32_t ui32FunctionID;
	int32_t i32Size;
	uint32_t ui32ParamIn;
	int32_t i32InBufferSize;
	uint32_t ui32ParamOut;
	int32_t i32OutBufferSize;
};

#define GPU_TUNER_BUF_NAME_LEN 128

#define GED_MAGIC 'g'

#define GED_IO(INDEX)    _IO(GED_MAGIC, INDEX, struct GED_BRIDGE_PACKAGE)
#define GED_IOW(INDEX)   _IOW(GED_MAGIC, INDEX, struct GED_BRIDGE_PACKAGE)
#define GED_IOR(INDEX)   _IOR(GED_MAGIC, INDEX, struct GED_BRIDGE_PACKAGE)
#define GED_IOWR(INDEX)  _IOWR(GED_MAGIC, INDEX, struct GED_BRIDGE_PACKAGE)
#define GED_IOWR_32(INDEX)  _IOWR(GED_MAGIC, INDEX, struct GED_BRIDGE_PACKAGE_32)
#define GED_GET_BRIDGE_ID(X)  _IOC_NR(X)

#define GED_BRIDGE_COMMAND_LOG_BUF_GET        0
#define GED_BRIDGE_COMMAND_LOG_BUF_WRITE      1
#define GED_BRIDGE_COMMAND_LOG_BUF_RESET      2
#define GED_BRIDGE_COMMAND_BOOST_GPU_FREQ     3
#define GED_BRIDGE_COMMAND_MONITOR_3D_FENCE   4
#define GED_BRIDGE_COMMAND_QUERY_INFO         5
#define GED_BRIDGE_COMMAND_NOTIFY_VSYNC       6
#define GED_BRIDGE_COMMAND_DVFS_PROBE         7
#define GED_BRIDGE_COMMAND_DVFS_UM_RETURN     8
#define GED_BRIDGE_COMMAND_EVENT_NOTIFY       9
#define GED_BRIDGE_COMMAND_WAIT_HW_VSYNC      10
#define GED_BRIDGE_COMMAND_QUERY_TARGET_FPS   11
#define GED_BRIDGE_COMMAND_VSYNC_WAIT         12
#define GED_BRIDGE_COMMAND_GPU_HINT_TO_CPU    13
#define GED_BRIDGE_COMMAND_HINT_FORCE_MDP     14
#define GED_BRIDGE_COMMAND_QUERY_DVFS_FREQ_PRED   15
#define GED_BRIDGE_COMMAND_QUERY_GPU_DVFS_INFO    16
#define GED_BRIDGE_COMMAND_HINT_FRAME_INFO    17

#define GED_BRIDGE_COMMAND_GE_ALLOC           100
#define GED_BRIDGE_COMMAND_GE_GET             101
#define GED_BRIDGE_COMMAND_GE_SET             102
#define GED_BRIDGE_COMMAND_GPU_TIMESTAMP      103
#define GED_BRIDGE_COMMAND_TARGET_FPS         104
#define GED_BRIDGE_COMMAND_GE_INFO            105
#define GED_BRIDGE_COMMAND_GPU_TUNER_STATUS   106
#define GED_BRIDGE_COMMAND_DMABUF_SET_NAME    107

#define GED_BRIDGE_COMMAND_CREATE_TIMELINE    200

#define GED_BRIDGE_COMMAND_ID                 int

#define GED_BRIDGE_IO_LOG_BUF_GET \
	GED_IOWR(GED_BRIDGE_COMMAND_LOG_BUF_GET)
#define GED_BRIDGE_IO_LOG_BUF_WRITE \
	GED_IOWR(GED_BRIDGE_COMMAND_LOG_BUF_WRITE)
#define GED_BRIDGE_IO_LOG_BUF_RESET \
	GED_IOWR(GED_BRIDGE_COMMAND_LOG_BUF_RESET)
#define GED_BRIDGE_IO_BOOST_GPU_FREQ \
	GED_IOWR(GED_BRIDGE_COMMAND_BOOST_GPU_FREQ)
#define GED_BRIDGE_IO_MONITOR_3D_FENCE \
	GED_IOWR(GED_BRIDGE_COMMAND_MONITOR_3D_FENCE)
#define GED_BRIDGE_IO_QUERY_INFO \
	GED_IOWR(GED_BRIDGE_COMMAND_QUERY_INFO)
#define GED_BRIDGE_IO_NOTIFY_VSYNC \
	GED_IOWR(GED_BRIDGE_COMMAND_NOTIFY_VSYNC)
#define GED_BRIDGE_IO_DVFS_PROBE \
	GED_IOWR(GED_BRIDGE_COMMAND_DVFS_PROBE)
#define GED_BRIDGE_IO_DVFS_UM_RETURN \
	GED_IOWR(GED_BRIDGE_COMMAND_DVFS_UM_RETURN)
#define GED_BRIDGE_IO_EVENT_NOTIFY \
	GED_IOWR(GED_BRIDGE_COMMAND_EVENT_NOTIFY)
#define GED_BRIDGE_IO_WAIT_HW_VSYNC \
	GED_IOWR(GED_BRIDGE_COMMAND_WAIT_HW_VSYNC)
#define GED_BRIDGE_IO_VSYNC_WAIT \
	GED_IOWR(GED_BRIDGE_COMMAND_VSYNC_WAIT)
#define GED_BRIDGE_IO_GPU_HINT_TO_CPU \
	GED_IOWR(GED_BRIDGE_COMMAND_GPU_HINT_TO_CPU)
#define GED_BRIDGE_IO_HINT_FORCE_MDP \
	GED_IOWR(GED_BRIDGE_COMMAND_HINT_FORCE_MDP)
#define GED_BRIDGE_IO_QUERY_DVFS_FREQ_PRED \
	GED_IOWR(GED_BRIDGE_COMMAND_QUERY_DVFS_FREQ_PRED)
#define GED_BRIDGE_IO_QUERY_GPU_DVFS_INFO \
	GED_IOWR(GED_BRIDGE_COMMAND_QUERY_GPU_DVFS_INFO)
#define GED_BRIDGE_IO_HINT_FRAME_INFO \
	GED_IOWR(GED_BRIDGE_COMMAND_HINT_FRAME_INFO)

#define GED_BRIDGE_IO_GE_ALLOC \
	GED_IOWR(GED_BRIDGE_COMMAND_GE_ALLOC)
#define GED_BRIDGE_IO_GE_GET \
	GED_IOWR(GED_BRIDGE_COMMAND_GE_GET)
#define GED_BRIDGE_IO_GE_SET \
	GED_IOWR(GED_BRIDGE_COMMAND_GE_SET)
#define GED_BRIDGE_IO_QUERY_TARGET_FPS \
	GED_IOWR(GED_BRIDGE_COMMAND_QUERY_TARGET_FPS)
#define GED_BRIDGE_IO_GPU_TIMESTAMP \
	GED_IOWR(GED_BRIDGE_COMMAND_GPU_TIMESTAMP)
#define GED_BRIDGE_IO_GE_INFO \
	GED_IOWR(GED_BRIDGE_COMMAND_GE_INFO)
#define GED_BRIDGE_IO_GPU_TUNER_STATUS \
	GED_IOWR(GED_BRIDGE_COMMAND_GPU_TUNER_STATUS)
#define GED_BRIDGE_IO_DMABUF_SET_NAME \
	GED_IOWR(GED_BRIDGE_COMMAND_DMABUF_SET_NAME)
#define GED_BRIDGE_IO_CREATE_TIMELINE \
	GED_IOWR(GED_BRIDGE_COMMAND_CREATE_TIMELINE)

#define GED_LOG_BUF_NAME_LENGTH 64

struct GED_BRIDGE_IN_LOGBUFGET {
	char acName[GED_LOG_BUF_NAME_LENGTH];
};

struct GED_BRIDGE_OUT_LOGBUFGET {
	GED_ERROR eError;
	GED_LOG_BUF_HANDLE hLogBuf;
};

struct GED_BRIDGE_IN_LOGBUFWRITE {
	GED_LOG_BUF_HANDLE hLogBuf;
	int attrs;
	char acLogBuf[GED_BRIDGE_IN_LOGBUF_SIZE];
};

struct GED_BRIDGE_OUT_LOGBUFWRITE {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_LOGBUFRESET {
	GED_LOG_BUF_HANDLE hLogBuf;
};

struct GED_BRIDGE_OUT_LOGBUFRESET {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_BOOSTGPUFREQ {
	GED_BOOST_GPU_FREQ_LEVEL eGPUFreqLevel;
};

struct GED_BRIDGE_OUT_BOOSTGPUFREQ {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_MONITOR3DFENCE {
	int fd;
	int pid;
	int timeouts;
	int dump_flag;
	GED_SWD_FENCE_TYPE eType;
};

struct GED_BRIDGE_OUT_MONITOR3DFENCE {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_QUERY_INFO {
	GED_INFO eType;
};

struct GED_BRIDGE_OUT_QUERY_INFO {
	uint64_t   retrieve;
};

struct GED_BRIDGE_IN_NOTIFY_VSYNC {
	GED_VSYNC_TYPE eType;
};

struct GED_BRIDGE_OUT_NOTIFY_VSYNC {
	struct GED_DVFS_UM_QUERY_PACK sQueryData;
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_DVFS_PROBE {
	int          pid;
};

struct GED_BRIDGE_OUT_DVFS_PROBE {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_DVFS_UM_RETURN {
	uint64_t gpu_tar_freq;
	bool bFallback;
};

struct GED_BRIDGE_OUT_DVFS_UM_RETURN {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_EVENT_NOTIFY {
	GED_DVFS_VSYNC_OFFSET_SWITCH_CMD eEvent;
	bool bSwitch;
};

struct GED_BRIDGE_OUT_EVENT_NOTIFY {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_WAIT_HW_VSYNC {
	int tid;
};

struct GED_BRIDGE_OUT_WAIT_HW_VSYNC {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_GPU_TIMESTAMP {
	int pid;
	uint64_t ullWnd;
	int32_t i32FrameID;
	int fence_fd;
	int QedBuffer_length;
	int isSF;
};

struct GED_BRIDGE_OUT_GPU_TIMESTAMP {
	GED_ERROR eError;
	int is_ged_kpi_enabled;
};

struct GED_BRIDGE_IN_QUERY_TARGET_FPS {
	int pid;
	uint64_t cid;
	int fenceFd;
};

struct GED_BRIDGE_OUT_QUERY_TARGET_FPS {
	int fps;
};

struct GED_BRIDGE_IN_GPU_HINT_TO_CPU {
	int32_t i32BridgeFD;
	int32_t tid;
	int32_t hint;
};

struct GED_BRIDGE_OUT_GPU_HINT_TO_CPU {
	GED_ERROR eError;
	int32_t boost_flag;
	int32_t boost_value;
};

struct GED_BRIDGE_IN_HINT_FORCE_MDP {
	int32_t i32BridgeFD;
	int32_t hint;
};

struct GED_BRIDGE_OUT_HINT_FORCE_MDP {
	GED_ERROR eError;
	int32_t mdp_flag;
};

struct GED_BRIDGE_IN_QUERY_DVFS_FREQ_PRED {
	int32_t pid;
	int32_t hint;
};

struct GED_BRIDGE_OUT_QUERY_DVFS_FREQ_PRED {
	GED_ERROR eError;
	int gpu_freq_cur;
	int gpu_freq_max;
	int gpu_freq_dvfs_pred;
};

struct GED_BRIDGE_IN_QUERY_GPU_DVFS_INFO {
	int32_t pid;
	int32_t hint;
	int32_t gift_ratio;
};

struct GED_BRIDGE_OUT_QUERY_GPU_DVFS_INFO {
	GED_ERROR eError;
	int gpu_freq_cur;
	int gpu_freq_max;
	int gpu_freq_dvfs_pred;
	int target_fps;
	int target_fps_margin;
	int eara_fps_margin;
	int gpu_time;
};

struct GED_BRIDGE_IN_HINT_FRAME_INFO {
	uint64_t BBQ_id;
	int32_t target_fps;
	int32_t target_fps_margin;
	int32_t enable;
	uint32_t cmd;
	int32_t pid;
	int32_t tid;
	uint64_t core;
	bool	 by_mask;
	int32_t reserved1;
	int32_t reserved2;
	int32_t reserved3;
	int32_t reserved4;
};

struct GED_BRIDGE_OUT_HINT_FRAME_INFO {
	GED_ERROR eError;
	int32_t mainHead_fps_v;
	int32_t mainHead_fps_gpu;
	uint64_t mainHead_BQ_ID;
};

enum {
	GED_FRAME_INFO_TARGET_FPS,
	GED_FRAME_INFO_MC,
};

struct GED_BRIDGE_IN_GE_ALLOC {
	int region_num;
	uint32_t region_sizes[];
};

struct GED_BRIDGE_OUT_GE_ALLOC {
	int ge_fd;
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_GE_GET {
	int ge_fd;
	int region_id;

	int uint32_offset;
	int uint32_size;
};

struct GED_BRIDGE_OUT_GE_GET {
	GED_ERROR eError;
	uint32_t data[];
};

struct GED_BRIDGE_IN_GE_SET {
	int ge_fd;
	int region_id;

	int uint32_offset;
	int uint32_size;
	uint32_t data[];
};

struct GED_BRIDGE_OUT_GE_SET {
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_GE_INFO {
	int ge_fd;
};

struct GED_BRIDGE_OUT_GE_INFO {
	uint64_t unique_id;
	GED_ERROR eError;
};

struct GED_BRIDGE_IN_GPU_TUNER_STATUS {
	char name[GPU_TUNER_BUF_NAME_LEN];
};

struct GED_BRIDGE_OUT_GPU_TUNER_STATUS {
	int feature;
};

struct GED_BRIDGE_IN_CREATE_TIMELINE {
	char name[GED_LOG_BUF_NAME_LENGTH];
};

struct GED_BRIDGE_OUT_CREATE_TIMELINE {
	int timeline_fd;
};

#define DMABUF_NAME_LEN 48

struct GED_BRIDGE_IN_DMABUF_SET_NAME {
	int32_t share_fd;
	char name[DMABUF_NAME_LEN];
};

struct GED_BRIDGE_OUT_DMABUF_SET_NAME {
	GED_ERROR eError;
};

#endif
