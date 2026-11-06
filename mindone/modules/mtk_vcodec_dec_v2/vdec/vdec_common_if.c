/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#include <linux/module.h>
#include <linux/slab.h>

#include "mtk_vcodec_intr.h"
#include "vdec_vcu_if.h"
#include "vdec_drv_base.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_drv.h"
#include "vdec_drv_if.h"

static void put_fb_to_free(struct vdec_inst *inst, struct vdec_fb *fb)
{
	struct ring_fb_list *list;

	if (fb != NULL) {
		list = &inst->vsi->list_free;
		if (list->count >= DEC_MAX_FB_NUM || list->write_idx >= DEC_MAX_FB_NUM) {
			mtk_vcodec_err(inst, "[FB] put fb free_list full");
			return;
		}

		mtk_vcodec_debug(inst,
						 "[FB] put fb into free_list @(%p, %llx)",
						 fb->fb_base[0].va,
						 (u64)fb->fb_base[1].dma_addr);

		list->fb_list[list->write_idx].vdec_fb_va = (u64)(fb->index + 1);
		list->write_idx = (list->write_idx == DEC_MAX_FB_NUM - 1U) ?
						  0U : list->write_idx + 1U;
		list->count++;
	}
}

static void get_pic_info(struct vdec_inst *inst,
						 struct vdec_pic_info *pic)
{
	if (inst == NULL)
		return;
	if (inst->vsi == NULL)
		return;

	memcpy(pic, &inst->vsi->pic, sizeof(struct vdec_pic_info));

	mtk_vcodec_debug(inst, "pic(%d, %d), buf(%d, %d), bitdepth = %d, fourcc = %d\n",
		pic->pic_w, pic->pic_h, pic->buf_w, pic->buf_h,
		pic->bitdepth, pic->fourcc);
	mtk_vcodec_debug(inst, "Y/C(%d, %d)", pic->fb_sz[0], pic->fb_sz[1]);
}

static void get_crop_info(struct vdec_inst *inst, struct v4l2_rect *r)
{
	if (inst == NULL)
		return;
	if (inst->vsi == NULL)
		return;

	r->left      = inst->vsi->crop.left;
	r->top       = inst->vsi->crop.top;
	r->width     = inst->vsi->crop.width;
	r->height    = inst->vsi->crop.height;
	mtk_vcodec_debug(inst, "l=%d, t=%d, w=%d, h=%d",
		r->left, r->top, r->width, r->height);
}

static void get_dpb_size(struct vdec_inst *inst, unsigned int *dpb_sz)
{
	if (inst == NULL)
		return;
	if (inst->vsi == NULL)
		return;

	*dpb_sz = inst->vsi->dec.dpb_sz + 1U;
	mtk_vcodec_debug(inst, "sz=%d", *dpb_sz);
}

static int vdec_init(struct mtk_vcodec_ctx *ctx, unsigned long *h_vdec)
{
	struct vdec_inst *inst = NULL;
	int err = 0;
	struct vcu_v4l2_callback_func cb;
	bool listed = false;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	if (!ctx) {
		err = -ENOMEM;
		goto error_free_inst;
	}

	inst->ctx = ctx;
	inst->vcu.id = IPI_VDEC_COMMON;
	inst->vcu.dev = VCU_FPTR(vcu_get_plat_device)(ctx->dev->plat_dev);
	if (inst->vcu.dev  == NULL) {
		mtk_vcodec_err(inst, "vcu device is not ready");
		err = -EPROBE_DEFER;
		goto error_free_inst;
	}

	inst->vcu.ctx = ctx;
	inst->vcu.handler = vcu_dec_ipi_handler;
	*h_vdec = (unsigned long)inst;

	mtk_vcodec_add_ctx_list(ctx);
	listed = true;

	err = vcu_dec_init(&inst->vcu);
	if (err != 0) {
		mtk_vcodec_err(inst, "%s err=%d", __func__, err);
		goto error_free_inst;
	}

	memset(&cb, 0, sizeof(struct vcu_v4l2_callback_func));
	cb.gce_timeout_dump = mtk_vcodec_gce_timeout_dump;
	cb.vdec_realease_lock = vdec_check_release_lock;
	VCU_FPTR(vcu_set_v4l2_callback)(inst->vcu.dev, &cb);

	inst->vsi = inst->vcu.vsi;
	if (!inst->vsi) {
		err = -EFAULT;
		goto error_free_inst;
	}
	inst->vsi->dec_params.svp_mode = ctx->dec_params.svp_mode;
	inst->vsi->dec_params.priority = ctx->dec_params.priority;
	inst->vsi->meta_buf_fd = -1;
	memset(&inst->vsi->hdr10plus_buf, 0, sizeof(inst->vsi->hdr10plus_buf));
	inst->vsi->hdr10_info_valid = 0;
	ctx->output_async = inst->vsi->output_async;
	ctx->input_driven = inst->vsi->input_driven;
	ctx->ipi_blocked = &inst->vsi->ipi_blocked;
	*(ctx->ipi_blocked) = 0;

	mtk_vcodec_debug(inst, "Decoder Instance >> %p", inst);

	return 0;

error_free_inst:
	if (listed)
		mtk_vcodec_del_ctx_list(ctx);
	kfree(inst);
	*h_vdec = (unsigned long)NULL;

	return err;
}

