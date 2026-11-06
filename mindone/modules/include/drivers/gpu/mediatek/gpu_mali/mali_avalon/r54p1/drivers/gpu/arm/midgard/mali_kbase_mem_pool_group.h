/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_POOL_GROUP_H_
#define _KBASE_MEM_POOL_GROUP_H_

#include <mali_kbase_defs.h>

static inline struct kbase_mem_pool *
kbase_mem_pool_group_select(struct kbase_device *kbdev, u32 mem_group_id, bool is_small_page)
{
	if (WARN_ON(unlikely(kbdev == NULL)))
		return NULL;

	WARN_ON(mem_group_id > BASE_MEM_GROUP_COUNT);

	if (is_small_page)
		return &kbdev->mem_pools.small[mem_group_id];

	return &kbdev->mem_pools.large[mem_group_id];
}

void kbase_mem_pool_group_config_set_max_size(struct kbase_mem_pool_group_config *configs,
					      size_t max_size);

int kbase_mem_pool_group_init(struct kbase_mem_pool_group *mem_pools, struct kbase_device *kbdev,
			      const struct kbase_mem_pool_group_config *configs,
			      struct kbase_mem_pool_group *next_pools);

void kbase_mem_pool_group_mark_dying(struct kbase_mem_pool_group *mem_pools);

void kbase_mem_pool_group_term(struct kbase_mem_pool_group *mem_pools);

#endif
