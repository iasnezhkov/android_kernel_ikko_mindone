/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Copyright (c) 2022 MediaTek Inc. */

#ifndef __UAPI_MTK_V4L2_CONTROLS_H__
#define __UAPI_MTK_V4L2_CONTROLS_H__

#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>

#define V4L2_PIX_FMT_ARGB1010102  v4l2_fourcc('A', 'B', '3', '0')
#define V4L2_PIX_FMT_ABGR1010102  v4l2_fourcc('A', 'R', '3', '0')
#define V4L2_PIX_FMT_RGBA1010102  v4l2_fourcc('R', 'A', '3', '0')
#define V4L2_PIX_FMT_BGRA1010102  v4l2_fourcc('B', 'A', '3', '0')

#define V4L2_PIX_FMT_P010M   v4l2_fourcc('P', '0', '1', '0')

#define V4L2_PIX_FMT_H264_SLICE v4l2_fourcc('S', '2', '6', '4')
#define V4L2_PIX_FMT_HEIF     v4l2_fourcc('H', 'E', 'I', 'F')

#define V4L2_PIX_FMT_VP8_FRAME v4l2_fourcc('V', 'P', '8', 'F')
#define V4L2_PIX_FMT_WMV1     v4l2_fourcc('W', 'M', 'V', '1')
#define V4L2_PIX_FMT_WMV2     v4l2_fourcc('W', 'M', 'V', '2')
#define V4L2_PIX_FMT_WMV3     v4l2_fourcc('W', 'M', 'V', '3')
#define V4L2_PIX_FMT_WMVA     v4l2_fourcc('W', 'M', 'V', 'A')
#define V4L2_PIX_FMT_WVC1     v4l2_fourcc('W', 'V', 'C', '1')
#define V4L2_PIX_FMT_RV30     v4l2_fourcc('R', 'V', '3', '0')
#define V4L2_PIX_FMT_RV40     v4l2_fourcc('R', 'V', '4', '0')
#define V4L2_PIX_FMT_AV1      v4l2_fourcc('A', 'V', '1', '0')
#define V4L2_PIX_FMT_H266   v4l2_fourcc('H', 'V', 'V', 'C')
#define V4L2_PIX_FMT_AVS3   v4l2_fourcc('A', 'V', 'S', '3')

#define V4L2_PIX_FMT_MT21         v4l2_fourcc('M', 'M', '2', '1')

#define V4L2_PIX_FMT_MT2110T      v4l2_fourcc('M', 'T', '2', 'T')

#define V4L2_PIX_FMT_MT2110R      v4l2_fourcc('M', 'T', '2', 'R')

#define V4L2_PIX_FMT_MT21C10T     v4l2_fourcc('M', 'T', 'C', 'T')

#define V4L2_PIX_FMT_MT21C10R     v4l2_fourcc('M', 'T', 'C', 'R')

#define V4L2_PIX_FMT_MT21CS       v4l2_fourcc('M', '2', 'C', 'S')

#define V4L2_PIX_FMT_MT21S        v4l2_fourcc('M', '2', '1', 'S')

#define V4L2_PIX_FMT_MT21S10T     v4l2_fourcc('M', 'T', 'S', 'T')

#define V4L2_PIX_FMT_MT21S10R     v4l2_fourcc('M', 'T', 'S', 'R')

#define V4L2_PIX_FMT_MT21CS10T    v4l2_fourcc('M', 'C', 'S', 'T')

#define V4L2_PIX_FMT_MT21CS10R    v4l2_fourcc('M', 'C', 'S', 'R')

#define V4L2_PIX_FMT_MT21CSA      v4l2_fourcc('M', 'A', 'C', 'S')

#define V4L2_PIX_FMT_MT21S10TJ    v4l2_fourcc('M', 'J', 'S', 'T')

#define V4L2_PIX_FMT_MT21S10RJ    v4l2_fourcc('M', 'J', 'S', 'R')