static void vdec_deinit(unsigned long h_vdec)
{
	struct vdec_inst *inst = (struct vdec_inst *)h_vdec;
	if (inst == NULL)
		return;
	mtk_vcodec_debug_enter(inst);

	vcu_dec_deinit(&inst->vcu);

	mtk_vcodec_del_ctx_list(inst->ctx);

	kfree(inst);
}

static int vdec_decode(unsigned long h_vdec, struct mtk_vcodec_mem *bs,
	struct vdec_fb *fb, unsigned int *src_chg)
{
	struct vdec_inst *inst = (struct vdec_inst *)h_vdec;
	struct vdec_vcu_inst *vcu = &inst->vcu;
	int ret = 0;
	unsigned int data[6];
	uint64_t fb_dma[VIDEO_MAX_PLANES] = { 0 };
	uint32_t num_planes;
	unsigned int i = 0;
	unsigned int bs_fourcc = inst->ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
	unsigned int fm_fourcc = inst->ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
	unsigned int *errormap_info = &inst->ctx->errormap_info[0];

	num_planes = fb ? inst->vsi->dec.fb_num_planes : 0U;
	if (num_planes > VIDEO_MAX_PLANES)
		return -EINVAL;

	for (i = 0; i < num_planes; i++)
		fb_dma[i] = (u64)fb->fb_base[i].dma_addr;

	mtk_vcodec_debug(inst, "+ [%d] FB y_dma=%llx c_dma=%llx va=%p num_planes %d",
		inst->num_nalu, fb_dma[0], fb_dma[1], fb, num_planes);

	if (bs == NULL) {
		inst->vsi->flush_type = FLUSH_BITSTREAM | FLUSH_FRAME;
		if (fb == NULL)
			return vcu_dec_reset(vcu, VDEC_FLUSH);
		else if (fb->status == 0)
			return vcu_dec_reset(vcu, VDEC_DRAIN);
		else
			return vcu_dec_reset(vcu, VDEC_DRAIN_EOS);
	}

	mtk_vcodec_debug(inst, "+ BS dma=0x%llx dmabuf=%p format=%c%c%c%c",
		(uint64_t)bs->dma_addr, bs->dmabuf, bs_fourcc & 0xFF,
		(bs_fourcc >> 8) & 0xFF, (bs_fourcc >> 16) & 0xFF,
		(bs_fourcc >> 24) & 0xFF);

	inst->vsi->dec.vdec_bs_va = (u64)(bs->index + 1);
	inst->vsi->dec.bs_dma = (uint64_t)bs->dma_addr;

	for (i = 0; i < num_planes; i++)
		inst->vsi->dec.fb_dma[i] = fb_dma[i];

	if (inst->vsi->input_driven == NON_INPUT_DRIVEN) {
		inst->vsi->dec.vdec_fb_va = (u64)0;
		inst->vsi->dec.index = 0xFF;
	}
	if (fb != NULL) {
		inst->vsi->dec.vdec_fb_va = (u64)(fb->index + 1);
		inst->vsi->dec.index = fb->index;
		fb->slice_done_count = 0;
		if (fb->dma_general_buf != 0) {
			inst->vsi->general_buf_fd = fb->general_buf_fd;
			inst->vsi->general_buf_size = fb->dma_general_buf->size;
			inst->vsi->general_buf_dma = fb->dma_general_addr;
			mtk_vcodec_debug(inst, "dma_general_buf dma_buf=%p fd=%d dma=%llx size=%lu",
			    fb->dma_general_buf, inst->vsi->general_buf_fd,
			    inst->vsi->general_buf_dma,
			    fb->dma_general_buf->size);
		} else {
			fb->general_buf_fd = -1;
			inst->vsi->general_buf_fd = -1;
			inst->vsi->general_buf_size = 0;
			mtk_vcodec_debug(inst, "no general buf dmabuf");
		}
	}

