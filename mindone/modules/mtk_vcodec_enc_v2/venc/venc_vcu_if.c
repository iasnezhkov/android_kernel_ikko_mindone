/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <media/v4l2-mem2mem.h>
#include <linux/mtk_vcu_controls.h>
#include <linux/delay.h>
#include "mtk_vcu.h"
#include "venc_vcu_if.h"
#include "venc_drv_if.h"
#include "mtk_vcodec_caps.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_enc_pm.h"
#include "mtk_vcodec_enc.h"
#include "mtk_heap.h"

static void handle_enc_init_msg(struct venc_vcu_inst *vcu, void *data)
{
	struct venc_vcu_ipi_msg_init *msg = data;

	if (vcu == NULL)
		return;
	vcu->inst_addr = msg->vcu_inst_addr;
	vcu->vsi = VCU_FPTR(vcu_mapping_dm_addr)(vcu->dev,
						 msg->vcu_inst_addr, sizeof(struct venc_vsi));
	if (!vcu->vsi)
		msg->status = VENC_IPI_MSG_STATUS_FAIL;
}

static void handle_query_cap_ack_msg(struct venc_vcu_inst *vcu,
	struct venc_vcu_ipi_query_cap_ack *msg)
{
	void *data, *copy, *dest;
	size_t size;
	bool valid = false;

	if (!vcu || msg->status)
		return;
	switch (msg->id) {
	case GET_PARAM_VENC_CAP_SUPPORTED_FORMATS:
		dest = &mtk_venc_formats;
		size = sizeof(mtk_venc_formats);
		break;
	case GET_PARAM_VENC_CAP_FRAME_SIZES:
		dest = &mtk_venc_framesizes;
		size = sizeof(mtk_venc_framesizes);
		break;
	default:
		msg->status = -EINVAL;
		return;
	}
	data = VCU_FPTR(vcu_mapping_dm_addr)(vcu->dev,
					     msg->vcu_data_addr, size);
	if (!data) {
		msg->status = -EINVAL;
		return;
	}
	copy = kmemdup(data, size, GFP_KERNEL);
	if (!copy) {
		msg->status = -ENOMEM;
		return;
	}
	switch (msg->id) {
	case GET_PARAM_VENC_CAP_SUPPORTED_FORMATS:
		valid = mtk_vcodec_formats_valid(copy,
						 ARRAY_SIZE(mtk_venc_formats), MTK_FMT_ENC,
						 ARRAY_SIZE(vcu->ctx->q_data[0].sizeimage));
		break;
	case GET_PARAM_VENC_CAP_FRAME_SIZES:
		valid = mtk_vcodec_framesizes_valid(copy,
						    ARRAY_SIZE(mtk_venc_framesizes));
		break;
	}
	if (valid)
		memcpy(dest, copy, size);
	else
		msg->status = -EINVAL;
	kfree(copy);
}

static void handle_enc_waitisr_msg(struct venc_vcu_inst *vcu,
	void *data, uint32_t timeout)
{
	struct venc_vcu_ipi_msg_waitisr *msg = data;
	struct mtk_vcodec_ctx *ctx = vcu->ctx;

	msg->irq_status = ctx->irq_status;
	msg->timeout = timeout;
}

static int check_codec_id(struct venc_vcu_ipi_msg_common *msg, unsigned int fmt)
{
	int codec_id = 0, ret = 0;

	switch (fmt) {
	case V4L2_PIX_FMT_H264:
		codec_id = VENC_H264;
		break;
	case V4L2_PIX_FMT_VP8:
		codec_id = VENC_VP8;
		break;
	case V4L2_PIX_FMT_MPEG4:
		codec_id = VENC_MPEG4;
		break;
	case V4L2_PIX_FMT_H263:
		codec_id = VENC_H263;
		break;
	case V4L2_PIX_FMT_H265:
	case V4L2_PIX_FMT_HEVC:
		codec_id = VENC_H265;
		break;
	case V4L2_PIX_FMT_HEIF:
		codec_id = VENC_HEIF;
		break;
	default:
		pr_info("%s fourcc not supported", __func__);
		break;
	}

	if (codec_id == 0) {
		mtk_v4l2_err("[error] venc unsupported fourcc\n");
		ret = -1;
	} else if (msg->codec_id == codec_id) {
		pr_info("%s ipi id %d is correct\n", __func__, msg->codec_id);
		ret = 0;
	} else {
		mtk_v4l2_debug(2, "[Info] ipi id %d is incorrect\n", msg->codec_id);
		ret = -1;
	}

	return ret;
}

