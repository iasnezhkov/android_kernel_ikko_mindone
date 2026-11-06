/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_TIMECOUNTER_H
#define MINDONE_COMPAT_TIMECOUNTER_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_CYCLECOUNTER_CONST
#else
#define MINDONE_CYCLECOUNTER_CONST const
#endif

#endif
