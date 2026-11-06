/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_UFS_H
#define MINDONE_COMPAT_UFS_H

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

#endif
