// SPDX-License-Identifier: GPL-2.0
#include <linux/version_compat_defs.h>

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <device/mali_kbase_device.h>
#include "mali_kbase_l2_mmu_config.h"

struct l2_mmu_config_limit_region {
	u32 value, mask, shift;
};

struct l2_mmu_config_limit {
	u32 product_model;
	struct l2_mmu_config_limit_region read;
	struct l2_mmu_config_limit_region write;
};

static const struct l2_mmu_config_limit limits[] = {

	{ GPU_ID_PRODUCT_LBEX, { 0, GENMASK(10, 5), 5 }, { 0, GENMASK(16, 12), 12 } },
	{ GPU_ID_PRODUCT_TBEX, { 0, GENMASK(10, 5), 5 }, { 0, GENMASK(16, 12), 12 } },
	{ GPU_ID_PRODUCT_TBAX, { 0, GENMASK(10, 5), 5 }, { 0, GENMASK(16, 12), 12 } },
	{ GPU_ID_PRODUCT_TTRX, { 0, GENMASK(12, 7), 7 }, { 0, GENMASK(17, 13), 13 } },
	{ GPU_ID_PRODUCT_TNAX, { 0, GENMASK(12, 7), 7 }, { 0, GENMASK(17, 13), 13 } },
	{ GPU_ID_PRODUCT_TGOX,
	  { KBASE_3BIT_AID_32, GENMASK(14, 12), 12 },
	  { KBASE_3BIT_AID_32, GENMASK(17, 15), 15 } },
	{ GPU_ID_PRODUCT_TNOX,
	  { KBASE_3BIT_AID_32, GENMASK(14, 12), 12 },
	  { KBASE_3BIT_AID_32, GENMASK(17, 15), 15 } },
};

int kbase_set_mmu_quirks(struct kbase_device *kbdev)
{

	struct l2_mmu_config_limit limit = { 0,
					     { KBASE_AID_32, GENMASK(25, 24), 24 },
					     { KBASE_AID_32, GENMASK(27, 26), 26 } };
	u32 product_model;
	u32 mmu_config = 0;
	unsigned int i;

	product_model = kbdev->gpu_props.gpu_id.product_model;

	for (i = 0; i < ARRAY_SIZE(limits); i++) {
		if (product_model == limits[i].product_model) {
			limit = limits[i];
			break;
		}
	}

	if (kbase_reg_is_valid(kbdev, GPU_CONTROL_ENUM(L2_MMU_CONFIG)))
		mmu_config = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(L2_MMU_CONFIG));

	if (kbase_is_gpu_removed(kbdev))
		return -EIO;

	mmu_config &= ~(limit.read.mask | limit.write.mask);

	mmu_config |= (limit.read.value << limit.read.shift) |
		      (limit.write.value << limit.write.shift);

	kbdev->hw_quirks_mmu = mmu_config;

	if (kbdev->system_coherency == COHERENCY_ACE) {

		kbdev->hw_quirks_mmu |= L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY;
	}

	return 0;
}
