// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_jm.h>
#include <mali_kbase_js.h>
#include <tl/mali_kbase_tracepoints.h>
#include <hwcnt/mali_kbase_hwcnt_context.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase_kinstr_jm.h>
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
#include <mali_kbase_gpu_metrics.h>
#endif
#include <backend/gpu/mali_kbase_cache_policy_backend.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
#include <gpu_bm.h>
#include <ged_gpu_bm.h>
#endif

#define SLOT_RB_EMPTY(rb) (rb->write_idx == rb->read_idx)

#define SLOT_RB_ENTRIES(rb) ((int)(s8)(rb->write_idx - rb->read_idx))

static void kbase_gpu_release_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom,
				   ktime_t *end_timestamp);

static void kbase_gpu_enqueue_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[katom->slot_nr];

	WARN_ON(SLOT_RB_ENTRIES(rb) >= SLOT_RB_SIZE);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	rb->entries[rb->write_idx & SLOT_RB_MASK].katom = katom;
	rb->write_idx++;

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_WAITING_BLOCKED;
}

static struct kbase_jd_atom *kbase_gpu_dequeue_atom(struct kbase_device *kbdev, unsigned int js,
						    ktime_t *end_timestamp)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];
	struct kbase_jd_atom *katom;

	if (SLOT_RB_EMPTY(rb)) {
		WARN(1, "GPU ringbuffer unexpectedly empty\n");
		return NULL;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = rb->entries[rb->read_idx & SLOT_RB_MASK].katom;

	kbase_gpu_release_atom(kbdev, katom, end_timestamp);

	rb->read_idx++;

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB;

	return katom;
}

struct kbase_jd_atom *kbase_gpu_inspect(struct kbase_device *kbdev, unsigned int js, int idx)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if ((SLOT_RB_ENTRIES(rb) - 1) < idx)
		return NULL;

	return rb->entries[(rb->read_idx + idx) & SLOT_RB_MASK].katom;
}

struct kbase_jd_atom *kbase_backend_inspect_tail(struct kbase_device *kbdev, unsigned int js)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];

	if (SLOT_RB_EMPTY(rb))
		return NULL;

	return rb->entries[(rb->write_idx - 1) & SLOT_RB_MASK].katom;
}

bool kbase_gpu_atoms_submitted_any(struct kbase_device *kbdev)
{
	unsigned int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int i;

		for (i = 0; i < SLOT_RB_SIZE; i++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

			if (katom && katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
				return true;
		}
	}
	return false;
}

u32 kbase_backend_nr_atoms_submitted(struct kbase_device *kbdev, unsigned int js)
{
	u32 nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

		if (katom && (katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED))
			nr++;
	}

	return nr;
}

int kbase_backend_nr_atoms_on_slot(struct kbase_device *kbdev, unsigned int js)
{
	int nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		if (kbase_gpu_inspect(kbdev, js, i))
			nr++;
	}

	return nr;
}

static int kbase_gpu_nr_atoms_on_slot_min(struct kbase_device *kbdev, unsigned int js,
					  enum kbase_atom_gpu_rb_state min_rb_state)
{
	int nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

		if (katom && (katom->gpu_rb_state >= min_rb_state))
			nr++;
	}

	return nr;
}

static bool check_secure_atom(struct kbase_jd_atom *katom, bool secure)
{
	if (katom->gpu_rb_state >= KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION &&
	    ((kbase_jd_katom_is_protected(katom) && secure) ||
	     (!kbase_jd_katom_is_protected(katom) && !secure)))
		return true;

	return false;
}

static bool kbase_gpu_check_secure_atoms(struct kbase_device *kbdev, bool secure)
{
	unsigned int js;

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int i;

		for (i = 0; i < SLOT_RB_SIZE; i++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

			if (katom) {
				if (check_secure_atom(katom, secure))
					return true;
			}
		}
	}

	return false;
}

int kbase_backend_slot_free(struct kbase_device *kbdev, unsigned int js)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) != KBASE_RESET_GPU_NOT_PENDING) {

		return 0;
	}

	return SLOT_RB_SIZE - kbase_backend_nr_atoms_on_slot(kbdev, js);
}

