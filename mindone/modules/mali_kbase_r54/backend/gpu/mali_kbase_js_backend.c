// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_reset_gpu.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_js_internal.h>
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
#include <mali_kbase_gpu_metrics.h>
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
#include <platform/mtk_platform_common.h>
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif

static inline bool timer_callback_should_run(struct kbase_device *kbdev, int nr_running_ctxs)
{
	lockdep_assert_held(&kbdev->js_data.runpool_mutex);

#ifdef CONFIG_MALI_DEBUG
	if (kbdev->js_data.softstop_always) {

		return true;
	}
#endif

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9435)) {

		{
			int nr_compute_ctxs = kbasep_js_ctx_attr_count_on_runpool(
				kbdev, KBASEP_JS_CTX_ATTR_COMPUTE);
			int nr_noncompute_ctxs = nr_running_ctxs - nr_compute_ctxs;

			return (bool)(nr_compute_ctxs >= 2 || nr_noncompute_ctxs > 0);
		}
	} else {

		return (bool)(nr_running_ctxs > 0);
	}
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	unsigned long flags;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_backend_data *backend;
	unsigned int s;
	bool reset_needed = false;

	KBASE_DEBUG_ASSERT(timer != NULL);

	backend = container_of(timer, struct kbase_backend_data, scheduling_timer);
	kbdev = container_of(backend, struct kbase_device, hwaccess.backend);
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	for (s = 0; s < kbdev->gpu_props.num_job_slots; s++) {
		struct kbase_jd_atom *atom = NULL;

		if (kbase_backend_nr_atoms_on_slot(kbdev, s) > 0) {
			atom = kbase_gpu_inspect(kbdev, s, 0);
			KBASE_DEBUG_ASSERT(atom != NULL);
		}

		if (atom != NULL) {

			if (!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_5736)) {
				u32 ticks = atom->ticks++;

#if !defined(CONFIG_MALI_JOB_DUMP) && !defined(CONFIG_MALI_VECTOR_DUMP)
				u32 soft_stop_ticks, hard_stop_ticks, gpu_reset_ticks;
				if (atom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
					soft_stop_ticks = js_devdata->soft_stop_ticks_cl;
					hard_stop_ticks = js_devdata->hard_stop_ticks_cl;
					gpu_reset_ticks = js_devdata->gpu_reset_ticks_cl;
				} else {
					soft_stop_ticks = js_devdata->soft_stop_ticks;
					hard_stop_ticks = js_devdata->hard_stop_ticks_ss;
					gpu_reset_ticks = js_devdata->gpu_reset_ticks_ss;
				}

				if (backend->timeouts_updated && ticks > soft_stop_ticks)
					ticks = atom->ticks = soft_stop_ticks;

				if (ticks == soft_stop_ticks) {

#if !KBASE_DISABLE_SCHEDULING_SOFT_STOPS
					int disjoint_threshold =
						KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD;
					u32 softstop_flags = 0u;

					dev_dbg(kbdev->dev, "Soft-stop");

					if (js_devdata->nr_user_contexts_running >=
					    disjoint_threshold)
						softstop_flags |= JS_COMMAND_SW_CAUSES_DISJOINT;

					kbase_job_slot_softstop_swflags(kbdev, s, atom,
									softstop_flags);
#endif
				} else if (ticks == hard_stop_ticks) {

#if !KBASE_DISABLE_SCHEDULING_HARD_STOPS
					u32 ms = js_devdata->scheduling_period_ns / 1000000u;
					dev_warn(
						kbdev->dev,
						"JS: Job Hard-Stopped (took more than %u ticks at %u ms/tick)",
						ticks, ms);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
					mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
						"JS: Job Hard-Stopped (took more than %u ticks at %u ms/tick)\n",
						ticks, ms);
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
					mtk_common_debug(MTK_COMMON_DBG_DUMP_PM_STATUS, NULL, MTK_DBG_HOOK_NA);
					mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, NULL, MTK_DBG_HOOK_NA);
#endif
					kbase_job_slot_hardstop(atom->kctx, s, atom);
#endif
				} else if (ticks == gpu_reset_ticks) {

					reset_needed = true;
				}
