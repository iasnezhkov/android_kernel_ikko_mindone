/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef MTK_JPEG_STUBS_H
#define MTK_JPEG_STUBS_H

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef unsigned long long dma_addr_t;
typedef int irqreturn_t;
typedef struct { int unused; } spinlock_t;

#define __iomem
#define __maybe_unused __attribute__((unused))
#define CONFIG_ARCH_DMA_ADDR_T_64BIT 1
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define round_up(x, y) ((((x) - 1) | ((__typeof__(x))(y) - 1)) + 1)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define unlikely(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define msecs_to_jiffies(ms) (ms)
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define VIDEO_MAX_PLANES 8
#define VIDEO_MAJOR 81

#define pr_info(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define pr_debug(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define pr_err(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define dev_err(d, ...) do { (void)(d); if (0) printf(__VA_ARGS__); } while (0)
#define dev_info(d, ...) do { (void)(d); if (0) printf(__VA_ARGS__); } while (0)
#define v4l2_info(d, ...) do { (void)(d); if (0) printf(__VA_ARGS__); } while (0)
#define v4l2_err(d, ...) do { (void)(d); if (0) printf(__VA_ARGS__); } while (0)
#define v4l2_dbg(l, dbg, d, ...) do { (void)(d); (void)(dbg); if (0) printf(__VA_ARGS__); } while (0)

#define v4l2_fourcc(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define V4L2_PIX_FMT_JPEG v4l2_fourcc('J', 'P', 'E', 'G')
#define V4L2_PIX_FMT_NV12M v4l2_fourcc('N', 'M', '1', '2')
#define V4L2_PIX_FMT_NV21M v4l2_fourcc('N', 'M', '2', '1')
#define V4L2_PIX_FMT_YUYV v4l2_fourcc('Y', 'U', 'Y', 'V')
#define V4L2_PIX_FMT_YVYU v4l2_fourcc('Y', 'V', 'Y', 'U')
#define V4L2_PIX_FMT_YUV420M v4l2_fourcc('Y', 'M', '1', '2')
#define V4L2_PIX_FMT_YUV422M v4l2_fourcc('Y', 'M', '1', '6')
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 9
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 10
#define V4L2_CID_JPEG_RESTART_INTERVAL 0x009d0902
#define V4L2_CID_JPEG_COMPRESSION_QUALITY 0x009d0903
#define V4L2_CID_JPEG_ACTIVE_MARKER 0x009d0904
#define V4L2_JPEG_ACTIVE_MARKER_APP1 (1 << 1)

enum vb2_buffer_state {
	VB2_BUF_STATE_DEQUEUED,
	VB2_BUF_STATE_QUEUED,
	VB2_BUF_STATE_DONE,
	VB2_BUF_STATE_ERROR,
};

struct device_node {
	int unused;
};

struct device {
	struct device_node *of_node;
	void *drvdata;
	const char *name;
	int usage;
	bool active;
	bool needs_force_resume;
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
};

struct file {
	void *private_data;
	void *vdev_drvdata;
};

struct mutex {
	int held;
};

struct work_struct {
	int unused;
};

struct delayed_work {
	struct work_struct work;
	bool pending;
};

struct workqueue_struct;
struct clk;
struct vb2_ops;
struct v4l2_ioctl_ops;

struct video_device {
	void (*release)(struct video_device *vdev);
};

struct platform_device {
	struct device dev;
};

struct clk_bulk_data {
	const char *id;
	struct clk *clk;
};

struct regulator {
	int unused;
};

struct icc_path {
	const char *name;
	u32 avg;
	u32 peak;
};

struct dev_pm_opp {
	unsigned long freq;
	unsigned long volt;
};

struct v4l2_device {
	int unused;
};

struct v4l2_ctrl;
struct v4l2_ctrl_ops {
	int (*s_ctrl)(struct v4l2_ctrl *ctrl);
};

struct v4l2_ctrl_handler {
	int error;
	int count;
	bool freed;
	struct v4l2_ctrl *ctrls[8];
};

struct v4l2_ctrl {
	struct v4l2_ctrl_handler *handler;
	const struct v4l2_ctrl_ops *ops;
	u32 id;
	s32 minimum;
	s32 maximum;
	s32 step;
	s32 default_value;
	s32 val;
};

struct v4l2_m2m_ctx;
struct v4l2_fh {
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler *ctrl_handler;
};

struct v4l2_rect {
	s32 left;
	s32 top;
	u32 width;
	u32 height;
};

struct v4l2_plane_pix_format {
	u32 sizeimage;
	u32 bytesperline;
};

struct v4l2_pix_format_mplane {
	u32 width;
	u32 height;
	u32 pixelformat;
	u32 field;
	u32 colorspace;
	struct v4l2_plane_pix_format plane_fmt[VIDEO_MAX_PLANES];
	u8 num_planes;
	u8 flags;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;
};

struct v4l2_capability {
	char driver[16];
	char card[32];
	char bus_info[32];
	u32 version;
	u32 capabilities;
	u32 device_caps;
	u32 reserved[3];
};

struct v4l2_plane {
	u32 bytesused;
	u32 length;
	union {
		u32 mem_offset;
		unsigned long userptr;
		s32 fd;
	} m;
	u32 data_offset;
	u32 reserved[11];
};

struct v4l2_buffer {
	u32 index;
	u32 type;
	u32 bytesused;
	u32 flags;
	u32 field;
	u32 memory;
	union {
		u32 offset;
		unsigned long userptr;
		struct v4l2_plane *planes;
		s32 fd;
	} m;
	u32 length;
};

struct vb2_queue {
	u32 type;
};

struct vb2_plane {
	dma_addr_t dma;
	unsigned int length;
	unsigned int bytesused;
	unsigned int data_offset;
};

struct vb2_buffer {
	struct vb2_queue *vb2_queue;
	unsigned int index;
	unsigned int num_planes;
	struct vb2_plane planes[VIDEO_MAX_PLANES];
};

struct vb2_v4l2_buffer {
	struct vb2_buffer vb2_buf;
	enum vb2_buffer_state state;
	int done;
};

struct v4l2_m2m_ctx {
	void *priv;
	struct vb2_v4l2_buffer *src[4];
	int nsrc;
	struct vb2_v4l2_buffer *dst[4];
	int ndst;
	bool running;
	bool released;
};

struct v4l2_m2m_dev {
	struct v4l2_m2m_ctx *curr;
	bool paused;
	int finished;
};

struct v4l2_m2m_ops;

static struct {
	u32 regs[0x1000 / 4];
	bool hw_running;
	int starts;
	int clocks_on;
	int larb_refs;
	int resumes;
	int suspends;
	bool resume_fails;
	int unclocked;
	int clock_off_running;
	unsigned long volt;
	unsigned long volt_max;
	int volt_lowered_running;
	int bw_dropped_running;
	int bw_set;
	int open_files;
	int vdev_released;
	int m2m_released;
	int v4l2_unregistered;
	int larb_dev_refs;
	int pm_disabled;
	int timeout_cancelled;
} m;

static inline void video_device_release(struct video_device *vdev)
{
	(void)vdev;
	assert(!m.vdev_released && "video device released twice");
	m.vdev_released++;
}

static inline void video_unregister_device(struct video_device *vdev)
{
	if (!m.open_files)
		vdev->release(vdev);
}

static inline void *platform_get_drvdata(const struct platform_device *pdev)
{
	return pdev->dev.drvdata;
}

static inline void pm_runtime_disable(struct device *dev)
{
	(void)dev;
	m.pm_disabled++;
}

static inline void v4l2_m2m_release(struct v4l2_m2m_dev *dev)
{
	(void)dev;
	assert(!m.m2m_released);
	m.m2m_released++;
}

static inline void v4l2_device_unregister(struct v4l2_device *dev)
{
	(void)dev;
	assert(!m.v4l2_unregistered);
	m.v4l2_unregistered++;
}

static inline void put_device(struct device *dev)
{
	(void)dev;
	assert(m.larb_dev_refs > 0 && "larb device reference dropped twice");
	m.larb_dev_refs--;
}

static inline void m_access(void)
{
	if (!m.clocks_on)
		m.unclocked++;
}

static inline u32 readl(const volatile void *addr)
{
	size_t offset = (const char *)addr - (const char *)m.regs;

	assert(offset < sizeof(m.regs));
	m_access();
	return m.regs[offset / 4];
}

static inline void writel(u32 value, volatile void *addr)
{
	size_t offset = (char *)addr - (char *)m.regs;

	assert(offset < sizeof(m.regs));
	m_access();
	m.regs[offset / 4] = value;
	if (offset == 0x100)
		m.hw_running = false;
	if (offset == 0x104 && (value & 1)) {
		m.hw_running = true;
		m.starts++;
	}
}

static inline const char *dev_name(const struct device *dev)
{
	return dev->name;
}

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->drvdata;
}