static inline void trace_atom_completion_for_gpu_metrics(struct kbase_jd_atom *const katom,
							 ktime_t *end_timestamp)
{
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	u64 complete_ns;
	struct kbase_context *kctx = katom->kctx;
	struct kbase_jd_atom *queued = kbase_gpu_inspect(kctx->kbdev, katom->slot_nr, 1);

#ifdef CONFIG_MALI_DEBUG
	WARN_ON(!kbase_gpu_inspect(kctx->kbdev, katom->slot_nr, 0));
#endif

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (unlikely(queued == katom))
		return;

	if (unlikely(kbase_jd_katom_is_protected(katom)))
		return;

	if (likely(end_timestamp))
		complete_ns = ktime_to_ns(*end_timestamp);
	else
		complete_ns = ktime_get_raw_ns();

	kbase_gpu_metrics_ctx_end_activity(kctx, complete_ns);
	if (queued && queued->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
		kbase_gpu_metrics_ctx_start_activity(queued->kctx, complete_ns);
#else
	CSTD_UNUSED(katom);
	CSTD_UNUSED(end_timestamp);
#endif
}

static void kbase_gpu_release_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom,
				   ktime_t *end_timestamp)
{
	struct kbase_context *kctx = katom->kctx;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom->gpu_rb_state) {
	case KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB:

		WARN(1, "Attempting to release atom not in ringbuffer\n");
		break;

	case KBASE_ATOM_GPU_RB_SUBMITTED:
		trace_atom_completion_for_gpu_metrics(katom, end_timestamp);
		kbase_kinstr_jm_atom_hw_release(katom);

		katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;
		kbase_pm_metrics_update(kbdev, end_timestamp);

		kbasep_platform_event_atom_complete(katom);

		if (katom->core_req & BASE_JD_REQ_PERMON)
			kbase_pm_release_gpu_cycle_counter_nolock(kbdev);

		KBASE_TLSTREAM_TL_NRET_ATOM_LPU(kbdev, katom,
						&kbdev->gpu_props.js_features[katom->slot_nr]);
		KBASE_TLSTREAM_TL_NRET_ATOM_AS(kbdev, katom, &kbdev->as[kctx->as_nr]);
		KBASE_TLSTREAM_TL_NRET_CTX_LPU(kbdev, kctx,
					       &kbdev->gpu_props.js_features[katom->slot_nr]);

		fallthrough;
	case KBASE_ATOM_GPU_RB_READY:

		fallthrough;
	case KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE:
		break;

	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION:
		if (kbase_jd_katom_is_protected(katom) &&
		    (katom->protected_state.enter != KBASE_ATOM_ENTER_PROTECTED_CHECK) &&
		    (katom->protected_state.enter != KBASE_ATOM_ENTER_PROTECTED_HWCNT)) {
			kbase_pm_protected_override_disable(kbdev);
			kbase_pm_update_cores_state_nolock(kbdev);
		}
		if (kbase_jd_katom_is_protected(katom) &&
		    (katom->protected_state.enter == KBASE_ATOM_ENTER_PROTECTED_IDLE_L2))
			kbase_pm_protected_entry_override_disable(kbdev);
		if (!kbase_jd_katom_is_protected(katom) &&
		    (katom->protected_state.exit != KBASE_ATOM_EXIT_PROTECTED_CHECK) &&
		    (katom->protected_state.exit != KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT)) {
			kbase_pm_protected_override_disable(kbdev);
			kbase_pm_update_cores_state_nolock(kbdev);
		}

		if (katom->protected_state.enter != KBASE_ATOM_ENTER_PROTECTED_CHECK ||
		    katom->protected_state.exit != KBASE_ATOM_EXIT_PROTECTED_CHECK)
			kbdev->protected_mode_transition = false;

		if (kbase_jd_katom_is_protected(katom) &&
		    (katom->protected_state.enter == KBASE_ATOM_ENTER_PROTECTED_HWCNT)) {
			kbdev->protected_mode_hwcnt_desired = true;
			if (kbdev->protected_mode_hwcnt_disabled) {
				kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
				kbdev->protected_mode_hwcnt_disabled = false;
			}
		}

		if (kbase_jd_katom_is_protected(katom) &&
		    ((katom->protected_state.enter == KBASE_ATOM_ENTER_PROTECTED_IDLE_L2) ||
		     (katom->protected_state.enter == KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY) ||
		     (katom->protected_state.enter == KBASE_ATOM_ENTER_PROTECTED_FINISHED))) {
			WARN_ON(!kbdev->protected_mode_hwcnt_disabled);
			kbdev->protected_mode_hwcnt_desired = true;
			if (kbdev->protected_mode_hwcnt_disabled) {
				kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
				kbdev->protected_mode_hwcnt_disabled = false;
			}
		}

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {
			if (katom->atom_flags & KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT) {
				kbase_pm_protected_l2_override(kbdev, false);
				katom->atom_flags &= ~KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
			}
		}

		fallthrough;
	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV:

		fallthrough;
	case KBASE_ATOM_GPU_RB_WAITING_BLOCKED:

		fallthrough;
	case KBASE_ATOM_GPU_RB_RETURN_TO_JS:
		break;
	}

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_WAITING_BLOCKED;
	katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;
}

static void kbase_gpu_mark_atom_for_return(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JM_MARK_FOR_RETURN_TO_JS, katom->kctx, katom,
				      katom->jc, katom->slot_nr, katom->event_code);
	kbase_gpu_release_atom(kbdev, katom, NULL);
	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_RETURN_TO_JS;
}

static inline bool other_slots_busy(struct kbase_device *kbdev, unsigned int js)
{
	unsigned int slot;

	for (slot = 0; slot < kbdev->gpu_props.num_job_slots; slot++) {
		if (slot == js)
			continue;

		if (kbase_gpu_nr_atoms_on_slot_min(kbdev, slot, KBASE_ATOM_GPU_RB_SUBMITTED))
			return true;
	}

	return false;
}

static inline bool kbase_gpu_in_protected_mode(struct kbase_device *kbdev)
{
	return kbdev->protected_mode;
}

static void kbase_gpu_disable_coherent(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->system_coherency == COHERENCY_ACE)
		kbase_cache_set_coherency_mode(kbdev, COHERENCY_ACE_LITE);
}

