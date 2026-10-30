/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_WATCHDOG_IF_H_
#define _KBASE_HWCNT_WATCHDOG_IF_H_

#include <linux/types.h>

struct kbase_hwcnt_watchdog_info;

typedef void kbase_hwcnt_watchdog_callback_fn(void *user_data);

typedef int kbase_hwcnt_watchdog_enable_fn(const struct kbase_hwcnt_watchdog_info *timer,
					   u32 period_ms,
					   kbase_hwcnt_watchdog_callback_fn *callback,
					   void *user_data);

typedef void kbase_hwcnt_watchdog_disable_fn(const struct kbase_hwcnt_watchdog_info *timer);

typedef void kbase_hwcnt_watchdog_modify_fn(const struct kbase_hwcnt_watchdog_info *timer,
					    u32 delay_ms);

struct kbase_hwcnt_watchdog_interface {
	const struct kbase_hwcnt_watchdog_info *timer;
	kbase_hwcnt_watchdog_enable_fn *enable;
	kbase_hwcnt_watchdog_disable_fn *disable;
	kbase_hwcnt_watchdog_modify_fn *modify;
};

#endif