static int venc_vcu_msg_size(unsigned int id)
{
	switch (id) {
	case VCU_IPIMSG_ENC_INIT_DONE:
		return sizeof(struct venc_vcu_ipi_msg_init);
	case VCU_IPIMSG_ENC_QUERY_CAP_DONE:
		return sizeof(struct venc_vcu_ipi_query_cap_ack);
	case VCU_IPIMSG_ENC_SET_PARAM_DONE:
		return sizeof(struct venc_vcu_ipi_msg_set_param);
	case VCU_IPIMSG_ENC_ENCODE_DONE:
		return sizeof(struct venc_vcu_ipi_msg_enc);
	case VCU_IPIMSG_ENC_DEINIT_DONE:
		return sizeof(struct venc_vcu_ipi_msg_deinit);
	case VCU_IPIMSG_ENC_WAIT_ISR:
		return sizeof(struct venc_vcu_ipi_msg_waitisr);
	case VCU_IPIMSG_ENC_GET_BS_BUFFER:
		return sizeof(struct venc_vcu_ipi_msg_get_bs);
	default:
		return sizeof(struct venc_vcu_ipi_msg_common);
	}
}

static int handle_enc_get_bs_buf(struct venc_vcu_inst *vcu, void *data)
{
	struct venc_vcu_ipi_msg_get_bs *msg = data;
	struct mtk_vcodec_ctx *ctx = vcu->ctx;
	struct mtk_video_enc_buf *flush = ctx->enc_flush_buf;
	struct venc_vsi *vsi = vcu->vsi;
	struct mtk_vcodec_mem *bs;
	long ret;

	if (!vsi || !flush)
		return -EINVAL;
	ret = wait_event_interruptible_timeout(flush->bs_wq,
		READ_ONCE(flush->bs_stopping) ||
		v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx),
		msecs_to_jiffies(1000));
	if (ret <= 0 || READ_ONCE(flush->bs_stopping)) {
		msg->status = VENC_IPI_MSG_STATUS_FAIL;
		msg->bs_addr = 0;
		msg->bs_size = 0;
		return 1;
	}
	bs = mtk_vcodec_get_bs(ctx);
	if (!bs) {
		msg->status = VENC_IPI_MSG_STATUS_FAIL;
		msg->bs_addr = 0;
		msg->bs_size = 0;
		return 1;
	}
	vsi->venc.venc_bs_va = bs->index + 1;
	vsi->venc.bs_addr = bs->dma_addr;
	vsi->venc.bs_dma = bs->dma_addr;
	msg->bs_addr = bs->dma_addr;
	msg->bs_size = bs->size;
	bs->buf_fd = msg->bs_fd;
	msg->status = VENC_IPI_MSG_STATUS_OK;
	return 1;
}

