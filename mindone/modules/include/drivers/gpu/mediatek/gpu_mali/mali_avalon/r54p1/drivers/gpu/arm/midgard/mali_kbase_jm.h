/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JM_H_
#define _KBASE_JM_H_

#if !MALI_USE_CSF

u32 kbase_jm_kick(struct kbase_device *kbdev, u32 js_mask);

static inline u32 kbase_jm_kick_all(struct kbase_device *kbdev)
{
	return kbase_jm_kick(kbdev, (1U << kbdev->gpu_props.num_job_slots) - 1U);
}

void kbase_jm_try_kick(struct kbase_device *kbdev, u32 js_mask);

void kbase_jm_try_kick_all(struct kbase_device *kbdev);
#endif

#if !MALI_USE_CSF

void kbase_jm_idle_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

struct kbase_jd_atom *kbase_jm_return_atom_to_js(struct kbase_device *kbdev,
						 struct kbase_jd_atom *katom);

struct kbase_jd_atom *kbase_jm_complete(struct kbase_device *kbdev, struct kbase_jd_atom *katom,
					ktime_t *end_timestamp);
#endif

#endif
