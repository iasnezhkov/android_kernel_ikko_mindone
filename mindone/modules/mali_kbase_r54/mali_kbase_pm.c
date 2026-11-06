// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <mali_kbase_kinstr_prfcnt.h>
#include <hwcnt/mali_kbase_hwcnt_context.h>

#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include <arbiter/mali_kbase_arbiter_pm.h>
#endif

#include <backend/gpu/mali_kbase_clk_rate_trace_mgr.h>

int kbase_pm_powerup(struct kbase_device *kbdev, unsigned int flags)
{
	return kbase_hwaccess_pm_powerup(kbdev, flags);
}

void kbase_pm_halt(struct kbase_device *kbdev)
{
	kbase_hwaccess_pm_halt(kbdev);
}

void kbase_pm_context_active(struct kbase_device *kbdev)
{
	(void)kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE);
}

int kbase_pm_context_active_handle_suspend_locked(struct kbase_device *kbdev,
						  enum kbase_pm_suspend_handler suspend_handler)
{
	int c;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	dev_dbg(kbdev->dev, "%s - reason = %d, pid = %d\n", __func__, suspend_handler,
		current->pid);
	lockdep_assert_held(&kbdev->pm.lock);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_arbiter_pm_ctx_active_handle_suspend(kbdev, suspend_handler))
		return 1;
#endif

	if (kbase_pm_is_suspending(kbdev)) {
		switch (suspend_handler) {
		case KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE:
			if (kbdev->pm.active_count != 0)
				break;
			fallthrough;
		case KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE:
			return 1;

		case KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE:
			fallthrough;
		default:
			KBASE_DEBUG_ASSERT_MSG(false, "unreachable");
			break;
		}
	}
	c = ++kbdev->pm.active_count;
	KBASE_KTRACE_ADD(kbdev, PM_CONTEXT_ACTIVE, NULL, (u64)c);

	if (c == 1) {

		kbase_hwaccess_pm_gpu_active(kbdev);
#ifdef CONFIG_MALI_ARBITER_SUPPORT
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_REF_EVENT);
#endif
		kbase_clk_rate_trace_manager_gpu_active(kbdev);
	}

	dev_dbg(kbdev->dev, "%s %d\n", __func__, kbdev->pm.active_count);

	return 0;
}

int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev,
					   enum kbase_pm_suspend_handler suspend_handler)
{
	int ret;

	kbase_pm_lock(kbdev);
	ret = kbase_pm_context_active_handle_suspend_locked(kbdev, suspend_handler);
	kbase_pm_unlock(kbdev);

	return ret;
}

KBASE_EXPORT_TEST_API(kbase_pm_context_active);

void kbase_pm_context_idle_locked(struct kbase_device *kbdev)
{
	int c;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	lockdep_assert_held(&kbdev->pm.lock);

	c = --kbdev->pm.active_count;
	KBASE_KTRACE_ADD(kbdev, PM_CONTEXT_IDLE, NULL, (u64)c);

	KBASE_DEBUG_ASSERT(c >= 0);

	if (c == 0) {

		kbase_hwaccess_pm_gpu_idle(kbdev);
		kbase_clk_rate_trace_manager_gpu_idle(kbdev);

		wake_up(&kbdev->pm.zero_active_count_wait);
	}

	dev_dbg(kbdev->dev, "%s %d (pid = %d)\n", __func__, kbdev->pm.active_count, current->pid);
}