static int vcu_enc_ipi_handler_locked(void *data, unsigned int len, void *priv)
{
	struct mtk_vcodec_dev *dev = (struct mtk_vcodec_dev *)priv;
	struct venc_vcu_ipi_msg_common *msg = data;
	int msg_ctx_id;
	struct venc_vcu_inst *vcu;
	struct venc_inst *inst = NULL;
	struct mtk_vcodec_ctx *ctx;
	int ret = 0;
	unsigned int core_id;
	unsigned long flags;
	struct task_struct *task = NULL;
	struct list_head *p, *q;
	struct mtk_vcodec_ctx *temp_ctx;
	int msg_valid = 0;
	int lock = -1;
	int size;

	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_msg_init) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_query_cap) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_msg_set_param) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_msg_enc) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_msg_deinit) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(
		sizeof(struct venc_vcu_ipi_query_cap_ack) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_common) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_init) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(
		sizeof(struct venc_vcu_ipi_msg_set_param) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_enc) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_deinit) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_waitisr) > SHARE_BUF_SIZE);

	BUILD_BUG_ON(sizeof(struct venc_vcu_ipi_msg_init) != 32);
	BUILD_BUG_ON(offsetof(struct venc_vcu_ipi_msg_init, status) != 16);
	BUILD_BUG_ON(offsetof(struct venc_vcu_ipi_msg_init, vcu_inst_addr) != 24);
	BUILD_BUG_ON(sizeof(struct venc_ap_ipi_msg_set_param) != 64);
	BUILD_BUG_ON(offsetof(struct venc_ap_ipi_msg_set_param, vcu_inst_addr) != 8);
	BUILD_BUG_ON(offsetof(struct venc_ap_ipi_msg_set_param, param_id) != 16);
	BUILD_BUG_ON(AP_IPIMSG_ENC_QUERY_CAP != 0x1100);
	BUILD_BUG_ON(offsetof(struct mtk_hdr_dynamic_info, reserved) != 52);
	BUILD_BUG_ON(sizeof(struct mtk_hdr_dynamic_info) != 56);
	if (!dev || !data || len < sizeof(struct venc_vcu_ipi_msg_deinit) ||
	    len > SHARE_BUF_SIZE)
		return -EINVAL;
	size = venc_vcu_msg_size(msg->msg_id);
	if (len < size)
		return -EINVAL;

	VCU_FPTR(vcu_get_task)(&task, 0);
	if (msg == NULL || task == NULL ||
	   task->tgid != current->tgid ||
	   msg->ap_inst_addr == 0) {
		VCU_FPTR(vcu_put_task)();
		ret = -EINVAL;
		return ret;
	}
	VCU_FPTR(vcu_put_task)();

	if (msg->ap_inst_addr > INT_MAX)
		return -EINVAL;
	msg_ctx_id = (int)msg->ap_inst_addr;

	mutex_lock(&dev->ctx_mutex);
	msg_valid = 0;
	list_for_each_safe(p, q, &dev->ctx_list) {
		temp_ctx = list_entry(p, struct mtk_vcodec_ctx, list);
		inst = (struct venc_inst *)temp_ctx->drv_handle;
		if (inst != NULL && msg_ctx_id == temp_ctx->id) {
			vcu = &inst->vcu_inst;
			msg_valid = 1;
			break;
		}
	}
	if (!msg_valid) {
		mtk_v4l2_err(" msg msg_id %X vcu not exist %d\n",
			msg->msg_id, msg_ctx_id);
		mutex_unlock(&dev->ctx_mutex);
		ret = -EINVAL;
		return ret;
	}
	mutex_unlock(&dev->ctx_mutex);

	if (vcu->daemon_pid != current->tgid) {
		pr_info("%s, vcu->daemon_pid:%d != current %d\n",
			__func__, vcu->daemon_pid, current->tgid);
		return 1;
	}

	mtk_vcodec_debug(vcu, "msg_id %x inst %p status %d",
					 msg->msg_id, vcu, msg->status);

	if (vcu->abort || vcu->ctx == NULL)
		return -EINVAL;

	ctx = vcu->ctx;
	if (ctx == NULL)
		return -EINVAL;
	switch (msg->msg_id) {
	case VCU_IPIMSG_ENC_INIT_DONE:
		if (!msg->status)
			handle_enc_init_msg(vcu, data);
		break;
	case VCU_IPIMSG_ENC_SET_PARAM_DONE:

		current->flags |= PF_NOFREEZE;
		break;
	case VCU_IPIMSG_ENC_DEINIT_DONE:
		break;
	case VCU_IPIMSG_ENC_POWER_ON:
		if (msg->status < 0 || msg->status >= MTK_VENC_CORE_2)
			return -EINVAL;

		VCU_FPTR(vcu_get_gce_lock)(vcu->dev, VCU_VENC);
		while (lock != 0) {
			lock = venc_lock(ctx, 0, true);
			if (lock != 0) {
				VCU_FPTR(vcu_put_gce_lock)(vcu->dev, VCU_VENC);
				usleep_range(1000, 2000);
				VCU_FPTR(vcu_get_gce_lock)(vcu->dev, VCU_VENC);
			}
		}
		venc_encode_prepare(ctx, msg->status, &flags);
		VCU_FPTR(vcu_put_gce_lock)(vcu->dev, VCU_VENC);
		msg->status = VENC_IPI_MSG_STATUS_OK;
		ret = 1;
		break;
	case VCU_IPIMSG_ENC_POWER_OFF:
		if (msg->status < 0 || msg->status >= MTK_VENC_CORE_2)
			return -EINVAL;

		VCU_FPTR(vcu_get_gce_lock)(vcu->dev, VCU_VENC);
		venc_encode_unprepare(ctx, msg->status, &flags);
		venc_unlock(ctx, 0);
		VCU_FPTR(vcu_put_gce_lock)(vcu->dev, VCU_VENC);
		msg->status = VENC_IPI_MSG_STATUS_OK;
		ret = 1;
		break;
	case VCU_IPIMSG_ENC_QUERY_CAP_DONE:
		handle_query_cap_ack_msg(vcu, data);
		break;
	case VCU_IPIMSG_ENC_WAIT_ISR:
		core_id = msg->status;
		if (core_id >= MTK_VENC_CORE_2)
			return -EINVAL;
		if (core_id == MTK_VENC_CORE_0)
			vcodec_trace_count("VENC_HW_CORE_0", 2);
		else
			vcodec_trace_count("VENC_HW_CORE_1", 2);

		if (-1 == mtk_vcodec_wait_for_done_ctx(ctx, core_id,
			MTK_INST_IRQ_RECEIVED,
			WAIT_INTR_TIMEOUT_MS)) {
			handle_enc_waitisr_msg(vcu, data, 1);
			mtk_vcodec_debug(vcu,
				"irq_status %x <-", ctx->irq_status);
		} else
			handle_enc_waitisr_msg(vcu, data, 0);

		if (core_id == MTK_VENC_CORE_0)
			vcodec_trace_count("VENC_HW_CORE_0", 1);
		else
			vcodec_trace_count("VENC_HW_CORE_1", 1);

		ret = 1;
		break;
	case VCU_IPIMSG_ENC_PUT_BUFFER:
		mtk_enc_put_buf(ctx);
		ret = 1;
		break;
	case VCU_IPIMSG_ENC_ENCODE_DONE: {
		struct venc_vcu_ipi_msg_enc *ack = data;

		vcu->bs_size = ack->bs_size;
		vcu->is_key_frm = ack->is_key_frm;
		break;
	}
	case VCU_IPIMSG_ENC_GET_BS_BUFFER:
		ret = handle_enc_get_bs_buf(vcu, data);
		break;
	case VCU_IPIMSG_ENC_CHECK_CODEC_ID:
		if (check_codec_id(msg, ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc) == 0)
			msg->status = 0;
		else
			msg->status = -1;
		ret = 1;
		break;
	default:
		mtk_vcodec_err(vcu, "unknown msg id %x", msg->msg_id);
		return -EINVAL;
	}

	if (!ret && msg->msg_id != VCU_IPIMSG_ENC_DEINIT_DONE) {
		vcu->signaled = 1;
		vcu->failure = (msg->status != VENC_IPI_MSG_STATUS_OK);
	}

	mtk_vcodec_debug_leave(vcu);
	return ret;
}