#define V4L2_PIX_FMT_MT21CS10TJ   v4l2_fourcc('J', 'C', 'S', 'T')

#define V4L2_PIX_FMT_MT21CS10RJ   v4l2_fourcc('J', 'C', 'S', 'R')

#define V4L2_PIX_FMT_MT10S     v4l2_fourcc('M', '1', '0', 'S')

#define V4L2_PIX_FMT_MT10     v4l2_fourcc('M', 'T', '1', '0')

#define V4L2_PIX_FMT_P010S   v4l2_fourcc('P', '0', '1', 'S')

#define V4L2_PIX_FMT_RGB32_AFBC         v4l2_fourcc('M', 'C', 'R', '8')
#define V4L2_PIX_FMT_BGR32_AFBC         v4l2_fourcc('M', 'C', 'B', '8')

#define V4L2_PIX_FMT_RGBA1010102_AFBC   v4l2_fourcc('M', 'C', 'R', 'X')
#define V4L2_PIX_FMT_BGRA1010102_AFBC   v4l2_fourcc('M', 'C', 'B', 'X')

#define V4L2_PIX_FMT_NV12_AFBC          v4l2_fourcc('M', 'C', 'N', '8')

#define V4L2_PIX_FMT_NV21_AFBC          v4l2_fourcc('M', 'N', '2', '8')

#define V4L2_PIX_FMT_NV12_10B_AFBC      v4l2_fourcc('M', 'C', 'N', 'X')

#define V4L2_PIX_FMT_MTISP_B8	v4l2_fourcc('M', 'T', 'B', '8')

#define V4L2_PIX_FMT_NV12_HYFBC v4l2_fourcc('N', '1', '2', 'H')
#define V4L2_PIX_FMT_P010_HYFBC v4l2_fourcc('P', '0', '1', 'H')

#define V4L2_PIX_FMT_DVAV    v4l2_fourcc('D', 'V', 'A', 'V')
#define V4L2_PIX_FMT_DVHE    v4l2_fourcc('D', 'V', 'H', 'E')

#define V4L2_BUF_FLAG_REF_FREED			0x00000200

#define V4L2_BUF_FLAG_CROP_CHANGED		0x00008000
#define V4L2_BUF_FLAG_CSD			0x00200000
#define V4L2_BUF_FLAG_ROI			0x00400000
#define V4L2_BUF_FLAG_HDR_META			0x00800000
#define V4L2_BUF_FLAG_QP_META			0x01000000
#define V4L2_BUF_FLAG_HAS_META			0x04000000
#define V4L2_BUF_FLAG_COLOR_ASPECT_CHANGED	0x08000000
#define V4L2_BUF_FLAG_MULTINAL			0x20000000
#define V4L2_BUF_FLAG_NAL_LENGTH_BS		0x40000000
#define V4L2_BUF_FLAG_NEED_MORE_BS		0x80000000

#define V4L2_EVENT_MTK_VCODEC_START		(V4L2_EVENT_PRIVATE_START + 0x00002000)
#define V4L2_EVENT_MTK_VDEC_ERROR		(V4L2_EVENT_MTK_VCODEC_START + 1)
#define V4L2_EVENT_MTK_VDEC_NOHEADER		(V4L2_EVENT_MTK_VCODEC_START + 2)
#define V4L2_EVENT_MTK_VENC_ERROR		(V4L2_EVENT_MTK_VCODEC_START + 3)
#define V4L2_EVENT_MTK_VDEC_ERROR_INFO		(V4L2_EVENT_MTK_VCODEC_START + 4)
#define V4L2_EVENT_MTK_VCODEC_VIDEO_GO_INFO	(V4L2_EVENT_MTK_VCODEC_START + 5)

#define V4L2_CID_CODEC_MTK_BASE \
	(V4L2_CTRL_CLASS_CODEC | 0x2000)
#define V4L2_CID_CODEC_MTK_DEC_BASE \
	(V4L2_CTRL_CLASS_CODEC | 0x2100)
