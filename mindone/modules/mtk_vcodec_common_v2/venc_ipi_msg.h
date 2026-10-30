/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VENC_IPI_MSG_H_
#define _VENC_IPI_MSG_H_

#include <linux/videodev2.h>
#include "vcodec_ipi_msg.h"

#define MTK_MAX_ENC_CODECS_SUPPORT       (64)
#define VENC_MAX_FB_NUM              VIDEO_MAX_FRAME
#define VENC_MAX_BS_NUM              VIDEO_MAX_FRAME
#define VENC_CONFIG_LENGTH               (2048)
#define MTK_VENC_MULTIPLEX_RECORD_PARAM_SZ 10

enum venc_ipi_msg_id {
	AP_IPIMSG_ENC_INIT = AP_IPIMSG_VENC_SEND_BASE,
	AP_IPIMSG_ENC_SET_PARAM,
	AP_IPIMSG_ENC_ENCODE,
	AP_IPIMSG_ENC_DEINIT,

	AP_IPIMSG_ENC_QUERY_CAP = AP_IPIMSG_VENC_SEND_BASE + IPIMSG_NO_INST_OFFSET,
	AP_IPIMSG_ENC_BACKUP,
	AP_IPIMSG_ENC_PWR_CTRL,
	AP_IPIMSG_ENC_RESUME,
	AP_IPIMSG_ENC_SET_CONFIG,

	VCU_IPIMSG_ENC_INIT_DONE = VCU_IPIMSG_VENC_ACK_BASE,
	VCU_IPIMSG_ENC_SET_PARAM_DONE,
	VCU_IPIMSG_ENC_ENCODE_DONE,
	VCU_IPIMSG_ENC_DEINIT_DONE,
	VCU_IPIMSG_ENC_TRACE,

	VCU_IPIMSG_ENC_QUERY_CAP_DONE = VCU_IPIMSG_VENC_ACK_BASE + IPIMSG_NO_INST_OFFSET,
	VCU_IPIMSG_ENC_BACKUP_DONE,
	VCU_IPIMSG_ENC_PWR_CTRL_DONE,
	VCU_IPIMSG_ENC_RESUME_DONE,
	VCU_IPIMSG_ENC_SET_CONFIG_DONE,

	VCU_IPIMSG_ENC_POWER_ON = VCU_IPIMSG_VENC_SEND_BASE,
	VCU_IPIMSG_ENC_POWER_OFF,
	VCU_IPIMSG_ENC_PUT_BUFFER,
	VCU_IPIMSG_ENC_MEM_ALLOC,
	VCU_IPIMSG_ENC_MEM_FREE,
	VCU_IPIMSG_ENC_WAIT_ISR,
	VCU_IPIMSG_ENC_CHECK_CODEC_ID,
	VCU_IPIMSG_ENC_GET_BS_BUFFER,
	VCU_IPIMSG_ENC_SMI_BUS_DUMP,

	AP_IPIMSG_ENC_POWER_ON_DONE = AP_IPIMSG_VENC_ACK_BASE,
	AP_IPIMSG_ENC_POWER_OFF_DONE,
	AP_IPIMSG_ENC_PUT_BUFFER_DONE,
	AP_IPIMSG_ENC_MEM_ALLOC_DONE,
	AP_IPIMSG_ENC_MEM_FREE_DONE,
	AP_IPIMSG_ENC_WAIT_ISR_DONE,
	AP_IPIMSG_ENC_CHECK_CODEC_ID_DONE,
	AP_IPIMSG_ENC_GET_BS_BUFFER_DONE,
	AP_IPIMSG_ENC_SMI_BUS_DUMP_DONE,

	VCU_ASYNCIPIMSG_ENC_PUT_FRAME_BUFFER = VCU_IPIMSG_VENC_SEND_ASYNC_BASE
};

enum venc_get_param_type {
	GET_PARAM_VENC_CAP_SUPPORTED_FORMATS,
	GET_PARAM_VENC_CAP_FRAME_SIZES,
	GET_PARAM_VENC_CAP_COMMON,

	GET_PARAM_FREE_BUFFERS,
	GET_PARAM_ROI_RC_QP,
	GET_PARAM_RESOLUTION_CHANGE,
	GET_PARAM_VENC_PWR_CTRL,
	GET_PARAM_VENC_HW_TIME,
	GET_PARAM_VENC_VCU_VPUD_LOG
};

