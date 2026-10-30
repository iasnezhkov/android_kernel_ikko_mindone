/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _MTK_VCODEC_DRV_H_
#define _MTK_VCODEC_DRV_H_

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/semaphore.h>
#include <linux/regulator/consumer.h>
#include <linux/interconnect-provider.h>
#include <linux/types.h>
#include <linux/list.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include "mtk_vcodec_util.h"
#include "vcodec_ipi_msg.h"
#include "mtk_vcodec_pm.h"
#include "vcodec_dvfs.h"
#include "vcodec_bw.h"
#include "mtk_dma_contig.h"
#include "slbc_ops.h"

#define MTK_VCODEC_DRV_NAME     "mtk_vcodec_drv"
#define MTK_VCODEC_DEC_NAME     "mtk-vcodec-dec"
#define MTK_VCODEC_ENC_NAME     "mtk-vcodec-enc"
#define MTK_VCU_FW_VERSION      "0.2.14"

#define MTK_SLOWMOTION_GCE_TH   120
#define MTK_VCODEC_MAX_PLANES   3
#define WAIT_INTR_TIMEOUT_MS    500
#define SUSPEND_TIMEOUT_CNT     5000
#define MTK_MAX_CTRLS_HINT      64
#ifdef V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED
#undef V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED
#endif
#define V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED 0x02000000

#define MAX_CODEC_FREQ_STEP	10
#define MTK_VDEC_PORT_NUM	32
#define MTK_VENC_PORT_NUM	64
#define MTK_MAX_METADATA_NUM    8

#define DEBUG_GKI 1

enum mtk_vcodec_ipm {
	VCODEC_IPM_V1 = 1,
	VCODEC_IPM_V2 = 2,
	VCODEC_IPM_MAX,
};

enum mtk_instance_type {
	MTK_INST_DECODER                = 0,
	MTK_INST_ENCODER                = 1,
};

enum mtk_instance_state {
	MTK_STATE_FREE = 0,
	MTK_STATE_INIT = 1,
	MTK_STATE_HEADER = 2,
	MTK_STATE_FLUSH = 3,
	MTK_STATE_STOP = 4,
	MTK_STATE_ABORT = 5,
};

enum mtk_codec_type {
	VDEC_VENC_UNKNOWN = 0,
	VDEC_H264,
	VDEC_H265,
	VDEC_HEIF,
	VDEC_VP8,
	VDEC_VP9,
	VDEC_MPEG4,
	VDEC_H263,
	VDEC_MPEG12,
	VDEC_WMV,
	VDEC_RV30,
	VDEC_RV40,
	VDEC_AV1,
	VENC_H264,
	VENC_H265,
	VENC_HEIF,
	VENC_VP8,
	VENC_MPEG4,
	VENC_HYBRID_H264,
	VENC_H263,
	VDEC_VENC_MAX
};

enum mtk_encode_param {
	MTK_ENCODE_PARAM_NONE = 0,
	MTK_ENCODE_PARAM_BITRATE = (1 << 0),
	MTK_ENCODE_PARAM_FRAMERATE = (1 << 1),
	MTK_ENCODE_PARAM_INTRA_PERIOD = (1 << 2),
	MTK_ENCODE_PARAM_FORCE_INTRA = (1 << 3),
	MTK_ENCODE_PARAM_GOP_SIZE = (1 << 4),
	MTK_ENCODE_PARAM_SCENARIO = (1 << 5),
	MTK_ENCODE_PARAM_NONREFP = (1 << 6),
	MTK_ENCODE_PARAM_DETECTED_FRAMERATE = (1 << 7),
	MTK_ENCODE_PARAM_RFS_ON = (1 << 8),
	MTK_ENCODE_PARAM_PREPEND_SPSPPS_TO_IDR = (1 << 9),
	MTK_ENCODE_PARAM_OPERATION_RATE = (1 << 10),
	MTK_ENCODE_PARAM_BITRATE_MODE = (1 << 11),
	MTK_ENCODE_PARAM_ROI_ON = (1 << 12),
	MTK_ENCODE_PARAM_GRID_SIZE = (1 << 13),
	MTK_ENCODE_PARAM_COLOR_DESC = (1 << 14),
	MTK_ENCODE_PARAM_SEC_ENCODE = (1 << 15),
	MTK_ENCODE_PARAM_TSVC = (1 << 16),
	MTK_ENCODE_PARAM_NONREFPFREQ = (1 << 17),
	MTK_ENCODE_PARAM_HIGHQUALITY = (1 << 18),
	MTK_ENCODE_PARAM_MAXQP = (1 << 19),
	MTK_ENCODE_PARAM_MINQP = (1 << 20),
	MTK_ENCODE_PARAM_FRAMELVLQP = (1 << 21),
	MTK_ENCODE_PARAM_IP_QPDELTA = (1 << 22),
	MTK_ENCODE_PARAM_QP_CTRL_MODE = (1 << 23),
	MTK_ENCODE_PARAM_DUMMY_NAL = (1 << 24),
};