#define V4L2_CID_CODEC_MTK_ENC_BASE \
	(V4L2_CTRL_CLASS_CODEC | 0x2200)

#define V4L2_CID_MTK_VIDEO_COLOR_DESC \
	(V4L2_CID_CODEC_MTK_BASE+0)
#define V4L2_CID_MTK_VIDEO_SEC_MODE \
	(V4L2_CID_CODEC_MTK_BASE+1)
#define V4L2_CID_MTK_VIDEO_OPERATING_RATE \
	(V4L2_CID_CODEC_MTK_BASE+2)
#define V4L2_CID_MTK_VIDEO_LOG \
	(V4L2_CID_CODEC_MTK_BASE+3)
#define V4L2_CID_MTK_VIDEO_VCP_PROP \
	(V4L2_CID_CODEC_MTK_BASE+4)
#define V4L2_CID_MTK_VIDEO_GET_LOG \
	(V4L2_CID_CODEC_MTK_BASE+5)
#define V4L2_CID_MTK_VIDEO_GET_VCP_PROP \
	(V4L2_CID_CODEC_MTK_BASE+6)
#define V4L2_CID_MTK_VIDEO_CALLING_PID \
	(V4L2_CID_CODEC_MTK_BASE+7)
#define V4L2_CID_MTK_VIDEO_CONTEXT_ID \
	(V4L2_CID_CODEC_MTK_BASE+8)
#define V4L2_CID_MTK_VIDEO_DV_MODE \
	(V4L2_CID_CODEC_MTK_BASE+9)

#define V4L2_CID_MTK_VIDEO_DEC_DECODE_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+0)
#define V4L2_CID_MTK_VIDEO_DEC_FIXED_MAX_FRAME_BUFFER \
	(V4L2_CID_CODEC_MTK_DEC_BASE+1)
#define V4L2_CID_MTK_VIDEO_DEC_CRC_PATH \
	(V4L2_CID_CODEC_MTK_DEC_BASE+2)
#define V4L2_CID_MTK_VIDEO_DEC_GOLDEN_PATH \
	(V4L2_CID_CODEC_MTK_DEC_BASE+3)
#define V4L2_CID_MTK_VIDEO_DEC_SET_WAIT_KEY_FRAME \
	(V4L2_CID_CODEC_MTK_DEC_BASE+4)
#define V4L2_CID_MTK_VIDEO_DEC_FIX_BUFFERS \
	(V4L2_CID_CODEC_MTK_DEC_BASE+5)
#define V4L2_CID_MTK_VIDEO_DEC_INTERLACING \
	(V4L2_CID_CODEC_MTK_DEC_BASE+6)
#define V4L2_CID_MTK_VIDEO_DEC_QUEUED_FRAMEBUF_COUNT \
	(V4L2_CID_CODEC_MTK_DEC_BASE+7)
#define V4L2_CID_MTK_VIDEO_DEC_COMPRESSED_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+8)
#define V4L2_CID_MTK_VIDEO_DEC_REAL_TIME_PRIORITY \
	(V4L2_CID_CODEC_MTK_DEC_BASE+9)
#define V4L2_CID_MTK_VIDEO_DEC_DETECT_TIMESTAMP \
	(V4L2_CID_CODEC_MTK_DEC_BASE+10)
#define V4L2_CID_MTK_VIDEO_DEC_INTERLACING_FIELD_SEQ \
	(V4L2_CID_CODEC_MTK_DEC_BASE+11)
#define V4L2_CID_MTK_VIDEO_DEC_HDR10_INFO \
	(V4L2_CID_CODEC_MTK_DEC_BASE+12)
#define V4L2_CID_MTK_VIDEO_DEC_HDR10PLUS_DATA \
	(V4L2_CID_CODEC_MTK_DEC_BASE+13)

#define V4L2_CID_MTK_VIDEO_DEC_TRICK_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+14)
#define V4L2_CID_MTK_VIDEO_DEC_NO_REORDER \
	(V4L2_CID_CODEC_MTK_DEC_BASE+15)