	inst->vsi->dec_params.queued_frame_buf_count =
		inst->ctx->dec_params.queued_frame_buf_count;
	inst->vsi->dec.timestamp =
		inst->ctx->dec_params.timestamp;

	mtk_vcodec_debug(inst, "+ FB y_fd=%llx c_fd=%llx BS fd=%llx format=%c%c%c%c",
		inst->vsi->dec.fb_fd[0], inst->vsi->dec.fb_fd[1],
		inst->vsi->dec.bs_fd, fm_fourcc & 0xFF,
		(fm_fourcc >> 8) & 0xFF, (fm_fourcc >> 16) & 0xFF,
		(fm_fourcc >> 24) & 0xFF);

	data[0] = (unsigned int)bs->size;
	data[1] = (unsigned int)bs->length;
	data[2] = (unsigned int)bs->flags;
	data[3] = inst->ctx->dec_params.fixed_max_frame_size_width;
	data[4] = inst->ctx->dec_params.fixed_max_frame_size_height;
	data[5] = inst->ctx->dec_params.fixed_max_frame_buffer_mode;
	ret = vcu_dec_start(vcu, data, ARRAY_SIZE(data), bs, fb);

	*src_chg = inst->vsi->dec.vdec_changed_info;
	if (inst->vsi->dec.error_code[MTK_VDEC_LAT] ||
	    (!inst->vsi->output_async && inst->vsi->dec.error_code[MTK_VDEC_CORE]))
		mtk_vdec_queue_error_event(inst->ctx);
	*(errormap_info + bs->index % VB2_MAX_FRAME) =
		inst->vsi->dec.error_map;

	if ((*src_chg & VDEC_NEED_SEQ_HEADER) != 0U)
		mtk_vcodec_err(inst, "- need first seq header -");
	else if ((*src_chg & VDEC_RES_CHANGE) != 0U)
		mtk_vcodec_debug(inst, "- resolution changed -");
	else if ((*src_chg & VDEC_HW_NOT_SUPPORT) != 0U)
		mtk_vcodec_err(inst, "- unsupported -");

	if (ret == -EIO) {
		mtk_vcodec_err(inst, "- IPI msg ack timeout  -");
		*src_chg = *src_chg | VDEC_HW_NOT_SUPPORT;
	}

	if (ret < 0 || ((*src_chg & VDEC_HW_NOT_SUPPORT) != 0U)
		|| ((*src_chg & VDEC_NEED_SEQ_HEADER) != 0U))
		goto err_free_fb_out;

	inst->ctx->input_driven = inst->vsi->input_driven;
	inst->ctx->output_async = inst->vsi->output_async;
	inst->num_nalu++;
	return ret;

err_free_fb_out:
	put_fb_to_free(inst, fb);
	mtk_vcodec_debug(inst, "\n - NALU[%d] err=%d -\n", inst->num_nalu, ret);
	return ret;
}

static void vdec_get_bs(struct vdec_inst *inst,
						struct ring_bs_list *list,
						struct mtk_vcodec_mem **out_bs)
{
	u64 bs_index;
	unsigned long vdec_bs_va;
	struct mtk_vcodec_mem *bs;

	if (list->count == 0) {
		mtk_vcodec_debug(inst, "[BS] there is no bs");
		*out_bs = NULL;
		return;
	}

	if (list->read_idx >= DEC_MAX_BS_NUM || list->count > DEC_MAX_BS_NUM) {
		*out_bs = NULL;
		return;
	}
	bs_index = list->vdec_bs_va_list[list->read_idx];
	if (!bs_index || bs_index > VB2_MAX_FRAME) {
		*out_bs = NULL;
		return;
	}
	vdec_bs_va = (unsigned long)inst->ctx->bs_list[bs_index];
	bs = (struct mtk_vcodec_mem *)vdec_bs_va;

