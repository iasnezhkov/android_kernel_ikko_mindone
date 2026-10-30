/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_REGS_HISTORY_DEBUGFS_H
#define _KBASE_REGS_HISTORY_DEBUGFS_H

struct kbase_device;

#if defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_NO_MALI)

int kbase_io_history_init(struct kbase_io_history *h, u16 n);

void kbase_io_history_term(struct kbase_io_history *h);

void kbase_io_history_dump(struct kbase_device *kbdev);

void kbasep_regs_history_debugfs_init(struct kbase_device *kbdev);

#else

#define kbase_io_history_init(...) (0)

#define kbase_io_history_term CSTD_NOP

#define kbase_io_history_dump CSTD_NOP

#define kbasep_regs_history_debugfs_init CSTD_NOP

#endif

#endif
