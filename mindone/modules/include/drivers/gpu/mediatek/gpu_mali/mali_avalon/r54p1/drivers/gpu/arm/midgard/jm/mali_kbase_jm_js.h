/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JM_JS_H_
#define _KBASE_JM_JS_H_

#include "mali_kbase_js_ctx_attr.h"

#define JS_MAX_RUNNING_JOBS 8

int kbasep_js_devdata_init(struct kbase_device *const kbdev);

void kbasep_js_devdata_halt(struct kbase_device *kbdev);

void kbasep_js_devdata_term(struct kbase_device *kbdev);

int kbasep_js_kctx_init(struct kbase_context *const kctx);

void kbasep_js_kctx_term(struct kbase_context *kctx);

static inline void kbase_jsctx_slot_prio_blocked_set(struct kbase_context *kctx, unsigned int js,
						     int sched_prio)
{
	struct kbase_jsctx_slot_tracking *slot_tracking = &kctx->slot_tracking[js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	WARN(!slot_tracking->atoms_pulled_pri[sched_prio],
	     "When marking slot %u as blocked for priority %d on a kctx, no atoms were pulled - the slot cannot become unblocked",
	     js, sched_prio);

	slot_tracking->blocked |= ((kbase_js_prio_bitmap_t)1) << sched_prio;
	KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev, JS_SLOT_PRIO_BLOCKED, kctx, NULL, 0, js,
				      (unsigned int)sched_prio);
}

static inline int kbase_jsctx_atoms_pulled(struct kbase_context *kctx)
{
	return atomic_read(&kctx->atoms_pulled_all_slots);
}

bool kbasep_js_add_job(struct kbase_context *kctx, struct kbase_jd_atom *atom);

void kbasep_js_remove_job(struct kbase_device *kbdev, struct kbase_context *kctx,
			  struct kbase_jd_atom *atom);

bool kbasep_js_remove_cancelled_job(struct kbase_device *kbdev, struct kbase_context *kctx,
				    struct kbase_jd_atom *katom);

void kbasep_js_runpool_requeue_or_kill_ctx(struct kbase_device *kbdev, struct kbase_context *kctx,
					   bool has_pm_ref);

void kbasep_js_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbasep_js_runpool_release_ctx_and_katom_retained_state(
	struct kbase_device *kbdev, struct kbase_context *kctx,
	struct kbasep_js_atom_retained_state *katom_retained_state);

void kbasep_js_runpool_release_ctx_nolock(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbasep_js_schedule_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbasep_js_release_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

void kbase_js_try_run_jobs(struct kbase_device *kbdev);

void kbasep_js_suspend(struct kbase_device *kbdev);

void kbasep_js_resume(struct kbase_device *kbdev);

bool kbase_js_dep_resolved_submit(struct kbase_context *kctx, struct kbase_jd_atom *katom);

struct kbase_jd_atom *kbase_js_pull(struct kbase_context *kctx, unsigned int js);

void kbase_js_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom);

bool kbase_js_complete_atom_wq(struct kbase_context *kctx, struct kbase_jd_atom *katom);

struct kbase_jd_atom *kbase_js_complete_atom(struct kbase_jd_atom *katom, ktime_t *end_timestamp);

void kbase_js_sched(struct kbase_device *kbdev, unsigned int js_mask);

void kbase_js_zap_context(struct kbase_context *kctx);