	*out_bs = bs;
	mtk_vcodec_debug(inst, "[BS] get free bs %lld %lx", bs_index, vdec_bs_va);

	list->read_idx = (list->read_idx == DEC_MAX_BS_NUM - 1) ?
					 0 : list->read_idx + 1;
	list->count--;
}

static void vdec_get_fb(struct vdec_inst *inst,
	struct ring_fb_list *list,
	bool disp_list, struct vdec_fb **out_fb)
{
	u64 fb_index;
	unsigned long vdec_fb_va;
	struct vdec_fb *fb;

	if (list->count == 0) {
		mtk_vcodec_debug(inst, "[FB] there is no %s fb",
						 disp_list ? "disp" : "free");
		*out_fb = NULL;
		return;
	}

	if (list->read_idx >= DEC_MAX_FB_NUM || list->count > DEC_MAX_FB_NUM) {
		*out_fb = NULL;
		return;
	}
	fb_index = list->fb_list[list->read_idx].vdec_fb_va;
	if (!fb_index || fb_index > VB2_MAX_FRAME) {
		*out_fb = NULL;
		return;
	}
	vdec_fb_va = (unsigned long)inst->ctx->fb_list[fb_index];
	fb = (struct vdec_fb *)vdec_fb_va;
	if (fb == NULL) {
		*out_fb = NULL;
		return;
	}
	fb->timestamp = list->fb_list[list->read_idx].timestamp;
	fb->field = list->fb_list[list->read_idx].field;
	fb->frame_type = list->fb_list[list->read_idx].frame_type;

	if (disp_list) {
		fb->status |= FB_ST_DISPLAY;
		if (list->fb_list[list->read_idx].flags & VDEC_FB_NO_GENERATED)
			fb->status |= FB_ST_NO_GENERATED;
		if (list->fb_list[list->read_idx].flags & VDEC_FB_CROP_CHANGED)
			fb->status |= FB_ST_CROP_CHANGED;
	} else {
		fb->status |= FB_ST_FREE;
		if (list->fb_list[list->read_idx].flags & VDEC_FB_EOS)
			fb->status |= FB_ST_EOS;
	}

	*out_fb = fb;
	mtk_vcodec_debug(inst, "[FB] get %s fb st=%x id=%d ts=%llu %lld %lx gbuf fd %d dma %p",
		disp_list ? "disp" : "free", fb->status, list->read_idx,
		list->fb_list[list->read_idx].timestamp,
		list->fb_list[list->read_idx].vdec_fb_va, vdec_fb_va,
		fb->general_buf_fd, fb->dma_general_buf);

	list->read_idx = (list->read_idx == DEC_MAX_FB_NUM - 1U) ?
					 0U : list->read_idx + 1U;
	list->count--;
}

static int get_supported_format(struct vdec_inst *inst,
				struct mtk_video_fmt *video_fmt)
{
	unsigned int i = 0;
	int ret;

	inst->vcu.ctx = inst->ctx;
	ret = vcu_dec_query_cap(&inst->vcu, GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS,
				video_fmt);
	if (ret)
		return ret;
	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
		if (video_fmt[i].fourcc != 0) {
			mtk_vcodec_debug(inst, "video_formats[%d] fourcc %d type %d num_planes %d\n",
				i, video_fmt[i].fourcc, video_fmt[i].type,
				video_fmt[i].num_planes);
		}
	}
	return 0;
}

static int get_frame_sizes(struct vdec_inst *inst,
			   struct mtk_codec_framesizes *codec_framesizes)
{
	unsigned int i = 0;
	int ret;

	inst->vcu.ctx = inst->ctx;
	ret = vcu_dec_query_cap(&inst->vcu, GET_PARAM_VDEC_CAP_FRAME_SIZES,
				codec_framesizes);
	if (ret)
		return ret;
	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
		if (codec_framesizes[i].fourcc != 0) {
			mtk_vcodec_debug(inst,
				"codec_fs[%d] fourcc %d s %d %d %d %d %d %d P %d L %d\n",
				i, codec_framesizes[i].fourcc,
				codec_framesizes[i].stepwise.min_width,
				codec_framesizes[i].stepwise.max_width,
				codec_framesizes[i].stepwise.step_width,
				codec_framesizes[i].stepwise.min_height,
				codec_framesizes[i].stepwise.max_height,
				codec_framesizes[i].stepwise.step_height,
				codec_framesizes[i].profile,
				codec_framesizes[i].level);
		}
	}
	return 0;
}

