/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_JM_H_
#define _KBASE_HWACCESS_JM_H_

void kbase_backend_run_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

void kbase_backend_slot_update(struct kbase_device *kbdev);

int kbase_backend_find_and_release_free_address_space(struct kbase_device *kbdev,
						      struct kbase_context *kctx);

bool kbase_backend_use_ctx(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr);

bool kbase_backend_use_ctx_sched(struct kbase_device *kbdev, struct kbase_context *kctx,
				 unsigned int js);

void kbase_backend_release_ctx_irq(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbase_backend_release_ctx_noirq(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbase_backend_cache_clean(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

void kbase_backend_complete_wq(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

#if !MALI_USE_CSF

void kbase_backend_complete_wq_post_sched(struct kbase_device *kbdev, base_jd_core_req core_req);
#endif

void kbase_backend_reset(struct kbase_device *kbdev, ktime_t *end_timestamp);

struct kbase_jd_atom *kbase_backend_inspect_tail(struct kbase_device *kbdev, unsigned int js);

int kbase_backend_nr_atoms_on_slot(struct kbase_device *kbdev, unsigned int js);

u32 kbase_backend_nr_atoms_submitted(struct kbase_device *kbdev, unsigned int js);

void kbase_backend_ctx_count_changed(struct kbase_device *kbdev);

void kbase_backend_timeouts_changed(struct kbase_device *kbdev);

int kbase_backend_slot_free(struct kbase_device *kbdev, unsigned int js);

void kbase_job_check_leave_disjoint(struct kbase_device *kbdev, struct kbase_jd_atom *target_katom);

void kbase_backend_jm_kill_running_jobs_from_kctx(struct kbase_context *kctx);

void kbase_jm_wait_for_zero_jobs(struct kbase_context *kctx);

u32 kbase_backend_get_current_flush_id(struct kbase_device *kbdev);

void kbase_job_slot_hardstop(struct kbase_context *kctx, unsigned int js,
			     struct kbase_jd_atom *target_katom);

bool kbase_gpu_atoms_submitted_any(struct kbase_device *kbdev);

void kbase_backend_slot_kctx_purge_locked(struct kbase_device *kbdev, struct kbase_context *kctx);

#endif
