// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static bool coarse_demand_shaders_needed(struct kbase_device *kbdev)
{
	return kbase_pm_is_active(kbdev);
}

static bool coarse_demand_get_core_active(struct kbase_device *kbdev)
{
	return kbase_pm_is_active(kbdev);
}

static void coarse_demand_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void coarse_demand_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

const struct kbase_pm_policy kbase_pm_coarse_demand_policy_ops = {
	"coarse_demand",
	coarse_demand_init,
	coarse_demand_term,
	coarse_demand_shaders_needed,
	coarse_demand_get_core_active,
	NULL,
	KBASE_PM_POLICY_ID_COARSE_DEMAND,
#if MALI_USE_CSF
	COARSE_ON_DEMAND_PM_SCHED_FLAGS,
#endif
};

KBASE_EXPORT_TEST_API(kbase_pm_coarse_demand_policy_ops);