bool kbase_js_is_atom_valid(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

void kbase_js_set_timeouts(struct kbase_device *kbdev);

void kbase_js_set_ctx_priority(struct kbase_context *kctx, int new_priority);

void kbase_js_update_ctx_priority(struct kbase_context *kctx);

static inline bool kbasep_js_is_submit_allowed(struct kbasep_js_device_data *js_devdata,
					       struct kbase_context *kctx)
{
	u16 test_bit;
	bool is_allowed;

	if (WARN((kctx->as_nr == KBASEP_AS_NR_INVALID) || !kbase_ctx_flag(kctx, KCTX_SCHEDULED),
		 "%s: kctx %pK has assigned AS %d and context flag %d\n", __func__, (void *)kctx,
		 kctx->as_nr, atomic_read(&kctx->flags)))
		return false;

	test_bit = (u16)(1u << kctx->as_nr);

	is_allowed = (bool)(js_devdata->runpool_irq.submit_allowed & test_bit);
	dev_dbg(kctx->kbdev->dev, "JS: submit %s allowed on %pK (as=%d)",
		is_allowed ? "is" : "isn't", (void *)kctx, kctx->as_nr);
	return is_allowed;
}

static inline void kbasep_js_set_submit_allowed(struct kbasep_js_device_data *js_devdata,
						struct kbase_context *kctx)
{
	u16 set_bit;

	if (WARN((kctx->as_nr == KBASEP_AS_NR_INVALID) || !kbase_ctx_flag(kctx, KCTX_SCHEDULED),
		 "%s: kctx %pK has assigned AS %d and context flag %d\n", __func__, (void *)kctx,
		 kctx->as_nr, atomic_read(&kctx->flags)))
		return;

	set_bit = (u16)(1u << kctx->as_nr);

	dev_dbg(kctx->kbdev->dev, "JS: Setting Submit Allowed on %pK (as=%d)", kctx, kctx->as_nr);

	js_devdata->runpool_irq.submit_allowed |= set_bit;
}

static inline void kbasep_js_clear_submit_allowed(struct kbasep_js_device_data *js_devdata,
						  struct kbase_context *kctx)
{
	u16 clear_bit;
	u16 clear_mask;

	if (WARN((kctx->as_nr == KBASEP_AS_NR_INVALID) || !kbase_ctx_flag(kctx, KCTX_SCHEDULED),
		 "%s: kctx %pK has assigned AS %d and context flag %d\n", __func__, (void *)kctx,
		 kctx->as_nr, atomic_read(&kctx->flags)))
		return;

	clear_bit = (u16)(1u << kctx->as_nr);
	clear_mask = ~clear_bit;

	dev_dbg(kctx->kbdev->dev, "JS: Clearing Submit Allowed on %pK (as=%d)", kctx, kctx->as_nr);

	js_devdata->runpool_irq.submit_allowed &= clear_mask;
}

static inline void
kbasep_js_atom_retained_state_init_invalid(struct kbasep_js_atom_retained_state *retained_state)
{
	retained_state->event_code = BASE_JD_EVENT_NOT_STARTED;
	retained_state->core_req = KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID;
}

static inline void
kbasep_js_atom_retained_state_copy(struct kbasep_js_atom_retained_state *retained_state,
				   const struct kbase_jd_atom *katom)
{
	retained_state->event_code = katom->event_code;
	retained_state->core_req = katom->core_req;
	retained_state->sched_priority = katom->sched_priority;
	retained_state->device_nr = katom->device_nr;
}

static inline bool
kbasep_js_has_atom_finished(const struct kbasep_js_atom_retained_state *katom_retained_state)
{
	return (bool)(katom_retained_state->event_code != BASE_JD_EVENT_STOPPED &&
		      katom_retained_state->event_code != BASE_JD_EVENT_REMOVED_FROM_NEXT);
}

static inline bool kbasep_js_atom_retained_state_is_valid(
	const struct kbasep_js_atom_retained_state *katom_retained_state)
{
	return (bool)(katom_retained_state->core_req !=
		      KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID);
}

static inline void kbase_js_runpool_inc_context_count(struct kbase_device *kbdev,
						      struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);

	WARN_ON_ONCE(js_devdata->nr_all_contexts_running >= JS_MAX_RUNNING_JOBS);
	++(js_devdata->nr_all_contexts_running);

	if (!kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {

		WARN_ON_ONCE(js_devdata->nr_user_contexts_running >= JS_MAX_RUNNING_JOBS);
		++(js_devdata->nr_user_contexts_running);
	}
}

static inline void kbase_js_runpool_dec_context_count(struct kbase_device *kbdev,
						      struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);

	--(js_devdata->nr_all_contexts_running);
	WARN_ON_ONCE(js_devdata->nr_all_contexts_running < 0);

	if (!kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {

		--(js_devdata->nr_user_contexts_running);
		WARN_ON_ONCE(js_devdata->nr_user_contexts_running < 0);
	}
}

static inline void kbase_js_sched_all(struct kbase_device *kbdev)
{
	kbase_js_sched(kbdev, (1U << kbdev->gpu_props.num_job_slots) - 1U);
}

extern const int kbasep_js_atom_priority_to_relative[BASE_JD_NR_PRIO_LEVELS];

extern const base_jd_prio kbasep_js_relative_priority_to_atom[KBASE_JS_ATOM_SCHED_PRIO_COUNT];

static inline int kbasep_js_atom_prio_to_sched_prio(base_jd_prio atom_prio)
{
	if (atom_prio >= BASE_JD_NR_PRIO_LEVELS)
		return KBASE_JS_ATOM_SCHED_PRIO_INVALID;

	return kbasep_js_atom_priority_to_relative[atom_prio];
}

static inline base_jd_prio kbasep_js_sched_prio_to_atom_prio(struct kbase_device *kbdev,
							     int sched_prio)
{
	if (likely(sched_prio >= 0 && sched_prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT))
		return kbasep_js_relative_priority_to_atom[sched_prio];

	dev_warn(kbdev->dev, "Unknown JS scheduling priority %d", sched_prio);
	return BASE_JD_PRIO_INVALID;
}

base_jd_prio kbase_js_priority_check(struct kbase_device *kbdev, base_jd_prio priority);

bool kbase_js_atom_runs_before(struct kbase_device *kbdev, const struct kbase_jd_atom *katom_a,
			       const struct kbase_jd_atom *katom_b,
			       const kbase_atom_ordering_flag_t order_flags);

#endif
