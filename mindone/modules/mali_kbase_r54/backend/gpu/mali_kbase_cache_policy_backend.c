// SPDX-License-Identifier: GPL-2.0
#include "backend/gpu/mali_kbase_cache_policy_backend.h"
#include <device/mali_kbase_device.h>

void kbase_cache_set_coherency_mode(struct kbase_device *kbdev, u32 mode)
{

	kbdev->current_gpu_coherency_mode = mode;

#if MALI_USE_CSF
	if (kbdev->gpu_props.gpu_id.arch_id >= GPU_ID_ARCH_MAKE(12, 0, 1)) {

		u32 val = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(AMBA_ENABLE));

		val = AMBA_ENABLE_COHERENCY_PROTOCOL_SET(val, mode);
		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(AMBA_ENABLE), val);
	} else {

		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(COHERENCY_ENABLE), mode);
	}
#else
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(COHERENCY_ENABLE), mode);
#endif
}

void kbase_amba_set_shareable_cache_support(struct kbase_device *kbdev)
{
#if MALI_USE_CSF

	if (kbdev->gpu_props.gpu_id.arch_id < GPU_ID_ARCH_MAKE(12, 0, 1))
		return;

	if (kbdev->system_coherency != COHERENCY_NONE) {
		u32 val = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(AMBA_FEATURES));

		if (AMBA_FEATURES_SHAREABLE_CACHE_SUPPORT_GET(val)) {
			val = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(AMBA_ENABLE));
			val = AMBA_ENABLE_SHAREABLE_CACHE_SUPPORT_SET(val, 1);
			kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(AMBA_ENABLE), val);
		}
	}
#endif
}
