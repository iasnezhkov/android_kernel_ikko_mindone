/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_PM_POLICY_H_
#define _KBASE_PM_POLICY_H_

void kbase_pm_policy_init(struct kbase_device *kbdev);

void kbase_pm_policy_term(struct kbase_device *kbdev);

void kbase_pm_update_active(struct kbase_device *kbdev);

void kbase_pm_update_cores(struct kbase_device *kbdev);

static inline bool kbase_pm_cores_requested(struct kbase_device *kbdev, bool shader_required)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.l2_state != KBASE_L2_PEND_ON &&
	    kbdev->pm.backend.l2_state != KBASE_L2_ON &&
	    kbdev->pm.backend.l2_state != KBASE_L2_ON_HWCNT_ENABLE)
		return false;

	if (shader_required &&
	    kbdev->pm.backend.shaders_state != KBASE_SHADERS_PEND_ON_CORESTACK_ON &&
	    kbdev->pm.backend.shaders_state != KBASE_SHADERS_ON_CORESTACK_ON &&
	    kbdev->pm.backend.shaders_state != KBASE_SHADERS_ON_CORESTACK_ON_RECHECK)
		return false;

	return true;
}

#endif