enum venc_yuv_fmt {
	VENC_YUV_FORMAT_I420 = 3,
	VENC_YUV_FORMAT_YV12 = 5,
	VENC_YUV_FORMAT_NV12 = 6,
	VENC_YUV_FORMAT_NV21 = 7,
	VENC_YUV_FORMAT_24bitRGB888 = 11,
	VENC_YUV_FORMAT_24bitBGR888 = 12,
	VENC_YUV_FORMAT_32bitRGBA8888 = 13,
	VENC_YUV_FORMAT_32bitBGRA8888 = 14,
	VENC_YUV_FORMAT_32bitARGB8888 = 15,
	VENC_YUV_FORMAT_32bitABGR8888 = 16,
	VENC_YUV_FORMAT_32bitRGBA1010102 = 17,
	VENC_YUV_FORMAT_32bitBGRA1010102 = 18,
	VENC_YUV_FORMAT_32bitARGB1010102 = 19,
	VENC_YUV_FORMAT_32bitABGR1010102 = 20,
	VENC_YUV_FORMAT_32bitRGBA8888_AFBC = 21,
	VENC_YUV_FORMAT_32bitBGRA8888_AFBC = 22,
	VENC_YUV_FORMAT_32bitRGBA1010102_AFBC = 23,
	VENC_YUV_FORMAT_32bitBGRA1010102_AFBC = 24,
	VENC_YUV_FORMAT_MT10 = 25,
	VENC_YUV_FORMAT_P010 = 26,
	VENC_YUV_FORMAT_NV12_AFBC = 27,
	VENC_YUV_FORMAT_NV12_10B_AFBC = 28,
	VENC_YUV_FORMAT_NV21_AFBC = 29,
};

enum mtk_q_type {
	MTK_Q_DATA_SRC = 0,
	MTK_Q_DATA_DST = 1,
};

enum vdec_input_driven_mode {
	NON_INPUT_DRIVEN = 0,
	INPUT_DRIVEN_CB_FRM = 1,
	INPUT_DRIVEN_PUT_FRM = 2,
};

struct mtk_q_data {
	unsigned int    visible_width;
	unsigned int    visible_height;
	unsigned int    coded_width;
	unsigned int    coded_height;
	enum v4l2_field field;
	unsigned int    bytesperline[MTK_VCODEC_MAX_PLANES];
	unsigned int    sizeimage[MTK_VCODEC_MAX_PLANES];
	struct mtk_video_fmt    *fmt;
};

struct mtk_color_desc {
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
	__u32   is_hdr;
	__u32   full_range;
};

enum mtk_dec_host_param {
	MTK_DEC_HOST_PARAM_NONE = 0,
	MTK_DEC_HOST_PARAM_DECODE_MODE = (1 << 0),
	MTK_DEC_HOST_PARAM_FRAME_SIZE = (1 << 1),
	MTK_DEC_HOST_PARAM_FIXED_MAX_FRAME_SIZE = (1 << 2),
	MTK_DEC_HOST_PARAM_CRC_PATH = (1 << 3),
	MTK_DEC_HOST_PARAM_GOLDEN_PATH = (1 << 4),
	MTK_DEC_HOST_PARAM_WAIT_KEY_FRAME = (1 << 5),
	MTK_DEC_HOST_PARAM_NAL_SIZE_LENGTH = (1 << 6),
	MTK_DEC_HOST_PARAM_FIXED_MAX_OUTPUT_BUFFER = (1 << 7),
	MTK_DEC_HOST_PARAM_SEC_DECODE = (1 << 8),
	MTK_DEC_HOST_PARAM_OPERATING_RATE = (1 << 9)
};

enum venc_lock {
	VENC_LOCK_NONE,
	VENC_LOCK_NORMAL,
	VENC_LOCK_SEC
};

struct mtk_dec_host_params {
	unsigned int    decode_mode;
	unsigned int    frame_size_width;
	unsigned int    frame_size_height;
	unsigned int    fixed_max_frame_size_width;
	unsigned int    fixed_max_frame_size_height;
	unsigned int fixed_max_frame_buffer_mode;
	char            *crc_path;
	char            *golden_path;
	unsigned int    fb_num_planes;
	unsigned int	wait_key_frame;
	unsigned int	nal_size_length;
	unsigned int	svp_mode;
	unsigned int	operating_rate;
	u64	timestamp;
	unsigned int	total_frame_bufq_count;
	unsigned int	queued_frame_buf_count;
	int		priority;
};

struct mtk_enc_params {
	unsigned int    bitrate;
	unsigned int    num_b_frame;
	unsigned int    rc_frame;
	unsigned int    rc_mb;
	unsigned int    seq_hdr_mode;
	unsigned int    intra_period;
	unsigned int    gop_size;
	unsigned int    framerate_num;
	unsigned int    framerate_denom;
	unsigned int    h264_max_qp;
	unsigned int    profile;
	unsigned int    level;
	unsigned int    tier;
	unsigned int    force_intra;
	unsigned int    scenario;
	unsigned int    nonrefp;
	unsigned int    detectframerate;
	unsigned int    rfs;
	unsigned int    prependheader;
	unsigned int    operationrate;
	unsigned int    bitratemode;
	unsigned int    roion;
	unsigned int    heif_grid_size;
	struct mtk_color_desc color_desc;
	unsigned int    max_w;
	unsigned int    max_h;
	unsigned int    slbc_ready;
	unsigned int    i_qp;
	unsigned int    p_qp;
	unsigned int    b_qp;
	unsigned int    svp_mode;
	unsigned int    tsvc;
	unsigned int    nonrefpfreq;
	unsigned int    highquality;
	int             max_qp;
	int             min_qp;
	int             framelvl_qp;
	int             ip_qpdelta;
	unsigned int	qp_control_mode;
	unsigned int	dummynal;
	int		priority;
};

