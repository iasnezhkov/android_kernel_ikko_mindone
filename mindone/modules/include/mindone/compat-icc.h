/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mindone/compat-icc.h — interconnect-provider pieces of the 6.1/6.12 compatibility layer.
 * Kept apart from compat.h: MediaTek modules ship their own mtk-interconnect-provider.h
 * that redefines the icc structs and must not see <linux/interconnect-provider.h>.
 */
#ifndef __MINDONE_COMPAT_ICC_H__
#define __MINDONE_COMPAT_ICC_H__

#include <linux/version.h>
#include <linux/interconnect-provider.h>

/* icc_provider_add()/del() (6.1 wrappers) are gone in 6.3; init+register exist in both kernels. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define MINDONE_ICC_PROVIDER_ADD(_p)	({ icc_provider_init(_p); icc_provider_register(_p); })
#define MINDONE_ICC_PROVIDER_DEL(_p)	icc_provider_deregister(_p)
#else
#define MINDONE_ICC_PROVIDER_ADD(_p)	icc_provider_add(_p)
#define MINDONE_ICC_PROVIDER_DEL(_p)	icc_provider_del(_p)
#endif


#endif /* __MINDONE_COMPAT_ICC_H__ */
