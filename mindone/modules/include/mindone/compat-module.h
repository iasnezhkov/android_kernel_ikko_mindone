/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_MODULE_H
#define MINDONE_COMPAT_MODULE_H

#include <linux/module.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
#define MINDONE_MODULE_IMPORT_NS(ns) MODULE_IMPORT_NS(ns)
#else
#define MINDONE_MODULE_IMPORT_NS(ns) MODULE_INFO(import_ns, ns)
#endif

#endif
