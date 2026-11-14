/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_SYSFS_H
#define MINDONE_COMPAT_SYSFS_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_BIN_ATTR_CONST const
#else
#define MINDONE_BIN_ATTR_CONST
#endif

#endif