static inline void *video_drvdata(struct file *file)
{
	return file->vdev_drvdata;
}

static inline struct v4l2_fh *mindone_file_fh(struct file *file)
{
	return file->private_data;
}

static inline void mindone_v4l2_fh_del(struct v4l2_fh *fh, struct file *file)
{
	(void)fh;
	file->private_data = NULL;
}

static inline void v4l2_fh_exit(struct v4l2_fh *fh)
{
	(void)fh;
}

static inline void kfree(const void *p)
{
	free((void *)p);
}

static inline void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	lock->held = 1;
}

static inline void mutex_unlock(struct mutex *lock)
{
	assert(lock->held);
	lock->held = 0;
}

#define spin_lock_irqsave(lock, flags) do { (void)(lock); (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)

static inline bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	(void)delay;
	dwork->pending = true;
	return true;
}

static inline bool cancel_delayed_work(struct delayed_work *dwork)
{
	bool was = dwork->pending;

	dwork->pending = false;
	return was;
}

static inline bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	m.timeout_cancelled++;
	return cancel_delayed_work(dwork);
}

static inline size_t strscpy(char *dst, const char *src, size_t size)
{
	snprintf(dst, size, "%s", src);
	return strlen(dst);
}

static inline int clk_bulk_prepare_enable(int num, struct clk_bulk_data *clks)
{
	(void)clks;
	m.clocks_on += num;
	return 0;
}

