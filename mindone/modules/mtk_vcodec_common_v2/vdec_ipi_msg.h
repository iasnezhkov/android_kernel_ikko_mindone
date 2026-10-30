/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VDEC_IPI_MSG_H_
#define _VDEC_IPI_MSG_H_
#include "vcodec_ipi_msg.h"

#define MTK_MAX_DEC_CODECS_SUPPORT       (128)
#define DEC_MAX_FB_NUM              VIDEO_MAX_FRAME
#define DEC_MAX_BS_NUM              VIDEO_MAX_FRAME

enum vdec_src_chg_type {
	VDEC_NO_CHANGE              = (0 << 0),
	VDEC_RES_CHANGE             = (1 << 0),
	VDEC_REALLOC_MV_BUF         = (1 << 1),
	VDEC_HW_NOT_SUPPORT         = (1 << 2),
	VDEC_NEED_SEQ_HEADER        = (1 << 3),
	VDEC_NEED_MORE_OUTPUT_BUF   = (1 << 4),
	VDEC_CROP_CHANGED           = (1 << 5),
	VDEC_OUTPUT_NOT_GENERATED   = (1 << 6),
	VDEC_COLOR_ASPECT_CHANGED   = (1 << 7),
};

enum vdec_fb_flag_type {
	VDEC_FB_NO_FLAGS            = (0 << 0),
	VDEC_FB_EOS                 = (1 << 0),
	VDEC_FB_NO_GENERATED        = (1 << 1),
	VDEC_FB_CROP_CHANGED        = (1 << 2),
};

enum vdec_ipi_msg_status {
	VDEC_IPI_MSG_STATUS_OK      = 0,
	VDEC_IPI_MSG_STATUS_FAIL    = -1,
	VDEC_IPI_MSG_STATUS_MAX_INST    = -2,
	VDEC_IPI_MSG_STATUS_ILSEQ   = -3,
	VDEC_IPI_MSG_STATUS_INVALID_ID  = -4,
	VDEC_IPI_MSG_STATUS_DMA_FAIL    = -5,
};

enum vdec_ipi_msg_id {
	AP_IPIMSG_DEC_INIT = AP_IPIMSG_VDEC_SEND_BASE,
	AP_IPIMSG_DEC_START,
	AP_IPIMSG_DEC_DEINIT,
	AP_IPIMSG_DEC_RESET,
	AP_IPIMSG_DEC_SET_PARAM,
	AP_IPIMSG_DEC_FRAME_BUFFER,

	AP_IPIMSG_DEC_QUERY_CAP = AP_IPIMSG_VDEC_SEND_BASE + IPIMSG_NO_INST_OFFSET,
	AP_IPIMSG_DEC_BACKUP,
	AP_IPIMSG_DEC_RESUME,
	AP_IPIMSG_DEC_PWR_CTRL,

	VCU_IPIMSG_DEC_INIT_DONE = VCU_IPIMSG_VDEC_ACK_BASE,
	VCU_IPIMSG_DEC_START_DONE,
	VCU_IPIMSG_DEC_DEINIT_DONE,
	VCU_IPIMSG_DEC_RESET_DONE,
	VCU_IPIMSG_DEC_SET_PARAM_DONE,
	VCU_IPIMSG_DEC_DONE,

	VCU_IPIMSG_DEC_QUERY_CAP_DONE = VCU_IPIMSG_VDEC_ACK_BASE + IPIMSG_NO_INST_OFFSET,
	VCU_IPIMSG_DEC_BACKUP_DONE,
	VCU_IPIMSG_DEC_RESUME_DONE,
	VCU_IPIMSG_DEC_PWR_CTRL_DONE,

	VCU_IPIMSG_DEC_PUT_FRAME_BUFFER = VCU_IPIMSG_VDEC_SEND_BASE,
	VCU_IPIMSG_DEC_LOCK_CORE,
	VCU_IPIMSG_DEC_UNLOCK_CORE,
	VCU_IPIMSG_DEC_LOCK_LAT,
	VCU_IPIMSG_DEC_UNLOCK_LAT,
	VCU_IPIMSG_DEC_MEM_ALLOC,
	VCU_IPIMSG_DEC_MEM_FREE,
	VCU_IPIMSG_DEC_WAITISR,
	VCU_IPIMSG_DEC_CHECK_CODEC_ID,
	VCU_IPIMSG_DEC_GET_KERNEL_PARAM,
	VCU_IPIMSG_DEC_SMI_DBG_DUMP,

	VCU_IPIMSG_DEC_GET_FRAME_BUFFER,
	VCU_IPIMSG_DEC_SLICE_DONE_ISR,

