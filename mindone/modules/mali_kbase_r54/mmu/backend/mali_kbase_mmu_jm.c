// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_hwaccess_jm.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_as_fault_debugfs.h>
#include <mmu/mali_kbase_mmu_internal.h>
#include <mmu/mali_kbase_mmu_faults_decoder.h>

void kbase_mmu_get_as_setup(struct kbase_mmu_table *mmut, struct kbase_mmu_setup *const setup)
{

	setup->memattr =
		(KBASE_MEMATTR_IMPL_DEF_CACHE_POLICY
		 << (KBASE_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY * 8)) |
		(KBASE_MEMATTR_FORCE_TO_CACHE_ALL << (KBASE_MEMATTR_INDEX_FORCE_TO_CACHE_ALL * 8)) |
		(KBASE_MEMATTR_WRITE_ALLOC << (KBASE_MEMATTR_INDEX_WRITE_ALLOC * 8)) |
		(KBASE_MEMATTR_AARCH64_OUTER_IMPL_DEF << (KBASE_MEMATTR_INDEX_OUTER_IMPL_DEF * 8)) |
		(KBASE_MEMATTR_AARCH64_OUTER_WA << (KBASE_MEMATTR_INDEX_OUTER_WA * 8)) |
		(KBASE_MEMATTR_AARCH64_NON_CACHEABLE << (KBASE_MEMATTR_INDEX_NON_CACHEABLE * 8));

	setup->transtab = (u64)mmut->pgd & AS_TRANSTAB_BASE_MASK;
	setup->transcfg = AS_TRANSCFG_MODE_SET(0ULL, AS_TRANSCFG_MODE_AARCH64_4K);
}

void kbase_gpu_report_bus_fault_and_kill(struct kbase_context *kctx, struct kbase_as *as,
					 struct kbase_fault *fault)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	const u32 status = fault->status;
	const u32 exception_type = AS_FAULTSTATUS_EXCEPTION_TYPE_GET(status);
	const u32 access_type = AS_FAULTSTATUS_ACCESS_TYPE_GET(status);
	const u32 source_id = AS_FAULTSTATUS_SOURCE_ID_GET(status);
	unsigned int const as_no = as->number;
	unsigned long flags;
	const uintptr_t fault_addr = fault->addr;

	dev_err(kbdev->dev,
		"GPU bus fault in AS%u at PA %pK\n"
		"raw fault status: 0x%X\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X (core_id:utlb:IR 0x%X:0x%X:0x%X): %s, %s\n"
		"pid: %d\n",
		as_no, (void *)fault_addr, status, exception_type,
		kbase_gpu_exception_name(exception_type), access_type,
		kbase_gpu_access_type_name(access_type), source_id,
		FAULT_SOURCE_ID_CORE_ID_GET(source_id), FAULT_SOURCE_ID_UTLB_ID_GET(source_id),
		fault_source_id_internal_requester_get(kbdev, source_id),
		fault_source_id_core_type_description_get(kbdev, source_id),
		fault_source_id_internal_requester_get_str(kbdev, source_id, access_type),
		kctx->pid);

	mutex_lock(&kbdev->mmu_hw_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_mmu_hw_clear_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);

}

