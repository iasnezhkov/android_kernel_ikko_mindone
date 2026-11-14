/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define M_DST_LEN 0x400000u
#define M_SRC_LEN 24000000u

struct m_open {
	struct file file;
	struct mtk_jpeg_ctx *ctx;
	struct v4l2_m2m_ctx m2m;
};

static struct device m_dev;
static struct device m_larb;
static struct device_node m_np;
static struct clk_bulk_data m_clks[] = { { .id = "jpgenc" } };
static struct regulator m_reg;
static struct icc_path m_paths[4];
static struct v4l2_m2m_dev m_m2m;
static struct mtk_jpeg_variant m_variant;
static struct mtk_jpeg_dev m_jpeg;
static struct video_device m_vdev;
static struct platform_device m_pdev;
static struct vb2_queue m_srcq = { V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
static struct vb2_queue m_dstq = { V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE };
static struct vb2_v4l2_buffer m_src[4];
static struct vb2_v4l2_buffer m_dst[4];

static void hw_finish(u32 size, u32 status)
{
	assert(m.hw_running);
	m.regs[JPEG_ENC_DMA_ADDR0 / 4] = m.regs[JPEG_ENC_DST_ADDR0 / 4] + size;
	m.regs[JPEG_ENC_INT_STS / 4] = status;
	m.hw_running = false;
	assert(mtk_jpeg_enc_irq(0, &m_jpeg) == IRQ_HANDLED);
}

static inline int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *ctx, struct v4l2_buffer *buf)
{
	struct vb2_v4l2_buffer *vb;
	unsigned int i;

	(void)file;
	assert(buf->index < ARRAY_SIZE(m_src));
	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		vb = &m_src[buf->index];
		memset(vb, 0, sizeof(*vb));
		vb->vb2_buf.vb2_queue = &m_srcq;
		vb->vb2_buf.index = buf->index;
		vb->vb2_buf.num_planes = buf->length;
		for (i = 0; i < buf->length; i++) {
			vb->vb2_buf.planes[i].dma = 0x180000000ULL + buf->index * 0x4000000ULL + i * 0x2000000ULL;
			vb->vb2_buf.planes[i].length = buf->m.planes[i].length;
			vb->vb2_buf.planes[i].bytesused = buf->m.planes[i].bytesused;
			vb->vb2_buf.planes[i].data_offset = buf->m.planes[i].data_offset;
		}
		assert(ctx->nsrc < (int)ARRAY_SIZE(ctx->src));
		ctx->src[ctx->nsrc++] = vb;
		return 0;
	}
	assert(buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && buf->length == 1);
	vb = &m_dst[buf->index];
	memset(vb, 0, sizeof(*vb));
	vb->vb2_buf.vb2_queue = &m_dstq;
	vb->vb2_buf.index = buf->index;
	vb->vb2_buf.num_planes = 1;
	vb->vb2_buf.planes[0].dma = 0x140000000ULL + buf->index * 0x1000000ULL;
	vb->vb2_buf.planes[0].length = buf->m.planes[0].length;
	vb->vb2_buf.planes[0].data_offset = 0;
	assert(ctx->ndst < (int)ARRAY_SIZE(ctx->dst));
	ctx->dst[ctx->ndst++] = vb;
	return 0;
}

static inline void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *ctx)
{
	if (m_m2m.curr == ctx && ctx->running)
		hw_finish(0x1000, JPEG_ENC_INT_STATUS_DONE);
	assert(m_m2m.curr != ctx);
	ctx->released = true;
}

static inline void v4l2_m2m_suspend(struct v4l2_m2m_dev *dev)
{
	dev->paused = true;
	if (dev->curr && dev->curr->running)
		hw_finish(0x1000, JPEG_ENC_INT_STATUS_DONE);
}

static inline void v4l2_m2m_resume(struct v4l2_m2m_dev *dev)
{
	dev->paused = false;
}

static struct mtk_jpeg_fmt *find_fmt(u32 fourcc)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mtk_jpeg_enc_formats); i++)
		if (mtk_jpeg_enc_formats[i].fourcc == fourcc)
			return &mtk_jpeg_enc_formats[i];
	assert(!"format missing from mtk_jpeg_enc_formats");
	return NULL;
}

