/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CACHE_POLICY_BACKEND_H_
#define _KBASE_CACHE_POLICY_BACKEND_H_

#include <linux/types.h>

struct kbase_device;

void kbase_cache_set_coherency_mode(struct kbase_device *kbdev, u32 mode);

void kbase_amba_set_shareable_cache_support(struct kbase_device *kbdev);
#endif