#define V4L2_CID_MTK_VIDEO_DEC_SET_DECODE_ERROR_HANDLE_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+16)
#define V4L2_CID_MTK_VIDEO_DEC_SLC_SUPPORT_VER \
	(V4L2_CID_CODEC_MTK_DEC_BASE+17)
#define V4L2_CID_MTK_VIDEO_DEC_SUBSAMPLE_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+18)
#define V4L2_CID_MTK_VIDEO_DEC_ACQUIRE_RESOURCE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+19)
#define V4L2_CID_MTK_VIDEO_DEC_RESOURCE_METRICS \
	(V4L2_CID_CODEC_MTK_DEC_BASE+20)
#define V4L2_CID_MTK_VIDEO_DEC_VPEEK_MODE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+21)
#define V4L2_CID_MTK_VIDEO_DEC_PLUS_DROP_RATIO \
	(V4L2_CID_CODEC_MTK_DEC_BASE+22)
#define V4L2_CID_MTK_VIDEO_DEC_CONTAINER_FRAMERATE \
	(V4L2_CID_CODEC_MTK_DEC_BASE+23)
#define V4L2_CID_MTK_VIDEO_DEC_DISABLE_DEBLOCK \
	(V4L2_CID_CODEC_MTK_DEC_BASE+24)
#define V4L2_CID_MTK_VIDEO_DEC_LOW_LATENCY \
	(V4L2_CID_CODEC_MTK_DEC_BASE+25)
#define V4L2_CID_MTK_VIDEO_DEC_MAX_BUF_INFO \
	(V4L2_CID_CODEC_MTK_DEC_BASE+26)
#define V4L2_CID_MTK_VIDEO_DEC_BANDWIDTH_INFO \
	(V4L2_CID_CODEC_MTK_DEC_BASE+27)
#define V4L2_CID_MTK_VIDEO_DEC_LINECOUNT_THRESHOLD \
	(V4L2_CID_CODEC_MTK_DEC_BASE+28)
#define V4L2_CID_MTK_VIDEO_DEC_INPUT_SLOT \
	(V4L2_CID_CODEC_MTK_DEC_BASE+29)
#define V4L2_CID_MTK_VIDEO_DEC_OUTPUT_SLOT_MAP \
	(V4L2_CID_CODEC_MTK_DEC_BASE+30)

#define V4L2_CID_MTK_VIDEO_ENC_SET_NAL_SIZE_LENGTH \
	(V4L2_CID_CODEC_MTK_ENC_BASE+0)
#define V4L2_CID_MTK_VIDEO_ENC_SCENARIO \
	(V4L2_CID_CODEC_MTK_ENC_BASE+1)
#define V4L2_CID_MTK_VIDEO_ENC_NONREFP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+2)
#define V4L2_CID_MTK_VIDEO_ENC_NONREFP_FREQ \
	(V4L2_CID_CODEC_MTK_ENC_BASE+3)
#define V4L2_CID_MTK_VIDEO_ENC_DETECTED_FRAMERATE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+4)
#define V4L2_CID_MTK_VIDEO_ENC_RFS_ON \
	(V4L2_CID_CODEC_MTK_ENC_BASE+5)
#define V4L2_CID_MTK_VIDEO_ENC_ROI_RC_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+6)
#define V4L2_CID_MTK_VIDEO_ENC_ROI_ON \
	(V4L2_CID_CODEC_MTK_ENC_BASE+7)
#define V4L2_CID_MTK_VIDEO_ENC_GRID_SIZE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+8)
#define V4L2_CID_MTK_VIDEO_ENC_RESOLUTION_CHANGE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+9)
#define V4L2_CID_MTK_VIDEO_ENC_MAX_WIDTH \
	(V4L2_CID_CODEC_MTK_ENC_BASE+10)
#define V4L2_CID_MTK_VIDEO_ENC_MAX_HEIGHT \
	(V4L2_CID_CODEC_MTK_ENC_BASE+11)