int vcu_enc_ipi_handler(void *data, unsigned int len, void *priv)
{
	struct mtk_vcodec_dev *dev = priv;
	int ret;

	if (!dev)
		return -EINVAL;
	mutex_lock(&dev->ipi_dispatch_lock);
	ret = vcu_enc_ipi_handler_locked(data, len, priv);
	mutex_unlock(&dev->ipi_dispatch_lock);
	return ret;
}

static int vcu_enc_send_msg(struct venc_vcu_inst *vcu, void *msg,
							int len)
{
	int status;
	struct task_struct *task = NULL;
	unsigned int suspend_block_cnt = 0;

	mtk_vcodec_debug_enter(vcu);

	if (!vcu->dev || !vcu->ctx) {
		mtk_vcodec_err(vcu, "inst dev is NULL");
		return -EINVAL;
	}

	if (vcu->abort)
		return -EIO;
	if (vcu->ctx == NULL)
		return -EINVAL;
	while (vcu->ctx->dev->is_codec_suspending == 1) {
		suspend_block_cnt++;
		if (suspend_block_cnt > SUSPEND_TIMEOUT_CNT) {
			mtk_v4l2_debug(4, "VENC blocked by suspend\n");
			suspend_block_cnt = 0;
		}
		usleep_range(10000, 20000);
	}

	VCU_FPTR(vcu_get_task)(&task, 0);
	if (task == NULL ||
		vcu->daemon_pid != task->tgid) {
		if (task)
			mtk_vcodec_err(vcu, "send fail pid: inst %d curr %d",
				vcu->daemon_pid, task->tgid);
		VCU_FPTR(vcu_put_task)();
		vcu->abort = 1;
		return -EIO;
	}
	VCU_FPTR(vcu_put_task)();

	status = VCU_FPTR(vcu_ipi_send)(vcu->dev, vcu->id, msg, len, vcu->ctx->dev);
	if (status) {
		mtk_vcodec_err(vcu, "vcu_ipi_send msg_id %x len %d fail %d",
					   *(uint32_t *)msg, len, status);
		if (status == -EIO)
			vcu->abort = 1;
		return status;
	}
	else
		mtk_vcodec_debug(vcu, "vcu_ipi_send msg_id %x len %d success",
				*(uint32_t *)msg, len);
	if (vcu->failure)
		return -EINVAL;

	mtk_vcodec_debug_leave(vcu);

	return 0;
}

