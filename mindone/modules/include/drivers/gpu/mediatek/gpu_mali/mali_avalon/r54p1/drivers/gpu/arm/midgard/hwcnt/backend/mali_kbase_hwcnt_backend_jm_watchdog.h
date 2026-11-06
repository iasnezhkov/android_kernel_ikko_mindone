/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_BACKEND_JM_WATCHDOG_H_
#define _KBASE_HWCNT_BACKEND_JM_WATCHDOG_H_

#include <hwcnt/backend/mali_kbase_hwcnt_backend.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if.h>

int kbase_hwcnt_backend_jm_watchdog_create(struct kbase_hwcnt_backend_interface *backend_iface,
					   struct kbase_hwcnt_watchdog_interface *watchdog_iface,
					   struct kbase_hwcnt_backend_interface *out_iface);

void kbase_hwcnt_backend_jm_watchdog_destroy(struct kbase_hwcnt_backend_interface *iface);

#endif
