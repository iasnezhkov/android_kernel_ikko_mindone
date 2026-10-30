/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VENC_DRV_BASE_
#define _VENC_DRV_BASE_

#include "mtk_vcodec_drv.h"

#include "venc_drv_if.h"

struct venc_common_if {

	int (*init)(struct mtk_vcodec_ctx *ctx, unsigned long *handle);

	int (*encode)(unsigned long handle, enum venc_start_opt opt,
				  struct venc_frm_buf *frm_buf,
				  struct mtk_vcodec_mem *bs_buf,
				  struct venc_done_result *result);

	int (*get_param)(unsigned long handle, enum venc_get_param_type type,
					 void *out);

	int (*set_param)(unsigned long handle, enum venc_set_param_type type,
					 struct venc_enc_param *in);

	int (*deinit)(unsigned long handle);
};

#endif
