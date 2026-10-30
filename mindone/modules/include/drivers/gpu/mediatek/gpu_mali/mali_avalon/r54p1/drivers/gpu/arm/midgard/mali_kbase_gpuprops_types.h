/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GPUPROPS_TYPES_H_
#define _KBASE_GPUPROPS_TYPES_H_

#include <uapi/gpu/arm/midgard/mali_base_kernel.h>

#define KBASE_GPU_SPEED_MHZ 123
#define KBASE_GPU_PC_SIZE_LOG2 24U

struct kbase_current_config_regdump {
	u64 mem_features;
	u64 l2_features;
	u64 shader_present;
	u64 l2_present;
};

struct kbase_gpu_mmu_props {
	u8 va_bits;
	u8 pa_bits;
};

struct max_config_props {
	u8 l2_slices;
	u8 padding[3];
	u32 core_mask;
};

struct curr_config_props {
	u64 l2_present;
	u64 shader_present;
	u16 num_cores;
	u8 l2_slices;
	bool update_needed;
	u8 padding[4];
};

struct kbase_gpu_id_props {
	u16 version_status;
	u16 version_minor;
	u16 version_major;
	u16 product_major;
	u16 arch_rev;
	u16 arch_minor;
	u16 arch_major;

	u32 product_id;
	u32 product_model;
	u32 version_id;
	u32 arch_id;
};

struct kbase_gpu_features_props {
	bool ray_intersection;
	bool cross_stream_sync;
};

struct kbase_coherent_group_props {
	u64 core_mask;
	u16 num_cores;
};

struct kbase_coherency_props {
	bool coherent_core_group;
	bool coherent_super_group;
	struct kbase_coherent_group_props group;
};

struct kbase_js_features_props {
	bool null;
	bool write_value;
	bool cache_flush;
	bool compute_shader;
	bool tiler;
	bool fragment_shader;
};

struct kbase_gpu_props {

	u64 shader_present;
	u64 stack_present;
	u64 tiler_present;
	u64 l2_present;

	u8 num_cores;
	u8 num_core_groups;
	u8 num_address_spaces;
	u8 num_job_slots;

	u32 coherency_mode;
	u32 gpu_freq_khz_max;
	u32 log2_program_counter_size;

	u8 log2_line_size;
	u8 num_l2_slices;

	u32 max_threads;
	u8 impl_tech;

	struct kbase_js_features_props js_features[GPU_MAX_JOB_SLOTS];

	struct kbase_gpu_id_props gpu_id;

	struct kbase_gpu_features_props gpu_features;

	struct kbase_coherency_props coherency_info;

	struct kbase_gpu_mmu_props mmu;

	struct curr_config_props curr_config;

	struct max_config_props max_config;

	u32 prop_buffer_size;
	void *prop_buffer;
	void *priv_data;
};

#endif