static void setup(void)
{
	size_t i;

	memset(&m, 0, sizeof(m));
	memset(&m_jpeg, 0, sizeof(m_jpeg));
	memset(&m_m2m, 0, sizeof(m_m2m));
	memset(m_src, 0, sizeof(m_src));
	memset(m_dst, 0, sizeof(m_dst));
	m_dev = (struct device){ .of_node = &m_np, .drvdata = &m_jpeg, .name = "17030000.jpgenc",
				 .runtime_suspend = mtk_jpeg_pm_suspend, .runtime_resume = mtk_jpeg_pm_resume };
	m_larb = (struct device){ .name = "14023000.larb7" };
	m_variant = (struct mtk_jpeg_variant){
		.clks = m_clks,
		.num_clks = ARRAY_SIZE(m_clks),
		.formats = mtk_jpeg_enc_formats,
		.num_formats = ARRAY_SIZE(mtk_jpeg_enc_formats),
		.irq_handler = mtk_jpeg_enc_irq,
		.hw_reset = mtk_jpeg_enc_reset,
		.dev_name = "mtk-jpeg-enc",
		.out_q_default_fourcc = V4L2_PIX_FMT_YUYV,
		.cap_q_default_fourcc = V4L2_PIX_FMT_JPEG,
		.support_34bit = false,
	};
	m_jpeg.dev = &m_dev;
	m_jpeg.larb = &m_larb;
	m_jpeg.reg_base = (void __iomem *)m.regs;
	m_jpeg.variant = &m_variant;
	m_jpeg.m2m_dev = &m_m2m;
	m_jpeg.vdev = &m_vdev;
	m_jpeg.jpegenc_reg = &m_reg;
	m_jpeg.freq_cnt = ARRAY_SIZE(m_opps);
	for (i = 0; i < ARRAY_SIZE(m_opps); i++)
		m_jpeg.freqs[i] = m_opps[i].freq;
	for (i = 0; i < ARRAY_SIZE(m_paths); i++)
		m_paths[i] = (struct icc_path){ .name = "path" };
	m_jpeg.path_y_rdma = &m_paths[0];
	m_jpeg.path_c_rdma = &m_paths[1];
	m_jpeg.path_qtbl = &m_paths[2];
	m_jpeg.path_bsdma = &m_paths[3];
	m_vdev.release = video_device_release;
	m_pdev.dev = m_dev;
	m.volt = m_opps[0].volt;
	m.larb_dev_refs = 1;
	jpgenc_probe_time = 1;
}

static struct m_open *open_ctx(void)
{
	struct m_open *o = calloc(1, sizeof(*o));
	struct mtk_jpeg_ctx *ctx = calloc(1, sizeof(*ctx));
	struct mtk_jpeg_q_data *q;

	assert(o && ctx);
	ctx->jpeg = &m_jpeg;
	ctx->fh.m2m_ctx = &o->m2m;
	o->m2m.priv = ctx;
	o->file.private_data = &ctx->fh;
	o->file.vdev_drvdata = &m_jpeg;
	o->ctx = ctx;
	assert(!mtk_jpeg_enc_ctrls_setup(ctx));
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	q = &ctx->out_q;
	q->fmt = find_fmt(V4L2_PIX_FMT_YUYV);
	q->pix_mp.width = 4000;
	q->pix_mp.height = 3000;
	q->pix_mp.plane_fmt[0].bytesperline = 8000;
	q->pix_mp.plane_fmt[0].sizeimage = 8000 * 3000;
	q->enc_crop_rect.width = 4000;
	q->enc_crop_rect.height = 3000;
	q = &ctx->cap_q;
	q->fmt = find_fmt(V4L2_PIX_FMT_JPEG);
	q->pix_mp.width = 4000;
	q->pix_mp.height = 3000;
	q->pix_mp.plane_fmt[0].sizeimage = M_DST_LEN;
	m.open_files++;
	return o;
}

static void close_ctx(struct m_open *o)
{
	assert(!mtk_jpeg_release(&o->file));
	assert(o->m2m.released);
	assert(!o->file.private_data);
	m.open_files--;
	free(o);
}