struct venc_enc_param {
	enum venc_yuv_fmt input_yuv_fmt;
	unsigned int profile;
	unsigned int level;
	unsigned int tier;
	unsigned int width;
	unsigned int height;
	unsigned int buf_width;
	unsigned int buf_height;
	unsigned int frm_rate;
	unsigned int intra_period;
	unsigned int bitrate;
	unsigned int gop_size;
	unsigned int scenario;
	unsigned int nonrefp;
	unsigned int detectframerate;
	unsigned int rfs;
	unsigned int prependheader;
	unsigned int operationrate;
	unsigned int bitratemode;
	unsigned int roion;
	unsigned int heif_grid_size;

	struct mtk_color_desc *color_desc;
	unsigned int sizeimage[MTK_VCODEC_MAX_PLANES];
	unsigned int max_w;
	unsigned int max_h;
	unsigned int num_b_frame;
	unsigned int slbc_ready;
	unsigned int i_qp;
	unsigned int p_qp;
	unsigned int b_qp;
	unsigned int svp_mode;
	unsigned int tsvc;
	unsigned int nonrefpfreq;
	unsigned int highquality;
	unsigned int max_qp;
	unsigned int min_qp;
	unsigned int framelvl_qp;
	unsigned int qp_control_mode;
	unsigned int ip_qpdelta;
	unsigned int dummynal;
	unsigned int slbc_addr;
	char set_vcp_buf[1024];
	char property_buf[1024];
	char *log;
};

struct venc_frm_buf {
	struct mtk_vcodec_mem fb_addr[MTK_VCODEC_MAX_PLANES];
	u32 index;
	unsigned int num_planes;
	u64 timestamp;
	bool has_meta;
	struct dma_buf *meta_dma;
	struct dma_buf_attachment *buf_att;
	struct sg_table *sgt;
	dma_addr_t meta_addr;
	struct dma_buf_attachment *qpmap_dma_att;
	struct sg_table *qpmap_sgt;
	unsigned int meta_offset;
	bool has_qpmap;
	struct dma_buf *qpmap_dma;
	dma_addr_t qpmap_dma_addr;
	struct dma_buf *metabuffer_dma;
	dma_addr_t metabuffer_addr;
};

enum metadata_type {
	METADATA_HDR               = 0,
	METADATA_QPMAP             = 1
};

struct meta_describe {
	uint8_t invalid;
	uint8_t fd_flag;
	uint32_t type;
	uint32_t size;
	uint32_t value;
};

struct metadata_info {
	struct meta_describe metadata_dsc[MTK_MAX_METADATA_NUM];
};

struct mtk_vcodec_ctx {
	enum mtk_instance_type type;
	struct mtk_vcodec_dev *dev;
	struct list_head list;

	struct v4l2_fh fh;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct mtk_q_data q_data[2];
	int id;
	enum mtk_instance_state state;
	enum mtk_dec_host_param dec_param_change;
	struct mtk_dec_host_params dec_params;
	enum mtk_encode_param param_change;
	struct mtk_enc_params enc_params;

	const struct vdec_common_if *dec_if;
	const struct venc_common_if *enc_if;
	unsigned long drv_handle;
	uintptr_t bs_list[VB2_MAX_FRAME+1];
	uintptr_t fb_list[VB2_MAX_FRAME+1];

	struct vdec_pic_info picinfo;
	int dpb_size;
	int last_dpb_size;
	int is_hdr;
	int last_is_hdr;
	unsigned int errormap_info[VB2_MAX_FRAME];
	s64 input_max_ts;

	bool is_flushing;
	unsigned int eos_type;
	u64 early_eos_ts;

	int int_cond[MTK_VDEC_HW_NUM];
	int int_type;
	wait_queue_head_t queue[MTK_VDEC_HW_NUM];
	unsigned int irq_status;

	struct v4l2_ctrl_handler ctrl_hdl;
	struct work_struct decode_work;
	struct work_struct encode_work;
	struct vdec_pic_info last_decoded_picinfo;
	struct mtk_video_dec_buf *dec_flush_buf;
	struct mtk_video_enc_buf *enc_flush_buf;
	struct vb2_buffer *pend_src_buf;
	wait_queue_head_t fm_wq;
	unsigned int *ipi_blocked;
	enum vdec_input_driven_mode input_driven;
	bool output_async;

	int user_lock_hw;
	struct mutex hw_status;
	int hw_locked[MTK_VDEC_HW_NUM];
	int core_locked[MTK_VENC_HW_NUM];
	int async_mode;
	int oal_vcodec;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;

	int init_cnt;
	int decoded_frame_cnt;

	unsigned int vcp_action_cnt;
	unsigned int last_vcp_action_cnt;
	bool is_vcp_active;
	struct mutex vcp_active_mutex;

	struct mutex buf_lock;
	struct mutex worker_lock;
	struct slbc_data sram_data;
	struct mutex q_mutex;
	int use_slbc;
	unsigned int slbc_addr;
};

struct venc_larb_port {
	unsigned int total_port_num;
	unsigned int port_id[MTK_VENC_PORT_NUM];
	unsigned int ram_type[MTK_VENC_PORT_NUM];
};

