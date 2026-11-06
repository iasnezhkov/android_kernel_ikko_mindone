/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_LINUX_H_
#define _KBASE_LINUX_H_

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/atomic.h>

#if IS_ENABLED(MALI_KERNEL_TEST_API)
#define KBASE_EXPORT_TEST_API(func) EXPORT_SYMBOL(func)

#define KBASE_ALLOW_ERROR_INJECTION_TEST_API(func, etype) ALLOW_ERROR_INJECTION(func, etype)
#else
#define KBASE_EXPORT_TEST_API(func)
#define KBASE_ALLOW_ERROR_INJECTION_TEST_API(func, etype)
#endif

#define KBASE_EXPORT_SYMBOL(func) EXPORT_SYMBOL(func)

#endif
