/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_INSTR_H_
#define _KBASE_HWACCESS_INSTR_H_

#include <backend/gpu/mali_kbase_instr_defs.h>

struct kbase_instr_hwcnt_enable {
	u64 dump_buffer;
	u64 dump_buffer_bytes;
	u32 fe_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;
	u8 counter_set;
};

int kbase_instr_hwcnt_enable_internal(struct kbase_device *kbdev, struct kbase_context *kctx,
				      struct kbase_instr_hwcnt_enable *enable);

int kbase_instr_hwcnt_disable_internal(struct kbase_context *kctx);

int kbase_instr_hwcnt_request_dump(struct kbase_context *kctx);

int kbase_instr_hwcnt_wait_for_dump(struct kbase_context *kctx);

bool kbase_instr_hwcnt_dump_complete(struct kbase_context *kctx, bool *const success);

int kbase_instr_hwcnt_clear(struct kbase_context *kctx);

int kbase_instr_backend_init(struct kbase_device *kbdev);

void kbase_instr_backend_term(struct kbase_device *kbdev);

#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS

void kbase_instr_backend_debugfs_init(struct kbase_device *kbdev);
#endif

void kbase_instr_hwcnt_on_unrecoverable_error(struct kbase_device *kbdev);

void kbase_instr_hwcnt_on_before_reset(struct kbase_device *kbdev);

#endif