static inline void clk_bulk_disable_unprepare(int num, struct clk_bulk_data *clks)
{
	(void)clks;
	if (m.hw_running)
		m.clock_off_running++;
	assert(m.clocks_on >= num);
	m.clocks_on -= num;
}

static inline int mtk_smi_larb_get(struct device *larb)
{
	assert(larb);
	m.larb_refs++;
	return 0;
}

static inline void mtk_smi_larb_put(struct device *larb)
{
	assert(larb);
	assert(m.larb_refs > 0);
	m.larb_refs--;
}

static inline int m_resume(struct device *dev)
{
	if (dev->active)
		return 0;
	if (m.resume_fails)
		return -EIO;
	dev->runtime_resume(dev);
	dev->active = true;
	m.resumes++;
	return 0;
}

static inline void m_suspend(struct device *dev)
{
	if (!dev->active)
		return;
	dev->runtime_suspend(dev);
	dev->active = false;
	m.suspends++;
}

static inline int pm_runtime_resume_and_get(struct device *dev)
{
	int ret;

	dev->usage++;
	ret = m_resume(dev);
	if (ret)
		dev->usage--;
	return ret;
}

static inline int pm_runtime_get_sync(struct device *dev)
{
	dev->usage++;
	return m_resume(dev);
}

static inline int pm_runtime_put(struct device *dev)
{
	assert(dev->usage > 0 && "runtime-PM usage count underflow");
	dev->usage--;
	if (!dev->usage)
		m_suspend(dev);
	return 0;
}

static inline int pm_runtime_force_suspend(struct device *dev)
{
	if (!dev->active)
		return 0;
	m_suspend(dev);
	dev->needs_force_resume = dev->usage > 0;
	return 0;
}

static inline int pm_runtime_force_resume(struct device *dev)
{
	if (!dev->needs_force_resume)
		return 0;
	dev->needs_force_resume = false;
	return m_resume(dev);
}

static struct dev_pm_opp m_opps[] = {
	{ 218000000, 575000 },
	{ 312000000, 650000 },
	{ 458000000, 750000 },
};

static inline struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev, unsigned long *freq)
{
	size_t i;

	(void)dev;
	for (i = 0; i < ARRAY_SIZE(m_opps); i++) {
		if (m_opps[i].freq >= *freq) {
			*freq = m_opps[i].freq;
			return &m_opps[i];
		}
	}
	return (struct dev_pm_opp *)(intptr_t)-ERANGE;
}

static inline unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	return opp->volt;
}

static inline void dev_pm_opp_put(struct dev_pm_opp *opp)
{
	(void)opp;
}

static inline int regulator_set_voltage(struct regulator *reg, int min_uv, int max_uv)
{
	(void)reg;
	(void)max_uv;
	if (m.hw_running && (unsigned long)min_uv < m.volt)
		m.volt_lowered_running++;
	m.volt = min_uv;
	if (m.volt > m.volt_max)
		m.volt_max = m.volt;
	return 0;
}

static inline int of_property_read_u32(const struct device_node *np, const char *name, u32 *value)
{
	(void)np;
	(void)name;
	(void)value;
	return -EINVAL;
}

#define kBps_to_icc(x) (x)
#define MBps_to_icc(x) ((x) * 1000)