struct mtk_vcodec_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vfd_dec;
	struct video_device *vfd_enc;
	struct iommu_domain *io_domain;

	const char *platform;
	enum mtk_instance_type type;
	enum mtk_vcodec_ipm vdec_hw_ipm;
	enum mtk_vcodec_ipm venc_hw_ipm;

	struct v4l2_m2m_dev *m2m_dev_dec;
	struct v4l2_m2m_dev *m2m_dev_enc;
	struct platform_device *plat_dev;
	struct platform_device *vcu_plat_dev;
	struct list_head ctx_list;
	struct notifier_block vcp_notify;
	spinlock_t irqlock;
	struct mtk_vcodec_ctx *curr_dec_ctx[MTK_VDEC_HW_NUM];
	struct mtk_vcodec_ctx *curr_enc_ctx[MTK_VENC_HW_NUM];
	void __iomem *dec_reg_base[NUM_MAX_VDEC_REG_BASE];
	void __iomem *enc_reg_base[NUM_MAX_VENC_REG_BASE];

	bool dec_is_power_on[MTK_VDEC_HW_NUM];
	bool enc_is_power_on[MTK_VENC_HW_NUM];
	spinlock_t dec_power_lock[MTK_VDEC_HW_NUM];
	spinlock_t enc_power_lock[MTK_VENC_HW_NUM];
	int dec_m4u_ports[NUM_MAX_VDEC_M4U_PORT];

	unsigned long id_counter;

	struct workqueue_struct *decode_workqueue;
	struct workqueue_struct *encode_workqueue;
	struct workqueue_struct *check_alive_workqueue;
	struct work_struct check_alive_work;
	struct timer_list vdec_active_checker;
	bool has_timer;

	int int_cond;
	int int_type;
	struct mutex ctx_mutex;
	struct mutex ipi_dispatch_lock;
	struct mutex dev_mutex;
	struct mutex ipi_mutex;
	struct mutex ipi_mutex_res;
	struct mtk_vcodec_msgq mq;

	int dec_irq[MTK_VDEC_HW_NUM];
	int enc_irq[MTK_VENC_HW_NUM];
	int enc_lt_irq;

	struct semaphore dec_sem[MTK_VDEC_HW_NUM];
	struct semaphore enc_sem[MTK_VENC_HW_NUM];

	struct mutex dec_dvfs_mutex;
	struct mutex enc_dvfs_mutex;

	struct mtk_vcodec_pm pm;
	struct notifier_block pm_notifier;
	bool is_codec_suspending;

	int dec_cnt;
	int enc_cnt;

	struct share_obj dec_ipi_data;
	struct share_obj enc_ipi_data;
	int *dec_mem_slot_stat;
	int *enc_mem_slot_stat;

	int dec_hw_cnt;
	int enc_hw_cnt;

	struct plist_head vdec_rlist[MTK_VDEC_HW_NUM];
	struct plist_head venc_rlist[MTK_VENC_HW_NUM];
	struct icc_path *vdec_qos_req[MTK_VDEC_PORT_NUM];
	struct icc_path *venc_qos_req[MTK_VENC_PORT_NUM];

	int vdec_freq_cnt;
	int venc_freq_cnt;
	unsigned long vdec_freqs[MAX_CODEC_FREQ_STEP];
	unsigned long venc_freqs[MAX_CODEC_FREQ_STEP];

	struct regulator *vdec_reg;
	struct regulator *venc_reg;
	struct venc_larb_port venc_ports[MTK_VENC_HW_NUM];

	int vdec_op_rate_cnt;

	int vdec_tput_cnt;
	int venc_tput_cnt;
	int vdec_cfg_cnt;
	int venc_cfg_cnt;
	int vdec_port_cnt;
	int venc_port_cnt;
	int vdec_port_idx[MTK_VDEC_HW_NUM];
	int venc_port_idx[MTK_VENC_HW_NUM];
	struct mtk_tf_info *tf_info;
	struct vcodec_perf *vdec_tput;
	struct vcodec_perf *venc_tput;

	struct vcodec_config *venc_cfg;
	struct vcodec_op_rate *vdec_dflt_op_rate;

	struct list_head vdec_dvfs_inst;
	struct list_head venc_dvfs_inst;
	struct dvfs_params vdec_dvfs_params;
	struct dvfs_params venc_dvfs_params;
	struct vcodec_port_bw *vdec_port_bw;
	struct vcodec_port_bw *venc_port_bw;

	struct list_head log_param_list;
	struct list_head prop_param_list;
	struct mutex log_param_mutex;
	struct mutex prop_param_mutex;
	enum venc_lock enc_hw_locked[MTK_VENC_HW_NUM];

	unsigned int svp_mtee;
	unsigned int unique_domain;
	int num_of_cores;
};

static inline struct mtk_vcodec_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mtk_vcodec_ctx, fh);
}

static inline struct mtk_vcodec_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mtk_vcodec_ctx, ctrl_hdl);
}

#ifdef V4L2_PIX_FMT_ARGB1010102
#undef V4L2_PIX_FMT_ARGB1010102
#endif
#define V4L2_PIX_FMT_ARGB1010102  v4l2_fourcc('A', 'B', '3', '0')
#ifdef V4L2_PIX_FMT_ABGR1010102
#undef V4L2_PIX_FMT_ABGR1010102
#endif
#define V4L2_PIX_FMT_ABGR1010102  v4l2_fourcc('A', 'R', '3', '0')
#ifdef V4L2_PIX_FMT_RGBA1010102
#undef V4L2_PIX_FMT_RGBA1010102
#endif
#define V4L2_PIX_FMT_RGBA1010102  v4l2_fourcc('R', 'A', '3', '0')
#ifdef V4L2_PIX_FMT_BGRA1010102
#undef V4L2_PIX_FMT_BGRA1010102
#endif
#define V4L2_PIX_FMT_BGRA1010102  v4l2_fourcc('B', 'A', '3', '0')

#ifdef V4L2_PIX_FMT_P010M
#undef V4L2_PIX_FMT_P010M
#endif
#define V4L2_PIX_FMT_P010M   v4l2_fourcc('P', '0', '1', '0')