#else

				if (ticks == js_devdata->soft_stop_ticks) {

					dev_dbg(kbdev->dev, "Soft-stop");
				} else if (ticks == js_devdata->hard_stop_ticks_dumping) {

#if !KBASE_DISABLE_SCHEDULING_HARD_STOPS
					u32 ms = js_devdata->scheduling_period_ns / 1000000u;
					dev_warn(
						kbdev->dev,
						"JS: Job Hard-Stopped (took more than %u ticks at %u ms/tick)",
						ticks, ms);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
					mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
						"JS: Job Hard-Stopped (took more than %u ticks at %u ms/tick)\n",
						ticks, ms);
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
					mtk_common_debug(MTK_COMMON_DBG_DUMP_PM_STATUS, NULL, MTK_DBG_HOOK_NA);
					mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, NULL, MTK_DBG_HOOK_NA);
#endif
					kbase_job_slot_hardstop(atom->kctx, s, atom);
#endif
				} else if (ticks == js_devdata->gpu_reset_ticks_dumping) {

					reset_needed = true;
				}
#endif
			}
		}
	}
	if (reset_needed) {
		dev_err(kbdev->dev,
			"JS: Job has been on the GPU for too long (JS_RESET_TICKS_SS/DUMPING timeout hit). Issuing GPU soft-reset to resolve.");

		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu_locked(kbdev);
	}

	if (backend->timer_running)
		hrtimer_start(&backend->scheduling_timer,
			      HR_TIMER_DELAY_NSEC(js_devdata->scheduling_period_ns),
			      HRTIMER_MODE_REL);

	backend->timeouts_updated = false;

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return HRTIMER_NORESTART;
}

void kbase_backend_ctx_count_changed(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;
	unsigned long flags;

	const bool suspend_timer = backend->suspend_timer;
	const int nr_running_ctxs = atomic_read(&kbdev->js_data.nr_contexts_runnable);

	lockdep_assert_held(&js_devdata->runpool_mutex);

	if (suspend_timer || !timer_callback_should_run(kbdev, nr_running_ctxs)) {

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		backend->timer_running = false;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		hrtimer_cancel(&backend->scheduling_timer);
	}

	if (!suspend_timer && timer_callback_should_run(kbdev, nr_running_ctxs) &&
	    !backend->timer_running) {

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		backend->timer_running = true;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		hrtimer_start(&backend->scheduling_timer,
			      HR_TIMER_DELAY_NSEC(js_devdata->scheduling_period_ns),
			      HRTIMER_MODE_REL);

		KBASE_KTRACE_ADD_JM(kbdev, JS_POLICY_TIMER_START, NULL, NULL, 0u, 0u);
	}

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	if (unlikely(suspend_timer)) {
		js_devdata->gpu_metrics_timer_needed = false;

		hrtimer_cancel(&js_devdata->gpu_metrics_timer);
		js_devdata->gpu_metrics_timer_running = false;
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		kbase_gpu_metrics_emit_tracepoint(kbdev, ktime_get_raw_ns());
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return;
	}

	if (!nr_running_ctxs) {

		js_devdata->gpu_metrics_timer_needed = false;
		return;
	}

	if (!js_devdata->gpu_metrics_timer_needed) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		js_devdata->gpu_metrics_timer_needed = true;

		if (!js_devdata->gpu_metrics_timer_running) {
			hrtimer_start(&js_devdata->gpu_metrics_timer,
				      HR_TIMER_DELAY_NSEC(kbase_gpu_metrics_get_tp_emit_interval()),
				      HRTIMER_MODE_REL);
			js_devdata->gpu_metrics_timer_running = true;
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif
}

int kbase_backend_timer_init(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	hrtimer_init(&backend->scheduling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	backend->scheduling_timer.function = timer_callback;
	backend->timer_running = false;

	return 0;
}

void kbase_backend_timer_term(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	hrtimer_cancel(&backend->scheduling_timer);
}

void kbase_backend_timer_suspend(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->suspend_timer = true;

	kbase_backend_ctx_count_changed(kbdev);
}

void kbase_backend_timer_resume(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->suspend_timer = false;

	kbase_backend_ctx_count_changed(kbdev);
}

void kbase_backend_timeouts_changed(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->timeouts_updated = true;
}
