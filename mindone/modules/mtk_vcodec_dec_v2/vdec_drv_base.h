/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VDEC_DRV_BASE_
#define _VDEC_DRV_BASE_

#include "mtk_vcodec_drv.h"

#include "vdec_drv_if.h"

struct vdec_common_if {

	int (*init)(struct mtk_vcodec_ctx *ctx, unsigned long *h_vdec);

	int (*decode)(unsigned long h_vdec, struct mtk_vcodec_mem *bs,
				  struct vdec_fb *fb, unsigned int *src_chg);

	int (*get_param)(unsigned long h_vdec, enum vdec_get_param_type type,
					 void *out);

	int (*set_param)(unsigned long h_vdec, enum vdec_set_param_type type,
					 void *in);

	void (*deinit)(unsigned long h_vdec);
};

#endif