static int kbase_gpu_protected_mode_enter(struct kbase_device *kbdev)
{
	int err = -EINVAL;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ONCE(!kbdev->protected_ops,
		  "Cannot enter protected mode: protected callbacks not specified.\n");

	if (kbdev->protected_ops) {

		err = kbdev->protected_ops->protected_mode_enable(kbdev->protected_dev);

		if (err) {
			dev_warn(kbdev->dev, "Failed to enable protected mode: %d\n", err);
		} else {
			kbdev->protected_mode = true;
			kbase_ipa_protection_mode_switch_event(kbdev);
		}
	}

	return err;
}

static int kbase_gpu_protected_mode_reset(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ONCE(!kbdev->protected_ops,
		  "Cannot exit protected mode: protected callbacks not specified.\n");

	if (!kbdev->protected_ops)
		return -EINVAL;

	return kbase_reset_gpu_silent(kbdev);
}

static int kbase_jm_protected_entry(struct kbase_device *kbdev, struct kbase_jd_atom **katom,
				    int idx, unsigned int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	err = kbase_gpu_protected_mode_enter(kbdev);

	kbdev->protected_mode_transition = false;
	kbase_pm_protected_override_disable(kbdev);
	kbase_pm_update_cores_state_nolock(kbdev);

	KBASE_TLSTREAM_AUX_PROTECTED_ENTER_END(kbdev, kbdev);
	if (err) {

		katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
		kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);

		if (idx == 0 || katom[0]->gpu_rb_state == KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			kbase_jm_return_atom_to_js(kbdev, katom[idx]);
		}

		return -EINVAL;
	}

	WARN(kbase_jd_katom_is_protected(katom[idx]) != kbase_gpu_in_protected_mode(kbdev),
	     "Protected mode of atom (%d) doesn't match protected mode of GPU (%d)",
	     kbase_jd_katom_is_protected(katom[idx]), kbase_gpu_in_protected_mode(kbdev));
	katom[idx]->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;

	return err;
}

static int kbase_jm_enter_protected_mode(struct kbase_device *kbdev, struct kbase_jd_atom **katom,
					 int idx, unsigned int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom[idx]->protected_state.enter) {
	case KBASE_ATOM_ENTER_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_ENTER_START(kbdev, kbdev);

		WARN_ON(kbdev->protected_mode_transition);
		WARN_ON(kbase_gpu_atoms_submitted_any(kbdev));

		WARN_ON(kbdev->protected_mode_hwcnt_disabled);

		katom[idx]->protected_state.enter = KBASE_ATOM_ENTER_PROTECTED_HWCNT;

		kbdev->protected_mode_transition = true;

		fallthrough;
	case KBASE_ATOM_ENTER_PROTECTED_HWCNT:

		kbdev->protected_mode_hwcnt_desired = false;
		if (!kbdev->protected_mode_hwcnt_disabled) {
			if (kbase_hwcnt_context_disable_atomic(kbdev->hwcnt_gpu_ctx))
				kbdev->protected_mode_hwcnt_disabled = true;
		}

		if (!kbdev->protected_mode_hwcnt_disabled) {
			kbase_hwcnt_context_queue_work(kbdev->hwcnt_gpu_ctx,
						       &kbdev->protected_mode_hwcnt_disable_work);
			return -EAGAIN;
		}

		if (kbase_pm_protected_entry_override_enable(kbdev))
			return -EAGAIN;

		katom[idx]->protected_state.enter = KBASE_ATOM_ENTER_PROTECTED_IDLE_L2;

		kbase_pm_protected_override_enable(kbdev);

		if (!kbdev->pm.backend.protected_entry_transition_override)
			kbase_pm_update_cores_state_nolock(kbdev);

		fallthrough;
	case KBASE_ATOM_ENTER_PROTECTED_IDLE_L2:

		if (kbdev->system_coherency == COHERENCY_ACE) {
			if (kbdev->pm.backend.l2_always_on) {

				if (kbase_reset_gpu_is_active(kbdev))
					return -EAGAIN;
			}

			if (kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_L2) ||
			    kbase_pm_get_trans_cores(kbdev, KBASE_PM_CORE_L2) ||
			    kbase_is_gpu_removed(kbdev)) {

				return -EAGAIN;
			}
		}

		katom[idx]->protected_state.enter = KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY;

		fallthrough;
	case KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY:

		kbase_gpu_disable_coherent(kbdev);

		kbase_pm_protected_entry_override_disable(kbdev);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {

			kbase_pm_protected_l2_override(kbdev, true);

			katom[idx]->atom_flags |= KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
		}

		katom[idx]->protected_state.enter = KBASE_ATOM_ENTER_PROTECTED_FINISHED;

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234))
			return -EAGAIN;

		fallthrough;
	case KBASE_ATOM_ENTER_PROTECTED_FINISHED:
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {

			if (kbdev->pm.backend.l2_state == KBASE_L2_ON) {

				if (katom[idx]->atom_flags & KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT) {
					kbase_pm_protected_l2_override(kbdev, false);
					katom[idx]->atom_flags &=
						~KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
				}

				err = kbase_jm_protected_entry(kbdev, katom, idx, js);

				if (err)
					return err;
			} else {

				return -EAGAIN;
			}
		} else {
			err = kbase_jm_protected_entry(kbdev, katom, idx, js);

			if (err)
				return err;
		}
	}

	return 0;
}

