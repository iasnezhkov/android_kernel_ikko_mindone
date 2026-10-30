/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_BACKEND_JM_H_
#define _KBASE_HWCNT_BACKEND_JM_H_

#include "hwcnt/backend/mali_kbase_hwcnt_backend.h"

struct kbase_device;

int kbase_hwcnt_backend_jm_create(struct kbase_device *kbdev,
				  struct kbase_hwcnt_backend_interface *iface);

void kbase_hwcnt_backend_jm_destroy(struct kbase_hwcnt_backend_interface *iface);

#endif
