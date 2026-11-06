/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_PM_CA_H_
#define _KBASE_PM_CA_H_

int kbase_pm_ca_init(struct kbase_device *kbdev);

void kbase_pm_ca_term(struct kbase_device *kbdev);

u64 kbase_pm_ca_get_core_mask(struct kbase_device *kbdev);

u64 kbase_pm_ca_get_debug_core_mask(struct kbase_device *kbdev);

void kbase_pm_ca_update_core_status(struct kbase_device *kbdev, u64 cores_ready,
				    u64 cores_transitioning);

u64 kbase_pm_ca_get_instr_core_mask(struct kbase_device *kbdev);

#endif
