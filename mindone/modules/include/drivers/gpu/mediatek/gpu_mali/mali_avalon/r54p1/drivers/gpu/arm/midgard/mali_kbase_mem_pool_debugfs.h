/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_POOL_DEBUGFS_H_
#define _KBASE_MEM_POOL_DEBUGFS_H_

#include <mali_kbase.h>

void kbase_mem_pool_debugfs_init(struct dentry *parent, struct kbase_context *kctx);

void kbase_mem_pool_debugfs_trim(void *array, size_t index, size_t value);

void kbase_mem_pool_debugfs_set_max_size(void *array, size_t index, size_t value);

size_t kbase_mem_pool_debugfs_size(void *array, size_t index);

size_t kbase_mem_pool_debugfs_max_size(void *array, size_t index);

void kbase_mem_pool_config_debugfs_set_max_size(void *array, size_t index, size_t value);

size_t kbase_mem_pool_config_debugfs_max_size(void *array, size_t index);

#endif
