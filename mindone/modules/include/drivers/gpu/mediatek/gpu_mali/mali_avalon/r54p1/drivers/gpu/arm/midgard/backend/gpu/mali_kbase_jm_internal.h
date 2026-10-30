/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JM_HWACCESS_H_
#define _KBASE_JM_HWACCESS_H_

#include <mali_kbase_hw.h>
#include <mali_kbase_debug.h>
#include <linux/atomic.h>

#include <backend/gpu/mali_kbase_jm_rb.h>
#include <device/mali_kbase_device.h>

void kbase_job_done_slot(struct kbase_device *kbdev, int s, u32 completion_code, u64 job_tail,
			 ktime_t *end_timestamp);

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
static inline char *kbasep_make_job_slot_string(unsigned int js, char *js_string, size_t js_size)
{
	(void)scnprintf(js_string, js_size, "job_slot_%u", js);
	return js_string;
}
#endif

int kbase_job_hw_submit(struct kbase_device *kbdev, struct kbase_jd_atom *katom, unsigned int js);

#if !MALI_USE_CSF

void kbasep_job_slot_soft_or_hard_stop_do_action(struct kbase_device *kbdev, unsigned int js,
						 u32 action, base_jd_core_req core_reqs,
						 struct kbase_jd_atom *target_katom);
#endif

bool kbase_backend_soft_hard_stop_slot(struct kbase_device *kbdev, struct kbase_context *kctx,
				       unsigned int js, struct kbase_jd_atom *katom, u32 action);

int kbase_job_slot_init(struct kbase_device *kbdev);

void kbase_job_slot_halt(struct kbase_device *kbdev);

void kbase_job_slot_term(struct kbase_device *kbdev);

void kbase_gpu_cache_clean(struct kbase_device *kbdev);

#endif