static int kbase_jm_exit_protected_mode(struct kbase_device *kbdev, struct kbase_jd_atom **katom,
					int idx, unsigned int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom[idx]->protected_state.exit) {
	case KBASE_ATOM_EXIT_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_START(kbdev, kbdev);

		WARN_ON(kbdev->protected_mode_transition);
		WARN_ON(kbase_gpu_atoms_submitted_any(kbdev));

		katom[idx]->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_IDLE_L2;

		kbdev->protected_mode_transition = true;
		kbase_pm_protected_override_enable(kbdev);
		kbase_pm_update_cores_state_nolock(kbdev);

		fallthrough;
	case KBASE_ATOM_EXIT_PROTECTED_IDLE_L2:
		if (kbdev->pm.backend.l2_state != KBASE_L2_OFF) {

			return -EAGAIN;
		}
		katom[idx]->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_RESET;

		fallthrough;
	case KBASE_ATOM_EXIT_PROTECTED_RESET:

		kbase_pm_protected_override_disable(kbdev);

		err = kbase_gpu_protected_mode_reset(kbdev);

		if (err == -EAGAIN)
			return -EAGAIN;

		if (err) {
			kbdev->protected_mode_transition = false;

			katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
			kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);

			if (idx == 0 ||
			    katom[0]->gpu_rb_state == KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				kbase_jm_return_atom_to_js(kbdev, katom[idx]);
			}

			WARN_ON(!kbdev->protected_mode_hwcnt_disabled);
			kbdev->protected_mode_hwcnt_desired = true;
			if (kbdev->protected_mode_hwcnt_disabled) {
				kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
				kbdev->protected_mode_hwcnt_disabled = false;
			}

			return -EINVAL;
		}

		katom[idx]->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT;

		fallthrough;
	case KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT:

		return -EAGAIN;
	}

	return 0;
}

void kbase_backend_slot_update(struct kbase_device *kbdev)
{
	unsigned int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_reset_gpu_is_active(kbdev) || kbase_is_gpu_removed(kbdev))
#else
	if (kbase_reset_gpu_is_active(kbdev))
#endif
		return;

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		struct kbase_jd_atom *katom[2];
		int idx;

		katom[0] = kbase_gpu_inspect(kbdev, js, 0);
		katom[1] = kbase_gpu_inspect(kbdev, js, 1);
		WARN_ON(katom[1] && !katom[0]);

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			bool cores_ready;
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
			bool trace_atom_submit_for_gpu_metrics = true;
#endif
			int ret;

			if (!katom[idx])
				continue;

			switch (katom[idx]->gpu_rb_state) {
			case KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB:

				WARN(1, "Attempting to update atom not in ringbuffer\n");
				break;

			case KBASE_ATOM_GPU_RB_WAITING_BLOCKED:
				if (katom[idx]->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED)
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV;

				fallthrough;
			case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV:
				if (kbase_gpu_check_secure_atoms(
					    kbdev, !kbase_jd_katom_is_protected(katom[idx])))
					break;

				if ((idx == 1) && (kbase_jd_katom_is_protected(katom[0]) !=
						   kbase_jd_katom_is_protected(katom[1])))
					break;

				if (kbdev->protected_mode_transition)
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION;

				fallthrough;
			case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION:

				if (!kbase_gpu_in_protected_mode(kbdev) &&
				    kbase_jd_katom_is_protected(katom[idx])) {

					ret = kbase_jm_enter_protected_mode(kbdev, katom, idx, js);
					if (ret)
						break;
				} else if (kbase_gpu_in_protected_mode(kbdev) &&
					   !kbase_jd_katom_is_protected(katom[idx])) {

					ret = kbase_jm_exit_protected_mode(kbdev, katom, idx, js);
					if (ret)
						break;
				}
				katom[idx]->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE;

				fallthrough;
			case KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE:
				if (katom[idx]->will_fail_event_code) {
					kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);

					katom[idx]->event_code = BASE_JD_EVENT_DONE;

					if (idx == 0 || katom[0]->gpu_rb_state ==
								KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
						kbase_gpu_dequeue_atom(kbdev, js, NULL);
						kbase_jm_return_atom_to_js(kbdev, katom[idx]);
					}
					break;
				}

				cores_ready = kbase_pm_cores_requested(kbdev, true);

				if (!cores_ready)
					break;

				katom[idx]->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;

				fallthrough;
			case KBASE_ATOM_GPU_RB_READY:

				if (idx == 1) {
					enum kbase_atom_gpu_rb_state atom_0_gpu_rb_state =
						katom[0]->gpu_rb_state;

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
					trace_atom_submit_for_gpu_metrics =
						(atom_0_gpu_rb_state ==
						 KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB);
#endif

					if ((atom_0_gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED &&
					     atom_0_gpu_rb_state !=
						     KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB))
						break;

					if (kbdev->serialize_jobs & KBASE_SERIALIZE_INTRA_SLOT)
						break;
				}

				if ((kbdev->serialize_jobs & KBASE_SERIALIZE_INTER_SLOT) &&
				    other_slots_busy(kbdev, js))
					break;

				if (katom[idx]->core_req & BASE_JD_REQ_PERMON)
					kbase_pm_request_gpu_cycle_counter_l2_is_on(kbdev);

				if (!kbase_job_hw_submit(kbdev, katom[idx], js)) {
					katom[idx]->gpu_rb_state = KBASE_ATOM_GPU_RB_SUBMITTED;

					kbase_pm_metrics_update(kbdev,
								&katom[idx]->start_timestamp);

					kbasep_platform_event_atom_submit(katom[idx]);
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
					if (likely(trace_atom_submit_for_gpu_metrics &&
						   !kbase_jd_katom_is_protected(katom[idx])))
						kbase_gpu_metrics_ctx_start_activity(
							katom[idx]->kctx,
							ktime_to_ns(katom[idx]->start_timestamp));
#endif
				} else {
					if (katom[idx]->core_req & BASE_JD_REQ_PERMON)
						kbase_pm_release_gpu_cycle_counter_nolock(kbdev);

					break;
				}

				fallthrough;
			case KBASE_ATOM_GPU_RB_SUBMITTED:
				break;

			case KBASE_ATOM_GPU_RB_RETURN_TO_JS:

				if (idx == 0 ||
				    katom[0]->gpu_rb_state == KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
					kbase_gpu_dequeue_atom(kbdev, js, NULL);
					kbase_jm_return_atom_to_js(kbdev, katom[idx]);
				}
				break;
			}
		}
	}
}

