/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GPU_MEMORY_DEBUGFS_H
#define _KBASE_GPU_MEMORY_DEBUGFS_H

#include <linux/compiler_types.h>
#include <linux/types.h>

struct kbase_io_history;
struct kbase_device;

void kbase_io_history_add(struct kbase_io_history *h, void __iomem const *addr, u32 value,
			  u8 write);

void kbasep_gpu_memory_debugfs_init(struct kbase_device *kbdev);

#endif