enum venc_set_param_type {
	VENC_SET_PARAM_ENC,
	VENC_SET_PARAM_FORCE_INTRA,
	VENC_SET_PARAM_ADJUST_BITRATE,
	VENC_SET_PARAM_ADJUST_FRAMERATE,
	VENC_SET_PARAM_GOP_SIZE,
	VENC_SET_PARAM_INTRA_PERIOD,
	VENC_SET_PARAM_SKIP_FRAME,
	VENC_SET_PARAM_PREPEND_HEADER,
	VENC_SET_PARAM_TS_MODE,
	VENC_SET_PARAM_SCENARIO,
	VENC_SET_PARAM_NONREFP,
	VENC_SET_PARAM_DETECTED_FRAMERATE,
	VENC_SET_PARAM_RFS_ON,
	VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR,
	VENC_SET_PARAM_OPERATION_RATE,
	VENC_SET_PARAM_BITRATE_MODE,
	VENC_SET_PARAM_ROI_ON,
	VENC_SET_PARAM_HEIF_GRID_SIZE,
	VENC_SET_PARAM_COLOR_DESC,
	VENC_SET_PARAM_SEC_MODE,
	VENC_SET_PARAM_TSVC,
	VENC_SET_PARAM_NONREFPFREQ,
	VENC_SET_PARAM_ADJUST_MAX_QP,
	VENC_SET_PARAM_ADJUST_MIN_QP,
	VENC_SET_PARAM_ADJUST_I_P_QP_DELTA,
	VENC_SET_PARAM_ADJUST_FRAME_LEVEL_QP,
	VENC_SET_PARAM_ENABLE_HIGHQUALITY,
	VENC_SET_PARAM_ENABLE_DUMMY_NAL,
	VENC_SET_PARAM_PROPERTY,
	VENC_SET_PARAM_VCP_LOG_INFO,
	VENC_SET_PARAM_ADJUST_QP_CONTROL_MODE,
	VENC_SET_PARAM_TEMPORAL_LAYER_CNT,
	VENC_SET_PARAM_RELEASE_SLB,
	VENC_SET_PARAM_VCU_VPUD_LOG,
	VENC_SET_PARAM_ENABLE_LOW_LATENCY_WFD,
	VENC_SET_PARAM_MMDVFS,
	VENC_SET_PARAM_SLICE_CNT,
	VENC_SET_PARAM_VISUAL_QUALITY,
	VENC_SET_PARAM_INIT_QP,
	VENC_SET_PARAM_FRAME_QP_RANGE,
	VENC_SET_PARAM_ADJUST_CHROMQA_QP,
	VENC_SET_PARAM_MBRC_TKSPD,
	VENC_SET_PARAM_CONFIG,
	VENC_SET_PARAM_ADAB_INFO,
	VENC_SET_PARAM_I_FRM_SZ_CTRL,
	VENC_SET_PARAM_COMPATIBILITY_OPTION,
	VENC_SET_PARAM_TIMING_INFO,
	VENC_SET_PARAM_THERMAL_THROTTLE,
};

#define VENC_MSG_AP_SEND_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	__u64 vcu_inst_addr

#ifndef CONFIG_64BIT
#define VENC_MSG_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	union {	\
		__u64 ap_inst_addr_64;		\
		__u32 ap_inst_addr;	\
	};	\
	__s32 status;	\
	__u32 reserved
#else
#define VENC_MSG_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	__u64 ap_inst_addr;	\
	__s32 status;	\
	__u32 reserved
#endif

struct venc_ap_ipi_msg_init {
	VENC_MSG_PREFIX;
};

