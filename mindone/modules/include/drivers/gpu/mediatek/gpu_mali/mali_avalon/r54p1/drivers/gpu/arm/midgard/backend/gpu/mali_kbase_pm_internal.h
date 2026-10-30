/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_BACKEND_PM_INTERNAL_H_
#define _KBASE_BACKEND_PM_INTERNAL_H_

#include <mali_kbase_hwaccess_pm.h>

#include "backend/gpu/mali_kbase_pm_ca.h"
#include "mali_kbase_pm_policy.h"

#if IS_ENABLED(CONFIG_MALI_MTK_ADAPTIVE_POWER_POLICY)
#include <ged_dvfs.h>
extern bool shall_scheduler_sleep;
#endif

void kbase_pm_dev_idle(struct kbase_device *kbdev);

void kbase_pm_dev_activate(struct kbase_device *kbdev);

u64 kbase_pm_get_present_cores(struct kbase_device *kbdev, enum kbase_pm_core_type core_type);

u64 kbase_pm_get_active_cores(struct kbase_device *kbdev, enum kbase_pm_core_type core_type);

u64 kbase_pm_get_trans_cores(struct kbase_device *kbdev, enum kbase_pm_core_type core_type);

u64 kbase_pm_get_ready_cores(struct kbase_device *kbdev, enum kbase_pm_core_type core_type);

void kbase_pm_clock_on(struct kbase_device *kbdev, bool is_resume);

bool kbase_pm_clock_off(struct kbase_device *kbdev);

void kbase_pm_enable_interrupts(struct kbase_device *kbdev);

void kbase_pm_disable_interrupts(struct kbase_device *kbdev);

void kbase_pm_disable_interrupts_nolock(struct kbase_device *kbdev);

int kbase_pm_init_hw(struct kbase_device *kbdev, unsigned int flags);

void kbase_pm_reset_done(struct kbase_device *kbdev);

#if MALI_USE_CSF

int kbase_pm_wait_for_desired_state(struct kbase_device *kbdev);
#else

int kbase_pm_wait_for_desired_state(struct kbase_device *kbdev);
#endif

int kbase_pm_killable_wait_for_desired_state(struct kbase_device *kbdev);

int kbase_pm_wait_for_l2_powered(struct kbase_device *kbdev);

#if MALI_USE_CSF

int kbase_pm_wait_for_cores_down_scale(struct kbase_device *kbdev);
#endif

void kbase_pm_update_dynamic_cores_onoff(struct kbase_device *kbdev);

void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev);

void kbase_pm_update_state(struct kbase_device *kbdev);

int kbase_pm_state_machine_init(struct kbase_device *kbdev);

void kbase_pm_state_machine_term(struct kbase_device *kbdev);

void kbase_pm_update_cores_state(struct kbase_device *kbdev);

int kbasep_pm_metrics_init(struct kbase_device *kbdev);

void kbasep_pm_metrics_term(struct kbase_device *kbdev);

void kbase_pm_report_vsync(struct kbase_device *kbdev, int buffer_updated);

void kbase_pm_get_dvfs_action(struct kbase_device *kbdev);

void kbase_pm_request_gpu_cycle_counter(struct kbase_device *kbdev);

void kbase_pm_request_gpu_cycle_counter_l2_is_on(struct kbase_device *kbdev);

void kbase_pm_release_gpu_cycle_counter(struct kbase_device *kbdev);

void kbase_pm_release_gpu_cycle_counter_nolock(struct kbase_device *kbdev);

int kbase_pm_wait_for_poweroff_work_complete(struct kbase_device *kbdev);

int kbase_pm_killable_wait_for_poweroff_work_complete(struct kbase_device *kbdev);

void kbase_pm_wait_for_gpu_power_down(struct kbase_device *kbdev);

int kbase_pm_runtime_init(struct kbase_device *kbdev);

void kbase_pm_runtime_term(struct kbase_device *kbdev);

void kbase_pm_register_access_enable(struct kbase_device *kbdev);

void kbase_pm_register_access_disable(struct kbase_device *kbdev);

bool kbase_pm_metrics_is_active(struct kbase_device *kbdev);

void kbase_pm_do_poweron(struct kbase_device *kbdev, bool is_resume);

void kbase_pm_do_poweroff(struct kbase_device *kbdev);

#if defined(CONFIG_MALI_DEVFREQ) || defined(CONFIG_MALI_MIDGARD_DVFS)
void kbase_pm_get_dvfs_metrics(struct kbase_device *kbdev, struct kbasep_pm_metrics *last,
			       struct kbasep_pm_metrics *diff);
#endif

#ifdef CONFIG_MALI_MIDGARD_DVFS

#if MALI_USE_CSF

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);
#else

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation, u32 util_gl_share,
			      u32 util_cl_share[2]);
#endif

#endif

void kbase_pm_power_changed(struct kbase_device *kbdev);

void kbase_pm_metrics_update(struct kbase_device *kbdev, ktime_t *now);

void kbase_pm_cache_snoop_enable(struct kbase_device *kbdev);

void kbase_pm_cache_snoop_disable(struct kbase_device *kbdev);

#ifdef CONFIG_MALI_DEVFREQ

void kbase_devfreq_set_core_mask(struct kbase_device *kbdev, u64 core_mask);
#endif

void kbase_pm_reset_start_locked(struct kbase_device *kbdev);

void kbase_pm_reset_complete(struct kbase_device *kbdev);

#if !MALI_USE_CSF

