// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_reset_gpu.h>

#if MALI_USE_CSF && defined CONFIG_MALI_DEBUG
#include <csf/mali_kbase_csf_firmware.h>
#endif

#include <linux/of.h>

static const struct kbase_pm_policy *const all_policy_list[] = {
#if IS_ENABLED(CONFIG_MALI_NO_MALI) || IS_ENABLED(CONFIG_MALI_MTK_BRINGUP)
	&kbase_pm_always_on_policy_ops,
	&kbase_pm_coarse_demand_policy_ops,
#else
	&kbase_pm_coarse_demand_policy_ops,
	&kbase_pm_always_on_policy_ops,
#endif
};

void kbase_pm_policy_init(struct kbase_device *kbdev)
{
	const struct kbase_pm_policy *default_policy = all_policy_list[0];
	struct device_node *np = kbdev->dev->of_node;
	const char *power_policy_name;
	unsigned long flags;
	unsigned int i;

	if ((of_property_read_string(np, "power-policy", &power_policy_name) == 0) ||
	    (of_property_read_string(np, "power_policy", &power_policy_name) == 0)) {
		for (i = 0; i < ARRAY_SIZE(all_policy_list); i++)
			if (sysfs_streq(all_policy_list[i]->name, power_policy_name)) {
				default_policy = all_policy_list[i];
				break;
			}
	}

#if MALI_USE_CSF && defined(CONFIG_MALI_DEBUG)

	if (fw_debug)
		default_policy = &kbase_pm_always_on_policy_ops;
#endif

	default_policy->init(kbdev);

#if MALI_USE_CSF
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.pm_current_policy = default_policy;
	kbdev->pm.backend.csf_pm_sched_flags = default_policy->pm_sched_flags;

#ifdef KBASE_PM_RUNTIME
	if (kbase_pm_idle_groups_sched_suspendable(kbdev))
		clear_bit(KBASE_GPU_IGNORE_IDLE_EVENT, &kbdev->pm.backend.gpu_sleep_allowed);
	else
		set_bit(KBASE_GPU_IGNORE_IDLE_EVENT, &kbdev->pm.backend.gpu_sleep_allowed);
#endif

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

#else
	CSTD_UNUSED(flags);
	kbdev->pm.backend.pm_current_policy = default_policy;
#endif
}

void kbase_pm_policy_term(struct kbase_device *kbdev)
{
	kbdev->pm.backend.pm_current_policy->term(kbdev);
}

void kbase_pm_update_active(struct kbase_device *kbdev)
{
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;
	bool active;

	lockdep_assert_held(&pm->lock);

	KBASE_DEBUG_ASSERT(backend->pm_current_policy);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	active = backend->pm_current_policy->get_core_active(kbdev);
	WARN((kbase_pm_is_active(kbdev) && !active),
	     "GPU is active but policy '%s' is indicating that it can be powered off",
	     kbdev->pm.backend.pm_current_policy->name);

	if (active) {

		if (!pm->backend.invoke_poweroff_wait_wq_when_l2_off &&
		    pm->backend.poweroff_wait_in_progress) {
			KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
			pm->backend.poweron_required = true;
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {

			pm->backend.invoke_poweroff_wait_wq_when_l2_off = false;
			pm->backend.poweroff_wait_in_progress = false;
			pm->backend.l2_desired = true;
#if MALI_USE_CSF
			pm->backend.mcu_desired = pm->backend.mcu_poweron_required;
#endif

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			kbase_pm_do_poweron(kbdev, false);
		}
	} else {

		KBASE_DEBUG_ASSERT(pm->active_count == 0);

		pm->backend.poweron_required = false;

		if (pm->backend.gpu_powered) {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

			kbase_pm_do_poweroff(kbdev);
		} else {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		}
	}
}

void kbase_pm_update_dynamic_cores_onoff(struct kbase_device *kbdev)
{
	bool shaders_desired;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->pm.lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;
	if (kbdev->pm.backend.poweroff_wait_in_progress)
		return;

#if MALI_USE_CSF
	CSTD_UNUSED(shaders_desired);

	if (kbase_pm_is_mcu_desired(kbdev))
		kbase_pm_update_state(kbdev);
#else

	if (kbdev->pm.backend.protected_transition_override)
		return;

	shaders_desired = kbdev->pm.backend.pm_current_policy->shaders_needed(kbdev);

	if (shaders_desired && kbase_pm_is_l2_desired(kbdev))
		kbase_pm_update_state(kbdev);
#endif
}

void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev)
{
	bool shaders_desired = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;
	if (kbdev->pm.backend.poweroff_wait_in_progress)
		return;

#if !MALI_USE_CSF
	if (kbdev->pm.backend.protected_transition_override)

		shaders_desired = false;
	else
		shaders_desired = kbdev->pm.backend.pm_current_policy->shaders_needed(kbdev);
#endif

	if (kbdev->pm.backend.shaders_desired != shaders_desired) {
		KBASE_KTRACE_ADD(kbdev, PM_CORES_CHANGE_DESIRED, NULL,
				 kbdev->pm.backend.shaders_desired);

		kbdev->pm.backend.shaders_desired = shaders_desired;
		kbase_pm_update_state(kbdev);
	}
}