void kbase_backend_run_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "Backend running atom %pK\n", (void *)katom);

	kbase_gpu_enqueue_atom(kbdev, katom);
	kbase_backend_slot_update(kbdev);
}

static inline bool kbase_rb_atom_might_depend(const struct kbase_jd_atom *katom_a,
					      const struct kbase_jd_atom *katom_b)
{
	if (katom_a->kctx != katom_b->kctx)
		return false;
	return (katom_b->pre_dep || (katom_b->atom_flags & (KBASE_KATOM_FLAG_X_DEP_BLOCKED |
							    KBASE_KATOM_FLAG_FAIL_BLOCKER)));
}

static inline void kbase_gpu_remove_atom(struct kbase_device *kbdev, struct kbase_jd_atom *katom,
					 u32 action, bool disjoint)
{
	struct kbase_context *kctx = katom->kctx;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
	kbase_gpu_mark_atom_for_return(kbdev, katom);
	kbase_jsctx_slot_prio_blocked_set(kctx, katom->slot_nr, katom->sched_priority);

	if (disjoint)
		kbase_job_check_enter_disjoint(kbdev, action, katom->core_req, katom);
}

bool kbase_gpu_irq_evict(struct kbase_device *kbdev, unsigned int js, u32 completion_code)
{
	struct kbase_jd_atom *katom;
	struct kbase_jd_atom *next_katom;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = kbase_gpu_inspect(kbdev, js, 0);
	if (!katom) {
		dev_err(kbdev->dev, "Can't get a katom from js(%u)\n", js);
		return false;
	}
	next_katom = kbase_gpu_inspect(kbdev, js, 1);

	if (next_katom && next_katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED &&
	    (kbase_rb_atom_might_depend(katom, next_katom) ||
	     kbase_js_atom_runs_before(kbdev, katom, next_katom, 0u)) &&
	    kbase_reg_read64(kbdev, JOB_SLOT_OFFSET(js, HEAD_NEXT)) != 0) {
		kbase_reg_write32(kbdev, JOB_SLOT_OFFSET(js, COMMAND_NEXT), JS_COMMAND_NOP);

		if (completion_code == BASE_JD_EVENT_STOPPED) {
			kbase_gpu_remove_atom(kbdev, next_katom, JS_COMMAND_SOFT_STOP, false);
			KBASE_TLSTREAM_TL_NRET_ATOM_LPU(
				kbdev, next_katom,
				&kbdev->gpu_props.js_features[next_katom->slot_nr]);
			KBASE_TLSTREAM_TL_NRET_ATOM_AS(kbdev, next_katom,
						       &kbdev->as[next_katom->kctx->as_nr]);
			KBASE_TLSTREAM_TL_NRET_CTX_LPU(
				kbdev, next_katom->kctx,
				&kbdev->gpu_props.js_features[next_katom->slot_nr]);
		} else {
			next_katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;

			if (next_katom->core_req & BASE_JD_REQ_PERMON)
				kbase_pm_release_gpu_cycle_counter_nolock(kbdev);
		}

		kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged =
			SLOT_RB_TAG_KCTX(katom->kctx);

		return true;
	}

	return false;
}