static void get_color_desc(struct vdec_inst *inst,
	struct mtk_color_desc *color_desc)
{
	inst->vcu.ctx = inst->ctx;
	memcpy(color_desc, &inst->vsi->color_desc,
	       offsetof(struct mtk_color_desc, is_hdr));
	color_desc->is_hdr = inst->vsi->color_desc.hdr_type != 0;
	color_desc->full_range = inst->vsi->color_desc.full_range;
}

static void get_aspect_ratio(struct vdec_inst *inst, unsigned int *aspect_ratio)
{
	if (inst->vsi == NULL)
		return;

	inst->vcu.ctx = inst->ctx;
	*aspect_ratio = inst->vsi->aspect_ratio;
}

static void get_supported_fix_buffers(struct vdec_inst *inst,
					unsigned int *supported)
{
	inst->vcu.ctx = inst->ctx;
	if (inst->vsi != NULL)
		*supported = inst->vsi->fix_buffers;
}

static void get_supported_fix_buffers_svp(struct vdec_inst *inst,
					unsigned int *supported)
{
	inst->vcu.ctx = inst->ctx;
	if (inst->vsi != NULL)
		*supported = inst->vsi->fix_buffers;
}

static void get_interlacing(struct vdec_inst *inst,
			    unsigned int *interlacing)
{
	inst->vcu.ctx = inst->ctx;
	if (inst->vsi != NULL)
		*interlacing = inst->vsi->interlacing;
}

static void get_codec_type(struct vdec_inst *inst,
			   unsigned int *codec_type)
{
	inst->vcu.ctx = inst->ctx;
	if (inst->vsi != NULL)
		*codec_type = inst->vsi->codec_type;
}

static void get_input_driven(struct vdec_inst *inst,
			   unsigned int *input_driven)
{
	inst->vcu.ctx = inst->ctx;
	if (inst->vsi != NULL)
		*input_driven = inst->vsi->input_driven;
}

static int vdec_get_param(unsigned long h_vdec,
	enum vdec_get_param_type type, void *out)
{
	struct vdec_inst *inst = (struct vdec_inst *)h_vdec;
	int ret = 0;

	if (inst == NULL)
		return -EINVAL;

