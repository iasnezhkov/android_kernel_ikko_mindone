/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CAPS_H_
#define _KBASE_CAPS_H_

#include <linux/types.h>

enum mali_kbase_cap {
	MALI_KBASE_CAP_SYSTEM_MONITOR = 0,
	MALI_KBASE_CAP_JIT_PRESSURE_LIMIT,
	MALI_KBASE_CAP_MEM_DONT_NEED,
	MALI_KBASE_CAP_MEM_GROW_ON_GPF,
	MALI_KBASE_CAP_MEM_PROTECTED,
	MALI_KBASE_CAP_MEM_IMPORT_SYNC_ON_MAP_UNMAP,
	MALI_KBASE_CAP_MEM_KERNEL_SYNC,
	MALI_KBASE_CAP_MEM_SAME_VA,
	MALI_KBASE_NUM_CAPS
};

extern bool mali_kbase_supports_cap(unsigned long api_version, enum mali_kbase_cap cap);

static inline bool mali_kbase_supports_system_monitor(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_SYSTEM_MONITOR);
}

static inline bool mali_kbase_supports_jit_pressure_limit(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_JIT_PRESSURE_LIMIT);
}

static inline bool mali_kbase_supports_mem_dont_need(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_DONT_NEED);
}

static inline bool mali_kbase_supports_mem_grow_on_gpf(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_GROW_ON_GPF);
}

static inline bool mali_kbase_supports_mem_protected(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_PROTECTED);
}

static inline bool mali_kbase_supports_mem_import_sync_on_map_unmap(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_IMPORT_SYNC_ON_MAP_UNMAP);
}

static inline bool mali_kbase_supports_mem_kernel_sync(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_KERNEL_SYNC);
}

static inline bool mali_kbase_supports_mem_same_va(unsigned long api_version)
{
	return mali_kbase_supports_cap(api_version, MALI_KBASE_CAP_MEM_SAME_VA);
}

#endif
