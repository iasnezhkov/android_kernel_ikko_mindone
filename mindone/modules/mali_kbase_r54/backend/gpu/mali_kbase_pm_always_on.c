// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static bool always_on_shaders_needed(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return true;
}

static bool always_on_get_core_active(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return true;
}

static void always_on_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void always_on_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

const struct kbase_pm_policy kbase_pm_always_on_policy_ops = {
	"always_on",
	always_on_init,
	always_on_term,
	always_on_shaders_needed,
	always_on_get_core_active,
	NULL,
	KBASE_PM_POLICY_ID_ALWAYS_ON,
#if MALI_USE_CSF
	ALWAYS_ON_PM_SCHED_FLAGS,
#endif
};

KBASE_EXPORT_TEST_API(kbase_pm_always_on_policy_ops);
