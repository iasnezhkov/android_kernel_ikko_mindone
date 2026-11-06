/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_PM_H_
#define _KBASE_HWACCESS_PM_H_

#include <linux/types.h>
#include <linux/atomic.h>

struct kbase_device;
struct kbase_pm_policy;
struct kbase_pm_ca_policy;

int kbase_hwaccess_pm_init(struct kbase_device *kbdev);

void kbase_hwaccess_pm_term(struct kbase_device *kbdev);

int kbase_hwaccess_pm_powerup(struct kbase_device *kbdev, unsigned int flags);

void kbase_hwaccess_pm_halt(struct kbase_device *kbdev);

int kbase_hwaccess_pm_suspend(struct kbase_device *kbdev);

void kbase_hwaccess_pm_resume(struct kbase_device *kbdev);

void kbase_hwaccess_pm_gpu_active(struct kbase_device *kbdev);

void kbase_hwaccess_pm_gpu_idle(struct kbase_device *kbdev);

#if MALI_USE_CSF

void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev, u64 new_core_mask);
#else

void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev, u64 *new_core_mask,
				  size_t new_core_mask_size);
#endif

const struct kbase_pm_ca_policy *kbase_pm_ca_get_policy(struct kbase_device *kbdev);

void kbase_pm_ca_set_policy(struct kbase_device *kbdev, const struct kbase_pm_ca_policy *policy);

int kbase_pm_ca_list_policies(const struct kbase_pm_ca_policy *const **policies);

const struct kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev);

void kbase_pm_set_policy(struct kbase_device *kbdev, const struct kbase_pm_policy *policy);

size_t kbase_pm_list_policies(struct kbase_device *kbdev,
			      const struct kbase_pm_policy *const **list);

int kbase_pm_protected_mode_enable(struct kbase_device *kbdev);

int kbase_pm_protected_mode_disable(struct kbase_device *kbdev);

#endif