struct venc_ap_ipi_query_cap {
	VENC_MSG_PREFIX;
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

struct venc_vcu_ipi_query_cap_ack {
	VENC_MSG_PREFIX;
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

struct venc_ap_ipi_msg_set_param {
	VENC_MSG_AP_SEND_PREFIX;
	__u32 param_id;
	__u32 data_item;
	__u32 data[8];
	__u32 reserved;
};

struct venc_ap_ipi_msg_enc {
	VENC_MSG_AP_SEND_PREFIX;
	__u32 input_size[3];
	__u32 bs_size;
	__u32 data_offset[3];
	__u8 fb_num_planes;
	__u8 bs_mode;
	__u32 sec_mem_handle;
	__u16 reserved1;
	__u32 reserved2;
};

struct venc_ap_ipi_msg_deinit {
	VENC_MSG_AP_SEND_PREFIX;
	__u32 reserved;
};

enum venc_ipi_msg_status {
	VENC_IPI_MSG_STATUS_OK,
	VENC_IPI_MSG_STATUS_FAIL,
};

struct venc_ap_ipi_msg_common {
	VENC_MSG_AP_SEND_PREFIX;
};

struct venc_ap_ipi_msg_indp {
	VENC_MSG_PREFIX;
};

struct venc_vcu_ipi_msg_common {
	VENC_MSG_PREFIX;
	__s32 codec_id;
};

struct venc_vcu_ipi_msg_trace {
	VENC_MSG_PREFIX;
	__u32 trace_id;
	__u32 flag;
};

struct venc_vcu_ipi_msg_init {
	VENC_MSG_PREFIX;
	__u64 vcu_inst_addr;
};

struct venc_vcu_ipi_msg_set_param {
	VENC_MSG_PREFIX;
	__u32 param_id;
	__u32 data_item;
	__u32 data[6];
};

enum venc_ipi_msg_enc_state {
	VEN_IPI_MSG_ENC_STATE_FRAME,
	VEN_IPI_MSG_ENC_STATE_PART,
	VEN_IPI_MSG_ENC_STATE_SKIP,
	VEN_IPI_MSG_ENC_STATE_ERROR,
};

struct venc_vcu_ipi_msg_enc {
	VENC_MSG_PREFIX;
	__u32 state;
	__u32 is_key_frm;
	__u32 bs_size;
};

struct venc_vcu_ipi_msg_deinit {
	VENC_MSG_PREFIX;
};

struct venc_vcu_ipi_msg_waitisr {
	VENC_MSG_PREFIX;
	__u32 irq_status;
	__u32 timeout;
};

struct venc_vcu_ipi_msg_get_bs {
	VENC_MSG_PREFIX;
	__u64 bs_addr;
	__u32 bs_size;
	__s16 bs_fd;
};

struct venc_vcu_ipi_mem_op {
	VENC_MSG_PREFIX;
	struct vcodec_mem_obj mem;
	__u32 vcp_addr[2];
};

struct venc_ap_ipi_pwr_ctrl {
	VENC_MSG_PREFIX;
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

struct venc_vcu_config {
	__u32 input_fourcc;
	__u32 bitrate;
	__u32 pic_w;
	__u32 pic_h;
	__u32 buf_w;
	__u32 buf_h;
	__u32 gop_size;
	__u32 intra_period;
	__u32 framerate;
	__u32 profile;
	__u32 level;
	__u32 wfd;
	__u32 lowlatencywfd;
	__u32 operationrate;
	__u32 scenario;
	__u32 prependheader;
	__u32 bitratemode;
	__u32 roi_rc_qp;
	__u32 roion;
	__u32 heif_grid_size;
	__u32 resolutionChange;
	__u32 max_w;
	__u32 max_h;
	__u32 num_b_frame;
	__u32 slbc_ready;
	__u32 slbc_cpu_used_performance;
	__u32 i_qp;
	__u32 p_qp;
	__u32 b_qp;
	__u32 svp_mode;
	__u32 svp_is_hal_secure_handle;
	__u32 tsvc;
	__u32 max_qp;
	__u32 min_qp;
	__u32 max_qp_1;
	__u32 min_qp_1;
	__u32 max_qp_2;
	__u32 min_qp_2;
	__u32 i_p_qp_delta;
	__u32 qp_control_mode;
	__u32 frame_level_qp;
	__s32 highquality;
	__u32 dummynal;
	__u32 slbc_addr;
	__u32 wpp_mode;
	__u32 low_latency_mode;
	__u32 slice_count;
	__u32 hier_ref_layer;
	__u32 hier_ref_type;
	__u32 temporal_layer_pcount;
	__u32 temporal_layer_bcount;
	__u32 max_ltr_num;
	__u32 slice_header_spacing;
	__u32 sysram_enable;
	__u32 ctx_id;
	__s32 priority;
	__u32 codec_fmt;
	__s32 target_freq;
	__u32 target_bw_factor;
	__u8 cpu_hint;
	__u32 mlvec_mode;
	struct v4l2_mtk_color_desc color_desc;
	struct v4l2_venc_multi_ref multi_ref;
	struct mtk_venc_vui_info vui_info;
	__s32 qpvbr_upper_enable;
	__s32 qpvbr_qp_upper_threshold;
	__s32 qpvbr_qp_max_brratio;
	__s32 qpvbr_lower_enable;
	__s32 qpvbr_qp_lower_threshold;
	__s32 qpvbr_qp_min_brratio;
	__s32 cb_qp_offset;
	__s32 cr_qp_offset;
	__s32 mbrc_tk_spd;
	__s32 ifrm_q_ltr;
	__s32 pfrm_q_ltr;
	__s32 bfrm_q_ltr;
	struct v4l2_venc_visual_quality visual_quality;
	struct v4l2_venc_init_qp init_qp;
	struct v4l2_venc_frame_qp_range frame_qp_range;
	struct v4l2_venc_nal_length nal_length;
	__u8 use_clean_gop;
	struct v4l2_venc_adab_info adab_info;
	struct v4l2_venc_i_frame_size_control i_frm_sz_ctrl;
	__u32 compatibility_option;
	__u32 timing_info;
	__u32 multiplex_record_enable;
	__u32 multiplex_record_param[MTK_VENC_MULTIPLEX_RECORD_PARAM_SZ];
};

enum venc_bs_mode {
	VENC_BS_MODE_SPS = 0,
	VENC_BS_MODE_PPS,
	VENC_BS_MODE_SEQ_HDR,
	VENC_BS_MODE_FRAME,
	VENC_BS_MODE_FRAME_FINAL,
	VENC_BS_MODE_THREAD_EXIT,
	VENC_BS_MODE_MAX
};

struct venc_info {
	__u64 bs_dma;
	__u64 bs_fd;
	__u64 fb_dma[VIDEO_MAX_PLANES];
	__u64 fb_fd[VIDEO_MAX_PLANES];
	__u64 venc_bs_va;
	__u64 venc_fb_va;
	__u32 fb_num_planes;
	__u32 index;
	__u64 timestamp;
	__u64 input_addr[3];
	__u64 bs_addr;
	__u32 qpmap;
	__u32 reserved;
};

struct ring_input_list {
	__u64 venc_bs_va_list[VENC_MAX_FB_NUM];
	__u64 venc_fb_va_list[VENC_MAX_FB_NUM];
	__u32 bs_size[VENC_MAX_FB_NUM];
	__u32 is_key_frm[VENC_MAX_FB_NUM];
	__s32 read_idx;
	__s32 write_idx;
	__s32 count;
	__s32 reserved;
	__s32 is_last_slice[VENC_MAX_FB_NUM];
	__u32 flags[VENC_MAX_FB_NUM];
};

struct venc_vsi {
	struct venc_vcu_config config;
	__u32  sizeimage[VIDEO_MAX_PLANES];
	struct ring_input_list list_free;
	struct venc_info       venc;
	__u32 sync_mode;
	__u8  query_encode_param;
	__u32 meta_size;
	__u64 meta_addr;
	__u32 meta_offset;
	__u8  put_async_handling;
	__u8  is_aosp_qpmap;
	__u32 qpmap_size;
	__u64 qpmap_addr;
	__u32 adab_size;
	__u64 adab_addr;
	__u32 qprects_size;
	__u64 qprects_addr;
	__u32 qprects_meta_size;
	__u64 dynamicparams_addr;
	__u32 dynamicparams_size;
	__u32 dynamicparams_offset;
	__u64 general_buf_dma;
	__s32 general_buf_fd;
	__u32 general_buf_size;
	__u32 hw_proc_cnt[MTK_VENC_HW_NUM];
	__u32 hw_proc_time[MTK_VENC_HW_NUM];
};

struct venc_common_vsi {
	__u8 config_data[VENC_CONFIG_LENGTH];
	struct mtk_tf_info tf_info;
	struct mtk_vio_info vio_info;
};

#endif
