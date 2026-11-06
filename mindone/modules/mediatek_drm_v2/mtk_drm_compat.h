// SPDX-License-Identifier: GPL-2.0
#ifndef MTK_DRM_COMPAT_H
#define MTK_DRM_COMPAT_H

#include <linux/version.h>
#include <drm/drm_panel.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_mode.h>

static inline int mtk_drm_panel_prepare(struct drm_panel *panel)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	drm_panel_prepare(panel);
	return 0;
#else
	return drm_panel_prepare(panel);
#endif
}

static inline int mtk_drm_panel_enable(struct drm_panel *panel)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	drm_panel_enable(panel);
	return 0;
#else
	return drm_panel_enable(panel);
#endif
}

static inline int mtk_drm_panel_disable(struct drm_panel *panel)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	drm_panel_disable(panel);
	return 0;
#else
	return drm_panel_disable(panel);
#endif
}

static inline int mtk_drm_panel_unprepare(struct drm_panel *panel)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	drm_panel_unprepare(panel);
	return 0;
#else
	return drm_panel_unprepare(panel);
#endif
}

static inline void
mtk_drm_mode_fill_fb_struct(struct drm_device *dev, struct drm_framebuffer *fb,
			    const struct drm_mode_fb_cmd2 *mode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	drm_helper_mode_fill_fb_struct(dev, fb,
			drm_get_format_info(dev, mode->pixel_format,
					    mode->modifier[0]), mode);
#else
	drm_helper_mode_fill_fb_struct(dev, fb, mode);
#endif
}

#endif