	switch ((unsigned int)type) {
	case GET_PARAM_FREE_BITSTREAM_BUFFER:
		if (inst->vsi == NULL)
			return -EINVAL;
		vdec_get_bs(inst, &inst->vsi->list_free_bs, out);
		break;

	case GET_PARAM_DISP_FRAME_BUFFER:
	{
		struct vdec_fb *pfb;

		if (inst->vsi == NULL)
			return -EINVAL;
		vdec_get_fb(inst, &inst->vsi->list_disp, true, out);

		pfb = *((struct vdec_fb **)out);
		break;
	}

	case GET_PARAM_FREE_FRAME_BUFFER:
	{
		struct vdec_fb *pfb;

		if (inst->vsi == NULL)
			return -EINVAL;
		vdec_get_fb(inst, &inst->vsi->list_free, false, out);

		pfb = *((struct vdec_fb **)out);
		break;
	}

	case GET_PARAM_PIC_INFO:
		get_pic_info(inst, out);
		break;

	case GET_PARAM_DPB_SIZE:
		get_dpb_size(inst, out);
		break;

	case GET_PARAM_CROP_INFO:
		get_crop_info(inst, out);
		break;

	case GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS:
		ret = get_supported_format(inst, out);
		break;

	case GET_PARAM_VDEC_CAP_FRAME_SIZES:
		ret = get_frame_sizes(inst, out);
		break;

	case GET_PARAM_VDEC_CAP_MAX_BUF_INFO:
		ret = vcu_dec_query_cap(&inst->vcu, type, out);
		if (!ret)
			memcpy(out, &mtk_vdec_max_buf_info, sizeof(mtk_vdec_max_buf_info));
		break;
	case GET_PARAM_VDEC_CAP_FRAMEINTERVALS:
		ret = vcu_dec_query_cap(&inst->vcu, type, out);
		if (!ret)
			memcpy(out, &mtk_vdec_frameintervals, sizeof(mtk_vdec_frameintervals));
		break;
	case GET_PARAM_OUTPUT_ASYNC:
		if (!inst->vsi)
			return -EINVAL;
		*(bool *)out = inst->vsi->output_async;
		break;
	case GET_PARAM_COLOR_DESC:
		if (inst->vsi == NULL)
			return -EINVAL;
		get_color_desc(inst, out);
		break;

	case GET_PARAM_ASPECT_RATIO:
		get_aspect_ratio(inst, out);
		break;

	case GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS:
		get_supported_fix_buffers(inst, out);
		break;

	case GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS_SVP:
		get_supported_fix_buffers_svp(inst, out);
		break;

	case GET_PARAM_INTERLACING:
		get_interlacing(inst, out);
		break;

	case GET_PARAM_CODEC_TYPE:
		get_codec_type(inst, out);
		break;

	case GET_PARAM_INPUT_DRIVEN:
		get_input_driven(inst, out);
		break;

	case GET_PARAM_VDEC_VCU_VPUD_LOG:
		VCU_FPTR(vcu_get_log)(out, LOG_PROPERTY_SIZE);
		break;

	default:
		mtk_vcodec_err(inst, "invalid get parameter type=%d", type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int vdec_set_param(unsigned long h_vdec,
	enum vdec_set_param_type type, void *in)
{
	struct vdec_inst *inst = (struct vdec_inst *)h_vdec;
	unsigned long *param = in;
	struct mtk_dec_params *params;

	if (!inst)
		return -EINVAL;
	if (type == SET_PARAM_VDEC_VCU_VPUD_LOG)
		return VCU_FPTR(vcu_set_log)(in);
	if (!inst->vsi)
		return -EINVAL;
	params = &inst->vsi->dec_params;

	switch ((unsigned int)type) {
	case SET_PARAM_FRAME_BUFFER:
		return vcu_dec_set_frame_buffer(&inst->vcu, in);
	case SET_PARAM_FRAME_SIZE:
		return 0;
	case SET_PARAM_NAL_SIZE_LENGTH:
		return *param ? -EOPNOTSUPP : 0;
	case SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER:
		params->fixed_max_frame_size_width = param[0];
		params->fixed_max_frame_size_height = param[1];
		params->fixed_max_frame_buffer_mode = inst->ctx->dec_params.fixed_max_frame_buffer_mode;
		params->dec_param_change |= MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE;
		break;
	case SET_PARAM_DECODE_MODE:
		params->decode_mode = *param;
		params->dec_param_change |= MTK_DEC_PARAM_DECODE_MODE;
		break;
	case SET_PARAM_WAIT_KEY_FRAME:
		params->wait_key_frame = *param;
		params->dec_param_change |= MTK_DEC_PARAM_WAIT_KEY_FRAME;
		break;
	case SET_PARAM_OPERATING_RATE:
		params->operating_rate = *param;
		params->dec_param_change |= MTK_DEC_PARAM_OPERATING_RATE;
		break;
	case SET_PARAM_CRC_PATH:
		return strscpy(inst->vsi->crc_path, (char *)*param,
			       sizeof(inst->vsi->crc_path)) < 0 ? -E2BIG : 0;
	case SET_PARAM_GOLDEN_PATH:
		return strscpy(inst->vsi->golden_path, (char *)*param,
			       sizeof(inst->vsi->golden_path)) < 0 ? -E2BIG : 0;
	case SET_PARAM_FB_NUM_PLANES:
		if (*(unsigned int *)in > VIDEO_MAX_PLANES)
			return -EINVAL;
		inst->vsi->dec.fb_num_planes = *(unsigned int *)in;
		return 0;
	case SET_PARAM_TOTAL_FRAME_BUFQ_COUNT:
		if (inst->vsi->codec_type == 1)
			return vcu_dec_set_param(&inst->vcu, type, in, 1);
		return 0;
	case SET_PARAM_UFO_MODE:
	case SET_PARAM_VDEC_PROPERTY:
		return 0;
	default:
		return -EINVAL;
	}

	return vcu_dec_set_param(&inst->vcu, SET_PARAM_DEC_PARAMS, NULL, 0);
}

static struct vdec_common_if vdec_if = {
	vdec_init,
	vdec_decode,
	vdec_get_param,
	vdec_set_param,
	vdec_deinit,
};

const struct vdec_common_if *get_dec_vcu_if(void);

const struct vdec_common_if *get_dec_vcu_if(void)
{
	return &vdec_if;
}