void kbase_gpu_complete_hw(struct kbase_device *kbdev, unsigned int js, u32 completion_code,
			   u64 job_tail, ktime_t *end_timestamp)
{
	struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, 0);
	struct kbase_context *kctx = NULL;

	if (unlikely(!katom)) {
		dev_err(kbdev->dev, "Can't get a katom from js(%d)\n", js);
		return;
	}

	kctx = katom->kctx;

	dev_dbg(kbdev->dev, "Atom %pK completed on hw with code 0x%x and job_tail 0x%llx (s:%d)\n",
		(void *)katom, completion_code, job_tail, js);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TMIX_8438)) {
		if (completion_code == BASE_JD_EVENT_STOPPED &&
		    (katom->atom_flags & KBASE_KATOM_FLAG_BEEN_HARD_STOPPED)) {
			completion_code = BASE_JD_EVENT_TERMINATED;
		}
	}

	if ((katom->core_req & BASE_JD_REQ_SKIP_CACHE_END) &&
	    completion_code != BASE_JD_EVENT_DONE && !(completion_code & BASE_JD_SW_EVENT)) {

		katom->need_cache_flush_cores_retained = true;
	}

	katom = kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);

	if (completion_code == BASE_JD_EVENT_STOPPED) {
		struct kbase_jd_atom *next_katom = kbase_gpu_inspect(kbdev, js, 0);

		if (next_katom && kbase_js_atom_runs_before(kbdev, katom, next_katom, 0u)) {
			WARN_ON(next_katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED);
			kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);
			kbase_jm_return_atom_to_js(kbdev, next_katom);
		}
	} else if (completion_code != BASE_JD_EVENT_DONE) {
		struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
		unsigned int i;

		if (!kbase_ctx_flag(katom->kctx, KCTX_DYING) &&
		    !kbase_ctx_flag(katom->kctx, KCTX_PAGE_FAULT_REPORT_SKIP)) {
			dev_warn(kbdev->dev, "error detected from slot %d, job status 0x%08x (%s)",
				 js, completion_code, kbase_gpu_exception_name(completion_code));

		}

#if KBASE_KTRACE_DUMP_ON_JOB_SLOT_ERROR != 0
		KBASE_KTRACE_DUMP(kbdev);
#endif
		kbasep_js_clear_submit_allowed(js_devdata, katom->kctx);

		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++) {
			struct kbase_jd_atom *katom_idx0 = kbase_gpu_inspect(kbdev, i, 0);
			struct kbase_jd_atom *katom_idx1 = kbase_gpu_inspect(kbdev, i, 1);

			if (katom_idx0 && kbase_rb_atom_might_depend(katom, katom_idx0) &&
			    katom_idx0->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {

				kbase_gpu_dequeue_atom(kbdev, i, end_timestamp);

				if (katom_idx1 && kbase_rb_atom_might_depend(katom, katom_idx1) &&
				    katom_idx0->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {

					kbase_gpu_dequeue_atom(kbdev, i, end_timestamp);

					katom_idx1->event_code = BASE_JD_EVENT_STOPPED;
					kbase_jm_return_atom_to_js(kbdev, katom_idx1);
				}
				katom_idx0->event_code = BASE_JD_EVENT_STOPPED;
				kbase_jm_return_atom_to_js(kbdev, katom_idx0);

			} else if (katom_idx1 && kbase_rb_atom_might_depend(katom, katom_idx1) &&
				   katom_idx1->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {

				katom_idx1->event_code = BASE_JD_EVENT_STOPPED;
				kbase_gpu_mark_atom_for_return(kbdev, katom_idx1);
			}
		}
	}

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JM_JOB_DONE, kctx, katom, katom->jc, js,
				      completion_code);

	if (job_tail != 0 && job_tail != katom->jc) {

		dev_dbg(kbdev->dev, "Update job chain address of atom %pK to resume from 0x%llx\n",
			(void *)katom, job_tail);

		katom->jc = job_tail;
		KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_UPDATE_HEAD, katom->kctx, katom, job_tail, js);
	}

	if (katom->event_code != BASE_JD_EVENT_JOB_CANCELLED)
		katom->event_code = (enum base_jd_event_code)completion_code;

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	{

		struct kbase_jd_atom *next_katom = kbase_gpu_inspect(kbdev, js, 0);

		if (next_katom && next_katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED) {
			char js_string[16];

			trace_gpu_sched_switch(kbasep_make_job_slot_string(js, js_string,
									   sizeof(js_string)),
					       ktime_to_ns(*end_timestamp),
					       (u32)next_katom->kctx->id, 0, next_katom->work_id);
#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
			if(js == 0) {
				kbdev->v1->ctx = (u32)next_katom->kctx->id;
				kbdev->v1->job = next_katom->work_id;
				kbdev->v1->frame = (u32)qos_get_frame_nr();
				kbdev->v1->freq = js;
			}
#endif
		} else {
			char js_string[16];

			trace_gpu_sched_switch(kbasep_make_job_slot_string(js, js_string,
									   sizeof(js_string)),
					       ktime_to_ns(ktime_get_raw()), 0, 0, 0);
		}
	}
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
	{

		struct kbase_jd_atom *next_katom = kbase_gpu_inspect(kbdev, js,
									0);

		if (next_katom && next_katom->gpu_rb_state ==
						KBASE_ATOM_GPU_RB_SUBMITTED) {
			if (js == 0) {
				kbdev->v1->ctx = (u32)next_katom->kctx->id;
				kbdev->v1->job = next_katom->work_id;
				kbdev->v1->frame = (u32)qos_get_frame_nr();
				kbdev->v1->freq = js;
			}
		}
	}
#endif

	if (kbdev->serialize_jobs & KBASE_SERIALIZE_RESET)
		kbase_reset_gpu_silent(kbdev);

	if (completion_code == BASE_JD_EVENT_STOPPED)
		katom = kbase_jm_return_atom_to_js(kbdev, katom);
	else
		katom = kbase_jm_complete(kbdev, katom, end_timestamp);

	if (katom) {
		dev_dbg(kbdev->dev, "Cross-slot dependency %pK has become runnable.\n",
			(void *)katom);

		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		kbase_jm_try_kick(kbdev, 1UL << katom->slot_nr);
	}

	kbase_pm_update_state(kbdev);

	kbase_backend_slot_update(kbdev);
}

