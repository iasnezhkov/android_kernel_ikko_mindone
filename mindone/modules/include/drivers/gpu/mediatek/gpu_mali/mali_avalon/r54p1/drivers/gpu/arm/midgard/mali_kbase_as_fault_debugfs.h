/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_AS_FAULT_DEBUG_FS_H
#define _KBASE_AS_FAULT_DEBUG_FS_H

#include <mali_kbase.h>

void kbase_as_fault_debugfs_init(struct kbase_device *kbdev);

static inline void kbase_as_fault_debugfs_new(struct kbase_device *kbdev, unsigned int as_no)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
#ifdef CONFIG_MALI_DEBUG
	kbdev->debugfs_as_read_bitmap |= (1ULL << as_no);
#else
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(as_no);
#endif
#else
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(as_no);
#endif
}

#endif