#ifdef V4L2_PIX_FMT_H264_SLICE
#undef V4L2_PIX_FMT_H264_SLICE
#endif
#define V4L2_PIX_FMT_H264_SLICE v4l2_fourcc('S', '2', '6', '4')
#ifdef V4L2_PIX_FMT_H265
#undef V4L2_PIX_FMT_H265
#endif
#define V4L2_PIX_FMT_H265     v4l2_fourcc('H', '2', '6', '5')
#ifdef V4L2_PIX_FMT_HEIF
#undef V4L2_PIX_FMT_HEIF
#endif
#define V4L2_PIX_FMT_HEIF     v4l2_fourcc('H', 'E', 'I', 'F')

#ifdef V4L2_PIX_FMT_VP8_FRAME
#undef V4L2_PIX_FMT_VP8_FRAME
#endif
#define V4L2_PIX_FMT_VP8_FRAME v4l2_fourcc('V', 'P', '8', 'F')
#ifdef V4L2_PIX_FMT_WMV1
#undef V4L2_PIX_FMT_WMV1
#endif
#define V4L2_PIX_FMT_WMV1     v4l2_fourcc('W', 'M', 'V', '1')
#ifdef V4L2_PIX_FMT_WMV2
#undef V4L2_PIX_FMT_WMV2
#endif
#define V4L2_PIX_FMT_WMV2     v4l2_fourcc('W', 'M', 'V', '2')
#ifdef V4L2_PIX_FMT_WMV3
#undef V4L2_PIX_FMT_WMV3
#endif
#define V4L2_PIX_FMT_WMV3     v4l2_fourcc('W', 'M', 'V', '3')
#ifdef V4L2_PIX_FMT_WMVA
#undef V4L2_PIX_FMT_WMVA
#endif
#define V4L2_PIX_FMT_WMVA     v4l2_fourcc('W', 'M', 'V', 'A')
#ifdef V4L2_PIX_FMT_WVC1
#undef V4L2_PIX_FMT_WVC1
#endif
#define V4L2_PIX_FMT_WVC1     v4l2_fourcc('W', 'V', 'C', '1')
#ifdef V4L2_PIX_FMT_RV30
#undef V4L2_PIX_FMT_RV30
#endif
#define V4L2_PIX_FMT_RV30     v4l2_fourcc('R', 'V', '3', '0')
#ifdef V4L2_PIX_FMT_RV40
#undef V4L2_PIX_FMT_RV40
#endif
#define V4L2_PIX_FMT_RV40     v4l2_fourcc('R', 'V', '4', '0')
#ifdef V4L2_PIX_FMT_AV1
#undef V4L2_PIX_FMT_AV1
#endif
#define V4L2_PIX_FMT_AV1      v4l2_fourcc('A', 'V', '1', '0')

#ifdef V4L2_PIX_FMT_MT21
#undef V4L2_PIX_FMT_MT21
#endif
#define V4L2_PIX_FMT_MT21         v4l2_fourcc('M', 'M', '2', '1')

#ifdef V4L2_PIX_FMT_MT2110T
#undef V4L2_PIX_FMT_MT2110T
#endif
#define V4L2_PIX_FMT_MT2110T      v4l2_fourcc('M', 'T', '2', 'T')

#ifdef V4L2_PIX_FMT_MT2110R
#undef V4L2_PIX_FMT_MT2110R
#endif
#define V4L2_PIX_FMT_MT2110R      v4l2_fourcc('M', 'T', '2', 'R')

#ifdef V4L2_PIX_FMT_MT21C10T
#undef V4L2_PIX_FMT_MT21C10T
#endif
#define V4L2_PIX_FMT_MT21C10T     v4l2_fourcc('M', 'T', 'C', 'T')

#ifdef V4L2_PIX_FMT_MT21C10R
#undef V4L2_PIX_FMT_MT21C10R
#endif
#define V4L2_PIX_FMT_MT21C10R     v4l2_fourcc('M', 'T', 'C', 'R')

#ifdef V4L2_PIX_FMT_MT21CS
#undef V4L2_PIX_FMT_MT21CS
#endif
#define V4L2_PIX_FMT_MT21CS       v4l2_fourcc('M', '2', 'C', 'S')

#ifdef V4L2_PIX_FMT_MT21S
#undef V4L2_PIX_FMT_MT21S
#endif
#define V4L2_PIX_FMT_MT21S        v4l2_fourcc('M', '2', '1', 'S')

#ifdef V4L2_PIX_FMT_MT21S10T
#undef V4L2_PIX_FMT_MT21S10T
#endif
#define V4L2_PIX_FMT_MT21S10T     v4l2_fourcc('M', 'T', 'S', 'T')

#ifdef V4L2_PIX_FMT_MT21S10R
#undef V4L2_PIX_FMT_MT21S10R
#endif
#define V4L2_PIX_FMT_MT21S10R     v4l2_fourcc('M', 'T', 'S', 'R')

#ifdef V4L2_PIX_FMT_MT21CS10T
#undef V4L2_PIX_FMT_MT21CS10T
#endif
#define V4L2_PIX_FMT_MT21CS10T    v4l2_fourcc('M', 'C', 'S', 'T')

#ifdef V4L2_PIX_FMT_MT21CS10R
#undef V4L2_PIX_FMT_MT21CS10R
#endif
#define V4L2_PIX_FMT_MT21CS10R    v4l2_fourcc('M', 'C', 'S', 'R')