void kbase_pm_update_cores_state(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbase_pm_update_cores_state_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

size_t kbase_pm_list_policies(struct kbase_device *kbdev,
			      const struct kbase_pm_policy *const **list)
{
	CSTD_UNUSED(kbdev);
	if (list)
		*list = all_policy_list;

	return ARRAY_SIZE(all_policy_list);
}

KBASE_EXPORT_TEST_API(kbase_pm_list_policies);

const struct kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return kbdev->pm.backend.pm_current_policy;
}

KBASE_EXPORT_TEST_API(kbase_pm_get_policy);

#if MALI_USE_CSF
static int policy_change_wait_for_L2_off(struct kbase_device *kbdev)
{
	long remaining;
	long timeout = kbase_csf_timeout_in_jiffies(kbase_get_timeout_ms(kbdev, CSF_PM_TIMEOUT));
	int err = 0;

#if KERNEL_VERSION(4, 13, 1) <= LINUX_VERSION_CODE
	remaining = wait_event_killable_timeout(kbdev->pm.backend.gpu_in_desired_state_wait,
						kbdev->pm.backend.l2_state == KBASE_L2_OFF,
						timeout);
#else
	remaining = wait_event_timeout(kbdev->pm.backend.gpu_in_desired_state_wait,
				       kbdev->pm.backend.l2_state == KBASE_L2_OFF, timeout);
#endif

	if (!remaining) {
		err = -ETIMEDOUT;
	} else if (remaining < 0) {
		dev_info(kbdev->dev, "Wait for L2_off got interrupted");
		err = (int)remaining;
	}

	dev_dbg(kbdev->dev, "%s: err=%d mcu_state=%d, L2_state=%d\n", __func__, err,
		kbdev->pm.backend.mcu_state, kbdev->pm.backend.l2_state);

	return err;
}
#endif

