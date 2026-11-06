/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VENC_DRV_IF_H_
#define _VENC_DRV_IF_H_

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"
#include "venc_vcu_if.h"
#include "venc_ipi_msg.h"

struct venc_inst {
	void __iomem *hw_base;
	struct mtk_vcodec_mem pps_buf;
	bool work_buf_allocated;
	unsigned int frm_cnt;
	unsigned int prepend_hdr;
	struct venc_vcu_inst vcu_inst;
	struct venc_vsi *vsi;
	struct mtk_vcodec_ctx *ctx;
};

enum venc_start_opt {
	VENC_START_OPT_ENCODE_SEQUENCE_HEADER,
	VENC_START_OPT_ENCODE_FRAME,
	VENC_START_OPT_ENCODE_FRAME_FINAL
};

struct venc_done_result {
	__u32 bs_size;
	__u32 is_key_frm;
	unsigned long bs_va;
	unsigned long frm_va;
	__u32 flags;
	__u32 is_last_slc;
};

struct venc_resolution_change {
	__u32 width;
	__u32 height;
	__u32 framerate;
	__u32 resolutionchange;
};

extern struct mtk_video_fmt
	mtk_venc_formats[MTK_MAX_ENC_CODECS_SUPPORT];
extern struct mtk_codec_framesizes
	mtk_venc_framesizes[MTK_MAX_ENC_CODECS_SUPPORT];

int venc_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc);

int venc_if_deinit(struct mtk_vcodec_ctx *ctx);

int venc_if_get_param(struct mtk_vcodec_ctx *ctx, enum venc_get_param_type type,
					  void *out);

int venc_if_set_param(struct mtk_vcodec_ctx *ctx,
					  enum venc_set_param_type type,
					  struct venc_enc_param *in);

int venc_if_encode(struct mtk_vcodec_ctx *ctx,
				   enum venc_start_opt opt,
				   struct venc_frm_buf *frm_buf,
				   struct mtk_vcodec_mem *bs_buf,
				   struct venc_done_result *result);

void venc_encode_prepare(void *ctx_prepare,
		unsigned int core_id, unsigned long *flags);
void venc_encode_unprepare(void *ctx_unprepare,
		unsigned int core_id, unsigned long *flags);
void venc_check_release_lock(void *ctx_check);
int venc_lock(void *ctx_lock, int core_id, bool sec);
void venc_unlock(void *ctx_unlock, int core_id);

#endif