	AP_IPIMSG_DEC_PUT_FRAME_BUFFER_DONE = AP_IPIMSG_VDEC_ACK_BASE,
	AP_IPIMSG_DEC_LOCK_CORE_DONE,
	AP_IPIMSG_DEC_UNLOCK_CORE_DONE,
	AP_IPIMSG_DEC_LOCK_LAT_DONE,
	AP_IPIMSG_DEC_UNLOCK_LAT_DONE,
	AP_IPIMSG_DEC_MEM_ALLOC_DONE,
	AP_IPIMSG_DEC_MEM_FREE_DONE,
	AP_IPIMSG_DEC_WAITISR_DONE,
	AP_IPIMSG_DEC_CHECK_CODEC_ID_DONE,
	AP_IPIMSG_DEC_GET_KERNEL_PARAM_DONE,
	AP_IPIMSG_DEC_SMI_DBG_DUMP_DONE,

	VCU_ASYNCIPIMSG_DEC_PUT_FRAME_BUFFER = VCU_IPIMSG_VDEC_SEND_ASYNC_BASE,
};

enum vdec_flush_type {
	FLUSH_BITSTREAM = (1 << 0),
	FLUSH_FRAME     = (1 << 1),
};

enum vdec_reset_type {
	VDEC_FLUSH = 0,
	VDEC_DRAIN = 1,
	VDEC_DRAIN_EOS = 2,
};

enum vdec_get_param_type {
	GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS,
	GET_PARAM_VDEC_CAP_FRAME_SIZES,
	GET_PARAM_VDEC_CAP_FRAMEINTERVALS,
	GET_PARAM_VDEC_CAP_MAX_BUF_INFO,

	GET_PARAM_DISP_FRAME_BUFFER,
	GET_PARAM_FREE_FRAME_BUFFER,
	GET_PARAM_FREE_BITSTREAM_BUFFER,
	GET_PARAM_PIC_INFO,
	GET_PARAM_CROP_INFO,
	GET_PARAM_DPB_SIZE,
	GET_PARAM_FRAME_INTERVAL,
	GET_PARAM_COLOR_DESC,
	GET_PARAM_ASPECT_RATIO,
	GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS,
	GET_PARAM_INTERLACING,
	GET_PARAM_INPUT_DRIVEN,
	GET_PARAM_OUTPUT_ASYNC,
	GET_PARAM_LOW_POWER_MODE,
	GET_PARAM_VDEC_HW_TIME,
	GET_PARAM_INTERLACING_FIELD_SEQ,
	GET_PARAM_RES_INFO,
	GET_PARAM_BANDWIDTH_INFO,
	GET_PARAM_TRICK_MODE,
	GET_PARAM_VDEC_VCU_VPUD_LOG,

	GET_PARAM_MAX = 0xFFFFFFFF
};

enum vdec_set_param_type {
	SET_PARAM_DECODE_MODE,
	SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
	SET_PARAM_COMPRESSED_MODE,
	SET_PARAM_CRC_PATH,
	SET_PARAM_GOLDEN_PATH,
	SET_PARAM_FB_NUM_PLANES,
	SET_PARAM_WAIT_KEY_FRAME,
	SET_PARAM_OPERATING_RATE,
	SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT,
	SET_PARAM_TOTAL_FRAME_BUFQ_COUNT,
	SET_PARAM_FRAME_BUFFER,
	SET_PARAM_VDEC_PROPERTY,
	SET_PARAM_VDEC_VCP_LOG_INFO,
	SET_PARAM_SET_DV,
	SET_PARAM_PUT_FB,
	SET_PARAM_CROP_INFO,
	SET_PARAM_HDR10_INFO,
	SET_PARAM_TRICK_MODE,
	SET_PARAM_NO_REORDER,
	SET_PARAM_DECODE_ERROR_HANDLE_MODE,
	SET_PARAM_DEC_PARAMS,
	SET_PARAM_MMDVFS,
	SET_PARAM_PER_FRAME_SUBSAMPLE_MODE,
	SET_PARAM_ACQUIRE_RESOURCE,
	SET_PARAM_VPEEK_MODE,
	SET_PARAM_VDEC_PLUS_DROP_RATIO,
	SET_PARAM_CONTAINER_FRAMERATE,
	SET_PARAM_DISABLE_DEBLOCK,
	SET_PARAM_LOW_LATENCY,
	SET_PARAM_VDEC_LINECOUNT_THRESHOLD,

	SET_PARAM_VDEC_PWR_CTRL,
	SET_PARAM_VDEC_VCU_VPUD_LOG,
	SET_PARAM_VDEC_IN_GROUP,
	SET_PARAM_VDEC_DV_MODE,
	SET_PARAM_MAX = 0xFFFFFFFF
};

enum vdec_get_kernel_param_type {
	GET_KPARAM_VP_MODE_BUF,
	GET_KPARAM_MAX = 0xFFFFFFFF
};

#define VDEC_MSG_AP_SEND_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	__u64 vcu_inst_addr

#ifndef CONFIG_64BIT
#define VDEC_MSG_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	union {	\
		__u64 ap_inst_addr_64;		\
		__u32 ap_inst_addr;	\
	}; \
	__s32 status;	\
	__u32 reserved;