void kbase_pm_set_policy(struct kbase_device *kbdev, const struct kbase_pm_policy *new_policy)
{
	const struct kbase_pm_policy *old_policy;
	unsigned long flags;
#if MALI_USE_CSF
	unsigned int new_policy_csf_pm_sched_flags;
	bool sched_suspend;
	bool reset_gpu = false;
	bool reset_op_prevented = true;
	struct kbase_csf_scheduler *scheduler = NULL;
	u64 pwroff_ns;
	bool switching_to_always_on;
#endif

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(new_policy != NULL);

	KBASE_KTRACE_ADD(kbdev, PM_SET_POLICY, NULL, new_policy->id);

#if MALI_USE_CSF
	pwroff_ns = kbase_csf_firmware_get_mcu_core_pwroff_time(kbdev);
	switching_to_always_on = new_policy == &kbase_pm_always_on_policy_ops;
	if (pwroff_ns == 0 && !switching_to_always_on) {
		dev_warn(
			kbdev->dev,
			"power_policy: cannot switch away from always_on with mcu_shader_pwroff_timeout set to 0\n");
		dev_warn(
			kbdev->dev,
			"power_policy: resetting mcu_shader_pwroff_timeout to default value to switch policy from always_on\n");
		kbase_csf_firmware_reset_mcu_core_pwroff_time(kbdev);
	}

	scheduler = &kbdev->csf.scheduler;
	KBASE_DEBUG_ASSERT(scheduler != NULL);

	mutex_lock(&kbdev->pm.backend.policy_change_lock);

	if (kbase_reset_gpu_prevent_and_wait(kbdev)) {
		dev_warn(kbdev->dev, "Set PM policy failing to prevent gpu reset");
		reset_op_prevented = false;
	}

	mutex_lock(&scheduler->lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	WARN_ON(kbdev->pm.backend.policy_change_clamp_state_to_off);
	new_policy_csf_pm_sched_flags = new_policy->pm_sched_flags;

	sched_suspend = reset_op_prevented &&
			(CSF_DYNAMIC_PM_CORE_KEEP_ON &
			 (new_policy_csf_pm_sched_flags | kbdev->pm.backend.csf_pm_sched_flags));

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (sched_suspend) {

		sched_suspend = !kbase_csf_scheduler_pm_suspend_no_lock(kbdev);

		reset_gpu = !sched_suspend;
	}

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbdev->pm.backend.policy_change_clamp_state_to_off = sched_suspend;
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (sched_suspend)
		reset_gpu = policy_change_wait_for_L2_off(kbdev);
#endif

	kbase_pm_context_active(kbdev);

	kbase_pm_lock(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	old_policy = kbdev->pm.backend.pm_current_policy;
	kbdev->pm.backend.pm_current_policy = NULL;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	KBASE_KTRACE_ADD(kbdev, PM_CURRENT_POLICY_TERM, NULL, old_policy->id);
	if (old_policy->term)
		old_policy->term(kbdev);

	memset(&kbdev->pm.backend.pm_policy_data, 0, sizeof(union kbase_pm_policy_data));

	KBASE_KTRACE_ADD(kbdev, PM_CURRENT_POLICY_INIT, NULL, new_policy->id);
	if (new_policy->init)
		new_policy->init(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.pm_current_policy = new_policy;
#if MALI_USE_CSF
	kbdev->pm.backend.csf_pm_sched_flags = new_policy_csf_pm_sched_flags;

	kbdev->pm.backend.policy_change_clamp_state_to_off = false;
	kbase_pm_update_state(kbdev);

#ifdef KBASE_PM_RUNTIME
	if (kbase_pm_idle_groups_sched_suspendable(kbdev))
		clear_bit(KBASE_GPU_IGNORE_IDLE_EVENT, &kbdev->pm.backend.gpu_sleep_allowed);
	else
		set_bit(KBASE_GPU_IGNORE_IDLE_EVENT, &kbdev->pm.backend.gpu_sleep_allowed);
#endif
#endif
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	kbase_pm_update_active(kbdev);
	kbase_pm_update_cores_state(kbdev);

	kbase_pm_unlock(kbdev);

	kbase_pm_context_idle(kbdev);

#if MALI_USE_CSF

	if (sched_suspend)
		kbase_csf_scheduler_pm_resume_no_lock(kbdev);
	mutex_unlock(&scheduler->lock);

	if (reset_op_prevented)
		kbase_reset_gpu_allow(kbdev);

	if (reset_gpu) {
		dev_warn(kbdev->dev, "Resorting to GPU reset for policy change\n");
		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu(kbdev);
		kbase_reset_gpu_wait(kbdev);
	}

	mutex_unlock(&kbdev->pm.backend.policy_change_lock);
#endif
}

KBASE_EXPORT_TEST_API(kbase_pm_set_policy);