static void queue_pair(struct m_open *o, unsigned int index, u32 src_offset, u32 dst_offset)
{
	struct v4l2_plane src_plane = { .bytesused = M_SRC_LEN, .length = M_SRC_LEN, .data_offset = src_offset };
	struct v4l2_plane dst_plane = { .length = M_DST_LEN, .data_offset = dst_offset };
	struct v4l2_buffer src = { .index = index, .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, .memory = 4,
				   .m.planes = &src_plane, .length = 1 };
	struct v4l2_buffer dst = { .index = index, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = 4,
				   .m.planes = &dst_plane, .length = 1 };

	assert(!mtk_jpeg_qbuf(&o->file, o->file.private_data, &src));
	assert(!mtk_jpeg_qbuf(&o->file, o->file.private_data, &dst));
}

static void run_job(struct m_open *o)
{
	assert(!m_m2m.curr && !m_m2m.paused);
	m_m2m.curr = &o->m2m;
	o->m2m.running = true;
	mtk_jpeg_enc_device_run(o->ctx);
}

static void clean(void)
{
	size_t i;

	assert(m_dev.usage == 0);
	assert(!m_dev.active);
	assert(m.clocks_on == 0 && m.larb_refs == 0);
	assert(m.unclocked == 0);
	assert(m.clock_off_running == 0);
	assert(m.volt_lowered_running == 0);
	assert(m.bw_dropped_running == 0);
	assert(m.volt == m_opps[0].volt);
	for (i = 0; i < ARRAY_SIZE(m_paths); i++)
		assert(m_paths[i].avg == 0 && m_paths[i].peak == 0);
}

static void case_abi_querycap(void)
{
	struct v4l2_capability cap;
	struct m_open *o;

	setup();
	o = open_ctx();
	memset(&cap, 0, sizeof(cap));
	assert(!mtk_jpeg_querycap(&o->file, o->file.private_data, &cap));
	assert(!memcmp(cap.driver, "mtk-jpeg-enc", 13));
	assert(cap.reserved[0] == 1);
	close_ctx(o);
	clean();
}

static void case_abi_controls(void)
{
	struct mtk_jpeg_ctx *ctx;
	struct v4l2_ctrl *quality, *restart, *marker;
	struct m_open *o;

	setup();
	o = open_ctx();
	ctx = o->ctx;
	quality = m_find_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_COMPRESSION_QUALITY);
	restart = m_find_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_RESTART_INTERVAL);
	marker = m_find_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_ACTIVE_MARKER);
	assert(quality && restart && marker);
	assert(quality->minimum == 0);
	assert(quality->maximum == 100 && quality->default_value == 90);
	assert(restart->minimum == 0 && restart->maximum == 100 && restart->default_value == 0);
	assert(marker->minimum == 0 && marker->maximum == V4L2_JPEG_ACTIVE_MARKER_APP1 && marker->default_value == 0);
	assert(ctx->enc_quality == 90 && ctx->restart_interval == 0 && !ctx->enable_exif);
	assert(!m_s_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_COMPRESSION_QUALITY, 30));
	assert(ctx->enc_quality == 30);
	assert(!m_s_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_ACTIVE_MARKER, V4L2_JPEG_ACTIVE_MARKER_APP1));
	assert(ctx->enable_exif);
	assert(!m_s_ctrl(&ctx->ctrl_hdl, V4L2_CID_JPEG_RESTART_INTERVAL, 4));
	assert(ctx->restart_interval == 4);
	close_ctx(o);
	clean();
}