#ifdef V4L2_PIX_FMT_MT21CSA
#undef V4L2_PIX_FMT_MT21CSA
#endif
#define V4L2_PIX_FMT_MT21CSA      v4l2_fourcc('M', 'A', 'C', 'S')

#ifdef V4L2_PIX_FMT_MT21S10TJ
#undef V4L2_PIX_FMT_MT21S10TJ
#endif
#define V4L2_PIX_FMT_MT21S10TJ    v4l2_fourcc('M', 'J', 'S', 'T')

#ifdef V4L2_PIX_FMT_MT21S10RJ
#undef V4L2_PIX_FMT_MT21S10RJ
#endif
#define V4L2_PIX_FMT_MT21S10RJ    v4l2_fourcc('M', 'J', 'S', 'R')

#ifdef V4L2_PIX_FMT_MT21CS10TJ
#undef V4L2_PIX_FMT_MT21CS10TJ
#endif
#define V4L2_PIX_FMT_MT21CS10TJ   v4l2_fourcc('J', 'C', 'S', 'T')

#ifdef V4L2_PIX_FMT_MT21CS10RJ
#undef V4L2_PIX_FMT_MT21CS10RJ
#endif
#define V4L2_PIX_FMT_MT21CS10RJ   v4l2_fourcc('J', 'C', 'S', 'R')

#ifdef V4L2_PIX_FMT_MT10S
#undef V4L2_PIX_FMT_MT10S
#endif
#define V4L2_PIX_FMT_MT10S     v4l2_fourcc('M', '1', '0', 'S')

#ifdef V4L2_PIX_FMT_MT10
#undef V4L2_PIX_FMT_MT10
#endif
#define V4L2_PIX_FMT_MT10     v4l2_fourcc('M', 'T', '1', '0')

#ifdef V4L2_PIX_FMT_P010S
#undef V4L2_PIX_FMT_P010S
#endif
#define V4L2_PIX_FMT_P010S   v4l2_fourcc('P', '0', '1', 'S')

#ifdef V4L2_PIX_FMT_RGB32_AFBC
#undef V4L2_PIX_FMT_RGB32_AFBC
#endif
#define V4L2_PIX_FMT_RGB32_AFBC         v4l2_fourcc('M', 'C', 'R', '8')
#ifdef V4L2_PIX_FMT_BGR32_AFBC
#undef V4L2_PIX_FMT_BGR32_AFBC
#endif
#define V4L2_PIX_FMT_BGR32_AFBC         v4l2_fourcc('M', 'C', 'B', '8')

#ifdef V4L2_PIX_FMT_RGBA1010102_AFBC
#undef V4L2_PIX_FMT_RGBA1010102_AFBC
#endif
#define V4L2_PIX_FMT_RGBA1010102_AFBC   v4l2_fourcc('M', 'C', 'R', 'X')
#ifdef V4L2_PIX_FMT_BGRA1010102_AFBC
#undef V4L2_PIX_FMT_BGRA1010102_AFBC
#endif
#define V4L2_PIX_FMT_BGRA1010102_AFBC   v4l2_fourcc('M', 'C', 'B', 'X')

#ifdef V4L2_PIX_FMT_NV12_AFBC
#undef V4L2_PIX_FMT_NV12_AFBC
#endif
#define V4L2_PIX_FMT_NV12_AFBC          v4l2_fourcc('M', 'C', 'N', '8')

#ifdef V4L2_PIX_FMT_NV21_AFBC
#undef V4L2_PIX_FMT_NV21_AFBC
#endif
#define V4L2_PIX_FMT_NV21_AFBC          v4l2_fourcc('M', 'N', '2', '8')

#ifdef V4L2_PIX_FMT_NV12_10B_AFBC
#undef V4L2_PIX_FMT_NV12_10B_AFBC
#endif
#define V4L2_PIX_FMT_NV12_10B_AFBC      v4l2_fourcc('M', 'C', 'N', 'X')

#ifdef V4L2_PIX_FMT_MTISP_B8
#undef V4L2_PIX_FMT_MTISP_B8
#endif
#define V4L2_PIX_FMT_MTISP_B8	v4l2_fourcc('M', 'T', 'B', '8')

#ifdef V4L2_BUF_FLAG_REF_FREED
#undef V4L2_BUF_FLAG_REF_FREED
#endif
#define V4L2_BUF_FLAG_REF_FREED			0x00000200

#ifdef V4L2_BUF_FLAG_CROP_CHANGED
#undef V4L2_BUF_FLAG_CROP_CHANGED
#endif
#define V4L2_BUF_FLAG_CROP_CHANGED		0x00008000
#ifdef V4L2_BUF_FLAG_CSD
#undef V4L2_BUF_FLAG_CSD
#endif
#define V4L2_BUF_FLAG_CSD					0x00200000
#ifdef V4L2_BUF_FLAG_ROI
#undef V4L2_BUF_FLAG_ROI
#endif
#define V4L2_BUF_FLAG_ROI					0x00400000
#ifdef V4L2_BUF_FLAG_HDR_META
#undef V4L2_BUF_FLAG_HDR_META
#endif
#define V4L2_BUF_FLAG_HDR_META			0x00800000
#ifdef V4L2_BUF_FLAG_QP_META
#undef V4L2_BUF_FLAG_QP_META
#endif
#define V4L2_BUF_FLAG_QP_META			0x01000000
#ifdef V4L2_BUF_FLAG_HAS_META
#undef V4L2_BUF_FLAG_HAS_META
#endif
#define V4L2_BUF_FLAG_HAS_META			0x4000000

