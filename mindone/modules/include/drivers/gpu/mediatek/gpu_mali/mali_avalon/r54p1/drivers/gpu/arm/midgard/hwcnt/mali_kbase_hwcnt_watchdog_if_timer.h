/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_WATCHDOG_IF_TIMER_H_
#define _KBASE_HWCNT_WATCHDOG_IF_TIMER_H_

struct kbase_hwcnt_watchdog_interface;

int kbase_hwcnt_watchdog_if_timer_create(struct kbase_hwcnt_watchdog_interface *watchdog_if);

void kbase_hwcnt_watchdog_if_timer_destroy(struct kbase_hwcnt_watchdog_interface *watchdog_if);

#endif
