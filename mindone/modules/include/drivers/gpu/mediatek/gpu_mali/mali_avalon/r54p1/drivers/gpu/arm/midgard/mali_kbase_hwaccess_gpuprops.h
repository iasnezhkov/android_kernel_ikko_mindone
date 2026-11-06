/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_GPUPROPS_H_
#define _KBASE_HWACCESS_GPUPROPS_H_

#include <mali_kbase_gpuprops_private_types.h>

int kbase_backend_gpuprops_get(struct kbase_device *kbdev, struct kbasep_gpuprops_regdump *regdump);

int kbase_backend_gpuprops_get_curr_config(
	struct kbase_device *kbdev, struct kbase_current_config_regdump *curr_config_regdump);

int kbase_backend_gpuprops_get_l2_features(struct kbase_device *kbdev,
					   struct kbasep_gpuprops_regdump *regdump);

#endif