#ifdef V4L2_EVENT_MTK_VCODEC_START
#undef V4L2_EVENT_MTK_VCODEC_START
#endif
#define V4L2_EVENT_MTK_VCODEC_START	(V4L2_EVENT_PRIVATE_START + 0x00002000)
#ifdef V4L2_EVENT_MTK_VDEC_ERROR
#undef V4L2_EVENT_MTK_VDEC_ERROR
#endif
#define V4L2_EVENT_MTK_VDEC_ERROR	(V4L2_EVENT_MTK_VCODEC_START + 1)
#ifdef V4L2_EVENT_MTK_VDEC_NOHEADER
#undef V4L2_EVENT_MTK_VDEC_NOHEADER
#endif
#define V4L2_EVENT_MTK_VDEC_NOHEADER	(V4L2_EVENT_MTK_VCODEC_START + 2)
#ifdef V4L2_EVENT_MTK_VENC_ERROR
#undef V4L2_EVENT_MTK_VENC_ERROR
#endif
#define V4L2_EVENT_MTK_VENC_ERROR	(V4L2_EVENT_MTK_VCODEC_START + 3)

#ifdef V4L2_CID_MPEG_MTK_BASE
#undef V4L2_CID_MPEG_MTK_BASE
#endif
#define V4L2_CID_MPEG_MTK_BASE \
	(V4L2_CTRL_CLASS_CODEC | 0x2000)
#ifdef V4L2_CID_MPEG_MTK_FRAME_INTERVAL
#undef V4L2_CID_MPEG_MTK_FRAME_INTERVAL
#endif
#define V4L2_CID_MPEG_MTK_FRAME_INTERVAL \
	(V4L2_CID_MPEG_MTK_BASE+0)
#ifdef V4L2_CID_MPEG_MTK_ERRORMB_MAP
#undef V4L2_CID_MPEG_MTK_ERRORMB_MAP
#endif
#define V4L2_CID_MPEG_MTK_ERRORMB_MAP \
	(V4L2_CID_MPEG_MTK_BASE+1)
#ifdef V4L2_CID_MPEG_MTK_DECODE_MODE
#undef V4L2_CID_MPEG_MTK_DECODE_MODE
#endif
#define V4L2_CID_MPEG_MTK_DECODE_MODE \
	(V4L2_CID_MPEG_MTK_BASE+2)
#ifdef V4L2_CID_MPEG_MTK_FRAME_SIZE
#undef V4L2_CID_MPEG_MTK_FRAME_SIZE
#endif
#define V4L2_CID_MPEG_MTK_FRAME_SIZE \
	(V4L2_CID_MPEG_MTK_BASE+3)
#ifdef V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER
#undef V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER
#endif
#define V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER \
	(V4L2_CID_MPEG_MTK_BASE+4)
#ifdef V4L2_CID_MPEG_MTK_CRC_PATH
#undef V4L2_CID_MPEG_MTK_CRC_PATH
#endif
#define V4L2_CID_MPEG_MTK_CRC_PATH \
	(V4L2_CID_MPEG_MTK_BASE+5)
#ifdef V4L2_CID_MPEG_MTK_GOLDEN_PATH
#undef V4L2_CID_MPEG_MTK_GOLDEN_PATH
#endif
#define V4L2_CID_MPEG_MTK_GOLDEN_PATH \
	(V4L2_CID_MPEG_MTK_BASE+6)
#ifdef V4L2_CID_MPEG_MTK_COLOR_DESC
#undef V4L2_CID_MPEG_MTK_COLOR_DESC
#endif
#define V4L2_CID_MPEG_MTK_COLOR_DESC \
	(V4L2_CID_MPEG_MTK_BASE+7)
#ifdef V4L2_CID_MPEG_MTK_ASPECT_RATIO
#undef V4L2_CID_MPEG_MTK_ASPECT_RATIO
#endif
#define V4L2_CID_MPEG_MTK_ASPECT_RATIO \
	(V4L2_CID_MPEG_MTK_BASE+8)
#ifdef V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME
#undef V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME
#endif
#define V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME \
	(V4L2_CID_MPEG_MTK_BASE+9)
#ifdef V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH
#undef V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH
#endif
#define V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH \
	(V4L2_CID_MPEG_MTK_BASE+10)
#ifdef V4L2_CID_MPEG_MTK_SEC_DECODE
#undef V4L2_CID_MPEG_MTK_SEC_DECODE
#endif
#define V4L2_CID_MPEG_MTK_SEC_DECODE \
	(V4L2_CID_MPEG_MTK_BASE+11)
#ifdef V4L2_CID_MPEG_MTK_FIX_BUFFERS
#undef V4L2_CID_MPEG_MTK_FIX_BUFFERS
#endif
#define V4L2_CID_MPEG_MTK_FIX_BUFFERS \
	(V4L2_CID_MPEG_MTK_BASE+12)
#ifdef V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP
#undef V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP
#endif
#define V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP \
	(V4L2_CID_MPEG_MTK_BASE+13)
#ifdef V4L2_CID_MPEG_MTK_INTERLACING
#undef V4L2_CID_MPEG_MTK_INTERLACING
#endif
#define V4L2_CID_MPEG_MTK_INTERLACING \
	(V4L2_CID_MPEG_MTK_BASE+14)
#ifdef V4L2_CID_MPEG_MTK_CODEC_TYPE
#undef V4L2_CID_MPEG_MTK_CODEC_TYPE
#endif
#define V4L2_CID_MPEG_MTK_CODEC_TYPE \
	(V4L2_CID_MPEG_MTK_BASE+15)