void kbase_pm_context_idle(struct kbase_device *kbdev)
{
	kbase_pm_lock(kbdev);
	kbase_pm_context_idle_locked(kbdev);
	kbase_pm_unlock(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_context_idle);

static void reenable_hwcnt_on_resume(struct kbase_device *kbdev)
{
	unsigned long flags;

#if MALI_USE_CSF
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
#else
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#endif

#if MALI_USE_CSF
	if (kbdev->csf.firmware_inited)
#endif
	{
		kbase_kinstr_prfcnt_resume(kbdev->kinstr_prfcnt_ctx);
	}
}

static void resume_job_scheduling(struct kbase_device *kbdev)
{
#if !MALI_USE_CSF

	kbase_resume_suspended_soft_jobs(kbdev);

	kbasep_js_resume(kbdev);
#else
	kbase_csf_scheduler_pm_resume(kbdev);
#endif
}

int kbase_pm_driver_suspend(struct kbase_device *kbdev)
{
	bool scheduling_suspended = false;
	bool timers_halted = false;

#if MALI_USE_CSF
	if (kbdev->csf.firmware_inited)
#endif
	{
		kbase_kinstr_prfcnt_suspend(kbdev->kinstr_prfcnt_ctx);
	}

	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	mutex_lock(&kbdev->pm.lock);
	if (WARN_ON(kbase_pm_is_suspending(kbdev))) {
		mutex_unlock(&kbdev->pm.lock);

		return 0;
	}
	kbdev->pm.suspending = true;
	mutex_unlock(&kbdev->pm.lock);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if) {
		unsigned long flags;

#if MALI_USE_CSF
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_disjoint_state_up(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#else
		unsigned int i;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->js_data.runpool_irq.submit_allowed = 0;
		kbase_disjoint_state_up(kbdev);
		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
			kbase_job_slot_softstop(kbdev, i, NULL);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#endif
	}
#endif

#if !MALI_USE_CSF

	kbasep_js_suspend(kbdev);
#else
	if (kbase_csf_scheduler_pm_suspend(kbdev))
		goto exit;
#endif

	scheduling_suspended = true;

	dev_dbg(kbdev->dev, ">wait_event - waiting for active_count == 0 (pid = %d)\n",
		current->pid);
	wait_event(kbdev->pm.zero_active_count_wait, kbdev->pm.active_count == 0);
	dev_dbg(kbdev->dev, ">wait_event - waiting done\n");

#if MALI_USE_CSF

	if (kbase_csf_kcpu_queue_halt_timers(kbdev))
		goto exit;
#endif

	timers_halted = true;

	if (kbase_hwaccess_pm_suspend(kbdev)) {

		if (IS_ENABLED(CONFIG_MALI_ARBITER_SUPPORT))
			WARN_ON_ONCE(1);
		else
			goto exit;
	}

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if) {
		mutex_lock(&kbdev->pm.arb_vm_state->vm_state_lock);
		kbase_arbiter_pm_vm_stopped(kbdev);
		mutex_unlock(&kbdev->pm.arb_vm_state->vm_state_lock);
	}
#endif

#if MALI_USE_CSF
	kbase_backend_invalidate_gpu_timestamp_offset(kbdev);
#endif

	return 0;

exit:
	if (timers_halted) {
#if MALI_USE_CSF

		kbase_csf_kcpu_queue_resume_timers(kbdev);
#endif
	}

	mutex_lock(&kbdev->pm.lock);
	kbdev->pm.suspending = false;
	mutex_unlock(&kbdev->pm.lock);

	if (scheduling_suspended)
		resume_job_scheduling(kbdev);

	reenable_hwcnt_on_resume(kbdev);

	wake_up_all(&kbdev->pm.resume_wait);
	return -1;
}

void kbase_pm_driver_resume(struct kbase_device *kbdev, bool arb_gpu_start)
{
	CSTD_UNUSED(arb_gpu_start);

	kbase_hwaccess_pm_resume(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_context_active_handle_suspend(
		    kbdev, (arb_gpu_start ? KBASE_PM_SUSPEND_HANDLER_VM_GPU_GRANTED :
						  KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE)))
		return;
#else
	kbase_pm_context_active(kbdev);
#endif

	resume_job_scheduling(kbdev);

#if MALI_USE_CSF
	kbase_csf_kcpu_queue_resume_timers(kbdev);
#endif

	kbase_pm_context_idle(kbdev);

	reenable_hwcnt_on_resume(kbdev);

	kbdev->pm.resuming = false;

	wake_up_all(&kbdev->pm.resume_wait);
}

int kbase_pm_suspend(struct kbase_device *kbdev)
{
	int result = 0;

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_OS_SUSPEND_EVENT);
	else
		result = kbase_pm_driver_suspend(kbdev);
#else
	result = kbase_pm_driver_suspend(kbdev);
#endif

	return result;
}

void kbase_pm_resume(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_OS_RESUME_EVENT);
	else
		kbase_pm_driver_resume(kbdev, false);
#else
	kbase_pm_driver_resume(kbdev, false);
#endif
}