#define V4L2_CID_MTK_VIDEO_ENC_RC_I_FRAME_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+12)
#define V4L2_CID_MTK_VIDEO_ENC_RC_P_FRAME_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+13)
#define V4L2_CID_MTK_VIDEO_ENC_RC_B_FRAME_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+14)
#define V4L2_CID_MTK_VIDEO_ENC_RC_MAX_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+15)
#define V4L2_CID_MTK_VIDEO_ENC_RC_MIN_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+16)
#define V4L2_CID_MTK_VIDEO_ENC_RC_I_P_QP_DELTA \
	(V4L2_CID_CODEC_MTK_ENC_BASE+17)
#define V4L2_CID_MTK_VIDEO_ENC_RC_QP_CONTROL_MODE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+18)
#define V4L2_CID_MTK_VIDEO_ENC_RC_FRAME_LEVEL_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+19)
#define V4L2_CID_MTK_VIDEO_ENC_ENABLE_TSVC \
	(V4L2_CID_CODEC_MTK_ENC_BASE+20)
#define V4L2_CID_MTK_VIDEO_ENC_ENABLE_HIGHQUALITY \
	(V4L2_CID_CODEC_MTK_ENC_BASE+21)
#define V4L2_CID_MTK_VIDEO_ENC_ENABLE_DUMMY_NAL \
	(V4L2_CID_CODEC_MTK_ENC_BASE+22)
#define V4L2_CID_MTK_VIDEO_ENC_ENABLE_MLVEC_MODE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+23)
#define V4L2_CID_MTK_VIDEO_ENC_MULTI_REF \
	(V4L2_CID_CODEC_MTK_ENC_BASE+24)
#define V4L2_CID_MTK_VIDEO_ENC_WPP_MODE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+25)
#define V4L2_CID_MTK_VIDEO_ENC_LOW_LATENCY_MODE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+26)
#define V4L2_CID_MTK_VIDEO_ENC_TEMPORAL_LAYER_COUNT \
	(V4L2_CID_CODEC_MTK_ENC_BASE+27)
#define V4L2_CID_MTK_VIDEO_ENC_MAX_LTR_FRAMES \
	(V4L2_CID_CODEC_MTK_ENC_BASE+28)
#define V4L2_CID_MTK_VIDEO_ENC_LOW_LATENCY_WFD \
	(V4L2_CID_CODEC_MTK_ENC_BASE+29)
#define V4L2_CID_MTK_VIDEO_ENC_SLICE_CNT \
	(V4L2_CID_CODEC_MTK_ENC_BASE+30)
#define V4L2_CID_MTK_VIDEO_ENC_QPVBR \
	(V4L2_CID_CODEC_MTK_ENC_BASE+31)
#define V4L2_CID_MTK_VIDEO_ENC_CHROMA_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+32)
#define V4L2_CID_MTK_VIDEO_ENC_MB_RC_TK_SPD \
	(V4L2_CID_CODEC_MTK_ENC_BASE+33)
#define V4L2_CID_MTK_VIDEO_ENC_FRM_QP_LTR \
	(V4L2_CID_CODEC_MTK_ENC_BASE+34)
#define V4L2_CID_MTK_VIDEO_ENC_VISUAL_QUALITY \
	(V4L2_CID_CODEC_MTK_ENC_BASE+35)
#define V4L2_CID_MTK_VIDEO_ENC_INIT_QP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+36)
#define V4L2_CID_MTK_VIDEO_ENC_FRAME_QP_RANGE \
	(V4L2_CID_CODEC_MTK_ENC_BASE+37)
#define V4L2_CID_MTK_VIDEO_ENC_CONFIG_DATA \
	(V4L2_CID_CODEC_MTK_ENC_BASE+38)
#define V4L2_CID_MTK_VIDEO_ENC_GET_MAX_B_NUM \
	(V4L2_CID_CODEC_MTK_ENC_BASE+39)
