/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_PROFILE_DEBUGFS_H
#define _KBASE_MEM_PROFILE_DEBUGFS_H

#include <linux/debugfs.h>
#include <linux/seq_file.h>

struct kbase_context;

void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx);

int kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data, size_t size);

#endif