void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx, struct kbase_as *as,
				     const char *reason_str, struct kbase_fault *fault)
{
	unsigned long flags;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	unsigned int as_no = as->number;

	if (WARN_ON(atomic_read(&kctx->refcount) <= 0))
		return;

	if (!kbase_ctx_flag(kctx, KCTX_PAGE_FAULT_REPORT_SKIP)) {

		const u32 status = fault->status;
		const u32 exception_type = AS_FAULTSTATUS_EXCEPTION_TYPE_GET(status);
		const u32 access_type = AS_FAULTSTATUS_ACCESS_TYPE_GET(status);
		const u32 source_id = AS_FAULTSTATUS_SOURCE_ID_GET(status);

		if (kbdev->gpu_props.gpu_id.product_model < GPU_ID_MODEL_MAKE(9, 0)) {
			dev_err(kbdev->dev,
				"Unhandled Page fault in AS%u at VA 0x%016llX\n"
				"Reason: %s\n"
				"raw fault status: 0x%X\n"
				"exception type 0x%X: %s\n"
				"access type 0x%X: %s\n"
				"pid: %d\n",
				as_no, fault->addr, reason_str, status, exception_type,
				kbase_gpu_exception_name(exception_type), access_type,
				kbase_gpu_access_type_name(status), kctx->pid);
		} else {
			dev_err(kbdev->dev,
				"Unhandled Page fault in AS%u at VA 0x%016llX\n"
				"Reason: %s\n"
				"raw fault status: 0x%X\n"
				"exception type 0x%X: %s\n"
				"access type 0x%X: %s\n"
				"source id 0x%X (core_id:utlb:IR 0x%X:0x%X:0x%X): %s, %s\n"
				"pid: %d\n",
				as_no, fault->addr, reason_str, status, exception_type,
				kbase_gpu_exception_name(exception_type), access_type,
				kbase_gpu_access_type_name(status), source_id,
				FAULT_SOURCE_ID_CORE_ID_GET(source_id),
				FAULT_SOURCE_ID_UTLB_ID_GET(source_id),
				fault_source_id_internal_requester_get(kbdev, source_id),
				fault_source_id_core_type_description_get(kbdev, source_id),
				fault_source_id_internal_requester_get_str(kbdev, source_id,
									   access_type),
				kctx->pid);
		}
	}

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	if ((kbdev->hwcnt.kctx) && (kbdev->hwcnt.kctx->as_nr == as_no) &&
	    (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DUMPING)) {
		if ((fault->addr >= kbdev->hwcnt.addr) &&
		    (fault->addr < (kbdev->hwcnt.addr + kbdev->hwcnt.addr_bytes)))
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_FAULT;
	}
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbasep_js_clear_submit_allowed(js_devdata, kctx);

	kbase_backend_jm_kill_running_jobs_from_kctx(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_lock(&kbdev->mmu_hw_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_mmu_hw_clear_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);

}

static void kbase_mmu_interrupt_process(struct kbase_device *kbdev, struct kbase_context *kctx,
					struct kbase_as *as, struct kbase_fault *fault)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	dev_dbg(kbdev->dev, "Entering %s kctx %pK, as %pK\n", __func__, (void *)kctx, (void *)as);

	if (!kctx) {
		if (kbase_as_has_bus_fault(as, fault)) {
			dev_warn(
				kbdev->dev,
				"Bus error in AS%u at PA 0x%pK with no context present! Spurious IRQ or SW Design Error?\n",
				as->number, (void *)(uintptr_t)fault->addr);
		} else {
			dev_warn(
				kbdev->dev,
				"Page fault in AS%u at VA 0x%016llx with no context present! Spurious IRQ or SW Design Error?\n",
				as->number, fault->addr);
		}

		WARN_ON(as->current_setup.transtab);

		if (kbase_as_has_bus_fault(as, fault)) {
			kbase_mmu_hw_clear_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		} else if (kbase_as_has_page_fault(as, fault)) {
			kbase_mmu_hw_clear_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as, KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
		}

		return;
	}

	if (kbase_as_has_bus_fault(as, fault)) {
		struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
		if ((kbdev->hwcnt.kctx == kctx) &&
		    (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DUMPING))
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_FAULT;

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		dev_warn(kbdev->dev, "Bus error in AS%u at PA=0x%pK, IPA=0x%pK\n", as->number,
			 (void *)(uintptr_t)fault->addr, (void *)(uintptr_t)fault->extra_addr);

		WARN_ON(!queue_work(as->pf_wq, &as->work_busfault));
		atomic_inc(&kbdev->faults_pending);
	} else {
		WARN_ON(!queue_work(as->pf_wq, &as->work_pagefault));
		atomic_inc(&kbdev->faults_pending);
	}

	dev_dbg(kbdev->dev, "Leaving %s kctx %pK, as %pK\n", __func__, (void *)kctx, (void *)as);
}