#ifdef V4L2_CID_MPEG_MTK_OPERATING_RATE
#undef V4L2_CID_MPEG_MTK_OPERATING_RATE
#endif
#define V4L2_CID_MPEG_MTK_OPERATING_RATE \
	(V4L2_CID_MPEG_MTK_BASE+16)
#ifdef V4L2_CID_MPEG_MTK_SEC_ENCODE
#undef V4L2_CID_MPEG_MTK_SEC_ENCODE
#endif
#define V4L2_CID_MPEG_MTK_SEC_ENCODE \
	(V4L2_CID_MPEG_MTK_BASE+17)
#ifdef V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT
#undef V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT
#endif
#define V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT \
	(V4L2_CID_MPEG_MTK_BASE+18)
#ifdef V4L2_CID_MPEG_MTK_UFO_MODE
#undef V4L2_CID_MPEG_MTK_UFO_MODE
#endif
#define V4L2_CID_MPEG_MTK_UFO_MODE \
	(V4L2_CID_MPEG_MTK_BASE+19)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_SCENARIO
#undef V4L2_CID_MPEG_MTK_ENCODE_SCENARIO
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_SCENARIO \
	(V4L2_CID_MPEG_MTK_BASE+20)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_NONREFP
#undef V4L2_CID_MPEG_MTK_ENCODE_NONREFP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_NONREFP \
	(V4L2_CID_MPEG_MTK_BASE+21)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE
#undef V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE \
	(V4L2_CID_MPEG_MTK_BASE+22)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RFS_ON
#undef V4L2_CID_MPEG_MTK_ENCODE_RFS_ON
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RFS_ON \
	(V4L2_CID_MPEG_MTK_BASE+23)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE
#undef V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE \
	(V4L2_CID_MPEG_MTK_BASE+24)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP \
	(V4L2_CID_MPEG_MTK_BASE+25)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_ROI_ON
#undef V4L2_CID_MPEG_MTK_ENCODE_ROI_ON
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_ROI_ON \
	(V4L2_CID_MPEG_MTK_BASE+26)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE
#undef V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE \
	(V4L2_CID_MPEG_MTK_BASE+27)
#ifdef V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE
#undef V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE
#endif
#define V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE \
	(V4L2_CID_MPEG_MTK_BASE+28)
#ifdef V4L2_CID_MPEG_MTK_MAX_WIDTH
#undef V4L2_CID_MPEG_MTK_MAX_WIDTH
#endif
#define V4L2_CID_MPEG_MTK_MAX_WIDTH \
	(V4L2_CID_MPEG_MTK_BASE+29)
#ifdef V4L2_CID_MPEG_MTK_MAX_HEIGHT
#undef V4L2_CID_MPEG_MTK_MAX_HEIGHT
#endif
#define V4L2_CID_MPEG_MTK_MAX_HEIGHT \
	(V4L2_CID_MPEG_MTK_BASE+30)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP \
	(V4L2_CID_MPEG_MTK_BASE+31)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP \
	(V4L2_CID_MPEG_MTK_BASE+32)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP \
	(V4L2_CID_MPEG_MTK_BASE+33)
#ifdef V4L2_CID_MPEG_VIDEO_ENABLE_TSVC
#undef V4L2_CID_MPEG_VIDEO_ENABLE_TSVC
#endif
#define V4L2_CID_MPEG_VIDEO_ENABLE_TSVC \
	(V4L2_CID_MPEG_MTK_BASE+34)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ
#undef V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ \
	(V4L2_CID_MPEG_MTK_BASE+35)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP \
	(V4L2_CID_MPEG_MTK_BASE+36)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP \
	(V4L2_CID_MPEG_MTK_BASE+37)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA \
	(V4L2_CID_MPEG_MTK_BASE+38)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE \
	(V4L2_CID_MPEG_MTK_BASE+39)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP
#undef V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP \
	(V4L2_CID_MPEG_MTK_BASE+40)

#ifdef V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY
#undef V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_ENABLE_HIGHQUALITY \
	(V4L2_CID_MPEG_MTK_BASE+45)
#ifdef V4L2_CID_MPEG_MTK_LOG
#undef V4L2_CID_MPEG_MTK_LOG
#endif
#define V4L2_CID_MPEG_MTK_LOG \
	(V4L2_CID_MPEG_MTK_BASE+46)
#ifdef V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL
#undef V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL
#endif
#define V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL \
	(V4L2_CID_MPEG_MTK_BASE+47)
#ifdef V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY
#undef V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY
#endif
#define V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY \
	(V4L2_CID_MPEG_MTK_BASE+48)
#ifdef V4L2_CID_MPEG_MTK_VCP_PROP
#undef V4L2_CID_MPEG_MTK_VCP_PROP
#endif
#define V4L2_CID_MPEG_MTK_VCP_PROP \
	(V4L2_CID_MPEG_MTK_BASE+49)

#ifdef V4L2_CID_MPEG_MTK_GET_LOG
#undef V4L2_CID_MPEG_MTK_GET_LOG
#endif
#define V4L2_CID_MPEG_MTK_GET_LOG \
	(V4L2_CID_MPEG_MTK_BASE+63)

#ifdef V4L2_CID_MPEG_MTK_GET_VCP_PROP
#undef V4L2_CID_MPEG_MTK_GET_VCP_PROP
#endif
#define V4L2_CID_MPEG_MTK_GET_VCP_PROP \
	(V4L2_CID_MPEG_MTK_BASE+64)

#endif
