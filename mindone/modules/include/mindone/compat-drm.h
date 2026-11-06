/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_DRM_H
#define MINDONE_COMPAT_DRM_H

#include <drm/drm_mode_config.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_DRM_FB_CREATE_INFO const struct drm_format_info *info,
#define MINDONE_DRM_MODE_VALID_MODE const struct drm_display_mode
#else
#define MINDONE_DRM_FB_CREATE_INFO
#define MINDONE_DRM_MODE_VALID_MODE struct drm_display_mode
#endif

#endif