static void case_abi_dst_offset(void)
{
	struct m_open *o;
	dma_addr_t dma, src_dma;
	u32 dst_addr0;

	setup();
	o = open_ctx();
	assert(!m_s_ctrl(&o->ctx->ctrl_hdl, V4L2_CID_JPEG_ACTIVE_MARKER, V4L2_JPEG_ACTIVE_MARKER_APP1));
	queue_pair(o, 0, 0x100, 0x2a40);
	run_job(o);
	assert(m.hw_running && m.starts == 1);
	dma = m_dst[0].vb2_buf.planes[0].dma;
	src_dma = m_src[0].vb2_buf.planes[0].dma;
	dst_addr0 = m.regs[JPEG_ENC_DST_ADDR0 / 4];
	assert(dst_addr0 == (u32)(dma + 0x2a40));
	assert(m.regs[JPEG_ENC_OFFSET_ADDR / 4] == 0);
	assert(m.regs[JPEG_ENC_BYTE_OFFSET_MASK / 4] == 0);
	assert(m.regs[JPEG_ENC_STALL_ADDR0 / 4] == (u32)((dma + M_DST_LEN) & ~0xfULL));
	assert(m.regs[JPEG_ENC_SRC_LUMA_ADDR / 4] == (u32)(src_dma + 0x100));
	assert(m.regs[JPEG_ENC_CTRL / 4] & JPEG_ENC_CTRL_FILE_FORMAT_BIT);
	hw_finish(1234567, JPEG_ENC_INT_STATUS_DONE);
	assert(m_dst[0].vb2_buf.planes[0].bytesused == 1234567);
	assert(m_dst[0].state == VB2_BUF_STATE_DONE && m_src[0].state == VB2_BUF_STATE_DONE);
	assert(m_m2m.finished == 1);
	close_ctx(o);
	clean();
}

static void case_quality_table(void)
{
	static const struct {
		int quality;
		u32 reg;
	} table[] = {
		{ 0, JPEG_ENC_QUALITY_Q24 }, { 24, JPEG_ENC_QUALITY_Q24 }, { 30, JPEG_ENC_QUALITY_Q30 },
		{ 40, JPEG_ENC_QUALITY_Q38 }, { 48, JPEG_ENC_QUALITY_Q44 }, { 60, JPEG_ENC_QUALITY_Q60 },
		{ 85, JPEG_ENC_QUALITY_Q85 }, { 90, JPEG_ENC_QUALITY_Q90 }, { 95, JPEG_ENC_QUALITY_Q95 },
		{ 97, JPEG_ENC_QUALITY_Q97 }, { 100, JPEG_ENC_QUALITY_Q97 },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		struct m_open *o;
		u32 quality_reg;

		setup();
		o = open_ctx();
		assert(!m_s_ctrl(&o->ctx->ctrl_hdl, V4L2_CID_JPEG_COMPRESSION_QUALITY, table[i].quality));
		queue_pair(o, 0, 0, 0);
		run_job(o);
		quality_reg = m.regs[JPEG_ENC_QUALITY / 4];
		if (quality_reg != table[i].reg)
			printf("quality %d -> 0x%x, expected 0x%x\n", table[i].quality, quality_reg, table[i].reg);
		assert(quality_reg == table[i].reg);
		hw_finish(4096, JPEG_ENC_INT_STATUS_DONE);
		close_ctx(o);
		clean();
	}
}

static void case_pm_one_job(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	run_job(o);
	assert(m_dev.usage == 1 && m_dev.active);
	assert(m.clocks_on == 1 && m.larb_refs == 1);
	assert(m.volt == m_opps[2].volt && m.bw_set == 4);
	assert(m_jpeg.job_timeout_work.pending);
	hw_finish(4096, JPEG_ENC_INT_STATUS_DONE);
	assert(!m_jpeg.job_timeout_work.pending);
	assert(m_dev.usage == 0);
	assert(!m_dev.active && m.clocks_on == 0 && m.larb_refs == 0);
	assert(m_dst[0].state == VB2_BUF_STATE_DONE && m_dst[0].vb2_buf.planes[0].bytesused == 4096);
	close_ctx(o);
	clean();
	assert(m.resumes == 1 && m.suspends == 1);
}

static void case_pm_two_jobs(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	run_job(o);
	hw_finish(4096, JPEG_ENC_INT_STATUS_DONE);
	assert(m_dev.usage == 0);
	queue_pair(o, 1, 0, 0);
	run_job(o);
	assert(m_dev.usage == 1);
	hw_finish(8192, JPEG_ENC_INT_STATUS_DONE);
	assert(m_dev.usage == 0);
	assert(m.resumes == 2 && m.suspends == 2);
	assert(m_dst[1].state == VB2_BUF_STATE_DONE && m_dst[1].vb2_buf.planes[0].bytesused == 8192);
	close_ctx(o);
	clean();
}