void kbase_backend_reset(struct kbase_device *kbdev, ktime_t *end_timestamp)
{
	unsigned int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(kbase_gpu_in_protected_mode(kbdev));

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int atom_idx = 0;
		int idx;

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, atom_idx);
			bool keep_in_jm_rb = false;

			if (!katom)
				break;
			if (katom->protected_state.exit == KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT) {

				WARN(kbase_jd_katom_is_protected(katom) !=
					     kbase_gpu_in_protected_mode(kbdev),
				     "Protected mode of atom (%d) doesn't match protected mode of GPU (%d)",
				     kbase_jd_katom_is_protected(katom),
				     kbase_gpu_in_protected_mode(kbdev));
				WARN(!(kbase_jd_katom_is_protected(katom) && js == 0) &&
					     kbase_jd_katom_is_protected(katom),
				     "Protected atom on JS%u not supported", js);
			}
			if ((katom->gpu_rb_state < KBASE_ATOM_GPU_RB_SUBMITTED) &&
			    !kbase_ctx_flag(katom->kctx, KCTX_DYING))
				keep_in_jm_rb = true;

			kbase_gpu_release_atom(kbdev, katom, NULL);

			if (!kbase_is_gpu_removed(kbdev) && keep_in_jm_rb) {
				katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;

				atom_idx++;
				continue;
			}

			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
			kbase_jm_complete(kbdev, katom, end_timestamp);
		}

		kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged = SLOT_RB_NULL_TAG_VAL;
	}

	kbdev->protected_mode_hwcnt_desired = true;
	if (kbdev->protected_mode_hwcnt_disabled) {
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
		kbdev->protected_mode_hwcnt_disabled = false;

		KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_END(kbdev, kbdev);
	}

	kbdev->protected_mode_transition = false;
	kbase_pm_protected_override_disable(kbdev);
}

static bool should_stop_next_atom(struct kbase_device *kbdev,
				  const struct kbase_jd_atom *head_katom,
				  const struct kbase_jd_atom *next_katom, u32 action)
{
	bool ret = false;
	u32 hw_action = action & JS_COMMAND_MASK;

	switch (hw_action) {
	case JS_COMMAND_SOFT_STOP:
		ret = kbase_js_atom_runs_before(kbdev, head_katom, next_katom, 0u);
		break;
	case JS_COMMAND_HARD_STOP:

		ret = (head_katom->kctx == next_katom->kctx);
		break;
	default:

		WARN(1, "Unexpected stop action: 0x%.8x", hw_action);
		break;
	}
	return ret;
}

static inline void kbase_gpu_stop_atom(struct kbase_device *kbdev, unsigned int js,
				       struct kbase_jd_atom *katom, u32 action)
{
	struct kbase_context *kctx = katom->kctx;
	u32 hw_action = action & JS_COMMAND_MASK;

	kbase_job_check_enter_disjoint(kbdev, action, katom->core_req, katom);
	kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, hw_action, katom->core_req, katom);
	kbase_jsctx_slot_prio_blocked_set(kctx, js, katom->sched_priority);
}

static int should_stop_x_dep_slot(struct kbase_jd_atom *katom)
{
	if (katom->x_post_dep) {
		struct kbase_jd_atom *dep_atom = katom->x_post_dep;

		if (dep_atom->gpu_rb_state != KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB &&
		    dep_atom->gpu_rb_state != KBASE_ATOM_GPU_RB_RETURN_TO_JS)
			return (int)dep_atom->slot_nr;
	}
	return -1;
}

