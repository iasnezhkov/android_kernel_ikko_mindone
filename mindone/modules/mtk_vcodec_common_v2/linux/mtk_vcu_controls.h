/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 MediaTek Inc. */

#ifndef __MTK_VCU_CONTROLS_H__
#define __MTK_VCU_CONTROLS_H__

#include <linux/types.h>
#include <linux/string.h>
#include <linux/compat.h>

#define SHARE_BUF_SIZE 64
#define LOG_INFO_SIZE 1024
#define VCODEC_CMDQ_CMD_MAX           (2048)

struct mem_obj {
	uint64_t iova;
	uint32_t len;
	uint64_t pa;
	uint64_t va;
};

struct map_obj {
	uint32_t map_buf;
	uint32_t map_type;
	uint64_t reserved;
};

struct gce_cmds {
	uint8_t  cmd[VCODEC_CMDQ_CMD_MAX];
	uint64_t addr[VCODEC_CMDQ_CMD_MAX];
	uint64_t data[VCODEC_CMDQ_CMD_MAX];
	uint32_t mask[VCODEC_CMDQ_CMD_MAX];
	uint32_t dma_offset[VCODEC_CMDQ_CMD_MAX];
	uint32_t dma_size[VCODEC_CMDQ_CMD_MAX];
	uint32_t cmd_cnt;
};

struct gce_cmdq_obj {
	uint64_t	cmds_user_ptr;
	uint64_t	gce_handle;
	uint32_t	flush_order;
	uint32_t	codec_type;
	uint32_t	core_id;
	uint32_t secure;
};

struct gce_obj {
	uint64_t	gce_handle;
	uint32_t	flush_order;
	uint32_t	codec_type;
};

struct disp_iova_obj {
	uint32_t	mapped_iova;
};

enum gce_cmd_id {
	CMD_READ = 0,
	CMD_WRITE,
	CMD_POLL_REG,

	CMD_WAIT_EVENT,
	CMD_MEM_MV,
	CMD_POLL_ADDR,

	CMD_SEC_WRITE,
	CMD_MAX
};

enum gce_event_id {
	VDEC_EVENT_0,
	VDEC_EVENT_1,
	VDEC_EVENT_2,
	VDEC_EVENT_3,
	VDEC_EVENT_4,

	VDEC_EVENT_5,

	VDEC_EVENT_6,
	VDEC_EVENT_7,

	VDEC_EVENT_8,

	VDEC_EVENT_9,

	VDEC_EVENT_10,

	VDEC_EVENT_11,

	VDEC_EVENT_12,
	VDEC_EVENT_13,
	VDEC_EVENT_14,
	VDEC_EVENT_15,
	VDEC_LAT_EVENT_0,
	VDEC_LAT_EVENT_1,
	VDEC_LAT_EVENT_2,
	VDEC_LAT_EVENT_3,
	VDEC_LAT_EVENT_4,
	VDEC_LAT_EVENT_5,
	VDEC_LAT_EVENT_6,
	VDEC_LAT_EVENT_7,
	VDEC_LAT_EVENT_8,
	VDEC_LAT_EVENT_9,
	VDEC_LAT_EVENT_10,
	VDEC_LAT_EVENT_11,
	VDEC_LAT_EVENT_12,
	VDEC_LAT_EVENT_13,
	VDEC_LAT_EVENT_14,
	VDEC_LAT_EVENT_15,
	VDEC_EVENT_COUNT,
	VENC_EOF = VDEC_EVENT_COUNT,
	VENC_CMDQ_PAUSE_DONE,
	VENC_MB_DONE,
	VENC_128BYTE_CNT_DONE,
	VENC_EOF_C1,
	VENC_WP_2ND_DONE,
	VENC_WP_3ND_DONE,
	VENC_SPS_DONE,
	VENC_PPS_DONE
};