void kbase_pm_protected_override_enable(struct kbase_device *kbdev);

void kbase_pm_protected_override_disable(struct kbase_device *kbdev);

void kbase_pm_protected_l2_override(struct kbase_device *kbdev, bool override);

int kbase_pm_protected_entry_override_enable(struct kbase_device *kbdev);

void kbase_pm_protected_entry_override_disable(struct kbase_device *kbdev);
#endif

extern bool corestack_driver_control;

bool kbase_pm_is_l2_desired(struct kbase_device *kbdev);

#if MALI_USE_CSF

bool kbase_pm_is_mcu_desired(struct kbase_device *kbdev);

bool kbase_pm_is_mcu_inactive(struct kbase_device *kbdev, enum kbase_mcu_state state);

#ifdef KBASE_PM_RUNTIME

void kbase_pm_enable_mcu_db_notification(struct kbase_device *kbdev);

#endif

static inline bool kbase_pm_idle_groups_sched_suspendable(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return !(kbdev->pm.backend.csf_pm_sched_flags & CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE);
}

static inline bool kbase_pm_no_runnables_sched_suspendable(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return !(kbdev->pm.backend.csf_pm_sched_flags & CSF_DYNAMIC_PM_SCHED_NO_SUSPEND);
}

static inline bool kbase_pm_no_mcu_core_pwroff(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return kbdev->pm.backend.csf_pm_sched_flags & CSF_DYNAMIC_PM_CORE_KEEP_ON;
}

static inline bool kbase_pm_mcu_is_in_desired_state(struct kbase_device *kbdev)
{
	bool in_desired_state = true;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbase_pm_is_mcu_desired(kbdev) && kbdev->pm.backend.mcu_state != KBASE_MCU_ON)
		in_desired_state = false;
	else if (!kbase_pm_is_mcu_desired(kbdev) &&
		 (kbdev->pm.backend.mcu_state != KBASE_MCU_OFF) &&
		 (kbdev->pm.backend.mcu_state != KBASE_MCU_IN_SLEEP))
		in_desired_state = false;

	return in_desired_state;
}

#endif

static inline bool kbase_pm_l2_is_in_desired_state(struct kbase_device *kbdev)
{
	bool in_desired_state = true;

	if (kbase_pm_is_l2_desired(kbdev) && kbdev->pm.backend.l2_state != KBASE_L2_ON)
		in_desired_state = false;
	else if (!kbase_pm_is_l2_desired(kbdev) && kbdev->pm.backend.l2_state != KBASE_L2_OFF)
		in_desired_state = false;

	return in_desired_state;
}

static inline void kbase_pm_lock(struct kbase_device *kbdev)
{
#if !MALI_USE_CSF
	mutex_lock(&kbdev->js_data.runpool_mutex);
#endif
	mutex_lock(&kbdev->pm.lock);
}

static inline void kbase_pm_unlock(struct kbase_device *kbdev)
{
	mutex_unlock(&kbdev->pm.lock);
#if !MALI_USE_CSF
	mutex_unlock(&kbdev->js_data.runpool_mutex);
#endif
}

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)

static inline bool kbase_pm_gpu_sleep_allowed(struct kbase_device *kbdev)
{

#if IS_ENABLED(CONFIG_MALI_MTK_ADAPTIVE_POWER_POLICY) && IS_ENABLED(CONFIG_MALI_MTK_DISABLE_SOI)
	if (ged_get_autosuspend_stress()) {
		shall_scheduler_sleep = !shall_scheduler_sleep;
		return shall_scheduler_sleep && test_bit(KBASE_GPU_SUPPORTS_GPU_SLEEP,
				&kbdev->pm.backend.gpu_sleep_allowed);
	}
#endif

	if (unlikely(kbdev->dev->power.autosuspend_delay <= 0))
		return false;

	return test_bit(KBASE_GPU_SUPPORTS_GPU_SLEEP, &kbdev->pm.backend.gpu_sleep_allowed);
}

static inline bool kbase_pm_fw_sleep_on_idle_allowed(struct kbase_device *kbdev)
{
	if (unlikely(kbdev->dev->power.autosuspend_delay <= 0))
		return false;

#if IS_ENABLED(CONFIG_MALI_MTK_ADAPTIVE_POWER_POLICY)
	if (unlikely((int) ged_get_apo_autosuspend_delay_ms() == 0))
		return false;
#endif

	return kbdev->pm.backend.gpu_sleep_allowed == KBASE_GPU_FW_SLEEP_ON_IDLE_ALLOWED;
}

static inline void kbase_pm_enable_db_mirror_interrupt(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kbdev->pm.backend.db_mirror_interrupt_enabled) {
		u32 irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));

		WARN_ON(irq_mask & DOORBELL_MIRROR);

		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
				  irq_mask | DOORBELL_MIRROR);
		kbdev->pm.backend.db_mirror_interrupt_enabled = true;
	}
}

static inline void kbase_pm_disable_db_mirror_interrupt(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.db_mirror_interrupt_enabled) {
		u32 irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));

		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
				  irq_mask & ~DOORBELL_MIRROR);
		kbdev->pm.backend.db_mirror_interrupt_enabled = false;
	}
}
#endif

static inline bool kbase_pm_l2_allow_mmu_page_migration(struct kbase_device *kbdev)
{
	struct kbase_pm_backend_data *backend = &kbdev->pm.backend;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	return (backend->l2_state != KBASE_L2_PEND_ON && backend->l2_state != KBASE_L2_PEND_OFF);
}

#endif