static void validate_protected_page_fault(struct kbase_device *kbdev)
{

	u32 protected_debug_mode = 0;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_DEBUG_MODE)) {
		protected_debug_mode = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_STATUS)) &
				       GPU_STATUS_GPU_DBG_ENABLED;
	}

	if (!protected_debug_mode) {

		dev_err(kbdev->dev, "Fault address reported in protected mode\n");
	}
}

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat)
{
	const int num_as = 16;
	const int busfault_shift = MMU_PAGE_FAULT_FLAGS;
	const int pf_shift = 0;
	const unsigned long as_bit_mask = (1UL << num_as) - 1;
	unsigned long flags;
	u32 new_mask;
	u32 tmp, bf_bits, pf_bits;

	dev_dbg(kbdev->dev, "Entering %s irq_stat %u\n", __func__, irq_stat);

	bf_bits = (irq_stat >> busfault_shift) & as_bit_mask;

	pf_bits = ((irq_stat >> pf_shift) & as_bit_mask) & ~bf_bits;

	if (WARN_ON(kbdev == NULL))
		return;

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	new_mask = kbase_reg_read32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK));

	kbase_reg_write32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK), 0);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	while (bf_bits | pf_bits) {
		struct kbase_as *as;
		unsigned int as_no;
		struct kbase_context *kctx;
		struct kbase_fault *fault;

		as_no = (unsigned int)ffs((int)(bf_bits | pf_bits)) - 1;
		as = &kbdev->as[as_no];

		if (bf_bits & (1UL << as_no))
			fault = &as->bf_data;
		else
			fault = &as->pf_data;

		kctx = kbase_ctx_sched_as_to_ctx_refcount(kbdev, as_no);

		fault->addr = kbase_reg_read64(kbdev, MMU_AS_OFFSET(as_no, FAULTADDRESS));

		fault->protected_mode = kbdev->protected_mode;

		if (kbdev->protected_mode && fault->addr) {

			validate_protected_page_fault(kbdev);
		}

		kbase_as_fault_debugfs_new(kbdev, as_no);

		fault->status = kbase_reg_read32(kbdev, MMU_AS_OFFSET(as_no, FAULTSTATUS));
		fault->extra_addr = kbase_reg_read64(kbdev, MMU_AS_OFFSET(as_no, FAULTEXTRA));

		if (kbase_as_has_bus_fault(as, fault)) {

			bf_bits &= ~(1UL << as_no);

			new_mask &= ~(MMU_BUS_ERROR(as_no) | MMU_PAGE_FAULT(as_no));
		} else {

			pf_bits &= ~(1UL << as_no);

			new_mask &= ~MMU_PAGE_FAULT(as_no);
		}

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_mmu_interrupt_process(kbdev, kctx, as, fault);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	tmp = kbase_reg_read32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK));
	new_mask |= tmp;
	kbase_reg_write32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK), new_mask);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	dev_dbg(kbdev->dev, "Leaving %s irq_stat %u\n", __func__, irq_stat);
}

int kbase_mmu_as_init(struct kbase_device *kbdev, unsigned int i)
{
	kbdev->as[i].number = i;
	kbdev->as[i].bf_data.addr = 0ULL;
	kbdev->as[i].pf_data.addr = 0ULL;

	kbdev->as[i].pf_wq = alloc_workqueue("mali_mmu%u", 0, 0, i);
	if (!kbdev->as[i].pf_wq)
		return -ENOMEM;

	INIT_WORK(&kbdev->as[i].work_pagefault, kbase_mmu_page_fault_worker);
	INIT_WORK(&kbdev->as[i].work_busfault, kbase_mmu_bus_fault_worker);

	return 0;
}
