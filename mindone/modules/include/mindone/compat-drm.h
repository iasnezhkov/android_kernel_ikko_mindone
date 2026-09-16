/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-drm.h — DRM pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_DRM_H__
#define __MINDONE_COMPAT_DRM_H__

#include <linux/version.h>
#include <drm/drm_ioctl.h>

/* DRM_UNLOCKED (skip the legacy global mutex) is the only behaviour since 6.8 and the flag is gone. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define MINDONE_DRM_UNLOCKED		0
#else
#define MINDONE_DRM_UNLOCKED		DRM_UNLOCKED
#endif

/*
 * drm_driver::gem_prime_mmap is gone since 6.3: PRIME mmap always goes through the GEM
 * object's own funcs->mmap. Drivers keep the callback for 6.1 only.
 */
#include <drm/drm_drv.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define MINDONE_DRM_GEM_PRIME_MMAP(fn)
#else
#define MINDONE_DRM_GEM_PRIME_MMAP(fn)	.gem_prime_mmap = (fn),
#endif

#endif /* __MINDONE_COMPAT_DRM_H__ */