static void case_pm_timeout(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	run_job(o);
	assert(m_jpeg.job_timeout_work.pending);
	m_jpeg.job_timeout_work.pending = false;
	mtk_jpeg_job_timeout_work(&m_jpeg.job_timeout_work.work);
	assert(!m.hw_running);
	assert(m_dev.usage == 0 && !m_dev.active);
	assert(m_src[0].state == VB2_BUF_STATE_ERROR && m_dst[0].state == VB2_BUF_STATE_ERROR);
	assert(m_m2m.finished == 1);
	close_ctx(o);
	clean();
}

static void case_pm_get_fails(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	m.resume_fails = true;
	run_job(o);
	m.resume_fails = false;
	assert(!m.hw_running && m.starts == 0);
	assert(m_dev.usage == 0);
	assert(m.bw_set == 0 && m.volt == m_opps[0].volt);
	assert(m_src[0].state == VB2_BUF_STATE_ERROR && m_dst[0].state == VB2_BUF_STATE_ERROR);
	assert(m_m2m.finished == 1);
	close_ctx(o);
	clean();
}

static void case_release_in_flight(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	run_job(o);
	assert(m.hw_running);
	close_ctx(o);
	assert(m.volt_lowered_running == 0);
	assert(m.bw_dropped_running == 0);
	assert(m.clock_off_running == 0);
	assert(m_dst[0].state == VB2_BUF_STATE_DONE);
	clean();
}

static void case_system_suspend(void)
{
	struct m_open *o;

	setup();
	o = open_ctx();
	queue_pair(o, 0, 0, 0);
	run_job(o);
	assert(!mtk_jpeg_suspend(&m_dev));
	assert(m.clock_off_running == 0);
	assert(m_m2m.paused && !m_m2m.curr);
	assert(m_dst[0].state == VB2_BUF_STATE_DONE);
	assert(!m_dev.active && m_dev.usage == 0 && m.clocks_on == 0 && m.larb_refs == 0);
	assert(!mtk_jpeg_resume(&m_dev));
	assert(!m_m2m.paused && !m_dev.active && m.resumes == 1);
	assert(!mtk_jpeg_suspend(&m_dev));
	assert(m.suspends == 1);
	assert(!mtk_jpeg_resume(&m_dev));
	assert(m.resumes == 1);
	queue_pair(o, 1, 0, 0);
	run_job(o);
	hw_finish(4096, JPEG_ENC_INT_STATUS_DONE);
	assert(m.resumes == 2 && m.suspends == 2);
	close_ctx(o);
	clean();
}

static void case_remove(void)
{
	setup();
	m_pdev.dev = m_dev;
	m_jpeg.job_timeout_work.pending = true;
	mtk_jpeg_remove(&m_pdev);
	assert(!m_jpeg.job_timeout_work.pending && m.timeout_cancelled == 1);
	assert(m.pm_disabled == 1);
	assert(m.vdev_released == 1);
	assert(m.m2m_released == 1 && m.v4l2_unregistered == 1);
	assert(m.larb_dev_refs == 0);
	clean();
}

static const struct {
	const char *name;
	void (*run)(void);
} m_cases[] = {
	{ "abi-querycap", case_abi_querycap },
	{ "abi-controls", case_abi_controls },
	{ "abi-dst-offset", case_abi_dst_offset },
	{ "quality-table", case_quality_table },
	{ "pm-one-job", case_pm_one_job },
	{ "pm-two-jobs", case_pm_two_jobs },
	{ "pm-timeout", case_pm_timeout },
	{ "pm-get-fails", case_pm_get_fails },
	{ "release-in-flight", case_release_in_flight },
	{ "system-suspend", case_system_suspend },
	{ "remove", case_remove },
};

int main(int argc, char **argv)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(m_cases); i++) {
		if (argc > 1 && strcmp(argv[1], m_cases[i].name))
			continue;
		m_cases[i].run();
		printf("PASS %s\n", m_cases[i].name);
		if (argc > 1)
			return 0;
	}
	if (argc > 1) {
		fprintf(stderr, "unknown case %s\n", argv[1]);
		return 2;
	}
	return 0;
}