bool kbase_backend_soft_hard_stop_slot(struct kbase_device *kbdev, struct kbase_context *kctx,
				       unsigned int js, struct kbase_jd_atom *katom, u32 action)
{
	struct kbase_jd_atom *katom_idx0;
	struct kbase_context *kctx_idx0 = NULL;
	struct kbase_jd_atom *katom_idx1;
	struct kbase_context *kctx_idx1 = NULL;

	bool katom_idx0_valid, katom_idx1_valid;

	bool ret = false;

	int stop_x_dep_idx0 = -1, stop_x_dep_idx1 = -1;
	int prio_idx0 = 0, prio_idx1 = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom_idx0 = kbase_gpu_inspect(kbdev, js, 0);
	katom_idx1 = kbase_gpu_inspect(kbdev, js, 1);

	if (katom_idx0) {
		kctx_idx0 = katom_idx0->kctx;
		prio_idx0 = katom_idx0->sched_priority;
	}
	if (katom_idx1) {
		kctx_idx1 = katom_idx1->kctx;
		prio_idx1 = katom_idx1->sched_priority;
	}

	if (katom) {
		katom_idx0_valid = (katom_idx0 == katom);
		if (katom_idx1)
			katom_idx1_valid = (katom_idx1 == katom);
		else
			katom_idx1_valid = false;
	} else {
		katom_idx0_valid = (katom_idx0 && (!kctx || kctx_idx0 == kctx));
		katom_idx1_valid = (katom_idx1 && (!kctx || kctx_idx1 == kctx));
	}

	if (!katom_idx1_valid && katom_idx0_valid && katom_idx1)
		katom_idx1_valid = should_stop_next_atom(kbdev, katom_idx0, katom_idx1, action);

	if (katom_idx0_valid)
		stop_x_dep_idx0 = should_stop_x_dep_slot(katom_idx0);
	if (katom_idx1_valid)
		stop_x_dep_idx1 = should_stop_x_dep_slot(katom_idx1);

	if (katom_idx0_valid) {
		if (katom_idx0->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {

			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			if (katom_idx1_valid) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				katom_idx1->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
				kbase_jm_return_atom_to_js(kbdev, katom_idx1);
				kbase_jsctx_slot_prio_blocked_set(kctx_idx1, js, prio_idx1);
			}

			katom_idx0->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
			kbase_jm_return_atom_to_js(kbdev, katom_idx0);
			kbase_jsctx_slot_prio_blocked_set(kctx_idx0, js, prio_idx0);
		} else {

			if (katom_idx1_valid &&
			    katom_idx1->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED) {

				if (kbase_reg_read32(kbdev, JOB_SLOT_OFFSET(js, COMMAND_NEXT)) ==
				    0) {

					if (katom_idx1_valid) {
						kbase_gpu_stop_atom(kbdev, js, katom_idx1, action);
						ret = true;
					}
				} else {

					kbase_reg_write32(kbdev, JOB_SLOT_OFFSET(js, COMMAND_NEXT),
							  JS_COMMAND_NOP);

					if (kbase_reg_read64(kbdev,
							     JOB_SLOT_OFFSET(js, HEAD_NEXT)) != 0) {

						kbase_gpu_remove_atom(kbdev, katom_idx1, action,
								      true);

						kbdev->hwaccess.backend.slot_rb[js]
							.last_kctx_tagged =
							SLOT_RB_TAG_KCTX(katom_idx0->kctx);

						stop_x_dep_idx1 =
							should_stop_x_dep_slot(katom_idx1);

						kbase_gpu_stop_atom(kbdev, js, katom_idx0, action);
						ret = true;
					} else if (katom_idx1_valid) {

						kbase_gpu_stop_atom(kbdev, js, katom_idx1, action);
						ret = true;
					}
				}
			} else if (katom_idx1_valid) {

				kbase_gpu_remove_atom(kbdev, katom_idx1, action, false);

				kbase_gpu_stop_atom(kbdev, js, katom_idx0, action);
				ret = true;
			} else {

				kbase_gpu_stop_atom(kbdev, js, katom_idx0, action);
				ret = true;
			}
		}
	} else if (katom_idx1_valid) {
		if (katom_idx1->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {

			kbase_gpu_remove_atom(kbdev, katom_idx1, action, false);
		} else {

			if (kbase_reg_read32(kbdev, JOB_SLOT_OFFSET(js, COMMAND_NEXT)) == 0) {

				kbase_gpu_stop_atom(kbdev, js, katom_idx1, action);
				ret = true;
			} else {

				kbase_reg_write32(kbdev, JOB_SLOT_OFFSET(js, COMMAND_NEXT),
						  JS_COMMAND_NOP);

				if (kbase_reg_read64(kbdev, JOB_SLOT_OFFSET(js, HEAD_NEXT)) != 0) {

					kbase_gpu_remove_atom(kbdev, katom_idx1, action, false);

					kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged =
						kctx_idx0 ? SLOT_RB_TAG_KCTX(katom_idx0->kctx) :
								  SLOT_RB_TAG_PURGED;
				} else {

					kbase_gpu_stop_atom(kbdev, js, katom_idx1, action);
					ret = true;
				}
			}
		}
	}

	if (stop_x_dep_idx0 != -1)
		kbase_backend_soft_hard_stop_slot(kbdev, kctx, (unsigned int)stop_x_dep_idx0, NULL,
						  action);

	if (stop_x_dep_idx1 != -1)
		kbase_backend_soft_hard_stop_slot(kbdev, kctx, (unsigned int)stop_x_dep_idx1, NULL,
						  action);

	return ret;
}

void kbase_backend_cache_clean(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	if (katom->need_cache_flush_cores_retained) {
		kbase_gpu_start_cache_clean(kbdev, GPU_COMMAND_CACHE_CLN_INV_FULL);
		kbase_gpu_wait_cache_clean(kbdev);

		katom->need_cache_flush_cores_retained = false;
	}
}

void kbase_backend_complete_wq(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{

	kbase_backend_cache_clean(kbdev, katom);
}

void kbase_backend_complete_wq_post_sched(struct kbase_device *kbdev, base_jd_core_req core_req)
{
	CSTD_UNUSED(core_req);

	if (!kbdev->pm.active_count) {
		kbase_pm_lock(kbdev);
		kbase_pm_update_active(kbdev);
		kbase_pm_unlock(kbdev);
	}
}

void kbase_gpu_dump_slots(struct kbase_device *kbdev)
{
	unsigned long flags;
	unsigned int js;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	dev_info(kbdev->dev, "%s:\n", __func__);

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int idx;

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, idx);

			if (katom)
				dev_info(kbdev->dev, "  js%u idx%d : katom=%pK gpu_rb_state=%d\n",
					 js, idx, katom, katom->gpu_rb_state);
			else
				dev_info(kbdev->dev, "  js%u idx%d : empty\n", js, idx);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_backend_slot_kctx_purge_locked(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	unsigned int js;
	bool tracked = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		u64 tagged_kctx = kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged;

		if (tagged_kctx == SLOT_RB_TAG_KCTX(kctx)) {

			kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged = SLOT_RB_TAG_PURGED;
			tracked = true;
		}
	}

	if (tracked) {

		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged ==
			    SLOT_RB_NULL_TAG_VAL)
				kbdev->hwaccess.backend.slot_rb[js].last_kctx_tagged =
					SLOT_RB_TAG_PURGED;
		}
	}
}