#else
#define VDEC_MSG_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	__u64 ap_inst_addr;	\
	__s32 status;	\
	__u32 reserved
#endif

struct vdec_ap_ipi_cmd {
	VDEC_MSG_AP_SEND_PREFIX;
	__u32 drain_type;
	__u32 reserved;
};

struct vdec_ap_ipi_cmd_indp {
	VDEC_MSG_PREFIX;
};

struct vdec_vcu_ipi_ack {
	VDEC_MSG_PREFIX;
	__s32 id;
	__u32 data;
	__u64 payload;
};

struct vdec_vcu_ipi_mem_op {
	VDEC_MSG_PREFIX;
	struct vcodec_mem_obj mem;
	__u32 vcp_addr[2];
};

struct vdec_ap_ipi_pwr_ctrl {
	VDEC_MSG_PREFIX;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_data_addr;
#endif
	struct mtk_smi_pwr_ctrl_info info;
};

struct vdec_ap_ipi_init {
	VDEC_MSG_PREFIX;
};

struct vdec_vcu_ipi_init_ack {
	VDEC_MSG_PREFIX;
	__u64 vcu_inst_addr;
};

struct vdec_ap_ipi_dec_start {
	VDEC_MSG_AP_SEND_PREFIX;
	__u32 data[6];
};

struct vdec_ap_ipi_set_param {
	VDEC_MSG_AP_SEND_PREFIX;
	__u32 id;
	__u32 reserved;
	__u32 data[10];
};

struct vdec_ap_ipi_query_cap {
	VDEC_MSG_PREFIX;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_data_addr;
#endif
	__u32 id;
};

struct vdec_vcu_ipi_query_cap_ack {
	VDEC_MSG_PREFIX;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_data_addr;
#endif
	__u64 vcu_data_addr;
	__u32 id;
};

struct vdec_ipi_fb {
	__u64 vdec_fb_va;
	__u64 y_fb_dma;
	__u64 c_fb_dma;
	__u64 dma_general_addr;
	__s32 general_size;
	__u32 reserved;
};

struct vdec_fb_entry {
	__u64 vdec_fb_va;
	__u64 timestamp;
	__u16 field;
	__u16 frame_type;
	__u32 flags;
};

struct ring_bs_list {
	__u64 vdec_bs_va_list[DEC_MAX_BS_NUM];
	__u32 read_idx;
	__u32 write_idx;
	__u32 count;
	__u32 reserved;
};

struct ring_fb_list {
	struct vdec_fb_entry fb_list[DEC_MAX_FB_NUM];
	__u32 read_idx;
	__u32 write_idx;
	__u32 count;
	__u32 reserved;
};

struct vdec_vp_mode_buf_info {
	__u8 enable_smmu;
	__u8 alloc_src_buf[2];
	__u8 reserved;
	__u64 src_buf[2][3];
};

struct vdec_vsi {
	struct ring_bs_list list_free_bs;
	struct ring_fb_list list_free;
	struct ring_fb_list list_disp;
	struct mtk_dec_params dec_params;
	struct vdec_dec_info dec;
	struct vdec_pic_info pic;
	struct v4l2_mtk_color_desc color_desc;
	struct v4l2_rect crop;
	struct v4l2_fract time_per_frame;
	__u32 aspect_ratio;
	__u32 fix_buffers;
	__u32 interlacing;
	__u32 codec_type;
	__u8 input_driven;
	__u8 output_async;
	__u8 output_async_handling;
	__u8 low_pw_mode;
	__u8 dynamic_low_latency;
	__u8 in_group;
	__u8 cpu_hint;
	__u8 thermal_hint;
	__s8 crc_path[256];
	__s8 golden_path[256];
	__u64 general_buf_dma;
	__s32 general_buf_fd;
	__u32 general_buf_size;
	__u64 meta_buf_dma;
	__s32 meta_buf_fd;
	__u32 meta_buf_size;
	__u32 ipi_blocked;
	__u32 interlacing_fieldseq;
	struct hdr10plus_info hdr10plus_buf;
	struct v4l2_vdec_hdr10_info hdr10_info;
	__u8 hdr10_info_valid;
	__u8 trick_mode;
	__u8 flush_type;
	__u32 ctx_id;
	__u32 hw_proc_cnt[MTK_VDEC_HW_NUM];
	__u32 hw_proc_time[MTK_VDEC_HW_NUM];

	__s32 op_rate;
	__s32 op_rate_adaptive;
	__s32 priority;
	__u32 codec_fmt;
	__s32 target_freq;
	__u32 is_active;
	struct vdec_resource_info res_info;
	struct v4l2_vdec_bandwidth_info bandwidth_info;
};

struct vdec_common_vsi {
	struct mtk_tf_info tf_info;
	struct vdec_vp_mode_buf_info vp_mode_info;
};

#endif