#define VCU_SET_OBJECT	_IOW('v', 0, struct share_obj)
#define VCU_MVA_ALLOCATION	_IOWR('v', 1, struct mem_obj)
#define VCU_MVA_FREE		_IOWR('v', 2, struct mem_obj)
#define VCU_CACHE_FLUSH_ALL	_IOWR('v', 3, struct mem_obj)
#define VCU_CACHE_FLUSH_BUFF	_IOWR('v', 4, struct mem_obj)
#define VCU_CACHE_INVALIDATE_BUFF	_IOWR('v', 5, struct mem_obj)
#define VCU_PA_ALLOCATION	_IOWR('v', 6, struct mem_obj)
#define VCU_PA_FREE		_IOWR('v', 7, struct mem_obj)
#define VCU_GCE_SET_CMD_FLUSH _IOW('v', 8, struct gce_cmdq_obj)
#define VCU_GCE_WAIT_CALLBACK _IOW('v', 9, struct gce_obj)
#define VCU_GET_OBJECT		_IOWR('v', 10, struct share_obj)
#define VCU_GET_LOG_OBJECT	_IOW('v', 11, struct log_test_nofuse)
#define VCU_SET_LOG_OBJECT	_IOW('v', 12, struct log_test_nofuse)
#define VCU_SET_MMAP_TYPE	_IOW('v', 13, struct map_obj)
#define VCU_GET_DISP_MAPPED_IOVA	_IOWR('v', 14, struct disp_iova_obj)
#define VCU_CLEAR_DISP_MAPPED_IOVA	_IOW('v', 15, struct disp_iova_obj)
#define VCU_GET_DISP_WDMA_Y_ADDR	_IOWR('v', 16, struct disp_iova_obj)
#define VCU_UBE_MVA_ALLOCATION	_IOWR('v', 17, struct mem_obj)
#define VCU_UBE_MVA_FREE	_IOWR('v', 18, struct mem_obj)
#define VCU_SECURE_HANDLE_ALLOCATION	_IOWR('v', 19, struct mem_obj)
#define VCU_SECURE_HANDLE_FREE		_IOWR('v', 20, struct mem_obj)
#define VCU_SECURE_BUFFER_ALLOCATION	_IOWR('v', 21, struct mem_obj)
#define VCU_SECURE_BUFFER_FREE		_IOWR('v', 22, struct mem_obj)
#define VCU_GET_SEC_HANDLE	_IOWR('v', 23, struct sec_handle_obj)

#define COMPAT_VCU_SET_OBJECT		_IOW('v', 0, struct share_obj)
#define COMPAT_VCU_MVA_ALLOCATION	_IOWR('v', 1, struct compat_mem_obj)
#define COMPAT_VCU_MVA_FREE		_IOWR('v', 2, struct compat_mem_obj)
#define COMPAT_VCU_CACHE_FLUSH_ALL	_IOWR('v', 3, struct compat_mem_obj)
#define COMPAT_VCU_CACHE_FLUSH_BUFF	_IOWR('v', 4, struct compat_mem_obj)
#define COMPAT_VCU_CACHE_INVALIDATE_BUFF _IOWR('v', 5, struct compat_mem_obj)
#define COMPAT_VCU_PA_ALLOCATION	_IOWR('v', 6, struct compat_mem_obj)
#define COMPAT_VCU_PA_FREE		_IOWR('v', 7, struct compat_mem_obj)
#define COMPAT_VCU_SET_MMAP_TYPE	_IOW('v', 13, struct map_obj)
#define COMPAT_VCU_UBE_MVA_ALLOCATION	_IOWR('v', 17, struct compat_mem_obj)
#define COMPAT_VCU_UBE_MVA_FREE	_IOWR('v', 18, struct compat_mem_obj)
#define COMPAT_VCU_SECURE_HANDLE_ALLOCATION	_IOWR('v', 19, struct mem_obj)
#define COMPAT_VCU_SECURE_HANDLE_FREE		_IOWR('v', 20, struct mem_obj)
#define COMPAT_VCU_SECURE_BUFFER_ALLOCATION	_IOWR('v', 21, struct mem_obj)
#define COMPAT_VCU_SECURE_BUFFER_FREE		_IOWR('v', 22, struct mem_obj)

struct compat_mem_obj {
	uint64_t iova;
	uint32_t len;
	compat_u64 pa;
	compat_u64 va;
};

struct share_obj {
	int32_t id;
	uint32_t len;
	unsigned char share_buf[SHARE_BUF_SIZE];
};

struct log_test_nofuse {
	int type;
	char log_info[LOG_INFO_SIZE];
};

struct sec_handle_obj {
	uint32_t fd;
	uint64_t iova;
	uint32_t len;
	int is_sec;
};
#endif
