/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_KBASE_GPUPROPS_PRIVATE_TYPES_H_
#define _MALI_KBASE_GPUPROPS_PRIVATE_TYPES_H_

#include <uapi/gpu/arm/midgard/mali_base_kernel.h>

struct kbasep_gpuprops_regdump {
	u64 shader_present;
	u64 tiler_present;
	u64 l2_present;
	u64 stack_present;
	u64 core_features;
	u64 tiler_features;
	u64 l2_features;
	u64 mem_features;
	u64 mmu_features;
	u64 gpu_features;
	u32 as_present;
	u32 js_present;
	u32 js_features[GPU_MAX_JOB_SLOTS];
	u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];
	u64 gpu_id;
	u32 thread_max_threads;
	u32 thread_max_workgroup_size;
	u32 thread_max_barrier_size;
	u32 thread_features;
	u32 coherency_features;
	u32 thread_tls_alloc;
	u32 l2_config;
	u32 l2_slice_hash[GPU_ASN_HASH_COUNT];
};

struct kbasep_gpuprops_priv_data {
	struct kbasep_gpuprops_regdump regdump;
};

#endif
