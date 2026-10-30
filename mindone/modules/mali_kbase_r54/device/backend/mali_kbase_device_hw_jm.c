// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>

bool kbase_is_gpu_removed(struct kbase_device *kbdev)
{
	if (!IS_ENABLED(CONFIG_MALI_ARBITER_SUPPORT))
		return false;

	return (KBASE_REG_READ(kbdev, GPU_CONTROL_ENUM(GPU_ID)) == 0);
}

static void kbase_report_gpu_fault(struct kbase_device *kbdev, int multiple)
{
	u32 status = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_FAULTSTATUS));
	uintptr_t phys_addr = kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(GPU_FAULTADDRESS));

	dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at PA 0x%pK", status,
		 kbase_gpu_exception_name(status & 0xFF), (void *)phys_addr);
	if (multiple)
		dev_warn(kbdev->dev,
			 "There were multiple GPU faults - some have not been reported\n");

}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, val);
	if (val & GPU_FAULT)
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, val & ~CLEAN_CACHES_COMPLETED);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), val & ~CLEAN_CACHES_COMPLETED);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	if (val & POWER_CHANGED_ALL) {
		kbase_pm_power_changed(kbdev);
	} else if (val & CLEAN_CACHES_COMPLETED) {

		if (kbdev->pm.backend.l2_always_on ||
		    kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_921))
			kbase_pm_power_changed(kbdev);
	}

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, val);
}
KBASE_EXPORT_TEST_API(kbase_gpu_interrupt);
