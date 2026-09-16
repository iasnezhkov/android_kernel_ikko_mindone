// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_mode_config.h>
#include "mindone_log.h"
#include <drm/drm_fourcc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <linux/dma-buf.h>

#include "mtk_drm_drv.h"
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_fb.h>
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_gem.h>
#include "mtk_drm_crtc.h"

/*
 * mtk specific framebuffer structure.
 *
 * @fb: drm framebuffer object.
 * @gem_obj: array of gem objects.
 */
struct mtk_drm_fb {
	struct drm_framebuffer base;
	/* For now we only support a single plane */
	struct drm_gem_object *gem_obj;
};

#define to_mtk_fb(x) container_of(x, struct mtk_drm_fb, base)

struct drm_gem_object *mtk_fb_get_gem_obj(struct drm_framebuffer *fb)
{
	struct mtk_drm_fb *mtk_fb = to_mtk_fb(fb);

	return mtk_fb->gem_obj;
}

size_t mtk_fb_get_size(struct drm_framebuffer *fb)
{
	struct mtk_drm_fb *mtk_fb = to_mtk_fb(fb);
	struct mtk_drm_gem_obj *mtk_gem = NULL;

	if (!mtk_fb->gem_obj)
		return 0;

	mtk_gem = to_mtk_gem_obj(mtk_fb->gem_obj);
	if (!mtk_gem)
		return 0;

	return mtk_gem->size;
}

dma_addr_t mtk_fb_get_dma(struct drm_framebuffer *fb)
{
	struct mtk_drm_fb *mtk_fb = to_mtk_fb(fb);
	struct mtk_drm_gem_obj *mtk_gem = NULL;

	if (!mtk_fb->gem_obj)
		return 0;

	mtk_gem = to_mtk_gem_obj(mtk_fb->gem_obj);
	if (!mtk_gem)
		return 0;

	return mtk_gem->dma_addr;
}

bool mtk_drm_fb_is_secure(struct drm_framebuffer *fb)
{
	struct drm_gem_object *gem = NULL;
	struct mtk_drm_gem_obj *mtk_gem = NULL;


	if (!fb)
		return false;
	gem = mtk_fb_get_gem_obj(fb);
	if (!gem)
		return false;
	mtk_gem = to_mtk_gem_obj(gem);
	return mtk_gem->sec;
}

int mtk_fb_get_sec_id(struct drm_framebuffer *fb)
{
	struct drm_gem_object *gem = NULL;
	struct mtk_drm_gem_obj *mtk_gem = NULL;


	if (!fb)
		return false;
	gem = mtk_fb_get_gem_obj(fb);
	if (!gem)
		return false;
	mtk_gem = to_mtk_gem_obj(gem);
	return mtk_gem->sec_id;
}
EXPORT_SYMBOL_GPL(mtk_fb_get_sec_id);

static int mtk_drm_fb_create_handle(struct drm_framebuffer *fb,
				    struct drm_file *file_priv,
				    unsigned int *handle)
{
	struct mtk_drm_fb *mtk_fb = to_mtk_fb(fb);

	return drm_gem_handle_create(file_priv, mtk_fb->gem_obj, handle);
}

static void mtk_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct mtk_drm_fb *mtk_fb = to_mtk_fb(fb);

	drm_framebuffer_cleanup(fb);
	drm_gem_object_put(mtk_fb->gem_obj);

	kfree(mtk_fb);
}

static const struct drm_framebuffer_funcs mtk_drm_fb_funcs = {
	.create_handle = mtk_drm_fb_create_handle,
	.destroy = mtk_drm_fb_destroy,
};

static struct mtk_drm_fb *
mtk_drm_framebuffer_init(struct drm_device *dev,
			 const struct drm_mode_fb_cmd2 *mode,
			 struct drm_gem_object *obj)
{
	struct mtk_drm_fb *mtk_fb;
	int ret;

	mtk_fb = kzalloc(sizeof(*mtk_fb), GFP_KERNEL);
	if (!mtk_fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, &mtk_fb->base, mode);

	mtk_fb->gem_obj = obj;
	/* MINDONE-FB-OBJ (F687): the driver is architecturally single-plane (see the
	 * struct mtk_drm_fb comment and mtk_drm_mode_fb_create(), which reads only
	 * cmd->handles[0]) and fills ONLY its private gem_obj, leaving fb->obj[] NULL.
	 * The kernel's generic drm_gem_plane_helper_prepare_fb() (used automatically
	 * since mtk_plane_helper_funcs.prepare_fb isn't set) reads fb->obj[plane] and
	 * unconditionally returns -EINVAL on NULL, rejecting EVERY atomic commit and
	 * crashing vendor.hwcomposer-2-3 each cycle. Mirror the value under the name
	 * the kernel expects; num_planes is always 1 here, so index 0 suffices. */
	mtk_fb->base.obj[0] = obj;