void vcu_enc_set_pid(struct venc_vcu_inst *vcu)
{
	struct task_struct *task = NULL;

	VCU_FPTR(vcu_get_task)(&task, 0);
	if (task != NULL)
		vcu->daemon_pid = task->tgid;
	else
		vcu->daemon_pid = -1;
	VCU_FPTR(vcu_put_task)();
}

int vcu_enc_set_ctx(struct venc_vcu_inst *vcu,
	struct venc_frm_buf *frm_buf,
	struct mtk_vcodec_mem *bs_buf)
{
	int err = 0;
	struct vb2_buffer *src_vb = NULL;
	struct vb2_buffer *dst_vb = NULL;
	struct mtk_video_enc_buf *temp;

	if (frm_buf != NULL) {
		temp = container_of(frm_buf, struct mtk_video_enc_buf, frm_buf);
		src_vb = &temp->vb.vb2_buf;
	}
	if (bs_buf != NULL) {
		temp = container_of(bs_buf, struct mtk_video_enc_buf, bs_buf);
		dst_vb = &temp->vb.vb2_buf;
	}
	VCU_FPTR(vcu_set_codec_ctx)(vcu->dev,
		(void *)vcu->ctx,
		src_vb, dst_vb,
		VCU_VENC);

	return err;
}

int vcu_enc_clear_ctx(struct venc_vcu_inst *vcu)
{
	int err = 0;

	VCU_FPTR(vcu_clear_codec_ctx)(vcu->dev,
		(void *)vcu->ctx, VCU_VENC);

	return err;
}

int vcu_enc_init(struct venc_vcu_inst *vcu)
{
	int status;
	struct venc_ap_ipi_msg_init out;

	mtk_vcodec_debug_enter(vcu);

	if (sizeof(out) > SHARE_BUF_SIZE) {
		mtk_vcodec_err(vcu, "venc_ap_ipi_msg_init cannot be large than %d",
					   SHARE_BUF_SIZE);
		return -EINVAL;
	}

	vcu->signaled = 0;
	vcu->failure = 0;
	VCU_FPTR(vcu_get_ctx_ipi_binding_lock)(vcu->dev, &vcu->ctx_ipi_lock, VCU_VENC);

	status = VCU_FPTR(vcu_ipi_register)(vcu->dev,
		vcu->id, vcu->handler, NULL, vcu->ctx->dev);
	if (status) {
		mtk_vcodec_err(vcu, "vcu_ipi_register fail %d", status);
		return -EINVAL;
	}

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_INIT;
	out.ap_inst_addr = vcu->ctx->id;
	out.ctx_id = vcu->ctx->id;

	vcu_enc_set_pid(vcu);
	status = vcu_enc_send_msg(vcu, &out, sizeof(out));

	if (status) {
		mtk_vcodec_err(vcu, "AP_IPIMSG_ENC_INIT fail");
		return -EINVAL;
	}
	mtk_vcodec_debug_leave(vcu);

	return 0;
}

