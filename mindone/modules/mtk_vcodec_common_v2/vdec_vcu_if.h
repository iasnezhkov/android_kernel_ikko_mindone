/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef _VDEC_VCU_IF_H_
#define _VDEC_VCU_IF_H_

#include <linux/dma-buf.h>
#include "mtk_vcu.h"
#include "vdec_ipi_msg.h"

struct vdec_vcu_inst {
	wait_queue_head_t wq;
	wait_queue_head_t wq_res;
	enum ipi_id id;
	void *vsi;
	int32_t failure;
	uint64_t inst_addr;
	unsigned int signaled;
	unsigned int signaled_res;
	bool in_ipi;
	bool in_res_ipi;
	struct mtk_vcodec_ctx *ctx;
	struct platform_device *dev;
	ipi_handler_t handler;
	bool init_done;
	bool abort;
	int daemon_pid;
	struct mutex *ctx_ipi_lock;
	struct list_head bufs;
};

int vcu_dec_init(struct vdec_vcu_inst *vcu);

int vcu_dec_start(struct vdec_vcu_inst *vcu,
	uint32_t *data, unsigned int len,
	struct mtk_vcodec_mem *bs, struct vdec_fb *fb);

int vcu_dec_deinit(struct vdec_vcu_inst *vcu);

int vcu_dec_reset(struct vdec_vcu_inst *vcu, enum vdec_reset_type drain_type);

int vcu_dec_ipi_handler(void *data, unsigned int len, void *priv);
int vcu_dec_query_cap(struct vdec_vcu_inst *vcu, unsigned int id, void *out);
int vcu_dec_set_param(struct vdec_vcu_inst *vcu, unsigned int id,
					  void *param, unsigned int size);
int vcu_dec_set_frame_buffer(struct vdec_vcu_inst *vcu, void *fb);

#endif
