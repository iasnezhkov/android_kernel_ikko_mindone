/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_UFS_H
#define MINDONE_COMPAT_UFS_H

#include <linux/version.h>
#include <ufs/ufs_quirks.h>

#ifdef UFS_DEVICE_QUIRK_DELAY_AFTER_LPM
#define MINDONE_UFS_QUIRK_DELAY_AFTER_LPM UFS_DEVICE_QUIRK_DELAY_AFTER_LPM
#else
#define MINDONE_UFS_QUIRK_DELAY_AFTER_LPM 0
#endif

static inline bool mindone_ufs_vcc_always_on_quirk(unsigned int dev_quirks)
{
#ifdef UFS_DEVICE_QUIRK_DELAY_AFTER_LPM
	return dev_quirks & UFS_DEVICE_QUIRK_DELAY_AFTER_LPM;
#else
	return true;
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_UFS_PWR_MAX_PARAM(name)
#define MINDONE_UFS_PWR_MAX_ARG(name)
#define MINDONE_UFS_NEGOTIATE_PWR_MODE_OP(fn) .negotiate_pwr_mode = fn,
#define MINDONE_UFS_PRE_NEGOTIATE_PWR_MODE(fn, hba, max, req) 0
#else
#define MINDONE_UFS_PWR_MAX_PARAM(name) struct ufs_pa_layer_attr *name,
#define MINDONE_UFS_PWR_MAX_ARG(name) name,
#define MINDONE_UFS_NEGOTIATE_PWR_MODE_OP(fn)
#define MINDONE_UFS_PRE_NEGOTIATE_PWR_MODE(fn, hba, max, req) fn(hba, max, req)
#endif

#endif