	ret = drm_framebuffer_init(dev, &mtk_fb->base, &mtk_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer\n");
		kfree(mtk_fb);
		return ERR_PTR(ret);
	}

	return mtk_fb;
}

struct drm_framebuffer *
mtk_drm_framebuffer_create(struct drm_device *dev,
			   const struct drm_mode_fb_cmd2 *mode,
			   struct drm_gem_object *obj)
{
	struct mtk_drm_fb *mtk_fb;

	mtk_fb = mtk_drm_framebuffer_init(dev, mode, obj);
	if (IS_ERR(mtk_fb))
		return ERR_CAST(mtk_fb);

	return &mtk_fb->base;
}

/*
 * Wait for any exclusive fence in fb's gem object's reservation object.
 *
 * Returns -ERESTARTSYS if interrupted, else 0.
 */
int mtk_fb_wait(struct drm_framebuffer *fb)
{
	struct drm_gem_object *gem;
	struct dma_resv *resv;
	long ret;

	if (!fb)
		return 0;

	gem = mtk_fb_get_gem_obj(fb);
	if (!gem || !gem->dma_buf || !gem->dma_buf->resv)
		return 0;

	resv = gem->dma_buf->resv;
	/* dma_resv_wait_timeout_rcu(resv, test_all=false, intr, timeout)
	 * removed upstream. dma_resv_usage_rw(write=false) is the real,
	 * documented helper that returns the equivalent modern usage
	 * level: this function (see docstring above: "wait for any
	 * exclusive fence") is a read-side wait, and dma_resv_usage_rw(false)
	 * returns DMA_RESV_USAGE_WRITE -- wait only for write-level fences,
	 * exactly matching the old test_all=false (exclusive-only) behavior. */
	ret = dma_resv_wait_timeout(resv, dma_resv_usage_rw(false), true,
				    MAX_SCHEDULE_TIMEOUT);
	/* MAX_SCHEDULE_TIMEOUT on success, -ERESTARTSYS if interrupted */
	if (ret < 0) {
		DDPAEE("%s:%d, invalid ret:%ld\n",
			__func__, __LINE__,
			ret);
		return ret;
	}

	return 0;
}

struct drm_framebuffer *
mtk_drm_mode_fb_create(struct drm_device *dev, struct drm_file *file,
		       const struct drm_mode_fb_cmd2 *cmd)
{
	struct mtk_drm_fb *mtk_fb = NULL;
	struct drm_gem_object *gem = NULL;
	struct mtk_drm_gem_obj *mtk_gem = NULL;
	unsigned int width = cmd->width;
	unsigned int height = cmd->height;
	unsigned int size, bpp;
	int ret;

	MINDONE_PR("MINDONE-AC: fb-create begin fmt=0x%x w=%u h=%u\n",
		cmd->pixel_format, cmd->width, cmd->height);
	if (cmd->pixel_format == DRM_FORMAT_C8)
		goto fb_init;

	gem = drm_gem_object_lookup(file, cmd->handles[0]);
	if (!gem) {
		MINDONE_PR("MINDONE-AC: fb-create end ret=err (no-gem)\n");
		return ERR_PTR(-ENOENT);
	}

	bpp = mtk_drm_format_plane_cpp(cmd->pixel_format, 0);
	size = (height - 1) * cmd->pitches[0] + width * bpp;
	size += cmd->offsets[0];

	mtk_gem = to_mtk_gem_obj(gem);

	if (cmd->modifier[0] & MTK_FMT_SECURE)
		mtk_gem->sec = true;

	//TO-DO: should need remove "!mtk_gem->sec"
	if (gem->size < size && !mtk_gem->sec) {
		DRM_ERROR("%s:%d, size:(%ld,%d), sec:%d\n",
			__func__, __LINE__,
			gem->size, size,
			mtk_gem->sec);
		DRM_ERROR("w:%d, h:%d, bpp:(%d,%d), pitch:%d, offset:%d\n",
			width, height,
			cmd->pixel_format, bpp,
			cmd->pitches[0],
			cmd->offsets[0]);
		ret = -EINVAL;
		goto unreference;
	}

fb_init:
	mtk_fb = mtk_drm_framebuffer_init(dev, cmd, gem);
	if (IS_ERR(mtk_fb)) {
		ret = PTR_ERR(mtk_fb);
		goto unreference;
	}

	MINDONE_PR("MINDONE-AC: fb-create end ret=0 fb=%px\n", mtk_fb);
	return &mtk_fb->base;

unreference:
	MINDONE_PR("MINDONE-AC: fb-create end ret=%d (unreference)\n", ret);
	drm_gem_object_put(gem);
	return ERR_PTR(ret);
}