int vcu_enc_query_cap(struct venc_vcu_inst *vcu, unsigned int id, void *out)
{
	struct venc_ap_ipi_query_cap msg;
	int err = 0;

	if (sizeof(msg) > SHARE_BUF_SIZE) {
		mtk_vcodec_err(vcu, "venc_ap_ipi_query_cap cannot be large than %d",
					   SHARE_BUF_SIZE);
		return -EINVAL;
	}

	mtk_vcodec_debug(vcu, "+ id=%X", AP_IPIMSG_ENC_QUERY_CAP);
	if(vcu == NULL || vcu->ctx == NULL)
		return -EINVAL;
	vcu->dev = VCU_FPTR(vcu_get_plat_device)(vcu->ctx->dev->plat_dev);
	if (vcu->dev  == NULL) {
		mtk_vcodec_err(vcu, "vcu device in not ready");
		return -EPROBE_DEFER;
	}

	vcu->id = (vcu->id == IPI_VCU_INIT) ? IPI_VENC_COMMON : vcu->id;
	vcu->handler = vcu_enc_ipi_handler;

	err = VCU_FPTR(vcu_ipi_register)(vcu->dev,
		vcu->id, vcu->handler, NULL, vcu->ctx->dev);
	if (err != 0) {
		mtk_vcodec_err(vcu, "vcu_ipi_register fail status=%d", err);
		return err;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_ENC_QUERY_CAP;
	msg.id = id;
	msg.ap_inst_addr = vcu->ctx->id;
	msg.ctx_id = vcu->ctx->id;

	vcu_enc_set_pid(vcu);
	err = vcu_enc_send_msg(vcu, &msg, sizeof(msg));
	mtk_vcodec_debug(vcu, "- id=%X ret=%d", msg.msg_id, err);

	return err;
}

int vcu_enc_set_param(struct venc_vcu_inst *vcu,
					  enum venc_set_param_type id,
					  struct venc_enc_param *enc_param)
{
	struct venc_ap_ipi_msg_set_param out;

	mtk_vcodec_debug(vcu, "id %d ->", id);
	if (sizeof(out) > SHARE_BUF_SIZE) {
		mtk_vcodec_err(vcu, "venc_ap_ipi_msg_set_param cannot be large than %d",
					   SHARE_BUF_SIZE);
		return -EINVAL;
	}

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_SET_PARAM;
	out.vcu_inst_addr = vcu->inst_addr;
	out.ctx_id = vcu->ctx->id;
	out.param_id = id;
	switch (id) {
	case VENC_SET_PARAM_ENC:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_FORCE_INTRA:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_ADJUST_BITRATE:
		out.data_item = 1;
		out.data[0] = enc_param->bitrate;
		break;
	case VENC_SET_PARAM_ADJUST_FRAMERATE:
		out.data_item = 1;
		out.data[0] = enc_param->frm_rate;
		break;
	case VENC_SET_PARAM_GOP_SIZE:
		out.data_item = 1;
		out.data[0] = enc_param->gop_size;
		break;
	case VENC_SET_PARAM_INTRA_PERIOD:
		out.data_item = 1;
		out.data[0] = enc_param->intra_period;
		break;
	case VENC_SET_PARAM_SKIP_FRAME:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_PREPEND_HEADER:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_SCENARIO:
		out.data_item = 1;
		out.data[0] = enc_param->scenario;
		break;
	case VENC_SET_PARAM_NONREFP:
		out.data_item = 1;
		out.data[0] = enc_param->nonrefp;
		break;
	case VENC_SET_PARAM_NONREFPFREQ:
		out.data_item = 1;
		out.data[0] = enc_param->nonrefpfreq;
		break;
	case VENC_SET_PARAM_DETECTED_FRAMERATE:
		out.data_item = 1;
		out.data[0] = enc_param->detectframerate;
		break;
	case VENC_SET_PARAM_RFS_ON:
		out.data_item = 1;
		out.data[0] = enc_param->rfs;
		break;
	case VENC_SET_PARAM_PREPEND_SPSPPS_TO_IDR:
		out.data_item = 1;
		out.data[0] = enc_param->prependheader;
		break;
	case VENC_SET_PARAM_OPERATION_RATE:
		out.data_item = 1;
		out.data[0] = enc_param->operationrate;
		break;
	case VENC_SET_PARAM_BITRATE_MODE:
		out.data_item = 1;
		out.data[0] = enc_param->bitratemode;
		break;
	case VENC_SET_PARAM_ROI_ON:
		out.data_item = 1;
		out.data[0] = enc_param->roion;
		break;
	case VENC_SET_PARAM_HEIF_GRID_SIZE:
		out.data_item = 1;
		out.data[0] = enc_param->heif_grid_size;
		break;
	case VENC_SET_PARAM_COLOR_DESC:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_SEC_MODE:
		out.data_item = 1;
		out.data[0] = enc_param->svp_mode;
		break;
	case VENC_SET_PARAM_TSVC:
		out.data_item = 1;
		out.data[0] = enc_param->tsvc;
		break;
	case VENC_SET_PARAM_ENABLE_HIGHQUALITY:
		out.data_item = 1;
		out.data[0] = enc_param->highquality;
		break;
	case VENC_SET_PARAM_ADJUST_MAX_QP:
		out.data_item = 3;
		out.data[0] = enc_param->max_qp;
		out.data[1] = enc_param->max_qp;
		out.data[2] = enc_param->max_qp;
		break;
	case VENC_SET_PARAM_ADJUST_MIN_QP:
		out.data_item = 3;
		out.data[0] = enc_param->min_qp;
		out.data[1] = enc_param->min_qp;
		out.data[2] = enc_param->min_qp;
		break;
	case VENC_SET_PARAM_ADJUST_I_P_QP_DELTA:
		out.data_item = 1;
		out.data[0] = enc_param->ip_qpdelta;
		break;
	case VENC_SET_PARAM_ADJUST_QP_CONTROL_MODE:
		out.data_item = 1;
		out.data[0] = enc_param->qp_control_mode;
		break;
	case VENC_SET_PARAM_ADJUST_FRAME_LEVEL_QP:
		out.data_item = 1;
		out.data[0] = enc_param->framelvl_qp;
		break;
	case VENC_SET_PARAM_ENABLE_DUMMY_NAL:
		out.data_item = 1;
		out.data[0] = enc_param->dummynal;
		break;
	default:
		mtk_vcodec_err(vcu, "id %d not supported", id);
		return -EINVAL;
	}

	if (vcu_enc_send_msg(vcu, &out, sizeof(out))) {
		mtk_vcodec_err(vcu,
			"AP_IPIMSG_ENC_SET_PARAM %d fail", id);
		return -EINVAL;
	}

	mtk_vcodec_debug(vcu, "id %d <-", id);

	return 0;
}

int vcu_enc_encode(struct venc_vcu_inst *vcu, unsigned int bs_mode,
				   struct venc_frm_buf *frm_buf,
				   struct mtk_vcodec_mem *bs_buf,
				   unsigned int *bs_size)
{

	struct venc_ap_ipi_msg_enc out;
	struct venc_vsi *vsi = (struct venc_vsi *)vcu->vsi;
	unsigned int i, ret;

	mtk_vcodec_debug(vcu, "bs_mode %d ->", bs_mode);

	if (sizeof(out) > SHARE_BUF_SIZE) {
		mtk_vcodec_err(vcu, "venc_ap_ipi_msg_enc cannot be large than %d",
					   SHARE_BUF_SIZE);
		return -EINVAL;
	}

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_ENCODE;
	out.vcu_inst_addr = vcu->inst_addr;
	out.ctx_id = vcu->ctx->id;
	out.bs_mode = bs_mode;
	memset(vsi->venc.input_addr, 0, sizeof(vsi->venc.input_addr));
	memset(vsi->venc.fb_dma, 0, sizeof(vsi->venc.fb_dma));
	vsi->venc.bs_addr = 0;
	vsi->venc.bs_dma = 0;
	vsi->meta_addr = 0;
	vsi->meta_size = 0;
	vsi->meta_offset = 0;
	vsi->qpmap_addr = 0;
	vsi->qpmap_size = 0;
	vsi->is_aosp_qpmap = 0;
	vsi->adab_addr = 0;
	vsi->adab_size = 0;
	vsi->qprects_addr = 0;
	vsi->qprects_size = 0;
	vsi->qprects_meta_size = 0;
	vsi->dynamicparams_addr = 0;
	vsi->dynamicparams_size = 0;
	vsi->dynamicparams_offset = 0;
	vsi->general_buf_dma = 0;
	vsi->general_buf_fd = -1;
	vsi->general_buf_size = 0;
	if (frm_buf) {
		if (frm_buf->num_planes > ARRAY_SIZE(vsi->venc.input_addr))
			return -EINVAL;
		out.fb_num_planes = frm_buf->num_planes;
		for (i = 0; i < frm_buf->num_planes; i++) {
			vsi->venc.input_addr[i] =
				frm_buf->fb_addr[i].dma_addr;
			vsi->venc.fb_dma[i] =
				frm_buf->fb_addr[i].dma_addr;
			out.input_size[i] =
				frm_buf->fb_addr[i].size;
			out.data_offset[i] =
				frm_buf->fb_addr[i].data_offset;
		}
		if (frm_buf->has_meta) {
			if (!frm_buf->meta_dma ||
			    frm_buf->meta_offset > frm_buf->meta_dma->size ||
			    offsetof(struct mtk_hdr_dynamic_info, reserved) >
			    frm_buf->meta_dma->size - frm_buf->meta_offset)
				return -EINVAL;
			vsi->meta_addr = frm_buf->meta_addr;
			vsi->meta_size = offsetof(struct mtk_hdr_dynamic_info, reserved);
			vsi->meta_offset = frm_buf->meta_offset;
		} else {
			vsi->meta_size = 0;
			vsi->meta_addr = 0;
		}



		if (frm_buf->has_qpmap) {
			vsi->qpmap_addr = frm_buf->qpmap_dma_addr;
			vsi->qpmap_size = frm_buf->qpmap_dma->size;
		} else {
			vsi->qpmap_addr = 0;
			vsi->qpmap_size = 0;
		}

		mtk_vcodec_debug(vcu, " num_planes = %d input (dmabuf:%lx), size %d %llx",
			frm_buf->num_planes,
			(unsigned long)frm_buf->fb_addr[0].dmabuf,
			vsi->meta_size,
			vsi->meta_addr);
		mtk_vcodec_debug(vcu, "vsi qpmap addr %llx size%d",
			vsi->meta_addr, vsi->qpmap_size);
	}
	else {
		mtk_vcodec_debug(vcu, "frm_buf is null");
	}

	if (bs_buf) {
		vsi->venc.bs_addr = bs_buf->dma_addr;
		vsi->venc.bs_dma = bs_buf->dma_addr;
		out.bs_size = bs_buf->size;

		if (vsi->config.svp_mode) {
			out.sec_mem_handle = dmabuf_to_secure_handle(bs_buf->dmabuf);
			pr_info("%s %d out.sec_mem_handle 0x%x", __func__,
				 __LINE__, out.sec_mem_handle);
		}

		mtk_vcodec_debug(vcu, " output (dma:%lx)",
			(unsigned long)bs_buf->dmabuf);
	}
	else {
		mtk_vcodec_debug(vcu, "bs_buf is null");
	}

	vsi->config.sysram_enable = vcu->ctx->use_slbc;
	mutex_lock(vcu->ctx_ipi_lock);
	vcu_enc_set_ctx(vcu, frm_buf, bs_buf);
	ret = vcu_enc_send_msg(vcu, &out, sizeof(out));
	mutex_unlock(vcu->ctx_ipi_lock);

	if (ret) {
		mtk_vcodec_err(vcu, "AP_IPIMSG_ENC_ENCODE %d fail %d",
					   bs_mode, ret);
		return ret;
	}
	else{
		mtk_vcodec_debug(vcu, " vcu_enc_send_msg done");
	}

	mtk_vcodec_debug(vcu, "bs_mode %d size %d key_frm %d <-",
		bs_mode, vcu->bs_size, vcu->is_key_frm);

	return 0;
}

int vcu_enc_deinit(struct venc_vcu_inst *vcu)
{
	struct venc_ap_ipi_msg_deinit out;
	int ret = 0;

	mtk_vcodec_debug_enter(vcu);
	if(vcu == NULL)
		return -EINVAL;
	if (sizeof(out) > SHARE_BUF_SIZE) {
		mtk_vcodec_err(vcu, "venc_ap_ipi_msg_deint cannot be large than %d",
					   SHARE_BUF_SIZE);
		return -EINVAL;
	}

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_DEINIT;
	out.vcu_inst_addr = vcu->inst_addr;
	out.ctx_id = vcu->ctx->id;

	mutex_lock(vcu->ctx_ipi_lock);
	ret = vcu_enc_send_msg(vcu, &out, sizeof(out));
	vcu_enc_clear_ctx(vcu);
	mutex_unlock(vcu->ctx_ipi_lock);
	current->flags &= ~PF_NOFREEZE;

	mtk_vcodec_debug_leave(vcu);

	return ret;
}
