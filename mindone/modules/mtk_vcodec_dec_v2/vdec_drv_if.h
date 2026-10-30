/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VDEC_DRV_IF_H_
#define _VDEC_DRV_IF_H_

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_util.h"
#include "vdec_vcu_if.h"
#include "vdec_legacy_if.h"
#include "vdec_ipi_msg.h"

struct vdec_inst {
	unsigned int num_nalu;
	struct mtk_vcodec_ctx *ctx;
	struct vdec_vcu_inst vcu;
	struct vdec_vsi *vsi;
};

struct vdec_fb_node {
	struct list_head list;
	struct vdec_fb *fb;
};

extern struct mtk_video_fmt
	mtk_vdec_formats[MTK_MAX_DEC_CODECS_SUPPORT];
extern struct mtk_codec_framesizes
	mtk_vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT];

extern struct v4l2_vdec_max_buf_info mtk_vdec_max_buf_info;
extern struct mtk_video_frame_frameintervals mtk_vdec_frameintervals;

int vdec_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc);

void vdec_if_deinit(struct mtk_vcodec_ctx *ctx);

int vdec_if_decode(struct mtk_vcodec_ctx *ctx, struct mtk_vcodec_mem *bs,
				   struct vdec_fb *fb, unsigned int *src_chg);

int vdec_if_get_param(struct mtk_vcodec_ctx *ctx, enum vdec_get_param_type type,
					  void *out);

int vdec_if_set_param(struct mtk_vcodec_ctx *ctx,
					  enum vdec_set_param_type type,
					  void *in);

void vdec_decode_prepare(void *ctx_prepare,
	unsigned int hw_id);
void vdec_decode_unprepare(void *ctx_unprepare,
	unsigned int hw_id);
void vdec_check_release_lock(void *ctx_check);

#endif
