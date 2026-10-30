/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#include "../venc_drv_base.h"

const struct venc_common_if *get_enc_vcp_if(void);

const struct venc_common_if *get_enc_vcp_if(void)
{
	return NULL;
}

void venc_vcp_probe(struct mtk_vcodec_dev *dev);

void venc_vcp_probe(struct mtk_vcodec_dev *dev)
{
}
