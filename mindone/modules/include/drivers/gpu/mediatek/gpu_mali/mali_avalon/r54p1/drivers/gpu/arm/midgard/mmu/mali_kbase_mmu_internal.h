/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MMU_INTERNAL_H_
#define _KBASE_MMU_INTERNAL_H_

void kbase_mmu_get_as_setup(struct kbase_mmu_table *mmut, struct kbase_mmu_setup *const setup);

void kbase_mmu_report_mcu_as_fault_and_reset(struct kbase_device *kbdev, struct kbase_fault *fault);

void kbase_gpu_report_bus_fault_and_kill(struct kbase_context *kctx, struct kbase_as *as,
					 struct kbase_fault *fault);

void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx, struct kbase_as *as,
				     const char *reason_str, struct kbase_fault *fault);

void kbase_mmu_page_fault_worker(struct work_struct *data);

void kbase_mmu_bus_fault_worker(struct work_struct *data);

#endif