#define V4L2_CID_MTK_VIDEO_ENC_GET_MAX_TEMPORAL_LAYER \
	(V4L2_CID_CODEC_MTK_ENC_BASE+40)
#define V4L2_CID_MTK_VIDEO_ENC_CLEAN_GOP \
	(V4L2_CID_CODEC_MTK_ENC_BASE+41)
#define V4L2_CID_MTK_VIDEO_ENC_ADAB_INFO \
	(V4L2_CID_CODEC_MTK_ENC_BASE+42)
#define V4L2_CID_MTK_VIDEO_ENC_GET_ADAB_CAPABILITY \
	(V4L2_CID_CODEC_MTK_ENC_BASE+43)
#define V4L2_CID_MTK_VIDEO_ENC_I_FRAME_SIZE_CONTROL \
	(V4L2_CID_CODEC_MTK_ENC_BASE+44)
#define V4L2_CID_MTK_VIDEO_ENC_QUERY_PARAM \
	(V4L2_CID_CODEC_MTK_ENC_BASE+45)
#define V4L2_CID_MTK_VIDEO_ENC_COMPATIBILITY_OPTION \
	(V4L2_CID_CODEC_MTK_ENC_BASE+46)
#define V4L2_CID_MTK_VIDEO_ENC_TIMING_INFO \
	(V4L2_CID_CODEC_MTK_ENC_BASE+47)
#define V4L2_CID_MTK_VIDEO_ENC_ENABLE_MULTIPLEX_RECORD  \
	(V4L2_CID_CODEC_MTK_ENC_BASE+48)

enum v4l2_vdec_decode_mode {
	V4L2_VDEC_DECODE_MODE_UNKNOWN = 0,
	V4L2_VDEC_DECODE_MODE_NORMAL,
	V4L2_VDEC_DECODE_MODE_I_ONLY,
	V4L2_VDEC_DECODE_MODE_B_SKIP,
	V4L2_VDEC_DECODE_MODE_LOW_LATENCY_DECODE,
	V4L2_VDEC_DECODE_MODE_NO_REORDER,
	V4L2_VDEC_DECODE_MODE_THUMBNAIL,

	V4L2_VDEC_DECODE_MODE_SKIP_REFERENCE_CHECK,
	V4L2_VDEC_DECODE_MODE_DROP_ERROR_FRAME,
};

enum v4l2_vdec_bandwidth_type {
	V4L2_AV1_COMPRESS = 0,
	V4L2_AV1_NO_COMPRESS = 1,
	V4L2_HEVC_COMPRESS = 2,
	V4L2_HEVC_NO_COMPRESS = 3,
	V4L2_VSD = 4,
	V4L2_BW_COUNT = 5,
};

enum v4l2_vdec_compressed_mode {
	V4L2_VDEC_COMPRESSED_DEFAULT = 0,
	V4L2_VDEC_COMPRESSED_ON,
	V4L2_VDEC_COMPRESSED_OFF,
};

enum v4l2_vdec_subsample_mode {

	V4L2_VDEC_SUBSAMPLE_DEFAULT = 0,

	V4L2_VDEC_SUBSAMPLE_OFF,

	V4L2_VDEC_SUBSAMPLE_ON,
};

enum v4l2_venc_scenario {
	V4L2_VENC_SCENARIO_WFD = 1,
	V4L2_VENC_SCENARIO_TIMELAPSE,
	V4L2_VENC_SCENARIO_SMVR,
	V4L2_VENC_SCENARIO_LIVE_PHOTO,
	V4L2_VENC_SCENARIO_VILTE,
	V4L2_VENC_SCENARIO_WECHAT,
	V4L2_VENC_SCENARIO_HDR,
	V4L2_VENC_SCENARIO_HDR10PLUS,
	V4L2_VENC_SDK_SCENARIO_BASED = 0xffff
};

