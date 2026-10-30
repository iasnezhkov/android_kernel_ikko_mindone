/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#ifndef _KBASE_GATOR_API_H_
#define _KBASE_GATOR_API_H_

#define MALI_DDK_GATOR_API_VERSION 4

enum hwc_type {
	JM_BLOCK = 0,
	TILER_BLOCK,
	SHADER_BLOCK,
	MMU_L2_BLOCK,
	RESERVED_BLOCK
};

struct kbase_gator_hwcnt_info {

	uint16_t bitmask[4];

	void *kernel_dump_buffer;

	uint32_t size;

	uint32_t gpu_id;

	uint32_t nr_cores;

	uint32_t nr_core_groups;

	enum hwc_type *hwc_layout;

	uint32_t nr_hwc_blocks;
};

struct kbase_gator_hwcnt_handles;

extern struct kbase_gator_hwcnt_handles *kbase_gator_hwcnt_init(struct kbase_gator_hwcnt_info *in_out_info);

extern void kbase_gator_hwcnt_term(struct kbase_gator_hwcnt_info *in_out_info, struct kbase_gator_hwcnt_handles *opaque_handles);

extern uint32_t kbase_gator_instr_hwcnt_dump_complete(struct kbase_gator_hwcnt_handles *opaque_handles, uint32_t * const success);

extern uint32_t kbase_gator_instr_hwcnt_dump_irq(struct kbase_gator_hwcnt_handles *opaque_handles);

extern const char * const *kbase_gator_hwcnt_init_names(uint32_t *total_counters);

extern void kbase_gator_hwcnt_term_names(void);

#endif