static inline int mtk_icc_set_bw(struct icc_path *path, u32 avg, u32 peak)
{
	assert(path);
	if (m.hw_running && !avg && path->avg)
		m.bw_dropped_running++;
	if (avg)
		m.bw_set++;
	path->avg = avg;
	path->peak = peak;
	return 0;
}

static inline dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *vb, unsigned int plane)
{
	return vb->planes[plane].dma;
}

static inline unsigned long vb2_plane_size(struct vb2_buffer *vb, unsigned int plane)
{
	return vb->planes[plane].length;
}

static inline void vb2_set_plane_payload(struct vb2_buffer *vb, unsigned int plane, unsigned long size)
{
	vb->planes[plane].bytesused = size;
}

static inline struct vb2_v4l2_buffer *v4l2_m2m_next_src_buf(struct v4l2_m2m_ctx *ctx)
{
	return ctx->nsrc ? ctx->src[0] : NULL;
}

static inline struct vb2_v4l2_buffer *v4l2_m2m_next_dst_buf(struct v4l2_m2m_ctx *ctx)
{
	return ctx->ndst ? ctx->dst[0] : NULL;
}

static inline struct vb2_v4l2_buffer *m_pop(struct vb2_v4l2_buffer **queue, int *count)
{
	struct vb2_v4l2_buffer *vb;

	if (!*count)
		return NULL;
	vb = queue[0];
	memmove(queue, queue + 1, (*count - 1) * sizeof(*queue));
	(*count)--;
	return vb;
}

static inline struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(struct v4l2_m2m_ctx *ctx)
{
	return m_pop(ctx->src, &ctx->nsrc);
}

static inline struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(struct v4l2_m2m_ctx *ctx)
{
	return m_pop(ctx->dst, &ctx->ndst);
}

static inline void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *vb, enum vb2_buffer_state state)
{
	assert(vb);
	assert(!vb->done && "buffer completed twice");
	vb->state = state;
	vb->done++;
}

static inline void *v4l2_m2m_get_curr_priv(struct v4l2_m2m_dev *dev)
{
	return dev->curr ? dev->curr->priv : NULL;
}

static inline void v4l2_m2m_job_finish(struct v4l2_m2m_dev *dev, struct v4l2_m2m_ctx *ctx)
{
	assert(dev->curr == ctx && "job finished by a context that does not run");
	dev->curr = NULL;
	ctx->running = false;
	dev->finished++;
}

static inline int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *ctx, struct v4l2_buffer *buf);
static inline void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *ctx);
static inline void v4l2_m2m_suspend(struct v4l2_m2m_dev *dev);
static inline void v4l2_m2m_resume(struct v4l2_m2m_dev *dev);

static inline void v4l2_ctrl_handler_init(struct v4l2_ctrl_handler *hdl, int hint)
{
	(void)hint;
	memset(hdl, 0, sizeof(*hdl));
}

static inline struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *hdl, const struct v4l2_ctrl_ops *ops,
						 u32 id, s64 min, s64 max, u64 step, s64 def)
{
	struct v4l2_ctrl *ctrl;

	assert(hdl->count < (int)ARRAY_SIZE(hdl->ctrls));
	ctrl = calloc(1, sizeof(*ctrl));
	assert(ctrl);
	ctrl->handler = hdl;
	ctrl->ops = ops;
	ctrl->id = id;
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = step;
	ctrl->default_value = def;
	ctrl->val = def;
	hdl->ctrls[hdl->count++] = ctrl;
	return ctrl;
}

static inline int v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	int i;

	for (i = 0; i < hdl->count; i++)
		hdl->ctrls[i]->ops->s_ctrl(hdl->ctrls[i]);
	return 0;
}

static inline void v4l2_ctrl_handler_free(struct v4l2_ctrl_handler *hdl)
{
	int i;

	assert(!hdl->freed);
	for (i = 0; i < hdl->count; i++)
		free(hdl->ctrls[i]);
	hdl->count = 0;
	hdl->freed = true;
}

static inline struct v4l2_ctrl *m_find_ctrl(struct v4l2_ctrl_handler *hdl, u32 id)
{
	int i;

	for (i = 0; i < hdl->count; i++)
		if (hdl->ctrls[i]->id == id)
			return hdl->ctrls[i];
	return NULL;
}

static inline int m_s_ctrl(struct v4l2_ctrl_handler *hdl, u32 id, s32 value)
{
	struct v4l2_ctrl *ctrl = m_find_ctrl(hdl, id);

	if (!ctrl)
		return -EINVAL;
	ctrl->val = clamp(value, ctrl->minimum, ctrl->maximum);
	return ctrl->ops->s_ctrl(ctrl);
}

#endif
