/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_GPU_H_
#define _KBASE_HWCNT_GPU_H_

#include "hwcnt/mali_kbase_hwcnt_types.h"

#include <linux/bug.h>
#include <linux/types.h>

struct kbase_device;
struct kbase_hwcnt_metadata;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

#define KBASE_HWCNT_V5_BLOCK_TYPE_COUNT 7
#define KBASE_HWCNT_V5_HEADERS_PER_BLOCK 4
#define KBASE_HWCNT_V5_DEFAULT_COUNTERS_PER_BLOCK 60
#define KBASE_HWCNT_V5_DEFAULT_VALUES_PER_BLOCK \
	(KBASE_HWCNT_V5_HEADERS_PER_BLOCK + KBASE_HWCNT_V5_DEFAULT_COUNTERS_PER_BLOCK)

#define KBASE_HWCNT_V5_FE_BLOCK_COUNT 1

#define KBASE_HWCNT_V5_TILER_BLOCK_COUNT 1

#define KBASE_HWCNT_V5_PRFCNT_EN_HEADER 2

#define KBASE_HWCNT_VALUE_HW_BYTES (sizeof(u32))

enum kbase_hwcnt_gpu_v5_block_type {
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW3,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW_UNDEFINED,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG2,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG3,
	KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG_UNDEFINED,
};

enum kbase_hwcnt_set {
	KBASE_HWCNT_SET_PRIMARY,
	KBASE_HWCNT_SET_SECONDARY,
	KBASE_HWCNT_SET_TERTIARY,
	KBASE_HWCNT_SET_UNDEFINED = 255,
};

struct kbase_hwcnt_physical_enable_map {
	u32 fe_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;
	u32 fw_bm;
	u32 csg_bm;
};

struct kbase_hwcnt_enable_cm {
	u64 fe_bm[2];
	u64 shader_bm[2];
	u64 tiler_bm[2];
	u64 mmu_l2_bm[2];
	u64 fw_bm[2];
	u64 csg_bm[2];
};

enum kbase_hwcnt_physical_set {
	KBASE_HWCNT_PHYSICAL_SET_PRIMARY = 0,
	KBASE_HWCNT_PHYSICAL_SET_SECONDARY = 1,
	KBASE_HWCNT_PHYSICAL_SET_TERTIARY = 2,
};

struct kbase_hwcnt_gpu_info {
	size_t l2_count;
	u64 sc_core_mask;
	u8 clk_cnt;
	u8 csg_cnt;
	size_t prfcnt_values_per_block;
	bool has_fw_counters;
};

struct kbase_hwcnt_curr_config {
	size_t num_l2_slices;
	u64 shader_present;
};

static inline bool kbase_hwcnt_is_block_type_undefined(const uint64_t blk_type)
{
	return (blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED ||
		blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED ||
		blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED ||
		blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED);
}

int kbase_hwcnt_jm_metadata_create(const struct kbase_hwcnt_gpu_info *info,
				   enum kbase_hwcnt_set counter_set,
				   const struct kbase_hwcnt_metadata **out_metadata,
				   size_t *out_dump_bytes);

int kbase_hwcnt_csf_metadata_create(const struct kbase_hwcnt_gpu_info *info,
				    enum kbase_hwcnt_set counter_set,
				    const struct kbase_hwcnt_metadata **out_metadata);

int kbase_hwcnt_jm_dump_get(struct kbase_hwcnt_dump_buffer *dst, u64 *src,
			    const struct kbase_hwcnt_enable_map *dst_enable_map,
			    const u64 pm_core_mask, u64 debug_core_mask, size_t max_l2_slices,
			    const struct kbase_hwcnt_curr_config *curr_config, bool accumulate);

int kbase_hwcnt_csf_dump_get(struct kbase_hwcnt_dump_buffer *dst, u64 *src,
			     blk_stt_t *src_block_stt,
			     const struct kbase_hwcnt_enable_map *dst_enable_map,
			     size_t num_l2_slices, u64 powered_shader_core_mask, bool accumulate);

static inline u32 kbase_hwcnt_backend_gpu_block_map_to_physical(u64 lo, u64 hi)
{
	u32 phys = 0;
	u64 dwords[2] = { lo, hi };
	size_t dword_idx;

	for (dword_idx = 0; dword_idx < 2; dword_idx++) {
		const u64 dword = dwords[dword_idx];
		u16 packed = 0;

		size_t hword_bit;

		for (hword_bit = 0; hword_bit < 16; hword_bit++) {
			const size_t dword_bit = hword_bit * 4;
			const u16 mask = ((dword >> (dword_bit + 0)) & 0x1) |
					 ((dword >> (dword_bit + 1)) & 0x1) |
					 ((dword >> (dword_bit + 2)) & 0x1) |
					 ((dword >> (dword_bit + 3)) & 0x1);
			packed |= (mask << hword_bit);
		}
		phys |= ((u32)packed) << (16 * dword_idx);
	}
	return phys;
}

void kbase_hwcnt_gpu_enable_map_to_physical(struct kbase_hwcnt_physical_enable_map *dst,
					    const struct kbase_hwcnt_enable_map *src);

void kbase_hwcnt_gpu_set_to_physical(enum kbase_hwcnt_physical_set *dst, enum kbase_hwcnt_set src);

void kbase_hwcnt_gpu_enable_map_from_physical(struct kbase_hwcnt_enable_map *dst,
					      const struct kbase_hwcnt_physical_enable_map *src);

void kbase_hwcnt_gpu_patch_dump_headers(struct kbase_hwcnt_dump_buffer *buf,
					const struct kbase_hwcnt_enable_map *enable_map);

bool kbase_hwcnt_is_block_type_shader(const enum kbase_hwcnt_gpu_v5_block_type blk_type);

bool kbase_hwcnt_is_block_type_memsys(const enum kbase_hwcnt_gpu_v5_block_type blk_type);

bool kbase_hwcnt_is_block_type_tiler(const enum kbase_hwcnt_gpu_v5_block_type blk_type);

bool kbase_hwcnt_is_block_type_fe(const enum kbase_hwcnt_gpu_v5_block_type blk_type);

void kbase_hwcnt_gpu_enable_map_from_cm(struct kbase_hwcnt_enable_map *dst,
					const struct kbase_hwcnt_enable_cm *src);

#endif
