/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_GPU_H_
#define _KBASE_HWACCESS_GPU_H_

#include <backend/gpu/mali_kbase_pm_internal.h>

bool kbase_gpu_irq_evict(struct kbase_device *kbdev, unsigned int js, u32 completion_code);

void kbase_gpu_complete_hw(struct kbase_device *kbdev, unsigned int js, u32 completion_code,
			   u64 job_tail, ktime_t *end_timestamp);

struct kbase_jd_atom *kbase_gpu_inspect(struct kbase_device *kbdev, unsigned int js, int idx);

void kbase_gpu_dump_slots(struct kbase_device *kbdev);

#endif