struct v4l2_mtk_color_desc {
	__u32	color_primaries;
	__u32	transform_character;
	__u32	matrix_coeffs;
	__u32	display_primaries_x[3];
	__u32	display_primaries_y[3];
	__u32	white_point_x;
	__u32	white_point_y;
	__u32	max_display_mastering_luminance;
	__u32	min_display_mastering_luminance;
	__u32	max_content_light_level;
	__u32	max_pic_light_level;
	__u32	hdr_type;
	__u32	full_range;
	__u32	reserved;
};

struct v4l2_vdec_hdr10_info {
	__u8 matrix_coefficients;
	__u8 bits_per_channel;
	__u8 chroma_subsampling_horz;
	__u8 chroma_subsampling_vert;
	__u8 cb_subsampling_horz;
	__u8 cb_subsampling_vert;
	__u8 chroma_siting_horz;
	__u8 chroma_siting_vert;
	__u8 color_range;
	__u8 transfer_characteristics;
	__u8 colour_primaries;
	__u16 max_CLL;
	__u16 max_FALL;
	__u16 primaries[3][2];
	__u16 white_point[2];
	__u32 max_luminance;
	__u32 min_luminance;
};

struct v4l2_vdec_hdr10plus_data {
	__u64 addr;
	__u32 size;
};

struct v4l2_vdec_resource_parameter {
	__u32 width;
	__u32 height;
	struct v4l2_fract frame_rate;
	__u32 priority;
};

struct v4l2_vdec_resource_metrics {
	__u32 core_used;
	__u32 core_usage;
	__u8 gce;
};

struct v4l2_vdec_low_latency_parameter {

	__u16 slice_count;
	__u8 racing_display;

	__u16 threshold_numerator;
	__u16 threshold_denominator;
};

struct v4l2_vdec_max_buf_info {

	__u32 pixelformat;
	__u32 max_width;
	__u32 max_height;

	__u32 max_internal_buf_size;
	__u32 max_dpb_count;
	__u32 max_frame_buf_size;
};

struct v4l2_vdec_fmt_modifier {
	union {
		struct {
			__u8 tile:1;
			__u8 raster:1;
			__u8 compress:1;
			__u8 jump:1;
			__u8 vsd_mode:3;
			__u8 vsd_ce_mode:1;
			__u8 is_vsd_buf_layout:1;
			__u8 is_10bit:1;
			__u8 compress_engine:2;
			__u8 color_space_num:2;
			__u8 size_multiplier:3;
			__u8 luma_target;
			__u8 chroma_target;
		};

		__u64 padding;
	};
};

struct v4l2_vdec_bandwidth_info {

	__u32 bandwidth_per_pixel[V4L2_BW_COUNT];
	__u8 compress;
	__u8 vsd;
	struct v4l2_vdec_fmt_modifier modifier;
};

struct v4l2_venc_nal_length {
	__u32	prefer;
	__u32	bytes;
};

struct v4l2_venc_resolution_change {
	__u32 width;
	__u32 height;
	__u32 framerate;
	__u32 resolutionchange;
};

struct v4l2_venc_multi_ref {
	__u32	multi_ref_en;
	__u32	intra_period;
	__u32	superp_period;
	__u32	superp_ref_type;
	__u32	ref0_distance;
	__u32	ref1_dsitance;
	__u32	max_distance;
	__u32	reserved;
};

struct v4l2_venc_visual_quality {
	__s32	quant;
	__s32	psyrd;
	__s32	pfrmquant;
	__s32	bfrmquant;
	__s32	aqoffsetreduction;
	__s32	lumaAq10bitEnhance;
};

struct v4l2_venc_init_qp {
	__s32	enable;
	__s32	qpi;
	__s32	qpp;
	__s32	qpb;
};

struct v4l2_venc_frame_qp_range {
	__s32	enable;
	__s32	max;
	__s32	min;
};

struct v4l2_venc_adab_info {
	__u32	buf_width;
	__u32	buf_height;
	__u32	crop_width;
	__u32	crop_height;
	__u32	pixelformat;
};

struct v4l2_venc_i_frame_size_control {
	__s32   max_i_ratio;
	__s32   shrink_i_ratio;
};

#endif
