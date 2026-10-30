/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MMU_HW_H_
#define _KBASE_MMU_HW_H_

#include "mali_kbase_mmu.h"

struct kbase_device;
struct kbase_as;
struct kbase_context;

enum kbase_mmu_fault_type {
	KBASE_MMU_FAULT_TYPE_UNKNOWN = 0,
	KBASE_MMU_FAULT_TYPE_PAGE,
	KBASE_MMU_FAULT_TYPE_BUS,
	KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED,
	KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED
};

struct kbase_mmu_hw_op_param {
	u64 vpfn;
	u32 nr;
	enum kbase_mmu_op_type op;
	u32 kctx_id;
	enum kbase_caller_mmu_sync_info mmu_sync_info;
	u64 flush_skip_levels;
};

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as);

int kbase_mmu_hw_do_lock(struct kbase_device *kbdev, struct kbase_as *as,
			 const struct kbase_mmu_hw_op_param *op_param);

int kbase_mmu_hw_do_unlock_no_addr(struct kbase_device *kbdev, struct kbase_as *as,
				   const struct kbase_mmu_hw_op_param *op_param);

int kbase_mmu_hw_do_unlock(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param);

int kbase_mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param);

int kbase_mmu_hw_do_flush_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_as *as,
				      const struct kbase_mmu_hw_op_param *op_param);

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
			      enum kbase_mmu_fault_type type);

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
			       enum kbase_mmu_fault_type type);

#endif
