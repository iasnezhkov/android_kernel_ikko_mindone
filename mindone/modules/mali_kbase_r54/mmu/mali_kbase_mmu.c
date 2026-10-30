// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/migrate.h>
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <tl/mali_kbase_tracepoints.h>
#include <backend/gpu/mali_kbase_instr_defs.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_debug.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hw.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>
#include <mmu/mali_kbase_mmu_internal.h>
#include <device/mali_kbase_device.h>
#include <uapi/gpu/arm/midgard/gpu/mali_kbase_gpu_id.h>
#if !MALI_USE_CSF
#include <mali_kbase_hwaccess_jm.h>
#endif
#include <linux/version_compat_defs.h>

#include <mali_kbase_trace_gpu_mem.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#define KBASE_PA_RANGE_THRESHOLD_NR_PAGES 20
#define MGM_DEFAULT_PTE_GROUP (0)

#define pgd_level_to_skip_flush(dirty_pgds) (~(dirty_pgds)&0xF)

static inline void *kmap_pgd(struct page *p, phys_addr_t pgd)
{
#if GPU_PAGES_PER_CPU_PAGE > 1
	return kbase_kmap(p) + (pgd & ~PAGE_MASK);
#else
	CSTD_UNUSED(pgd);
	return kbase_kmap(p);
#endif
}

static inline void *kmap_atomic_pgd(struct page *p, phys_addr_t pgd)
{
#if GPU_PAGES_PER_CPU_PAGE > 1
	return kbase_kmap_atomic(p) + (pgd & ~PAGE_MASK);
#else
	CSTD_UNUSED(pgd);
	return kbase_kmap_atomic(p);
#endif
}

static inline void kunmap_pgd(struct page *p, void *pgd_address)
{

	kbase_kunmap(p, pgd_address);
}

static inline void kunmap_atomic_pgd(void *pgd_address)
{

	kbase_kunmap_atomic(pgd_address);
}

static inline dma_addr_t pgd_dma_addr(struct page *p, phys_addr_t pgd)
{
#if GPU_PAGES_PER_CPU_PAGE > 1
	return kbase_page_private(p)->dma_addr + (pgd & ~PAGE_MASK);
#else
	CSTD_UNUSED(pgd);
	return kbase_dma_addr(p);
#endif
}

static inline u32 get_pgd_sub_page_index(phys_addr_t pgd)
{
	return (pgd & ~PAGE_MASK) / GPU_PAGE_SIZE;
}

#if GPU_PAGES_PER_CPU_PAGE > 1

static bool alloc_pgd_page_metadata(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    struct page *p)
{
	struct kbase_page_metadata *page_md;

	if (!kbase_is_page_migration_enabled()) {
		page_md = kmem_cache_zalloc(kbdev->page_metadata_slab, GFP_KERNEL);
		if (!page_md)
			return false;

		page_md->dma_addr = kbase_dma_addr_as_priv(p);
		set_page_private(p, (unsigned long)page_md);
	} else {
		page_md = kbase_page_private(p);
	}

	page_md->data.pt_mapped.num_allocated_sub_pages = 1;
	set_bit(0, page_md->data.pt_mapped.allocated_sub_pages);
	page_md->data.pt_mapped.pgd_page = p;
	list_add(&page_md->data.pt_mapped.pgd_link, &mmut->pgd_pages_list);

	return true;
}

static void free_pgd_page_metadata(struct kbase_device *kbdev, struct page *p)
{
	struct kbase_page_metadata *page_md = kbase_page_private(p);

	WARN_ON_ONCE(page_md->data.pt_mapped.num_allocated_sub_pages);
	page_md->data.pt_mapped.pgd_page = NULL;
	list_del_init(&page_md->data.pt_mapped.pgd_link);

	if (kbase_is_page_migration_enabled())
		return;

	set_page_private(p, (unsigned long)page_md->dma_addr);
	kmem_cache_free(kbdev->page_metadata_slab, page_md);
}

static inline phys_addr_t allocate_pgd_sub_page(struct kbase_page_metadata *page_md)
{
	unsigned long sub_page_index;

	if (page_md->data.pt_mapped.num_allocated_sub_pages == GPU_PAGES_PER_CPU_PAGE)
		return KBASE_INVALID_PHYSICAL_ADDRESS;
	sub_page_index = find_first_zero_bit(page_md->data.pt_mapped.allocated_sub_pages,
					     GPU_PAGES_PER_CPU_PAGE);

#ifdef CONFIG_MALI_DEBUG
	if (WARN_ON_ONCE(sub_page_index >= GPU_PAGES_PER_CPU_PAGE))
		return KBASE_INVALID_PHYSICAL_ADDRESS;
	if (WARN_ON_ONCE(page_md->data.pt_mapped.num_allocated_sub_pages > GPU_PAGES_PER_CPU_PAGE))
		return KBASE_INVALID_PHYSICAL_ADDRESS;
#endif
	set_bit(sub_page_index, page_md->data.pt_mapped.allocated_sub_pages);
	page_md->data.pt_mapped.num_allocated_sub_pages++;

	return (page_to_phys(page_md->data.pt_mapped.pgd_page) + (sub_page_index * GPU_PAGE_SIZE));
}

static int free_pgd_sub_page(phys_addr_t pgd)
{
	struct page *p = pfn_to_page(PFN_DOWN(pgd));
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	const u32 sub_page_index = get_pgd_sub_page_index(pgd);

#ifdef CONFIG_MALI_DEBUG
	if (WARN_ON_ONCE(!test_bit(sub_page_index, page_md->data.pt_mapped.allocated_sub_pages)))
		return page_md->data.pt_mapped.num_allocated_sub_pages;
#endif
	clear_bit(sub_page_index, page_md->data.pt_mapped.allocated_sub_pages);
	if (!WARN_ON_ONCE(page_md->data.pt_mapped.num_allocated_sub_pages <= 0))
		page_md->data.pt_mapped.num_allocated_sub_pages--;

	if (kbase_is_page_migration_enabled()) {
		spin_lock(&page_md->migrate_lock);
		page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index] = 0;
		spin_unlock(&page_md->migrate_lock);
	}

	return page_md->data.pt_mapped.num_allocated_sub_pages;
}

static inline phys_addr_t allocate_from_pgd_pages_list(struct kbase_mmu_table *mmut)
{
	struct list_head *entry;
	phys_addr_t pgd;

	lockdep_assert_held(&mmut->mmu_lock);

	if (unlikely(!mmut->num_free_pgd_sub_pages))
		return KBASE_INVALID_PHYSICAL_ADDRESS;

	if (mmut->last_allocated_pgd_page) {
		pgd = allocate_pgd_sub_page(kbase_page_private(mmut->last_allocated_pgd_page));
		if (pgd != KBASE_INVALID_PHYSICAL_ADDRESS)
			goto success;
	}

	if (mmut->last_freed_pgd_page) {
		pgd = allocate_pgd_sub_page(kbase_page_private(mmut->last_freed_pgd_page));
		if (pgd != KBASE_INVALID_PHYSICAL_ADDRESS)
			goto success;
	}

	list_for_each(entry, &mmut->pgd_pages_list) {
		struct kbase_page_metadata *page_md =
			list_entry(entry, struct kbase_page_metadata, data.pt_mapped.pgd_link);

		pgd = allocate_pgd_sub_page(page_md);
		if (pgd != KBASE_INVALID_PHYSICAL_ADDRESS)
			goto success;
	}

	return KBASE_INVALID_PHYSICAL_ADDRESS;
success:
	mmut->num_free_pgd_sub_pages--;
	return pgd;
}
#endif

static int mmu_insert_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				     const u64 start_vpfn, struct tagged_addr *phys, size_t nr,
				     unsigned long flags, int const group_id, u64 *dirty_pgds,
				     struct kbase_va_region *reg, bool ignore_page_migration);

static void release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
#if MALI_USE_CSF
	CSTD_UNUSED(kbdev);
	kbase_ctx_sched_release_ctx_lock(kctx);
#else
	kbasep_js_runpool_release_ctx(kbdev, kctx);
#endif
}

static bool mmu_flush_cache_on_gpu_ctrl(struct kbase_device *kbdev)
{
	return kbdev->gpu_props.gpu_id.arch_major > 11;
}

#if MALI_USE_CSF
static void mmu_flush_pa_range(struct kbase_device *kbdev, phys_addr_t phys, size_t nr_bytes,
			       enum kbase_mmu_op_type op)
{
	u32 flush_op;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (op == KBASE_MMU_OP_FLUSH_PT)
		flush_op = GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2;
	else if (op == KBASE_MMU_OP_FLUSH_MEM)
		flush_op = GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2_LSC;
	else {
		dev_warn(kbdev->dev, "Invalid flush request (op = %d)", op);
		return;
	}

	if (kbase_gpu_cache_flush_pa_range_and_busy_wait(kbdev, phys, nr_bytes, flush_op))
		dev_err(kbdev->dev, "Flush for physical address range did not complete");
}
#endif

static void mmu_invalidate(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr,
			   const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_ready && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		if (kbase_mmu_hw_do_unlock(kbdev, &kbdev->as[as_nr], op_param))
			dev_err(kbdev->dev,
				"Invalidate after GPU page table update did not complete");
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

static inline void mmu_invalidate_on_teardown(struct kbase_device *kbdev,
					      struct kbase_context *kctx, u64 vpfn,
					      size_t num_pages, int level, int as_nr)
{
	u32 invalidate_range_num_pages = num_pages;
	u64 invalidate_range_start_vpfn = vpfn;
	struct kbase_mmu_hw_op_param op_param;

	if (level != MIDGARD_MMU_BOTTOMLEVEL) {
		invalidate_range_num_pages = 1 << ((3 - level) * 9);
		invalidate_range_start_vpfn = vpfn - (vpfn & (invalidate_range_num_pages - 1));
	}

	op_param = (struct kbase_mmu_hw_op_param){
		.vpfn = invalidate_range_start_vpfn,
		.nr = invalidate_range_num_pages,
		.mmu_sync_info = CALLER_MMU_ASYNC,
		.kctx_id = kctx ? kctx->id : 0xFFFFFFFF,
		.flush_skip_levels = (1ULL << level) - 1,
	};

	mmu_invalidate(kbdev, kctx, as_nr, &op_param);
}

static void mmu_flush_invalidate_as(struct kbase_device *kbdev, struct kbase_as *as,
				    const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_ready && kbase_mmu_hw_do_flush(kbdev, as, op_param))
		dev_err(kbdev->dev, "Flush for GPU page table update did not complete");

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

}

static void mmu_flush_invalidate(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr,
				 const struct kbase_mmu_hw_op_param *op_param)
{
	bool ctx_is_in_runpool;

	if (op_param->nr == 0)
		return;

	if (!kctx) {
		mmu_flush_invalidate_as(kbdev, &kbdev->as[as_nr], op_param);
	} else {
#if !MALI_USE_CSF
		mutex_lock(&kbdev->js_data.queue_mutex);
		ctx_is_in_runpool = kbase_ctx_sched_inc_refcount(kctx);
		mutex_unlock(&kbdev->js_data.queue_mutex);
#else
		ctx_is_in_runpool = kbase_ctx_sched_inc_refcount_if_as_valid(kctx);
#endif

		if (ctx_is_in_runpool) {
			KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

			mmu_flush_invalidate_as(kbdev, &kbdev->as[kctx->as_nr], op_param);

			release_ctx(kbdev, kctx);
		}
	}
}

static void mmu_flush_invalidate_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_context *kctx,
					     int as_nr,
					     const struct kbase_mmu_hw_op_param *op_param)
{
	unsigned long flags;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->pm.backend.gpu_ready && (!kctx || kctx->as_nr >= 0)) {
		as_nr = kctx ? kctx->as_nr : as_nr;
		if (kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, &kbdev->as[as_nr], op_param))
			dev_err(kbdev->dev, "Flush for GPU page table update did not complete");
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
}

static void kbase_mmu_sync_pgd_gpu(struct kbase_device *kbdev, struct kbase_context *kctx,
				   phys_addr_t phys, size_t size, enum kbase_mmu_op_type flush_op)
{
	kbase_mmu_flush_pa_range(kbdev, kctx, phys, size, flush_op);
}

static void kbase_mmu_sync_pgd_cpu(struct kbase_device *kbdev, dma_addr_t handle, size_t size)
{

	if (kbdev->system_coherency == COHERENCY_NONE)
		dma_sync_single_for_device(kbdev->dev, handle, size, DMA_TO_DEVICE);
}

static void kbase_mmu_sync_pgd(struct kbase_device *kbdev, struct kbase_context *kctx,
			       phys_addr_t phys, dma_addr_t handle, size_t size,
			       enum kbase_mmu_op_type flush_op)
{
	kbase_mmu_sync_pgd_cpu(kbdev, handle, size);
	kbase_mmu_sync_pgd_gpu(kbdev, kctx, phys, size, flush_op);
}

static int kbase_mmu_update_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					   u64 vpfn, struct tagged_addr *phys, size_t nr,
					   unsigned long flags, int group_id, u64 *dirty_pgds);

static void kbase_mmu_update_and_free_parent_pgds(struct kbase_device *kbdev,
						  struct kbase_mmu_table *mmut, phys_addr_t *pgds,
						  u64 vpfn, int level,
						  enum kbase_mmu_op_type flush_op, u64 *dirty_pgds,
						  int as_nr);

static void kbase_mmu_account_freed_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	atomic_sub(1, &kbdev->memdev.used_pages);

	if (mmut->kctx) {
		kbase_process_page_usage_dec(mmut->kctx, 1);
		atomic_sub(1, &mmut->kctx->used_pages);
	}

	kbase_trace_gpu_mem_usage_dec(kbdev, mmut->kctx, 1);
}

static bool kbase_mmu_handle_isolated_pgd_page(struct kbase_device *kbdev,
					       struct kbase_mmu_table *mmut, struct page *p)
{
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	bool page_is_isolated = false;

	lockdep_assert_held(&mmut->mmu_lock);

	if (!kbase_is_page_migration_enabled())
		return false;

	spin_lock(&page_md->migrate_lock);
	if (PAGE_STATUS_GET(page_md->status) == PT_MAPPED) {
		WARN_ON_ONCE(!mmut->kctx);
		if (IS_PAGE_ISOLATED(page_md->status)) {
			page_md->status =
				PAGE_STATUS_SET(page_md->status, FREE_PT_ISOLATED_IN_PROGRESS);
			page_is_isolated = true;
		} else {
			page_md->status = PAGE_STATUS_SET(page_md->status, FREE_IN_PROGRESS);
		}
	} else if ((PAGE_STATUS_GET(page_md->status) == FREE_IN_PROGRESS) ||
		   (PAGE_STATUS_GET(page_md->status) == ALLOCATE_IN_PROGRESS)) {

	} else {
		WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) != NOT_MOVABLE);
	}
	spin_unlock(&page_md->migrate_lock);

	if (unlikely(page_is_isolated)) {

		dma_sync_single_for_device(kbdev->dev, pgd_dma_addr(p, page_to_phys(p)), PAGE_SIZE,
					   DMA_BIDIRECTIONAL);
		kbase_mmu_account_freed_pgd(kbdev, mmut);
	}

	return page_is_isolated;
}

static void kbase_mmu_free_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			       phys_addr_t pgd)
{
	struct page *p;
	bool page_is_isolated = false;

	lockdep_assert_held(&mmut->mmu_lock);

	p = pfn_to_page(PFN_DOWN(pgd));
#if GPU_PAGES_PER_CPU_PAGE > 1
	if (free_pgd_sub_page(pgd)) {
		mmut->num_free_pgd_sub_pages++;
		mmut->last_freed_pgd_page = p;
		return;
	}

	mmut->num_free_pgd_sub_pages -= (GPU_PAGES_PER_CPU_PAGE - 1);
	if (p == mmut->last_freed_pgd_page)
		mmut->last_freed_pgd_page = NULL;
	if (p == mmut->last_allocated_pgd_page)
		mmut->last_allocated_pgd_page = NULL;
	free_pgd_page_metadata(kbdev, p);
#endif
	page_is_isolated = kbase_mmu_handle_isolated_pgd_page(kbdev, mmut, p);

	if (likely(!page_is_isolated)) {
		kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id], p, true);
		kbase_mmu_account_freed_pgd(kbdev, mmut);
	}
}

static void kbase_mmu_free_pgds_list(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	size_t i;

	lockdep_assert_held(&mmut->mmu_lock);

	for (i = 0; i < mmut->scratch_mem.free_pgds.head_index; i++)
		kbase_mmu_free_pgd(kbdev, mmut, mmut->scratch_mem.free_pgds.pgds[i]);

	mmut->scratch_mem.free_pgds.head_index = 0;
}

static void kbase_mmu_add_to_free_pgds_list(struct kbase_mmu_table *mmut, phys_addr_t pgd)
{
	lockdep_assert_held(&mmut->mmu_lock);

	if (WARN_ON_ONCE(mmut->scratch_mem.free_pgds.head_index > (MAX_FREE_PGDS - 1)))
		return;

	mmut->scratch_mem.free_pgds.pgds[mmut->scratch_mem.free_pgds.head_index++] = pgd;
}

static inline void kbase_mmu_reset_free_pgds_list(struct kbase_mmu_table *mmut)
{
	lockdep_assert_held(&mmut->mmu_lock);

	mmut->scratch_mem.free_pgds.head_index = 0;
}

static size_t reg_grow_calc_extra_pages(struct kbase_device *kbdev, struct kbase_va_region *reg,
					size_t fault_rel_pfn)
{
	size_t multiple = reg->extension;
	size_t reg_current_size = kbase_reg_current_backed_size(reg);
	size_t minimum_extra = fault_rel_pfn - reg_current_size + 1;
	size_t remainder;

	if (!multiple) {
		dev_warn(
			kbdev->dev,
			"VA Region 0x%llx extension was 0, allocator needs to set this properly for KBASE_REG_PF_GROW",
			((unsigned long long)reg->start_pfn) << PAGE_SHIFT);
		return minimum_extra;
	}

	remainder = minimum_extra % multiple;

#if !MALI_USE_CSF
	if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {

		size_t initial_commit = reg->initial_commit;

		if (fault_rel_pfn < initial_commit) {

			minimum_extra = initial_commit - reg_current_size;
			remainder = 0;
		} else {

			size_t pages_after_initial =
				minimum_extra + reg_current_size - initial_commit;

			remainder = pages_after_initial % multiple;
		}
	}
#endif

	if (remainder == 0)
		return minimum_extra;

	return minimum_extra + multiple - remainder;
}

#ifdef CONFIG_MALI_CINSTR_GWT
static void kbase_gpu_mmu_handle_write_faulting_as(struct kbase_device *kbdev,
						   struct kbase_as *faulting_as, u64 start_pfn,
						   size_t nr, u32 kctx_id, u64 dirty_pgds)
{

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;
	struct kbase_mmu_hw_op_param op_param;
	unsigned long irq_flags;
	int ret = 0;

	kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

	op_param.vpfn = start_pfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.kctx_id = kctx_id;
	op_param.mmu_sync_info = mmu_sync_info;
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {
		op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);
		ret = kbase_mmu_hw_do_flush_on_gpu_ctrl(kbdev, faulting_as, &op_param);
	} else {
		ret = kbase_mmu_hw_do_flush(kbdev, faulting_as, &op_param);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);

	if (ret)
		dev_err(kbdev->dev,
			"Flush for GPU page fault due to write access did not complete");

	kbase_mmu_hw_enable_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);
}

static void set_gwt_element_page_addr_and_size(struct kbasep_gwt_list_element *element,
					       u64 fault_page_addr, struct tagged_addr fault_phys)
{
	u64 fault_pfn = fault_page_addr >> PAGE_SHIFT;
	unsigned int vindex = fault_pfn & (NUM_PAGES_IN_2MB_LARGE_PAGE - 1);

	if (is_huge(fault_phys) && (vindex == index_in_large_page(fault_phys))) {
		element->page_addr = fault_page_addr & ~(SZ_2M - 1UL);
		element->num_pages = NUM_PAGES_IN_2MB_LARGE_PAGE;
	} else {
		element->page_addr = fault_page_addr;
		element->num_pages = 1;
	}
}

static void kbase_gpu_mmu_handle_write_fault(struct kbase_context *kctx,
					     struct kbase_as *faulting_as)
{
	struct kbasep_gwt_list_element *pos;
	struct kbase_va_region *region;
	struct kbase_device *kbdev;
	struct tagged_addr *fault_phys_addr;
	struct kbase_fault *fault;
	u64 fault_pfn, pfn_offset;
	unsigned int as_no;
	u64 dirty_pgds = 0;

	as_no = faulting_as->number;
	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);
	fault = &faulting_as->pf_data;
	fault_pfn = fault->addr >> PAGE_SHIFT;

	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx, fault->addr);
	if (kbase_is_region_invalid_or_free(region)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"Memory is not mapped on the GPU",
						&faulting_as->pf_data);
		return;
	}

	if (!(region->flags & KBASE_REG_GPU_WR)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"Region does not have write permissions",
						&faulting_as->pf_data);
		return;
	}

	if (unlikely(region->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(
			kctx, faulting_as, "Unexpected write permission fault on an alias region",
			&faulting_as->pf_data);
		return;
	}

	pfn_offset = fault_pfn - region->start_pfn;
	fault_phys_addr = &kbase_get_gpu_phy_pages(region)[pfn_offset];

	if (kctx->gwt_enabled) {
		u64 fault_page_addr = fault->addr & PAGE_MASK;
		bool found = false;

		list_for_each_entry(pos, &kctx->gwt_current_list, link) {
			if (fault_page_addr == pos->page_addr) {
				found = true;
				break;
			}
		}

		if (!found) {
			pos = kmalloc(sizeof(*pos), GFP_KERNEL);
			if (pos) {
				pos->region = region;
				set_gwt_element_page_addr_and_size(pos, fault_page_addr,
								   *fault_phys_addr);
				list_add(&pos->link, &kctx->gwt_current_list);
			} else {
				dev_warn(kbdev->dev, "kmalloc failure");
			}
		}
	}

	kbase_mmu_update_pages_no_flush(kbdev, &kctx->mmu, fault_pfn, fault_phys_addr, 1,
					region->flags, region->gpu_alloc->group_id, &dirty_pgds);

	kbase_gpu_mmu_handle_write_faulting_as(kbdev, faulting_as, fault_pfn, 1, kctx->id,
					       dirty_pgds);

	kbase_gpu_vm_unlock(kctx);
}

static void kbase_gpu_mmu_handle_permission_fault(struct kbase_context *kctx,
						  struct kbase_as *faulting_as)
{
	struct kbase_fault *fault = &faulting_as->pf_data;

	switch (AS_FAULTSTATUS_ACCESS_TYPE_GET(fault->status)) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		kbase_gpu_mmu_handle_write_fault(kctx, faulting_as);
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_EXECUTE:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Execute Permission fault",
						fault);
		break;
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Read Permission fault", fault);
		break;
	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Unknown Permission fault",
						fault);
		break;
	}
}
#endif

static size_t estimate_pool_space_required(struct kbase_mem_pool *pool, const size_t pages_required)
{
	size_t pages_still_required;

	for (pages_still_required = pages_required; pool != NULL && pages_still_required;
	     pool = pool->next_pool) {
		size_t pool_size_small;

		kbase_mem_pool_lock(pool);

		pool_size_small = kbase_mem_pool_size(pool) << pool->order;
		if (pool_size_small >= pages_still_required)
			pages_still_required = 0;
		else
			pages_still_required -= pool_size_small;

		kbase_mem_pool_unlock(pool);
	}
	return pages_still_required;
}

static bool page_fault_try_alloc(struct kbase_context *kctx, struct kbase_va_region *region,
				 size_t new_pages, size_t *pages_to_grow, bool *grow_2mb_pool,
				 bool fallback_to_small, struct kbase_sub_alloc **prealloc_sas)
{
	size_t total_gpu_pages_alloced = 0;
	size_t total_cpu_pages_alloced = 0;
	struct kbase_mem_pool *pool, *root_pool;
	bool alloc_failed = false;
	size_t pages_still_required;
	size_t total_mempools_free_small = 0;

	lockdep_assert_held(&kctx->reg_lock);
	lockdep_assert_held(&kctx->mem_partials_lock);

	if (WARN_ON(region->gpu_alloc->group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)) {

		*pages_to_grow = 0;
		return false;
	}

	if (kbase_is_large_pages_enabled() && new_pages >= NUM_PAGES_IN_2MB_LARGE_PAGE &&
	    !fallback_to_small) {
		root_pool = &kctx->mem_pools.large[region->gpu_alloc->group_id];
		*grow_2mb_pool = true;
	} else {
		root_pool = &kctx->mem_pools.small[region->gpu_alloc->group_id];
		*grow_2mb_pool = false;
	}

	if (region->gpu_alloc != region->cpu_alloc)
		new_pages *= 2;

	pages_still_required = estimate_pool_space_required(root_pool, new_pages);

	if (pages_still_required) {

		*pages_to_grow = pages_still_required;

		return false;
	}

	pages_still_required = new_pages;
	for (pool = root_pool; pool != NULL && pages_still_required; pool = pool->next_pool) {
		size_t pool_size_small;
		size_t pages_to_alloc_small;
		size_t pages_to_alloc_small_per_alloc;

		kbase_mem_pool_lock(pool);

		pool_size_small = kbase_mem_pool_size(pool) << pool->order;
		total_mempools_free_small += pool_size_small;
		pages_to_alloc_small = MIN(pages_still_required, pool_size_small);
		if (region->gpu_alloc == region->cpu_alloc)
			pages_to_alloc_small_per_alloc = pages_to_alloc_small;
		else
			pages_to_alloc_small_per_alloc = pages_to_alloc_small >> 1;

		if (pages_to_alloc_small) {
			struct tagged_addr *gpu_pages = kbase_alloc_phy_pages_helper_locked(
				region->gpu_alloc, pool, pages_to_alloc_small_per_alloc,
				&prealloc_sas[0]);

			if (!gpu_pages)
				alloc_failed = true;
			else
				total_gpu_pages_alloced += pages_to_alloc_small_per_alloc;

			if (!alloc_failed && region->gpu_alloc != region->cpu_alloc) {
				struct tagged_addr *cpu_pages = kbase_alloc_phy_pages_helper_locked(
					region->cpu_alloc, pool, pages_to_alloc_small_per_alloc,
					&prealloc_sas[1]);

				if (!cpu_pages)
					alloc_failed = true;
				else
					total_cpu_pages_alloced += pages_to_alloc_small_per_alloc;
			}
		}

		kbase_mem_pool_unlock(pool);

		if (alloc_failed) {
			WARN_ON(!pages_still_required);
			WARN_ON(pages_to_alloc_small >= pages_still_required);
			WARN_ON(pages_to_alloc_small_per_alloc >= pages_still_required);
			break;
		}

		pages_still_required -= pages_to_alloc_small;
	}

	if (pages_still_required) {

		if (total_gpu_pages_alloced > 0)
			kbase_free_phy_pages_helper(region->gpu_alloc, total_gpu_pages_alloced);
		if (region->gpu_alloc != region->cpu_alloc && total_cpu_pages_alloced > 0)
			kbase_free_phy_pages_helper(region->cpu_alloc, total_cpu_pages_alloced);

		if (alloc_failed) {

			dev_warn(
				kctx->kbdev->dev,
				"Page allocation failure of %zu pages: managed %zu pages, mempool (inc linked pools) had %zu pages available",
				new_pages, total_gpu_pages_alloced + total_cpu_pages_alloced,
				total_mempools_free_small);
			*pages_to_grow = 0;
		} else {

			pages_still_required = estimate_pool_space_required(root_pool, new_pages);
			if (pages_still_required) {
				*pages_to_grow = pages_still_required;
			} else {

				*pages_to_grow = 1u;
			}
		}

		return false;
	}

	*pages_to_grow = 0;

	return true;
}

void kbase_mmu_page_fault_worker(struct work_struct *data)
{
	u64 fault_pfn;
	u32 fault_status;
	size_t new_pages;
	size_t fault_rel_pfn;
	struct kbase_as *faulting_as;
	unsigned int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_va_region *region;
	struct kbase_fault *fault;
	int err;
	bool grown = false;
	size_t pages_to_grow;
	bool grow_2mb_pool = false;
	bool fallback_to_small = false;
	struct kbase_sub_alloc *prealloc_sas[2] = { NULL, NULL };
	int i;
	size_t current_backed_size;
#if MALI_JIT_PRESSURE_LIMIT_BASE
	size_t pages_trimmed = 0;
#endif
	unsigned long hwaccess_flags;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;

	faulting_as = container_of(data, struct kbase_as, work_pagefault);
	fault = &faulting_as->pf_data;
	fault_pfn = fault->addr >> PAGE_SHIFT;
	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);
	dev_dbg(kbdev->dev, "Entering %s %pK, fault_pfn %lld, as_no %u", __func__, (void *)data,
		fault_pfn, as_no);

	kctx = kbase_ctx_sched_as_to_ctx(kbdev, as_no);
	if (!kctx) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

	KBASE_DEBUG_ASSERT(kctx->kbdev == kbdev);

#if MALI_JIT_PRESSURE_LIMIT_BASE
#if !MALI_USE_CSF
	mutex_lock(&kctx->jctx.lock);
#endif
#endif

#ifdef CONFIG_MALI_ARBITER_SUPPORT

	if (unlikely(kbase_is_gpu_removed(kbdev))) {
		dev_dbg(kbdev->dev, "%s: GPU has been removed", __func__);
		goto fault_done;
	}
#endif

	if (unlikely(fault->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Protected mode fault", fault);
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

		goto fault_done;
	}

	fault_status = fault->status;
	switch (AS_FAULTSTATUS_EXCEPTION_TYPE_GET(fault_status)) {
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_3:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_4:
#if !MALI_USE_CSF
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSLATION_FAULT_IDENTITY:
#endif

		break;

	case AS_FAULTSTATUS_EXCEPTION_TYPE_PERMISSION_FAULT_0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_PERMISSION_FAULT_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_PERMISSION_FAULT_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_PERMISSION_FAULT_3:
#ifdef CONFIG_MALI_CINSTR_GWT

		if (kctx->gwt_was_enabled) {
			kbase_gpu_mmu_handle_permission_fault(kctx, faulting_as);
			goto fault_done;
		}
#endif

		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Permission failure", fault);
		goto fault_done;

#if !MALI_USE_CSF
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSTAB_BUS_FAULT_0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSTAB_BUS_FAULT_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSTAB_BUS_FAULT_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_TRANSTAB_BUS_FAULT_3:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Translation table bus fault",
						fault);
		goto fault_done;
#endif

#if !MALI_USE_CSF
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ACCESS_FLAG_0:
		fallthrough;
#endif
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ACCESS_FLAG_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ACCESS_FLAG_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ACCESS_FLAG_3:

		dev_warn(kbdev->dev, "Access flag unexpectedly set");
		goto fault_done;

#if MALI_USE_CSF
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN:
		fallthrough;
#else
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN3:
		fallthrough;
#endif
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT3:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Address size fault", fault);
		goto fault_done;

	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_3:
#if !MALI_USE_CSF
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_NONCACHEABLE_0:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_NONCACHEABLE_1:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_NONCACHEABLE_2:
		fallthrough;
	case AS_FAULTSTATUS_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_NONCACHEABLE_3:
#endif
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Memory attributes fault",
						fault);
		goto fault_done;

	default:
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Unknown fault code", fault);
		goto fault_done;
	}

page_fault_retry:
	if (kbase_is_large_pages_enabled() && !fallback_to_small) {

		for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i) {
			if (!prealloc_sas[i]) {
				prealloc_sas[i] = kmalloc(sizeof(*prealloc_sas[i]), GFP_KERNEL);

				if (!prealloc_sas[i]) {
					kbase_mmu_report_fault_and_kill(
						kctx, faulting_as,
						"Failed pre-allocating memory for sub-allocations' metadata",
						fault);
					goto fault_done;
				}
			}
		}
	}

	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx, fault->addr);
	if (kbase_is_region_invalid_or_free(region)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"Memory is not mapped on the GPU", fault);
		goto fault_done;
	}

	if (region->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"DMA-BUF is not mapped on the GPU", fault);
		goto fault_done;
	}

	if (unlikely(region->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"Unexpected page fault on an alias region",
						&faulting_as->pf_data);
		goto fault_done;
	}

	if (region->gpu_alloc->group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Bad physical memory group ID",
						fault);
		goto fault_done;
	}

	if ((region->flags & GROWABLE_FLAGS_REQUIRED) != GROWABLE_FLAGS_REQUIRED) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Memory is not growable", fault);
		goto fault_done;
	}

	if ((region->flags & KBASE_REG_DONT_NEED)) {
		kbase_gpu_vm_unlock(kctx);
		kbase_mmu_report_fault_and_kill(kctx, faulting_as,
						"Don't need memory can't be grown", fault);
		goto fault_done;
	}

	if (AS_FAULTSTATUS_ACCESS_TYPE_GET(fault_status) == AS_FAULTSTATUS_ACCESS_TYPE_READ)
		dev_warn(kbdev->dev, "Grow on pagefault while reading");

	fault_rel_pfn = fault_pfn - region->start_pfn;

	current_backed_size = kbase_reg_current_backed_size(region);

	if (fault_rel_pfn < current_backed_size) {
		struct kbase_mmu_hw_op_param op_param;

		dev_dbg(kbdev->dev,
			"Page fault @ VA 0x%llx in allocated region 0x%llx-0x%llx of growable TMEM: Ignoring",
			fault->addr, region->start_pfn, region->start_pfn + current_backed_size);

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

		op_param.mmu_sync_info = mmu_sync_info;
		op_param.kctx_id = kctx->id;

		op_param.flush_skip_levels = 0x0;
		op_param.vpfn = fault_pfn;
		op_param.nr = 1;
		spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
		err = kbase_mmu_hw_do_unlock(kbdev, faulting_as, &op_param);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

		if (err) {
			dev_err(kbdev->dev,
				"Invalidation for MMU did not complete on handling page fault @ VA 0x%llx",
				fault->addr);
		}

		kbase_mmu_hw_enable_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);

		goto fault_done;
	}

	new_pages = reg_grow_calc_extra_pages(kbdev, region, fault_rel_pfn);

	new_pages = min(new_pages, region->nr_pages - current_backed_size);
	dev_dbg(kctx->kbdev->dev, "Allocate %zu pages on page fault", new_pages);

	if (new_pages == 0) {
		struct kbase_mmu_hw_op_param op_param;

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

		op_param.mmu_sync_info = mmu_sync_info;
		op_param.kctx_id = kctx->id;

		op_param.flush_skip_levels = 0x0;
		op_param.vpfn = fault_pfn;
		op_param.nr = 1;
		spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
		err = kbase_mmu_hw_do_unlock(kbdev, faulting_as, &op_param);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

		if (err) {
			dev_err(kbdev->dev,
				"Invalidation for MMU did not complete on handling page fault @ VA 0x%llx",
				fault->addr);
		}

		kbase_mmu_hw_enable_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);
		kbase_gpu_vm_unlock(kctx);
		goto fault_done;
	}

	pages_to_grow = 0;

#if MALI_JIT_PRESSURE_LIMIT_BASE
	if ((region->flags & KBASE_REG_ACTIVE_JIT_ALLOC) && !pages_trimmed) {
		kbase_jit_request_phys_increase(kctx, new_pages);
		pages_trimmed = new_pages;
	}
#endif

	spin_lock(&kctx->mem_partials_lock);
	grown = page_fault_try_alloc(kctx, region, new_pages, &pages_to_grow, &grow_2mb_pool,
				     fallback_to_small, prealloc_sas);
	spin_unlock(&kctx->mem_partials_lock);

	if (grown) {
		u64 dirty_pgds = 0;
		u64 pfn_offset;
		struct kbase_mmu_hw_op_param op_param;

		WARN_ON(kbase_reg_current_backed_size(region) > region->nr_pages);

		pfn_offset = kbase_reg_current_backed_size(region) - new_pages;

		err = mmu_insert_pages_no_flush(kbdev, &kctx->mmu, region->start_pfn + pfn_offset,
						&kbase_get_gpu_phy_pages(region)[pfn_offset],
						new_pages, region->flags,
						region->gpu_alloc->group_id, &dirty_pgds, region,
						false);
		if (err) {
			kbase_free_phy_pages_helper(region->gpu_alloc, new_pages);
			if (region->gpu_alloc != region->cpu_alloc)
				kbase_free_phy_pages_helper(region->cpu_alloc, new_pages);
			kbase_gpu_vm_unlock(kctx);

			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
							"Page table update failure", fault);
			goto fault_done;
		}
		KBASE_TLSTREAM_AUX_PAGEFAULT(kbdev, kctx->id, as_no, (u64)new_pages);
		if (kbase_reg_is_valid(kbdev, MMU_AS_OFFSET(as_no, FAULTEXTRA)))
			trace_mali_mmu_page_fault_extra_grow(region, fault, new_pages);
		else
			trace_mali_mmu_page_fault_grow(region, fault, new_pages);

		kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

		op_param.vpfn = region->start_pfn + pfn_offset;
		op_param.nr = new_pages;
		op_param.op = KBASE_MMU_OP_FLUSH_PT;
		op_param.kctx_id = kctx->id;
		op_param.mmu_sync_info = mmu_sync_info;
		spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
		if (mmu_flush_cache_on_gpu_ctrl(kbdev)) {

			op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);
			err = kbase_mmu_hw_do_unlock(kbdev, faulting_as, &op_param);
		} else {

			err = kbase_mmu_hw_do_flush(kbdev, faulting_as, &op_param);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

		if (err) {
			dev_err(kbdev->dev,
				"Flush for GPU page table update did not complete on handling page fault @ VA 0x%llx",
				fault->addr);
		}

		kbase_mmu_hw_enable_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_PAGE);

#ifdef CONFIG_MALI_CINSTR_GWT
		if (kctx->gwt_enabled) {

			struct kbasep_gwt_list_element *pos;

			pos = kmalloc(sizeof(*pos), GFP_KERNEL);
			if (pos) {
				pos->region = region;
				pos->page_addr = (region->start_pfn + pfn_offset) << PAGE_SHIFT;
				pos->num_pages = new_pages;
				list_add(&pos->link, &kctx->gwt_current_list);
			} else {
				dev_warn(kbdev->dev, "kmalloc failure");
			}
		}
#endif

#if MALI_JIT_PRESSURE_LIMIT_BASE
		if (pages_trimmed) {
			kbase_jit_done_phys_increase(kctx, pages_trimmed);
			pages_trimmed = 0;
		}
#endif
		kbase_gpu_vm_unlock(kctx);
	} else {
		int ret = -ENOMEM;
		const u8 group_id = region->gpu_alloc->group_id;

		kbase_gpu_vm_unlock(kctx);

		if (pages_to_grow > 0) {
			if (kbase_is_large_pages_enabled() && grow_2mb_pool) {

				struct kbase_mem_pool *const lp_mem_pool =
					&kctx->mem_pools.large[group_id];

				pages_to_grow =
					(pages_to_grow + ((1u << lp_mem_pool->order) - 1u)) >>
					lp_mem_pool->order;

				ret = kbase_mem_pool_grow(lp_mem_pool, pages_to_grow, kctx->task);

				if (ret < 0) {
					fallback_to_small = true;
					dev_dbg(kbdev->dev,
						"No room for 2MB pages, fallback to small pages");
					goto page_fault_retry;
				}
			} else {
				struct kbase_mem_pool *const mem_pool =
					&kctx->mem_pools.small[group_id];

				ret = kbase_mem_pool_grow(mem_pool, pages_to_grow, kctx->task);
			}
		}
		if (ret < 0) {

			if (unlikely(ret == -EPERM))
				kbase_ctx_flag_set(kctx, KCTX_PAGE_FAULT_REPORT_SKIP);
			kbase_mmu_report_fault_and_kill(kctx, faulting_as,
							"Page allocation failure", fault);
		} else {
			dev_dbg(kbdev->dev, "Try again after pool_grow");
			goto page_fault_retry;
		}
	}

fault_done:
#if MALI_JIT_PRESSURE_LIMIT_BASE
	if (pages_trimmed) {
		kbase_gpu_vm_lock(kctx);
		kbase_jit_done_phys_increase(kctx, pages_trimmed);
		kbase_gpu_vm_unlock(kctx);
	}
#if !MALI_USE_CSF
	mutex_unlock(&kctx->jctx.lock);
#endif
#endif

	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i)
		kfree(prealloc_sas[i]);

	release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
	dev_dbg(kbdev->dev, "Leaving page_fault_worker %pK", (void *)data);
}

static phys_addr_t kbase_mmu_alloc_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	u64 *page;
	struct page *p;
	phys_addr_t pgd;

	lockdep_assert_held(&mmut->mmu_lock);

#if GPU_PAGES_PER_CPU_PAGE > 1
	pgd = allocate_from_pgd_pages_list(mmut);
	if (pgd != KBASE_INVALID_PHYSICAL_ADDRESS)
		return pgd;
#endif

	p = kbase_mem_pool_alloc(&kbdev->mem_pools.small[mmut->group_id]);
	if (!p)
		return KBASE_INVALID_PHYSICAL_ADDRESS;

	page = kbase_kmap(p);

	if (page == NULL)
		goto alloc_free;

#if GPU_PAGES_PER_CPU_PAGE > 1
	if (!alloc_pgd_page_metadata(kbdev, mmut, p)) {
		kbase_kunmap(p, page);
		goto alloc_free;
	}
	mmut->num_free_pgd_sub_pages += (GPU_PAGES_PER_CPU_PAGE - 1);
	mmut->last_allocated_pgd_page = p;
#endif

	pgd = page_to_phys(p);

	if (mmut->kctx) {
		int new_page_count;

		new_page_count = atomic_add_return(1, &mmut->kctx->used_pages);
		KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, mmut->kctx->id, (u64)new_page_count);
		kbase_process_page_usage_inc(mmut->kctx, 1);
	}

	atomic_add(1, &kbdev->memdev.used_pages);

	kbase_trace_gpu_mem_usage_inc(kbdev, mmut->kctx, 1);

	kbdev->mmu_mode->entries_invalidate(page, KBASE_MMU_PAGE_ENTRIES * GPU_PAGES_PER_CPU_PAGE);

	kbase_mmu_sync_pgd_cpu(kbdev, pgd_dma_addr(p, pgd), PAGE_SIZE);

	kbase_kunmap(p, page);
	return pgd;

alloc_free:
	kbase_mem_pool_free(&kbdev->mem_pools.small[mmut->group_id], p, false);

	return KBASE_INVALID_PHYSICAL_ADDRESS;
}

static int mmu_get_next_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			    phys_addr_t *pgd, u64 vpfn, int level)
{
	u64 *page;
	phys_addr_t target_pgd;
	struct page *p;

	lockdep_assert_held(&mmut->mmu_lock);

	vpfn >>= (3 - level) * 9;
	vpfn &= 0x1FF;

	p = pfn_to_page(PFN_DOWN(*pgd));
	page = kmap_pgd(p, *pgd);
	if (page == NULL) {
		dev_err(kbdev->dev, "%s: kmap failure", __func__);
		return -EINVAL;
	}

	if (!kbdev->mmu_mode->pte_is_valid(page[vpfn], level)) {
		dev_dbg(kbdev->dev, "%s: invalid PTE at level %d vpfn 0x%llx", __func__, level,
			vpfn);
		kunmap_pgd(p, page);
		return -EFAULT;
	} else {
		target_pgd = kbdev->mmu_mode->pte_to_phy_addr(
			kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[vpfn]));
	}

	kunmap_pgd(p, page);
	*pgd = target_pgd;

	return 0;
}

static int mmu_get_lowest_valid_pgd(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    u64 vpfn, int in_level, int *out_level, phys_addr_t *out_pgd)
{
	phys_addr_t pgd;
	int l;
	int err = 0;

	lockdep_assert_held(&mmut->mmu_lock);
	pgd = mmut->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < in_level; l++) {
		err = mmu_get_next_pgd(kbdev, mmut, &pgd, vpfn, l);

		if (err) {
			dev_dbg(kbdev->dev,
				"%s: mmu_get_next_pgd() failed to find a valid pgd at level %d",
				__func__, l + 1);
			break;
		}
	}

	*out_pgd = pgd;
	*out_level = l;

	if (err == -EFAULT)
		err = 0;

	return err;
}
KBASE_ALLOW_ERROR_INJECTION_TEST_API(mmu_get_lowest_valid_pgd, ERRNO);

static int mmu_get_pgd_at_level(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
				int level, phys_addr_t *out_pgd)
{
	phys_addr_t pgd;
	int l;

	lockdep_assert_held(&mmut->mmu_lock);
	pgd = mmut->pgd;

	for (l = MIDGARD_MMU_TOPLEVEL; l < level; l++) {
		int err = mmu_get_next_pgd(kbdev, mmut, &pgd, vpfn, l);

		if (err) {
			dev_err(kbdev->dev,
				"%s: mmu_get_next_pgd() failed to find a valid pgd at level %d",
				__func__, l + 1);
			return err;
		}
	}

	*out_pgd = pgd;

	return 0;
}

static void mmu_insert_pages_failure_recovery(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, u64 from_vpfn,
					      u64 to_vpfn, u64 *dirty_pgds,
					      struct tagged_addr *phys, bool ignore_page_migration)
{
	u64 vpfn = from_vpfn;
	struct kbase_mmu_mode const *mmu_mode;

	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / GPU_PAGE_SIZE));
	KBASE_DEBUG_ASSERT(from_vpfn <= to_vpfn);

	lockdep_assert_held(&mmut->mmu_lock);

	mmu_mode = kbdev->mmu_mode;
	kbase_mmu_reset_free_pgds_list(mmut);

	while (vpfn < to_vpfn) {
		unsigned int idx = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - idx;
		unsigned int pcount = 0;
		unsigned int left = to_vpfn - vpfn;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
		phys_addr_t pgd = mmut->pgd;
		struct page *p = phys_to_page(pgd);

		register unsigned int num_of_valid_entries;

		if (count > left)
			count = left;

		for (level = MIDGARD_MMU_TOPLEVEL; level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			idx = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			pgds[level] = pgd;
			page = kmap_pgd(p, pgd);
			if (mmu_mode->ate_is_valid(page[idx], level))
				break;
			kunmap_pgd(p, page);
			pgd = mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
				kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[idx]));
			p = phys_to_page(pgd);
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(2):

			pcount = 1;
			break;
		case MIDGARD_MMU_BOTTOMLEVEL:

			pcount = count;
			break;
		default:
			dev_warn(kbdev->dev, "%sNo support for ATEs at level %d", __func__, level);
			goto next;
		}

		if (dirty_pgds && pcount > 0)
			*dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		mmu_mode->entries_invalidate(&page[idx], pcount);

		if (!num_of_valid_entries) {
			mmu_mode->set_num_valid_entries(page, 0);

			kunmap_pgd(p, page);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level - 1,
							      KBASE_MMU_OP_NONE, dirty_pgds, 0);

			kbase_mmu_add_to_free_pgds_list(mmut, pgd);

			vpfn += count;
			continue;
		}

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (idx * sizeof(u64)),
				   pgd_dma_addr(p, pgd) + sizeof(u64) * idx, sizeof(u64) * pcount,
				   KBASE_MMU_OP_NONE);
		kunmap_pgd(p, page);
next:
		vpfn += count;
	}

	if (kbase_is_page_migration_enabled() && !ignore_page_migration && phys &&
	    !is_huge(*phys) && !is_partial(*phys)) {
		const u64 num_pages = (to_vpfn - from_vpfn) / GPU_PAGES_PER_CPU_PAGE;
		u64 i;

		for (i = 0; i < num_pages; i++) {
			struct page *phys_page = as_page(phys[i]);
			struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

			if (page_md) {
				spin_lock(&page_md->migrate_lock);
				page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
				spin_unlock(&page_md->migrate_lock);
			}
		}
	}
}

static void mmu_flush_invalidate_insert_pages(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, const u64 vpfn,
					      size_t nr, u64 dirty_pgds,
					      enum kbase_caller_mmu_sync_info mmu_sync_info,
					      bool insert_pages_failed)
{
	struct kbase_mmu_hw_op_param op_param;
	int as_nr = 0;

	op_param.vpfn = vpfn;
	op_param.nr = nr;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.mmu_sync_info = mmu_sync_info;
	op_param.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF;
	op_param.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds);

#if MALI_USE_CSF
	as_nr = mmut->kctx ? mmut->kctx->as_nr : MCU_AS_NR;
#else
	WARN_ON(!mmut->kctx);
#endif

	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && !insert_pages_failed)
		mmu_invalidate(kbdev, mmut->kctx, as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, mmut->kctx, as_nr, &op_param);
}

static int update_parent_pgds(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			      int cur_level, int insert_level, u64 insert_vpfn,
			      phys_addr_t *pgds_to_insert)
{
	int pgd_index;
	int err = 0;

	for (pgd_index = cur_level; pgd_index > insert_level; pgd_index--) {
		int parent_index = pgd_index - 1;
		phys_addr_t parent_pgd = pgds_to_insert[parent_index];
		unsigned int current_valid_entries;
		u64 pte;
		phys_addr_t target_pgd = pgds_to_insert[pgd_index];
		u64 parent_vpfn = (insert_vpfn >> ((3 - parent_index) * 9)) & 0x1FF;
		struct page *parent_page = pfn_to_page(PFN_DOWN(parent_pgd));
		u64 *parent_page_va;

		if (WARN_ON_ONCE(target_pgd == KBASE_INVALID_PHYSICAL_ADDRESS)) {
			err = -EFAULT;
			goto failure_recovery;
		}

		parent_page_va = kmap_pgd(parent_page, parent_pgd);

		if (unlikely(parent_page_va == NULL)) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -EINVAL;
			goto failure_recovery;
		}

		current_valid_entries = kbdev->mmu_mode->get_num_valid_entries(parent_page_va);

		kbdev->mmu_mode->entry_set_pte(&pte, target_pgd);
		parent_page_va[parent_vpfn] = kbdev->mgm_dev->ops.mgm_update_gpu_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, parent_index, pte);
		kbdev->mmu_mode->set_num_valid_entries(parent_page_va, current_valid_entries + 1);
		kunmap_pgd(parent_page, parent_page_va);

		if (parent_index != insert_level) {

			kbase_mmu_sync_pgd_cpu(kbdev,
					       pgd_dma_addr(parent_page, parent_pgd) +
						       (parent_vpfn * sizeof(u64)),
					       sizeof(u64));
		} else {

			kbase_mmu_sync_pgd(
				kbdev, mmut->kctx, parent_pgd + (parent_vpfn * sizeof(u64)),
				pgd_dma_addr(parent_page, parent_pgd) + (parent_vpfn * sizeof(u64)),
				sizeof(u64), KBASE_MMU_OP_FLUSH_PT);
		}

		if (kbase_is_page_migration_enabled()) {
			struct kbase_page_metadata *page_md =
				kbase_page_private(phys_to_page(target_pgd));

			spin_lock(&page_md->migrate_lock);

#if GPU_PAGES_PER_CPU_PAGE > 1
			if (mmut->kctx) {
				u32 sub_page_index = get_pgd_sub_page_index(target_pgd);

				if (likely(PAGE_STATUS_GET(page_md->status) != NOT_MOVABLE)) {
					if (PAGE_STATUS_GET(page_md->status) != PT_MAPPED) {
						WARN_ON_ONCE(IS_PAGE_ISOLATED(page_md->status));
						WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) !=
							     ALLOCATE_IN_PROGRESS);

						page_md->status =
							PAGE_STATUS_SET(page_md->status, PT_MAPPED);
						page_md->data.pt_mapped.mmut = mmut;
					} else {
						WARN_ON_ONCE(page_md->data.pt_mapped.mmut != mmut);
					}

					page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index] =
						PGD_VPFN_LEVEL_SET(insert_vpfn, parent_index);
				} else {

					WARN_ON_ONCE(!sub_page_index);
					WARN_ON_ONCE(mmut->pgd != (target_pgd & PAGE_MASK));
				}
			} else if (PAGE_STATUS_GET(page_md->status) != NOT_MOVABLE) {
				WARN_ON_ONCE(IS_PAGE_ISOLATED(page_md->status));
				WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) !=
					     ALLOCATE_IN_PROGRESS);
				page_md->status = PAGE_STATUS_SET(page_md->status, NOT_MOVABLE);
			}
#else
			WARN_ON_ONCE(PAGE_STATUS_GET(page_md->status) != ALLOCATE_IN_PROGRESS ||
				     IS_PAGE_ISOLATED(page_md->status));

			if (mmut->kctx) {
				page_md->status = PAGE_STATUS_SET(page_md->status, PT_MAPPED);
				page_md->data.pt_mapped.mmut = mmut;
				page_md->data.pt_mapped.pgd_vpfn_level[0] =
					PGD_VPFN_LEVEL_SET(insert_vpfn, parent_index);
			} else {
				page_md->status = PAGE_STATUS_SET(page_md->status, NOT_MOVABLE);
			}
#endif

			spin_unlock(&page_md->migrate_lock);
		}
	}

	return 0;

failure_recovery:

	for (; pgd_index < cur_level; pgd_index++) {
		phys_addr_t pgd = pgds_to_insert[pgd_index];
		struct page *pgd_page = pfn_to_page(PFN_DOWN(pgd));
		u64 *pgd_page_va = kmap_pgd(pgd_page, pgd);
		u64 vpfn = (insert_vpfn >> ((3 - pgd_index) * 9)) & 0x1FF;

		kbdev->mmu_mode->entries_invalidate(&pgd_page_va[vpfn], 1);
		kunmap_pgd(pgd_page, pgd_page_va);
	}

	return err;
}

static int mmu_insert_alloc_pgds(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				 phys_addr_t *new_pgds, int level_low, int level_high,
				 bool *pool_grown)
{
	int err = 0;
	int i;

	lockdep_assert_held(&mmut->mmu_lock);

	*pool_grown = false;
	for (i = level_low; i <= level_high; i++) {
		if (new_pgds[i] != KBASE_INVALID_PHYSICAL_ADDRESS)
			continue;
		do {
			new_pgds[i] = kbase_mmu_alloc_pgd(kbdev, mmut);
			if (new_pgds[i] != KBASE_INVALID_PHYSICAL_ADDRESS)
				break;
			mutex_unlock(&mmut->mmu_lock);
			err = kbase_mem_pool_grow(&kbdev->mem_pools.small[mmut->group_id],
						  (size_t)level_high, NULL);
			mutex_lock(&mmut->mmu_lock);
			if (err) {
				dev_err(kbdev->dev, "%s: kbase_mem_pool_grow() returned error %d",
					__func__, err);
				return err;
			}
			*pool_grown = true;
		} while (1);
	}

	return 0;
}

static int kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 start_vpfn,
					struct tagged_addr phys, size_t nr, unsigned long flags,
					int const group_id,
					enum kbase_caller_mmu_sync_info mmu_sync_info,
					bool ignore_page_migration)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_device *kbdev;
	u64 dirty_pgds = 0;
	unsigned int i;
	phys_addr_t new_pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
	enum kbase_mmu_op_type flush_op;
	struct kbase_mmu_table *mmut = &kctx->mmu;
	int l, cur_level, insert_level;
	const phys_addr_t base_phys_address = as_phys_addr_t(phys);

	if (WARN_ON(kctx == NULL))
		return -EINVAL;

	lockdep_assert_held(&kctx->reg_lock);

	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	kbdev = kctx->kbdev;

	if (nr == 0)
		return 0;

	insert_vpfn *= GPU_PAGES_PER_CPU_PAGE;
	remain *= GPU_PAGES_PER_CPU_PAGE;

	if (kbase_is_page_migration_enabled() && !ignore_page_migration) {
		struct page *phys_page = as_page(phys);
		struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

		if (page_md) {
			spin_lock(&page_md->migrate_lock);
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
			spin_unlock(&page_md->migrate_lock);
		}
	}

	mutex_lock(&mmut->mmu_lock);

	while (remain) {
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		register unsigned int num_of_valid_entries;
		bool newly_created_pgd = false;
		bool pool_grown;

		if (count > remain)
			count = remain;

		cur_level = MIDGARD_MMU_BOTTOMLEVEL;
		insert_level = cur_level;

		for (l = MIDGARD_MMU_TOPLEVEL + 1; l <= cur_level; l++)
			new_pgds[l] = KBASE_INVALID_PHYSICAL_ADDRESS;

repeat_page_table_walk:

		err = mmu_get_lowest_valid_pgd(kbdev, mmut, insert_vpfn, cur_level, &insert_level,
					       &pgd);

		if (err) {
			dev_err(kbdev->dev, "%s: mmu_get_lowest_valid_pgd() returned error %d",
				__func__, err);
			goto fail_unlock_free_pgds;
		}

		if (insert_level != cur_level) {

			err = mmu_insert_alloc_pgds(kbdev, mmut, new_pgds, (insert_level + 1),
						    cur_level, &pool_grown);
			if (err)
				goto fail_unlock_free_pgds;

			if (pool_grown)
				goto repeat_page_table_walk;

			newly_created_pgd = true;

			new_pgds[insert_level] = pgd;

			pgd = new_pgds[cur_level];
		}

		p = pfn_to_page(PFN_DOWN(pgd));

		pgd_page = kmap_pgd(p, pgd);
		if (!pgd_page) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -ENOMEM;

			goto fail_unlock_free_pgds;
		}

		num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(pgd_page);

		for (i = 0; i < count; i += GPU_PAGES_PER_CPU_PAGE) {
			unsigned int j;

			for (j = 0; j < GPU_PAGES_PER_CPU_PAGE; j++) {
				unsigned int ofs = vindex + i + j;
				phys_addr_t page_address = base_phys_address + (j * GPU_PAGE_SIZE);

				WARN_ON_ONCE((pgd_page[ofs] & 1UL));
				pgd_page[ofs] = kbase_mmu_create_ate(kbdev, as_tagged(page_address),
								     flags, MIDGARD_MMU_BOTTOMLEVEL,
								     group_id);
			}
		}

		kbdev->mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries + count);

		dirty_pgds |= 1ULL << (newly_created_pgd ? insert_level : MIDGARD_MMU_BOTTOMLEVEL);

		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, kctx, pgd + (vindex * sizeof(u64)),
				   pgd_dma_addr(p, pgd) + (vindex * sizeof(u64)),
				   count * sizeof(u64), flush_op);

		if (newly_created_pgd) {
			err = update_parent_pgds(kbdev, mmut, cur_level, insert_level, insert_vpfn,
						 new_pgds);
			if (err) {
				dev_err(kbdev->dev, "%s: update_parent_pgds() failed (%d)",
					__func__, err);

				kbdev->mmu_mode->entries_invalidate(&pgd_page[vindex], count);

				kunmap_pgd(p, pgd_page);
				goto fail_unlock_free_pgds;
			}
		}

		insert_vpfn += count;
		remain -= count;
		kunmap_pgd(p, pgd_page);
	}

	mutex_unlock(&mmut->mmu_lock);

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr, dirty_pgds, mmu_sync_info,
					  false);

	return 0;

fail_unlock_free_pgds:

	for (l = cur_level; l > insert_level; l--)
		if (new_pgds[l] != KBASE_INVALID_PHYSICAL_ADDRESS)
			kbase_mmu_free_pgd(kbdev, mmut, new_pgds[l]);

	if (insert_vpfn != (start_vpfn * GPU_PAGES_PER_CPU_PAGE)) {

		mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn * GPU_PAGES_PER_CPU_PAGE,
						  insert_vpfn, &dirty_pgds, NULL, true);
	}

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr, dirty_pgds, mmu_sync_info,
					  true);
	kbase_mmu_free_pgds_list(kbdev, mmut);
	mutex_unlock(&mmut->mmu_lock);

	return err;
}

int kbase_mmu_insert_single_imported_page(struct kbase_context *kctx, u64 vpfn,
					  struct tagged_addr phys, size_t nr, unsigned long flags,
					  int const group_id,
					  enum kbase_caller_mmu_sync_info mmu_sync_info)
{

	return kbase_mmu_insert_single_page(kctx, vpfn, phys, nr, flags, group_id, mmu_sync_info,
					    false);
}

int kbase_mmu_insert_single_aliased_page(struct kbase_context *kctx, u64 vpfn,
					 struct tagged_addr phys, size_t nr, unsigned long flags,
					 int const group_id,
					 enum kbase_caller_mmu_sync_info mmu_sync_info)
{

	return kbase_mmu_insert_single_page(kctx, vpfn, phys, nr, flags, group_id, mmu_sync_info,
					    false);
}

static void kbase_mmu_progress_migration_on_insert(struct tagged_addr phys,
						   struct kbase_va_region *reg,
						   struct kbase_mmu_table *mmut, const u64 vpfn)
{
	struct page *phys_page = as_page(phys);
	struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

	if (!kbase_is_page_migration_enabled())
		return;

	spin_lock(&page_md->migrate_lock);

	if (!reg || PAGE_STATUS_GET(page_md->status) == (u8)ALLOCATED_MAPPED) {
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
	} else if (PAGE_STATUS_GET(page_md->status) == (u8)ALLOCATE_IN_PROGRESS) {
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)ALLOCATED_MAPPED);
		page_md->data.mapped.reg = reg;
		page_md->data.mapped.mmut = mmut;
		page_md->data.mapped.vpfn = vpfn;
	}

	spin_unlock(&page_md->migrate_lock);
}

static void kbase_mmu_progress_migration_on_teardown(struct kbase_device *kbdev,
						     struct tagged_addr *phys, size_t requested_nr)
{
	size_t i;

	if (!kbase_is_page_migration_enabled())
		return;

	for (i = 0; i < requested_nr; i++) {
		struct page *phys_page = as_page(phys[i]);
		struct kbase_page_metadata *page_md = kbase_page_private(phys_page);

		if (is_huge(phys[i]) || is_partial(phys[i]))
			continue;

		if (page_md) {
			u8 status;

			spin_lock(&page_md->migrate_lock);
			status = PAGE_STATUS_GET(page_md->status);

			if (status == ALLOCATED_MAPPED) {
				if (IS_PAGE_ISOLATED(page_md->status)) {
					page_md->status = PAGE_STATUS_SET(
						page_md->status, (u8)FREE_ISOLATED_IN_PROGRESS);

					phys[i] = as_tagged(KBASE_INVALID_PHYSICAL_ADDRESS);
				} else
					page_md->status = PAGE_STATUS_SET(page_md->status,
									  (u8)FREE_IN_PROGRESS);
			}

			spin_unlock(&page_md->migrate_lock);
		}
	}
}

u64 kbase_mmu_create_ate(struct kbase_device *const kbdev, struct tagged_addr const phy,
			 unsigned long const flags, int const level, int const group_id)
{
	u64 entry;

	kbdev->mmu_mode->entry_set_ate(&entry, phy, flags, level);
	return kbdev->mgm_dev->ops.mgm_update_gpu_pte(kbdev->mgm_dev, (unsigned int)group_id, level,
						      entry);
}

static int mmu_insert_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				     u64 start_vpfn, struct tagged_addr *phys, size_t nr,
				     unsigned long flags, int const group_id, u64 *dirty_pgds,
				     struct kbase_va_region *reg, bool ignore_page_migration)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	u64 insert_vpfn = start_vpfn;
	size_t remain = nr;
	int err;
	struct kbase_mmu_mode const *mmu_mode;
	unsigned int i;
	phys_addr_t new_pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
	int l, cur_level, insert_level;
	struct tagged_addr *start_phys = phys;

	if (mmut->kctx)
		lockdep_assert_held(&mmut->kctx->reg_lock);

	KBASE_DEBUG_ASSERT(start_vpfn <= (U64_MAX / PAGE_SIZE));

	mmu_mode = kbdev->mmu_mode;

	if (nr == 0)
		return 0;

	insert_vpfn *= GPU_PAGES_PER_CPU_PAGE;
	remain *= GPU_PAGES_PER_CPU_PAGE;
	mutex_lock(&mmut->mmu_lock);

	while (remain) {
		unsigned int vindex = insert_vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - vindex;
		struct page *p;
		register unsigned int num_of_valid_entries;
		bool newly_created_pgd = false;
		enum kbase_mmu_op_type flush_op;
		bool pool_grown;

		if (count > remain)
			count = remain;

		if (!vindex && is_huge_head(*phys) && (count == KBASE_MMU_PAGE_ENTRIES))
			cur_level = MIDGARD_MMU_LEVEL(2);
		else
			cur_level = MIDGARD_MMU_BOTTOMLEVEL;

		insert_level = cur_level;

		for (l = MIDGARD_MMU_TOPLEVEL + 1; l <= cur_level; l++)
			new_pgds[l] = KBASE_INVALID_PHYSICAL_ADDRESS;

repeat_page_table_walk:

		err = mmu_get_lowest_valid_pgd(kbdev, mmut, insert_vpfn, cur_level, &insert_level,
					       &pgd);

		if (err) {
			dev_err(kbdev->dev, "%s: mmu_get_lowest_valid_pgd() returned error %d",
				__func__, err);
			goto fail_unlock_free_pgds;
		}

		if (insert_level != cur_level) {

			err = mmu_insert_alloc_pgds(kbdev, mmut, new_pgds, (insert_level + 1),
						    cur_level, &pool_grown);
			if (err)
				goto fail_unlock_free_pgds;

			if (pool_grown)
				goto repeat_page_table_walk;

			newly_created_pgd = true;

			new_pgds[insert_level] = pgd;

			pgd = new_pgds[cur_level];
		}

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap_pgd(p, pgd);

		if (!pgd_page) {
			dev_err(kbdev->dev, "%s: kmap failure", __func__);
			err = -ENOMEM;

			goto fail_unlock_free_pgds;
		}

		num_of_valid_entries = mmu_mode->get_num_valid_entries(pgd_page);

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			int level_index = (insert_vpfn >> 9) & 0x1FF;
			pgd_page[level_index] =
				kbase_mmu_create_ate(kbdev, *phys, flags, cur_level, group_id);

			num_of_valid_entries++;
		} else {
			for (i = 0; i < count; i += GPU_PAGES_PER_CPU_PAGE) {
				struct tagged_addr base_tagged_addr =
					phys[i / GPU_PAGES_PER_CPU_PAGE];
				phys_addr_t base_phys_address = as_phys_addr_t(base_tagged_addr);
				unsigned int j;

				for (j = 0; j < GPU_PAGES_PER_CPU_PAGE; j++) {
					unsigned int ofs = vindex + i + j;
					u64 *target = &pgd_page[ofs];
					phys_addr_t page_address =
						base_phys_address + (j * GPU_PAGE_SIZE);

					WARN_ON_ONCE((*target & 1UL) != 0);

					*target = kbase_mmu_create_ate(kbdev,
								       as_tagged(page_address),
								       flags, cur_level, group_id);
				}

				if (kbase_is_page_migration_enabled() && !ignore_page_migration &&
				    !is_huge(base_tagged_addr) && !is_partial(base_tagged_addr))
					kbase_mmu_progress_migration_on_insert(
						base_tagged_addr, reg, mmut, insert_vpfn + i);
			}
			num_of_valid_entries += count;
		}

		mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

		if (dirty_pgds)
			*dirty_pgds |= 1ULL << (newly_created_pgd ? insert_level : cur_level);

		flush_op = newly_created_pgd ? KBASE_MMU_OP_NONE : KBASE_MMU_OP_FLUSH_PT;

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (vindex * sizeof(u64)),
				   pgd_dma_addr(p, pgd) + (vindex * sizeof(u64)),
				   count * sizeof(u64), flush_op);

		if (newly_created_pgd) {
			err = update_parent_pgds(kbdev, mmut, cur_level, insert_level, insert_vpfn,
						 new_pgds);
			if (err) {
				dev_err(kbdev->dev, "%s: update_parent_pgds() failed (%d)",
					__func__, err);

				kbdev->mmu_mode->entries_invalidate(&pgd_page[vindex], count);

				kunmap_pgd(p, pgd_page);
				goto fail_unlock_free_pgds;
			}
		}

		phys += (count / GPU_PAGES_PER_CPU_PAGE);
		insert_vpfn += count;
		remain -= count;
		kunmap_pgd(p, pgd_page);
	}

	mutex_unlock(&mmut->mmu_lock);

	return 0;

fail_unlock_free_pgds:

	for (l = cur_level; l > insert_level; l--)
		if (new_pgds[l] != KBASE_INVALID_PHYSICAL_ADDRESS)
			kbase_mmu_free_pgd(kbdev, mmut, new_pgds[l]);

	if (insert_vpfn != (start_vpfn * GPU_PAGES_PER_CPU_PAGE)) {

		mmu_insert_pages_failure_recovery(kbdev, mmut, start_vpfn * GPU_PAGES_PER_CPU_PAGE,
						  insert_vpfn, dirty_pgds, start_phys,
						  ignore_page_migration);
	}

	mmu_flush_invalidate_insert_pages(kbdev, mmut, start_vpfn, nr,
					  dirty_pgds ? *dirty_pgds : 0xF, CALLER_MMU_ASYNC, true);
	kbase_mmu_free_pgds_list(kbdev, mmut);
	mutex_unlock(&mmut->mmu_lock);

	return err;
}

int kbase_mmu_insert_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    const u64 start_vpfn, struct tagged_addr *phys, size_t nr,
				    unsigned long flags, int const group_id, u64 *dirty_pgds,
				    struct kbase_va_region *reg)
{
	int err;

	if (nr == 0)
		return 0;

	err = mmu_insert_pages_no_flush(kbdev, mmut, start_vpfn, phys, nr, flags, group_id,
					dirty_pgds, reg, false);

	return err;
}

int kbase_mmu_insert_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			   struct tagged_addr *phys, size_t nr, unsigned long flags, int as_nr,
			   int const group_id, enum kbase_caller_mmu_sync_info mmu_sync_info,
			   struct kbase_va_region *reg)
{
	int err;
	u64 dirty_pgds = 0;

	CSTD_UNUSED(as_nr);

	if (nr == 0)
		return 0;

	err = mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id, &dirty_pgds,
					reg, false);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_mmu_insert_pages);
KBASE_ALLOW_ERROR_INJECTION_TEST_API(kbase_mmu_insert_pages, ERRNO);

int kbase_mmu_insert_pages_skip_status_update(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, u64 vpfn,
					      struct tagged_addr *phys, size_t nr,
					      unsigned long flags, int as_nr, int const group_id,
					      enum kbase_caller_mmu_sync_info mmu_sync_info,
					      struct kbase_va_region *reg)
{
	int err;
	u64 dirty_pgds = 0;

	CSTD_UNUSED(as_nr);

	if (nr == 0)
		return 0;

	err = mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id, &dirty_pgds,
					reg, true);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}

int kbase_mmu_insert_aliased_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				   u64 vpfn, struct tagged_addr *phys, size_t nr,
				   unsigned long flags, int as_nr, int const group_id,
				   enum kbase_caller_mmu_sync_info mmu_sync_info,
				   struct kbase_va_region *reg)
{
	int err;
	u64 dirty_pgds = 0;

	CSTD_UNUSED(as_nr);

	if (nr == 0)
		return 0;

	err = mmu_insert_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id, &dirty_pgds,
					reg, false);
	if (err)
		return err;

	mmu_flush_invalidate_insert_pages(kbdev, mmut, vpfn, nr, dirty_pgds, mmu_sync_info, false);

	return 0;
}
KBASE_ALLOW_ERROR_INJECTION_TEST_API(kbase_mmu_insert_aliased_pages, ERRNO);

void kbase_mmu_update(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, int as_nr)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	KBASE_DEBUG_ASSERT(as_nr != KBASEP_AS_NR_INVALID);

	kbdev->mmu_mode->update(kbdev, mmut, as_nr);
}
KBASE_EXPORT_TEST_API(kbase_mmu_update);

void kbase_mmu_disable_as(struct kbase_device *kbdev, int as_nr)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
#if !MALI_USE_CSF
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
#endif

	kbdev->mmu_mode->disable_as(kbdev, as_nr);
}

void kbase_mmu_disable(struct kbase_context *kctx)
{

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_mmu_hw_op_param op_param = { 0 };
	int lock_err, flush_err;

	if (kctx->as_nr == KBASEP_AS_NR_INVALID) {
		dev_dbg(kbdev->dev, "Invalid as_nr for ctx %d_%d", kctx->tgid, kctx->id);
		return;
	}

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	op_param.vpfn = 0;
	op_param.nr = ~0U;
	op_param.op = KBASE_MMU_OP_FLUSH_MEM;
	op_param.kctx_id = kctx->id;
	op_param.mmu_sync_info = mmu_sync_info;

#if MALI_USE_CSF

	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		op_param.flush_skip_levels = pgd_level_to_skip_flush(0xF);

	lock_err = kbase_mmu_hw_do_lock(kbdev, &kbdev->as[kctx->as_nr], &op_param);
	if (lock_err)
		dev_err(kbdev->dev, "Failed to lock AS %d for ctx %d_%d", kctx->as_nr, kctx->tgid,
			kctx->id);

	if (kbdev->pm.backend.l2_state == KBASE_L2_ON) {
		flush_err = kbase_gpu_cache_flush_and_busy_wait(kbdev,
								GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
		if (flush_err)
			dev_err(kbdev->dev,
				"Failed to flush GPU cache when disabling AS %d for ctx %d_%d",
				kctx->as_nr, kctx->tgid, kctx->id);
	}
	kbdev->mmu_mode->disable_as(kbdev, kctx->as_nr);

	if (!lock_err) {

		lock_err =
			kbase_mmu_hw_do_unlock_no_addr(kbdev, &kbdev->as[kctx->as_nr], &op_param);
		if (lock_err)
			dev_err(kbdev->dev, "Failed to unlock AS %d for ctx %d_%d", kctx->as_nr,
				kctx->tgid, kctx->id);
	}
#else
	lockdep_assert_held(&kctx->kbdev->mmu_hw_mutex);

	CSTD_UNUSED(lock_err);

	flush_err = kbase_mmu_hw_do_flush(kbdev, &kbdev->as[kctx->as_nr], &op_param);
	if (flush_err) {
		dev_err(kbdev->dev,
			"Flush for GPU page table update did not complete to disable AS %d for ctx %d_%d",
			kctx->as_nr, kctx->tgid, kctx->id);

	}

	kbdev->mmu_mode->disable_as(kbdev, kctx->as_nr);

	kbase_backend_slot_kctx_purge_locked(kbdev, kctx);
#endif
}
KBASE_EXPORT_TEST_API(kbase_mmu_disable);

static void kbase_mmu_update_and_free_parent_pgds(struct kbase_device *kbdev,
						  struct kbase_mmu_table *mmut, phys_addr_t *pgds,
						  u64 vpfn, int level,
						  enum kbase_mmu_op_type flush_op, u64 *dirty_pgds,
						  int as_nr)
{
	phys_addr_t current_pgd = pgds[level];
	struct page *p = phys_to_page(current_pgd);
	u64 *current_page = kmap_pgd(p, current_pgd);
	unsigned int current_valid_entries = kbdev->mmu_mode->get_num_valid_entries(current_page);
	unsigned int index = (vpfn >> ((3 - level) * 9)) & 0x1FFU;

	lockdep_assert_held(&mmut->mmu_lock);

	if (dirty_pgds)
		*dirty_pgds |= 1ULL << level;

	kbdev->mmu_mode->entries_invalidate(&current_page[index], 1);
	if (current_valid_entries == 1 && level != MIDGARD_MMU_LEVEL(0)) {
		kbdev->mmu_mode->set_num_valid_entries(current_page, 0);

		kunmap_pgd(p, current_page);

		kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level - 1, flush_op,
						      dirty_pgds, as_nr);

		if (flush_op == KBASE_MMU_OP_FLUSH_PT) {

			kbase_mmu_sync_pgd_cpu(kbdev,
					       pgd_dma_addr(p, current_pgd) + (index * sizeof(u64)),
					       sizeof(u64));

			kbase_mmu_sync_pgd_gpu(kbdev, mmut->kctx, current_pgd, 512 * sizeof(u64),
					       KBASE_MMU_OP_FLUSH_PT);
		}

		kbase_mmu_add_to_free_pgds_list(mmut, current_pgd);
	} else {
		current_valid_entries--;

		kbdev->mmu_mode->set_num_valid_entries(current_page, current_valid_entries);

		kunmap_pgd(p, current_page);

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, current_pgd + (index * sizeof(u64)),
				   pgd_dma_addr(p, current_pgd) + (index * sizeof(u64)),
				   sizeof(u64), flush_op);

		if (flush_op == KBASE_MMU_OP_FLUSH_PT)
			mmu_invalidate_on_teardown(kbdev, mmut->kctx, vpfn, 1, level, as_nr);
	}
}

static void mmu_flush_invalidate_teardown_pages(struct kbase_device *kbdev,
						struct kbase_context *kctx, int as_nr,
						struct tagged_addr *phys, size_t phys_page_nr,
						struct kbase_mmu_hw_op_param *op_param)
{
	if (!mmu_flush_cache_on_gpu_ctrl(kbdev)) {

		mmu_flush_invalidate(kbdev, kctx, as_nr, op_param);
	} else if (op_param->op == KBASE_MMU_OP_FLUSH_MEM) {

		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, as_nr, op_param);
	}
#if MALI_USE_CSF
	else {

		unsigned long irq_flags;
		unsigned int i;
		bool flush_done = false;

		for (i = 0; !flush_done && i < phys_page_nr; i++) {
			spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
			if (kbdev->pm.backend.gpu_ready && (!kctx || kctx->as_nr >= 0))
				mmu_flush_pa_range(kbdev, as_phys_addr_t(phys[i]), PAGE_SIZE,
						   KBASE_MMU_OP_FLUSH_MEM);
			else
				flush_done = true;
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
		}
	}
#else
	CSTD_UNUSED(phys);
	CSTD_UNUSED(phys_page_nr);
#endif
}

static int kbase_mmu_teardown_pgd_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					u64 vpfn, size_t nr, u64 *dirty_pgds,
					struct list_head *free_pgds_list,
					enum kbase_mmu_op_type flush_op, int as_nr)
{
	struct kbase_mmu_mode const *mmu_mode = kbdev->mmu_mode;

	CSTD_UNUSED(free_pgds_list);

	lockdep_assert_held(&mmut->mmu_lock);
	kbase_mmu_reset_free_pgds_list(mmut);

	vpfn *= GPU_PAGES_PER_CPU_PAGE;
	nr *= GPU_PAGES_PER_CPU_PAGE;

	while (nr) {
		unsigned int index = vpfn & 0x1FF;
		unsigned int count = KBASE_MMU_PAGE_ENTRIES - index;
		unsigned int pcount;
		int level;
		u64 *page;
		phys_addr_t pgds[MIDGARD_MMU_BOTTOMLEVEL + 1];
		register unsigned int num_of_valid_entries;
		phys_addr_t pgd = mmut->pgd;
		struct page *p = phys_to_page(pgd);

		count = MIN(nr, count);

		for (level = MIDGARD_MMU_TOPLEVEL; level <= MIDGARD_MMU_BOTTOMLEVEL; level++) {
			phys_addr_t next_pgd;

			index = (vpfn >> ((3 - level) * 9)) & 0x1FF;
			page = kmap_pgd(p, pgd);
			if (mmu_mode->ate_is_valid(page[index], level))
				break;
			else if (!mmu_mode->pte_is_valid(page[index], level)) {
				dev_warn(kbdev->dev, "Invalid PTE found @ level %d for VA %llx",
					 level, vpfn << PAGE_SHIFT);

				count = (1 << ((3 - level) * 9));
				count -= (vpfn & (count - 1));
				count = MIN(nr, count);
				goto next;
			}
			next_pgd = mmu_mode->pte_to_phy_addr(
				kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
					kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, page[index]));
			kunmap_pgd(p, page);
			pgds[level] = pgd;
			pgd = next_pgd;
			p = phys_to_page(pgd);
		}

		switch (level) {
		case MIDGARD_MMU_LEVEL(0):
		case MIDGARD_MMU_LEVEL(1):
			dev_warn(kbdev->dev, "%s: No support for ATEs at level %d", __func__,
				 level);
			kunmap_pgd(p, page);
			goto out;
		case MIDGARD_MMU_LEVEL(2):

			if (count >= 512) {
				pcount = 1;
			} else {
				dev_warn(
					kbdev->dev,
					"%s: limiting teardown as it tries to do a partial 2MB teardown, need 512, but have %d to tear down",
					__func__, count);
				pcount = 0;
			}
			break;
		case MIDGARD_MMU_BOTTOMLEVEL:

			pcount = count;
			break;
		default:
			dev_err(kbdev->dev, "%s: found non-mapped memory, early out", __func__);
			vpfn += count;
			nr -= count;
			continue;
		}

		if (pcount > 0)
			*dirty_pgds |= 1ULL << level;

		num_of_valid_entries = mmu_mode->get_num_valid_entries(page);
		if (WARN_ON_ONCE(num_of_valid_entries < pcount))
			num_of_valid_entries = 0;
		else
			num_of_valid_entries -= pcount;

		mmu_mode->entries_invalidate(&page[index], pcount);

		if (!num_of_valid_entries) {
			mmu_mode->set_num_valid_entries(page, 0);

			kunmap_pgd(p, page);

			kbase_mmu_update_and_free_parent_pgds(kbdev, mmut, pgds, vpfn, level - 1,
							      flush_op, dirty_pgds, as_nr);

			if (flush_op == KBASE_MMU_OP_FLUSH_PT) {

				kbase_mmu_sync_pgd_cpu(kbdev,
						       pgd_dma_addr(p, pgd) + (index * sizeof(u64)),
						       pcount * sizeof(u64));

				kbase_mmu_sync_pgd_gpu(kbdev, mmut->kctx, pgd, 512 * sizeof(u64),
						       KBASE_MMU_OP_FLUSH_PT);
			}

			kbase_mmu_add_to_free_pgds_list(mmut, pgd);

			vpfn += count;
			nr -= count;
			continue;
		}

		mmu_mode->set_num_valid_entries(page, num_of_valid_entries);

		kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
				   pgd_dma_addr(p, pgd) + (index * sizeof(u64)),
				   pcount * sizeof(u64), flush_op);

		if (flush_op == KBASE_MMU_OP_FLUSH_PT)
			mmu_invalidate_on_teardown(kbdev, mmut->kctx, vpfn, pcount, level, as_nr);
next:
		kunmap_pgd(p, page);
		vpfn += count;
		nr -= count;
	}
out:
	return 0;
}

static int mmu_teardown_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			      struct tagged_addr *phys, size_t nr_phys_pages, size_t nr_virt_pages,
			      int as_nr, bool ignore_page_migration)
{
	u64 start_vpfn = vpfn;
	enum kbase_mmu_op_type flush_op = KBASE_MMU_OP_NONE;
	struct kbase_mmu_hw_op_param op_param;
	int err = -EFAULT;
	u64 dirty_pgds = 0;
	LIST_HEAD(free_pgds_list);

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	if (unlikely(nr_virt_pages == 0 || nr_phys_pages == 0))
		return 0;

	if (unlikely(nr_virt_pages < nr_phys_pages))
		return -EINVAL;

	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && phys &&
	    nr_phys_pages <= KBASE_PA_RANGE_THRESHOLD_NR_PAGES)
		flush_op = KBASE_MMU_OP_FLUSH_PT;

	mutex_lock(&mmut->mmu_lock);

	err = kbase_mmu_teardown_pgd_pages(kbdev, mmut, vpfn, nr_virt_pages, &dirty_pgds,
					   &free_pgds_list, flush_op, as_nr);

	op_param = (struct kbase_mmu_hw_op_param){
		.vpfn = start_vpfn,
		.nr = nr_virt_pages,
		.mmu_sync_info = mmu_sync_info,
		.kctx_id = mmut->kctx ? mmut->kctx->id : 0xFFFFFFFF,
		.op = (flush_op == KBASE_MMU_OP_FLUSH_PT) ? KBASE_MMU_OP_FLUSH_PT :
								  KBASE_MMU_OP_FLUSH_MEM,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};
	mmu_flush_invalidate_teardown_pages(kbdev, mmut->kctx, as_nr, phys, nr_phys_pages,
					    &op_param);

	if (kbase_is_page_migration_enabled() && phys && !ignore_page_migration)
		kbase_mmu_progress_migration_on_teardown(kbdev, phys, nr_phys_pages);

	kbase_mmu_free_pgds_list(kbdev, mmut);

	mutex_unlock(&mmut->mmu_lock);

	return err;
}

int kbase_mmu_teardown_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			     struct tagged_addr *phys, size_t nr_phys_pages, size_t nr_virt_pages,
			     int as_nr)
{
	return mmu_teardown_pages(kbdev, mmut, vpfn, phys, nr_phys_pages, nr_virt_pages, as_nr,
				  false);
}
KBASE_EXPORT_TEST_API(kbase_mmu_teardown_pages);

int kbase_mmu_teardown_imported_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				      u64 vpfn, struct tagged_addr *phys, size_t nr_phys_pages,
				      size_t nr_virt_pages, int as_nr)
{
	return mmu_teardown_pages(kbdev, mmut, vpfn, phys, nr_phys_pages, nr_virt_pages, as_nr,
				  true);
}

static int kbase_mmu_update_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
					   u64 vpfn, struct tagged_addr *phys, size_t nr,
					   unsigned long flags, int const group_id, u64 *dirty_pgds)
{
	phys_addr_t pgd;
	u64 *pgd_page;
	int err;

	KBASE_DEBUG_ASSERT(vpfn <= (U64_MAX / PAGE_SIZE));

	if (nr == 0)
		return 0;

	vpfn *= GPU_PAGES_PER_CPU_PAGE;
	nr *= GPU_PAGES_PER_CPU_PAGE;
	mutex_lock(&mmut->mmu_lock);

	while (nr) {
		unsigned int i;
		unsigned int index = vpfn & 0x1FF;
		size_t count = KBASE_MMU_PAGE_ENTRIES - index;
		struct page *p;
		register unsigned int num_of_valid_entries;
		int cur_level = MIDGARD_MMU_BOTTOMLEVEL;

		if (count > nr)
			count = nr;

		if (is_huge(*phys) &&
		    (index == (index_in_large_page(*phys) * GPU_PAGES_PER_CPU_PAGE)))
			cur_level = MIDGARD_MMU_LEVEL(2);

		err = mmu_get_pgd_at_level(kbdev, mmut, vpfn, cur_level, &pgd);
		if (WARN_ON(err))
			goto fail_unlock;

		p = pfn_to_page(PFN_DOWN(pgd));
		pgd_page = kmap_pgd(p, pgd);
		if (!pgd_page) {
			dev_warn(kbdev->dev, "kmap failure on update_pages");
			err = -ENOMEM;
			goto fail_unlock;
		}

		num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(pgd_page);

		if (cur_level == MIDGARD_MMU_LEVEL(2)) {
			unsigned int level_index = (vpfn >> 9) & 0x1FFU;
			struct tagged_addr *target_phys = phys - index_in_large_page(*phys);

#ifdef CONFIG_MALI_DEBUG
			WARN_ON_ONCE(!kbdev->mmu_mode->ate_is_valid(pgd_page[level_index],
								    MIDGARD_MMU_LEVEL(2)));
#endif
			pgd_page[level_index] = kbase_mmu_create_ate(
				kbdev, *target_phys, flags, MIDGARD_MMU_LEVEL(2), group_id);
			kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (level_index * sizeof(u64)),
					   pgd_dma_addr(p, pgd) + (level_index * sizeof(u64)),
					   sizeof(u64), KBASE_MMU_OP_NONE);
		} else {
			for (i = 0; i < count; i += GPU_PAGES_PER_CPU_PAGE) {
				phys_addr_t base_phys_address =
					as_phys_addr_t(phys[i / GPU_PAGES_PER_CPU_PAGE]);
				unsigned int j;

				for (j = 0; j < GPU_PAGES_PER_CPU_PAGE; j++) {
					phys_addr_t page_address =
						base_phys_address + (j * GPU_PAGE_SIZE);
#ifdef CONFIG_MALI_DEBUG
					WARN_ON_ONCE(!kbdev->mmu_mode->ate_is_valid(
						pgd_page[index + i + j], MIDGARD_MMU_BOTTOMLEVEL));
#endif
					pgd_page[index + i + j] = kbase_mmu_create_ate(
						kbdev, as_tagged(page_address), flags,
						MIDGARD_MMU_BOTTOMLEVEL, group_id);
				}
			}

			kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
					   pgd_dma_addr(p, pgd) + (index * sizeof(u64)),
					   count * sizeof(u64), KBASE_MMU_OP_NONE);
		}

		kbdev->mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

		if (dirty_pgds && count > 0)
			*dirty_pgds |= 1ULL << cur_level;

		phys += (count / GPU_PAGES_PER_CPU_PAGE);
		vpfn += count;
		nr -= count;

		kunmap_pgd(p, pgd_page);
	}

	mutex_unlock(&mmut->mmu_lock);
	return 0;

fail_unlock:
	mutex_unlock(&mmut->mmu_lock);
	return err;
}

static int kbase_mmu_update_pages_common(struct kbase_device *kbdev, struct kbase_context *kctx,
					 u64 vpfn, struct tagged_addr *phys, size_t nr,
					 unsigned long flags, int const group_id)
{
	int err;
	struct kbase_mmu_hw_op_param op_param;
	u64 dirty_pgds = 0;
	struct kbase_mmu_table *mmut;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;
	int as_nr;

#if !MALI_USE_CSF
	if (unlikely(kctx == NULL))
		return -EINVAL;

	as_nr = kctx->as_nr;
	mmut = &kctx->mmu;
#else
	if (kctx) {
		mmut = &kctx->mmu;
		as_nr = kctx->as_nr;
	} else {
		mmut = &kbdev->csf.mcu_mmu;
		as_nr = MCU_AS_NR;
	}
#endif

	err = kbase_mmu_update_pages_no_flush(kbdev, mmut, vpfn, phys, nr, flags, group_id,
					      &dirty_pgds);

	op_param = (const struct kbase_mmu_hw_op_param){
		.vpfn = vpfn,
		.nr = nr,
		.op = KBASE_MMU_OP_FLUSH_MEM,
		.kctx_id = kctx ? kctx->id : 0xFFFFFFFF,
		.mmu_sync_info = mmu_sync_info,
		.flush_skip_levels = pgd_level_to_skip_flush(dirty_pgds),
	};

	if (mmu_flush_cache_on_gpu_ctrl(kbdev))
		mmu_flush_invalidate_on_gpu_ctrl(kbdev, kctx, as_nr, &op_param);
	else
		mmu_flush_invalidate(kbdev, kctx, as_nr, &op_param);

	return err;
}

int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, struct tagged_addr *phys,
			   size_t nr, unsigned long flags, int const group_id)
{
	if (unlikely(kctx == NULL))
		return -EINVAL;

	return kbase_mmu_update_pages_common(kctx->kbdev, kctx, vpfn, phys, nr, flags, group_id);
}

#if MALI_USE_CSF
int kbase_mmu_update_csf_mcu_pages(struct kbase_device *kbdev, u64 vpfn, struct tagged_addr *phys,
				   size_t nr, unsigned long flags, int const group_id)
{
	return kbase_mmu_update_pages_common(kbdev, NULL, vpfn, phys, nr, flags, group_id);
}
#endif

static void mmu_page_migration_transaction_begin(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON_ONCE(kbdev->mmu_page_migrate_in_progress);
	kbdev->mmu_page_migrate_in_progress = true;
}

static void mmu_page_migration_transaction_end(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	WARN_ON_ONCE(!kbdev->mmu_page_migrate_in_progress);
	kbdev->mmu_page_migrate_in_progress = false;

	kbase_pm_update_state(kbdev);
}

static void mmu_undo_migrate_pgd_sub_page(struct kbase_mmu_table *mmut, phys_addr_t old_pgd_phys,
					  phys_addr_t new_pgd_phys, dma_addr_t new_pgd_dma_addr,
					  u64 pgd_vpfn_level)
{
	struct kbase_device *kbdev;
	u64 vpfn = PGD_VPFN_LEVEL_GET_VPFN(pgd_vpfn_level);
	int level = PGD_VPFN_LEVEL_GET_LEVEL(pgd_vpfn_level);
	unsigned int index = (vpfn >> ((3 - level) * 9)) & 0x1FFU;
	unsigned int num_of_valid_entries;
	u64 *parent_pgd_page, *new_pgd_page, *target;
	phys_addr_t parent_pgd;
	u64 managed_pte;

	kbdev = mmut->kctx->kbdev;

	lockdep_assert_held(&mmut->mmu_lock);

	if (mmu_get_pgd_at_level(kbdev, mmut, vpfn, level, &parent_pgd)) {
		dev_WARN(kbdev->dev, "Failed to get the PGD at level %u for VA %llx", level, vpfn);
		return;
	}

	parent_pgd_page = kmap_atomic_pgd(phys_to_page(parent_pgd), parent_pgd);
	num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(parent_pgd_page);

#ifdef CONFIG_MALI_DEBUG

	if (new_pgd_phys !=
	    kbdev->mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
		    kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, parent_pgd_page[index]))) {
		dev_WARN(kbdev->dev, "Unexpected PTE value for PGD at level %u for VA %llx", level,
			 vpfn);
	}
#endif

	kbdev->mmu_mode->entry_set_pte(&managed_pte, old_pgd_phys);
	target = &parent_pgd_page[index];
	*target = kbdev->mgm_dev->ops.mgm_update_gpu_pte(kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP,
							 level, managed_pte);

	kbdev->mmu_mode->set_num_valid_entries(parent_pgd_page, num_of_valid_entries);
	kunmap_atomic_pgd(parent_pgd_page);

	kbase_mmu_sync_pgd_cpu(
		kbdev, pgd_dma_addr(phys_to_page(parent_pgd), parent_pgd) + (index * sizeof(u64)),
		sizeof(u64));

	new_pgd_page = kmap_atomic_pgd(phys_to_page(new_pgd_phys), new_pgd_phys);
	kbdev->mmu_mode->entries_invalidate(new_pgd_page, KBASE_MMU_PAGE_ENTRIES);
	kunmap_atomic_pgd(new_pgd_page);
	dma_sync_single_for_device(kbdev->dev, new_pgd_dma_addr, GPU_PAGE_SIZE, DMA_BIDIRECTIONAL);
}

static int mmu_migrate_pgd_sub_page(struct kbase_mmu_table *mmut, phys_addr_t old_pgd_phys,
				    phys_addr_t new_pgd_phys, dma_addr_t old_pgd_dma_addr,
				    dma_addr_t new_pgd_dma_addr, u64 pgd_vpfn_level)
{
	struct kbase_mmu_hw_op_param op_param;
	struct kbase_device *kbdev;
	u64 *old_pgd_page, *new_pgd_page, *parent_pgd_page, *target;
	u64 vpfn = PGD_VPFN_LEVEL_GET_VPFN(pgd_vpfn_level);
	int level = PGD_VPFN_LEVEL_GET_LEVEL(pgd_vpfn_level);
	unsigned int index = (vpfn >> ((3 - level) * 9)) & 0x1FFU;
	unsigned long hwaccess_flags = 0;
	unsigned int num_of_valid_entries;
	phys_addr_t parent_pgd;
	u64 managed_pte;
	int ret = 0;

	kbdev = mmut->kctx->kbdev;

	lockdep_assert_held(&mmut->mmu_lock);

	old_pgd_page = kmap_pgd(phys_to_page(old_pgd_phys), old_pgd_phys);
	if (!old_pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for old pgd page.", __func__);
		ret = -EINVAL;
		goto old_page_map_error;
	}

	new_pgd_page = kmap_pgd(phys_to_page(new_pgd_phys), new_pgd_phys);
	if (!new_pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for new pgd page.", __func__);
		ret = -EINVAL;
		goto new_page_map_error;
	}

#define PGD_VPFN_MASK(level) (~((((u64)1) << ((3 - level) * 9)) - 1))
	op_param.mmu_sync_info = CALLER_MMU_ASYNC;
	op_param.kctx_id = mmut->kctx->id;
	op_param.vpfn = (vpfn / GPU_PAGES_PER_CPU_PAGE) & PGD_VPFN_MASK(level);
	op_param.nr = 1U << ((3 - level) * 9);
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.flush_skip_levels = pgd_level_to_skip_flush(3ULL << level);

	ret = mmu_get_pgd_at_level(kbdev, mmut, vpfn, level, &parent_pgd);
	if (ret) {
		dev_err(kbdev->dev, "%s: failed to find parent PGD for old PGD page.", __func__);
		goto get_pgd_at_level_error;
	}

	parent_pgd_page = kmap_pgd(phys_to_page(parent_pgd), parent_pgd);
	if (!parent_pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for parent PGD page.", __func__);
		ret = -EINVAL;
		goto pgd_page_map_error;
	}

	mutex_lock(&kbdev->mmu_hw_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (unlikely(!kbase_pm_l2_allow_mmu_page_migration(kbdev))) {

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		dev_dbg(kbdev->dev, "%s: L2 in transtion, abort PGD page migration", __func__);
		ret = -EAGAIN;
		goto l2_state_defer_out;
	}

	mmu_page_migration_transaction_begin(kbdev);
	if (kbdev->pm.backend.gpu_ready && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];

		ret = kbase_mmu_hw_do_lock(kbdev, as, &op_param);
		if (!ret) {
#if MALI_USE_CSF
			if (mmu_flush_cache_on_gpu_ctrl(kbdev))
				ret = kbase_gpu_cache_flush_pa_range_and_busy_wait(
					kbdev, old_pgd_phys, GPU_PAGE_SIZE,
					GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2_LSC);
			else
#endif
				ret = kbase_gpu_cache_flush_and_busy_wait(
					kbdev, GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
		}
		if (ret)
			mmu_page_migration_transaction_end(kbdev);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	if (ret < 0) {
		mutex_unlock(&kbdev->mmu_hw_mutex);
		dev_err(kbdev->dev, "%s: failed to lock MMU region or flush GPU cache", __func__);
		goto l2_state_defer_out;
	}

	dma_sync_single_for_cpu(kbdev->dev, old_pgd_dma_addr, GPU_PAGE_SIZE, DMA_BIDIRECTIONAL);
	memcpy(new_pgd_page, old_pgd_page, GPU_PAGE_SIZE);
	dma_sync_single_for_device(kbdev->dev, new_pgd_dma_addr, GPU_PAGE_SIZE, DMA_BIDIRECTIONAL);

	target = &parent_pgd_page[index];

	num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(parent_pgd_page);

#ifdef CONFIG_MALI_DEBUG

	WARN_ON_ONCE(
		old_pgd_phys !=
		kbdev->mmu_mode->pte_to_phy_addr(kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
			kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level, parent_pgd_page[index])));
#endif
	kbdev->mmu_mode->entry_set_pte(&managed_pte, new_pgd_phys);
	*target = kbdev->mgm_dev->ops.mgm_update_gpu_pte(kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP,
							 level, managed_pte);

	kbdev->mmu_mode->set_num_valid_entries(parent_pgd_page, num_of_valid_entries);

	kbase_mmu_sync_pgd(kbdev, mmut->kctx, parent_pgd + (index * sizeof(u64)),
			   pgd_dma_addr(phys_to_page(parent_pgd), parent_pgd) +
				   (index * sizeof(u64)),
			   sizeof(u64), KBASE_MMU_OP_FLUSH_PT);

	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (kbdev->pm.backend.gpu_ready && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];
		int local_ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, &op_param);

		CSTD_UNUSED(local_ret);
	}

	mmu_page_migration_transaction_end(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);

l2_state_defer_out:
	kunmap_pgd(phys_to_page(parent_pgd), parent_pgd_page);
pgd_page_map_error:
get_pgd_at_level_error:
	kunmap_pgd(phys_to_page(new_pgd_phys), new_pgd_page);
new_page_map_error:
	kunmap_pgd(phys_to_page(old_pgd_phys), old_pgd_page);
old_page_map_error:
	return ret;
}

int kbase_mmu_migrate_pgd_page(struct tagged_addr old_pgd_phys, struct tagged_addr new_pgd_phys,
			       dma_addr_t old_pgd_dma_addr, dma_addr_t new_pgd_dma_addr)
{
	struct kbase_page_metadata *page_md = kbase_page_private(as_page(old_pgd_phys));
	struct kbase_mmu_table *mmut;
	struct kbase_device *kbdev;
	u32 sub_page_index;
	u64 old_pgd_phys_addr = as_phys_addr_t(old_pgd_phys);
	u64 new_pgd_phys_addr = as_phys_addr_t(new_pgd_phys);
	int check_state, ret = 0;

	if (!kbase_is_page_migration_enabled())
		return -EINVAL;

	spin_lock(&page_md->migrate_lock);

	check_state = PAGE_STATUS_GET(page_md->status);

	if (WARN_ONCE(check_state != PT_MAPPED,
		      "Page metadata status %d doesn't match expected value %d", check_state,
		      PT_MAPPED)) {
		ret = -EINVAL;
		goto early_exit;
	}

	mmut = page_md->data.pt_mapped.mmut;

	spin_unlock(&page_md->migrate_lock);

	if (WARN_ONCE(!mmut->kctx, "Migration failed as kctx is null"))
		return -EINVAL;

	kbdev = mmut->kctx->kbdev;

	if (WARN_ON_ONCE(old_pgd_phys_addr & ~PAGE_MASK))
		return -EINVAL;

	if (WARN_ON_ONCE(new_pgd_phys_addr & ~PAGE_MASK))
		return -EINVAL;

	mutex_lock(&mmut->mmu_lock);
	spin_lock(&page_md->migrate_lock);

	check_state = PAGE_STATUS_GET(page_md->status);

	if (check_state != PT_MAPPED) {
		dev_dbg(kbdev->dev, "%s: state changed to %d (was %d), abort PGD page migration",
			__func__, check_state, PT_MAPPED);
		WARN_ON_ONCE(check_state != FREE_PT_ISOLATED_IN_PROGRESS);
		ret = -EAGAIN;
		goto metadata_unlock;
	}

	spin_unlock(&page_md->migrate_lock);

	for (sub_page_index = 0; sub_page_index < GPU_PAGES_PER_CPU_PAGE; sub_page_index++) {
		if (!page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index])
			continue;

		ret = mmu_migrate_pgd_sub_page(
			mmut, old_pgd_phys_addr + (sub_page_index * GPU_PAGE_SIZE),
			new_pgd_phys_addr + (sub_page_index * GPU_PAGE_SIZE),
			old_pgd_dma_addr + (sub_page_index * GPU_PAGE_SIZE),
			new_pgd_dma_addr + (sub_page_index * GPU_PAGE_SIZE),
			page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index]);
		if (ret)
			break;
	}

	if (ret == 0) {

		page_md->dma_addr = new_pgd_dma_addr;
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);

#if GPU_PAGES_PER_CPU_PAGE > 1
		page_md->data.pt_mapped.pgd_page = as_page(new_pgd_phys);
		if (mmut->last_allocated_pgd_page == as_page(old_pgd_phys))
			mmut->last_allocated_pgd_page = as_page(new_pgd_phys);
		if (mmut->last_freed_pgd_page == as_page(old_pgd_phys))
			mmut->last_freed_pgd_page = as_page(new_pgd_phys);
#endif
		set_page_private(as_page(new_pgd_phys), (unsigned long)page_md);

		set_page_private(as_page(old_pgd_phys), 0);
	} else {
		unsigned long hwaccess_flags;

		while (sub_page_index-- > 0) {
			if (!page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index])
				continue;

			mmu_undo_migrate_pgd_sub_page(
				mmut, old_pgd_phys_addr + (sub_page_index * GPU_PAGE_SIZE),
				new_pgd_phys_addr + (sub_page_index * GPU_PAGE_SIZE),
				new_pgd_dma_addr + (sub_page_index * GPU_PAGE_SIZE),
				page_md->data.pt_mapped.pgd_vpfn_level[sub_page_index]);
		}

		spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
		if (kbdev->pm.backend.gpu_ready && mmut->kctx->as_nr >= 0) {
			struct kbase_mmu_hw_op_param op_param = {
				.vpfn = 0,
				.nr = ~0U,
				.flush_skip_levels = 0,
				.op = KBASE_MMU_OP_FLUSH_PT,
				.kctx_id = mmut->kctx->id,
				.mmu_sync_info = CALLER_MMU_ASYNC,
			};
			int as_nr = mmut->kctx->as_nr;
			struct kbase_as *as = &kbdev->as[as_nr];
			int local_ret;

			local_ret = kbase_mmu_hw_do_lock(kbdev, as, &op_param);
			CSTD_UNUSED(local_ret);

			local_ret = kbase_gpu_cache_flush_and_busy_wait(
				kbdev, GPU_COMMAND_CACHE_CLN_INV_L2);
			CSTD_UNUSED(local_ret);

			local_ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, &op_param);
			CSTD_UNUSED(local_ret);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);
	}

	mutex_unlock(&mmut->mmu_lock);
	return ret;

metadata_unlock:
	spin_unlock(&page_md->migrate_lock);
	mutex_unlock(&mmut->mmu_lock);
	return ret;

early_exit:
	spin_unlock(&page_md->migrate_lock);
	return ret;
}

int kbase_mmu_migrate_data_page(struct tagged_addr old_phys, struct tagged_addr new_phys,
				dma_addr_t old_dma_addr, dma_addr_t new_dma_addr)
{
	struct kbase_page_metadata *page_md = kbase_page_private(as_page(old_phys));
	struct kbase_mmu_hw_op_param op_param;
	struct kbase_mmu_table *mmut;
	struct kbase_device *kbdev;
	phys_addr_t pgd;
	u64 *old_page, *new_page, *pgd_page, *target, vpfn;
	unsigned int index;
	int check_state, ret = 0;
	unsigned long hwaccess_flags = 0;
	unsigned int num_of_valid_entries;
	u8 vmap_count = 0;
	phys_addr_t base_phys_address = as_phys_addr_t(new_phys);
	enum kbase_page_status page_status;
	unsigned int i;

	if (!kbase_is_page_migration_enabled())
		return -EINVAL;

	spin_lock(&page_md->migrate_lock);

	check_state = PAGE_STATUS_GET(page_md->status);

	if (WARN_ONCE(check_state != ALLOCATED_MAPPED,
		      "Page metadata status %d doesn't match expected value %d", check_state,
		      ALLOCATED_MAPPED)) {
		ret = -EINVAL;
		goto early_exit;
	}

	mmut = page_md->data.mapped.mmut;
	vpfn = page_md->data.mapped.vpfn;

	spin_unlock(&page_md->migrate_lock);

	if (WARN_ONCE(!mmut->kctx, "Migration failed as kctx is null"))
		return -EINVAL;

	kbdev = mmut->kctx->kbdev;
	index = vpfn & 0x1FFU;

	old_page = kbase_kmap(as_page(old_phys));
	if (!old_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for old page.", __func__);
		ret = -EINVAL;
		goto old_page_map_error;
	}

	new_page = kbase_kmap(as_page(new_phys));
	if (!new_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for new page.", __func__);
		ret = -EINVAL;
		goto new_page_map_error;
	}

	op_param.mmu_sync_info = CALLER_MMU_ASYNC;
	op_param.kctx_id = mmut->kctx->id;
	op_param.vpfn = (vpfn / GPU_PAGES_PER_CPU_PAGE);
	op_param.nr = 1;
	op_param.op = KBASE_MMU_OP_FLUSH_PT;
	op_param.flush_skip_levels = pgd_level_to_skip_flush(1ULL << MIDGARD_MMU_BOTTOMLEVEL);

	mutex_lock(&mmut->mmu_lock);
	spin_lock(&page_md->migrate_lock);
	check_state = PAGE_STATUS_GET(page_md->status);
	vmap_count = page_md->vmap_count;

	if (check_state != ALLOCATED_MAPPED) {
		dev_dbg(kbdev->dev, "%s: state changed to %d (was %d), abort page migration",
			__func__, check_state, ALLOCATED_MAPPED);
		ret = -EAGAIN;
		goto page_state_change_out;
	} else if (vmap_count > 0) {
		dev_dbg(kbdev->dev, "%s: page was multi-mapped, abort page migration", __func__);
		ret = -EAGAIN;
		goto page_state_change_out;
	}

	ret = mmu_get_pgd_at_level(kbdev, mmut, vpfn, MIDGARD_MMU_BOTTOMLEVEL, &pgd);
	if (ret) {
		dev_err(kbdev->dev, "%s: failed to find PGD for old page.", __func__);
		goto get_pgd_at_level_error;
	}

	pgd_page = kmap_pgd(phys_to_page(pgd), pgd);
	if (!pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure for PGD page.", __func__);
		ret = -EINVAL;
		goto pgd_page_map_error;
	}

	spin_unlock(&page_md->migrate_lock);
	mutex_lock(&kbdev->mmu_hw_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (unlikely(!kbase_pm_l2_allow_mmu_page_migration(kbdev))) {

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);
		dev_dbg(kbdev->dev, "%s: L2 in transition, abort PGD page migration", __func__);
		ret = -EAGAIN;
		goto defer_out;
	}

	mmu_page_migration_transaction_begin(kbdev);
	if (kbdev->pm.backend.gpu_ready && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];

		ret = kbase_mmu_hw_do_lock(kbdev, as, &op_param);
		if (!ret) {
#if MALI_USE_CSF
			if (mmu_flush_cache_on_gpu_ctrl(kbdev))
				ret = kbase_gpu_cache_flush_pa_range_and_busy_wait(
					kbdev, as_phys_addr_t(old_phys), PAGE_SIZE,
					GPU_COMMAND_FLUSH_PA_RANGE_CLN_INV_L2_LSC);
			else
#endif
				ret = kbase_gpu_cache_flush_and_busy_wait(
					kbdev, GPU_COMMAND_CACHE_CLN_INV_L2_LSC);
		}
		if (ret)
			mmu_page_migration_transaction_end(kbdev);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	if (ret < 0) {
		dev_err(kbdev->dev, "%s: failed to lock MMU region or flush GPU cache", __func__);
		goto undo_mappings;
	}

	dma_sync_single_for_cpu(kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	memcpy(new_page, old_page, PAGE_SIZE);
	dma_sync_single_for_device(kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	target = &pgd_page[index];

	num_of_valid_entries = kbdev->mmu_mode->get_num_valid_entries(pgd_page);

	for (i = 0; i < GPU_PAGES_PER_CPU_PAGE; i++) {
		phys_addr_t page_address = base_phys_address + (i * GPU_PAGE_SIZE);

		WARN_ON_ONCE((*target & 1UL) == 0);
		*target = kbase_mmu_create_ate(kbdev, as_tagged(page_address),
					       page_md->data.mapped.reg->flags,
					       MIDGARD_MMU_BOTTOMLEVEL,
					       page_md->data.mapped.reg->gpu_alloc->group_id);
		target++;
	}

	kbdev->mmu_mode->set_num_valid_entries(pgd_page, num_of_valid_entries);

	kbase_mmu_sync_pgd(kbdev, mmut->kctx, pgd + (index * sizeof(u64)),
			   pgd_dma_addr(phys_to_page(pgd), pgd) + (index * sizeof(u64)),
			   GPU_PAGES_PER_CPU_PAGE * sizeof(u64), KBASE_MMU_OP_FLUSH_PT);

	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_flags);
	if (kbdev->pm.backend.gpu_ready && mmut->kctx->as_nr >= 0) {
		int as_nr = mmut->kctx->as_nr;
		struct kbase_as *as = &kbdev->as[as_nr];
		int local_ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, &op_param);

		CSTD_UNUSED(local_ret);
	}

	mmu_page_migration_transaction_end(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);
	spin_lock(&page_md->migrate_lock);

	page_status = PAGE_STATUS_GET(page_md->status);
	if (page_status == ALLOCATED_MAPPED) {

		size_t page_array_index = (page_md->data.mapped.vpfn / GPU_PAGES_PER_CPU_PAGE) -
					  page_md->data.mapped.reg->start_pfn;

		page_md->data.mapped.reg->gpu_alloc->pages[page_array_index] = new_phys;
	} else if (page_status == NOT_MOVABLE) {
		dev_dbg(kbdev->dev, "%s: migration completed and page has become NOT_MOVABLE.",
			__func__);
	} else {
		dev_WARN(kbdev->dev, "%s: migration completed but page has moved to status %d.",
			 __func__, page_status);
	}

	page_md->dma_addr = new_dma_addr;
	page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
	set_page_private(as_page(new_phys), (unsigned long)page_md);

	set_page_private(as_page(old_phys), 0);

	kunmap_pgd(phys_to_page(pgd), pgd_page);
pgd_page_map_error:
get_pgd_at_level_error:
page_state_change_out:
	spin_unlock(&page_md->migrate_lock);
	mutex_unlock(&mmut->mmu_lock);
	kbase_kunmap(as_page(new_phys), new_page);
new_page_map_error:
	kbase_kunmap(as_page(old_phys), old_page);
old_page_map_error:
	return ret;

undo_mappings:
defer_out:
	mutex_unlock(&kbdev->mmu_hw_mutex);
	mutex_unlock(&mmut->mmu_lock);
	kunmap_pgd(phys_to_page(pgd), pgd_page);
	kbase_kunmap(as_page(new_phys), new_page);
	kbase_kunmap(as_page(old_phys), old_page);
	return ret;

early_exit:
	spin_unlock(&page_md->migrate_lock);
	return ret;
}

static void mmu_teardown_level(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
			       phys_addr_t pgd, int level)
{
	u64 *pgd_page;
	int i;
	struct memory_group_manager_device *mgm_dev = kbdev->mgm_dev;
	struct kbase_mmu_mode const *mmu_mode = kbdev->mmu_mode;
	u64 *pgd_page_buffer = NULL;
	struct page *p = phys_to_page(pgd);

	lockdep_assert_held(&mmut->mmu_lock);

	pgd_page = kmap_atomic_pgd(p, pgd);

	if (WARN_ON_ONCE(pgd_page == NULL))
		return;
	if (level < MIDGARD_MMU_BOTTOMLEVEL) {

		pgd_page_buffer = mmut->scratch_mem.teardown_pages.levels[level];
		memcpy(pgd_page_buffer, pgd_page, GPU_PAGE_SIZE);
	}

	if (kbase_is_page_migration_enabled() && mmut->kctx)
		WARN_ON_ONCE(kbdev->mmu_mode->get_num_valid_entries(pgd_page));

	mmu_mode->entries_invalidate(pgd_page, KBASE_MMU_PAGE_ENTRIES);
	kunmap_atomic_pgd(pgd_page);
	pgd_page = pgd_page_buffer;

	if (level < MIDGARD_MMU_BOTTOMLEVEL) {
		for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				phys_addr_t target_pgd = mmu_mode->pte_to_phy_addr(
					mgm_dev->ops.mgm_pte_to_original_pte(mgm_dev,
									     MGM_DEFAULT_PTE_GROUP,
									     level, pgd_page[i]));

				mmu_teardown_level(kbdev, mmut, target_pgd, level + 1);
			}
		}
	}

	kbase_mmu_free_pgd(kbdev, mmut, pgd);
}

static void kbase_mmu_mark_non_movable(struct kbase_device *const kbdev, struct page *page)
{
	struct kbase_page_metadata *page_md;

	if (!kbase_is_page_migration_enabled())
		return;

	lock_page(page);

	if (is_huge_head(as_tagged(page_to_phys(page))) ||
	    is_partial(as_tagged(page_to_phys(page))))
		dev_WARN(kbdev->dev, "%s: migration on large-page attempted.", __func__);

	page_md = kbase_page_private(page);

	spin_lock(&page_md->migrate_lock);
	page_md->status = PAGE_STATUS_SET(page_md->status, NOT_MOVABLE);

	if (IS_PAGE_MOVABLE(page_md->status))
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);

	__ClearPageMovable(page);
	spin_unlock(&page_md->migrate_lock);
	unlock_page(page);
}

int kbase_mmu_init(struct kbase_device *const kbdev, struct kbase_mmu_table *const mmut,
		   struct kbase_context *const kctx, int const group_id)
{
	if (WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS) || WARN_ON(group_id < 0))
		return -EINVAL;

	compiletime_assert(KBASE_MEM_ALLOC_MAX_SIZE <= (((8ull << 30) >> PAGE_SHIFT)),
			   "List of free PGDs may not be large enough.");
	compiletime_assert(MAX_PAGES_FOR_FREE_PGDS >= MIDGARD_MMU_BOTTOMLEVEL,
			   "Array of MMU levels is not large enough.");

	mmut->group_id = group_id;
	mutex_init(&mmut->mmu_lock);
	mmut->kctx = kctx;
	mmut->pgd = KBASE_INVALID_PHYSICAL_ADDRESS;

#if GPU_PAGES_PER_CPU_PAGE > 1
	INIT_LIST_HEAD(&mmut->pgd_pages_list);
#endif

	while (mmut->pgd == KBASE_INVALID_PHYSICAL_ADDRESS) {
		int err;

		err = kbase_mem_pool_grow(&kbdev->mem_pools.small[mmut->group_id],
					  MIDGARD_MMU_BOTTOMLEVEL, kctx ? kctx->task : NULL);
		if (err) {
			kbase_mmu_term(kbdev, mmut);
			return -ENOMEM;
		}

		mutex_lock(&mmut->mmu_lock);
		mmut->pgd = kbase_mmu_alloc_pgd(kbdev, mmut);
		mutex_unlock(&mmut->mmu_lock);
	}

	kbase_mmu_mark_non_movable(kbdev, pfn_to_page(PFN_DOWN(mmut->pgd)));
	return 0;
}

void kbase_mmu_term(struct kbase_device *kbdev, struct kbase_mmu_table *mmut)
{
	WARN((mmut->kctx) && (mmut->kctx->as_nr != KBASEP_AS_NR_INVALID),
	     "kctx-%d_%d must first be scheduled out to flush GPU caches+tlbs before tearing down MMU tables",
	     mmut->kctx->tgid, mmut->kctx->id);

	if (mmut->pgd != KBASE_INVALID_PHYSICAL_ADDRESS) {
		mutex_lock(&mmut->mmu_lock);
		mmu_teardown_level(kbdev, mmut, mmut->pgd, MIDGARD_MMU_TOPLEVEL);
		mutex_unlock(&mmut->mmu_lock);

		if (mmut->kctx)
			KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, mmut->kctx->id, 0);
	}

	mutex_destroy(&mmut->mmu_lock);
}

void kbase_mmu_as_term(struct kbase_device *kbdev, unsigned int i)
{
	destroy_workqueue(kbdev->as[i].pf_wq);
}

void kbase_mmu_flush_pa_range(struct kbase_device *kbdev, struct kbase_context *kctx,
			      phys_addr_t phys, size_t size, enum kbase_mmu_op_type flush_op)
{
#if MALI_USE_CSF
	unsigned long irq_flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (mmu_flush_cache_on_gpu_ctrl(kbdev) && (flush_op != KBASE_MMU_OP_NONE) &&
	    kbdev->pm.backend.gpu_ready && (!kctx || kctx->as_nr >= 0))
		mmu_flush_pa_range(kbdev, phys, size, KBASE_MMU_OP_FLUSH_PT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
#else
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	CSTD_UNUSED(phys);
	CSTD_UNUSED(size);
	CSTD_UNUSED(flush_op);
#endif
}

#ifdef CONFIG_MALI_VECTOR_DUMP
static size_t kbasep_mmu_dump_level(struct kbase_context *kctx, phys_addr_t pgd, int level,
				    char **const buffer, size_t *size_left)
{
	phys_addr_t target_pgd;
	u64 *pgd_page;
	int i;
	size_t size = KBASE_MMU_PAGE_ENTRIES * sizeof(u64) + sizeof(u64);
	size_t dump_size;
	struct kbase_device *kbdev;
	struct kbase_mmu_mode const *mmu_mode;
	struct page *p;

	if (WARN_ON(kctx == NULL))
		return 0;
	lockdep_assert_held(&kctx->mmu.mmu_lock);

	kbdev = kctx->kbdev;
	mmu_mode = kbdev->mmu_mode;

	p = pfn_to_page(PFN_DOWN(pgd));
	pgd_page = kmap_pgd(p, pgd);
	if (!pgd_page) {
		dev_warn(kbdev->dev, "%s: kmap failure", __func__);
		return 0;
	}

	if (*size_left >= size) {

		u64 m_pgd = pgd | (u64)level;

		memcpy(*buffer, &m_pgd, sizeof(m_pgd));
		*buffer += sizeof(m_pgd);

		memcpy(*buffer, pgd_page, sizeof(u64) * KBASE_MMU_PAGE_ENTRIES);
		*buffer += sizeof(u64) * KBASE_MMU_PAGE_ENTRIES;

		*size_left -= size;
	}

	if (level < MIDGARD_MMU_BOTTOMLEVEL) {
		for (i = 0; i < KBASE_MMU_PAGE_ENTRIES; i++) {
			if (mmu_mode->pte_is_valid(pgd_page[i], level)) {
				target_pgd = mmu_mode->pte_to_phy_addr(
					kbdev->mgm_dev->ops.mgm_pte_to_original_pte(
						kbdev->mgm_dev, MGM_DEFAULT_PTE_GROUP, level,
						pgd_page[i]));

				dump_size = kbasep_mmu_dump_level(kctx, target_pgd, level + 1,
								  buffer, size_left);
				if (!dump_size) {
					kunmap_pgd(p, pgd_page);
					return 0;
				}
				size += dump_size;
			}
		}
	}

	kunmap_pgd(p, pgd_page);

	return size;
}

void *kbase_mmu_dump(struct kbase_context *kctx, size_t nr_pages)
{
	void *kaddr;
	size_t size_left;

	KBASE_DEBUG_ASSERT(kctx);

	if (nr_pages == 0) {

		return NULL;
	}

	size_left = nr_pages * PAGE_SIZE;

	if (WARN_ON(size_left == 0))
		return NULL;
	kaddr = vmalloc_user(size_left);

	mutex_lock(&kctx->mmu.mmu_lock);

	if (kaddr) {
		u64 end_marker = 0xFFULL;
		char *buffer;
		char *mmu_dump_buffer;
		u64 config[3];
		size_t dump_size, size = 0;
		struct kbase_mmu_setup as_setup;

		buffer = (char *)kaddr;
		mmu_dump_buffer = buffer;

		kctx->kbdev->mmu_mode->get_as_setup(&kctx->mmu, &as_setup);
		config[0] = as_setup.transtab;
		config[1] = as_setup.memattr;
		config[2] = as_setup.transcfg;
		memcpy(buffer, &config, sizeof(config));
		mmu_dump_buffer += sizeof(config);
		size_left -= sizeof(config);
		size += sizeof(config);

		dump_size = kbasep_mmu_dump_level(kctx, kctx->mmu.pgd, MIDGARD_MMU_TOPLEVEL,
						  &mmu_dump_buffer, &size_left);

		if (!dump_size)
			goto fail_free;

		size += dump_size;

		size += sizeof(u64);

		if (size > (nr_pages * PAGE_SIZE)) {

			goto fail_free;
		}

		memcpy(mmu_dump_buffer, &end_marker, sizeof(u64));
	}

	mutex_unlock(&kctx->mmu.mmu_lock);
	return kaddr;

fail_free:
	vfree(kaddr);
	mutex_unlock(&kctx->mmu.mmu_lock);
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_mmu_dump);
#endif

void kbase_mmu_bus_fault_worker(struct work_struct *data)
{
	struct kbase_as *faulting_as;
	unsigned int as_no;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_fault *fault;

	faulting_as = container_of(data, struct kbase_as, work_busfault);
	fault = &faulting_as->bf_data;

	flush_work(&faulting_as->work_pagefault);

	as_no = faulting_as->number;

	kbdev = container_of(faulting_as, struct kbase_device, as[as_no]);

	kctx = kbase_ctx_sched_as_to_ctx(kbdev, as_no);
	if (!kctx) {
		atomic_dec(&kbdev->faults_pending);
		return;
	}

#ifdef CONFIG_MALI_ARBITER_SUPPORT

	if (unlikely(kbase_is_gpu_removed(kbdev))) {
		dev_dbg(kbdev->dev, "%s: GPU has been removed", __func__);
		release_ctx(kbdev, kctx);
		atomic_dec(&kbdev->faults_pending);
		return;
	}
#endif

	if (unlikely(fault->protected_mode)) {
		kbase_mmu_report_fault_and_kill(kctx, faulting_as, "Permission failure", fault);
		kbase_mmu_hw_clear_fault(kbdev, faulting_as, KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		release_ctx(kbdev, kctx);
		atomic_dec(&kbdev->faults_pending);
		return;
	}

#if MALI_USE_CSF

	kbase_gpu_report_bus_fault_and_kill(kctx, faulting_as, fault);
#else

	if (!kbase_pm_context_active_handle_suspend(kbdev,
						    KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		kbase_gpu_report_bus_fault_and_kill(kctx, faulting_as, fault);
		kbase_pm_context_idle(kbdev);
	}
#endif

	release_ctx(kbdev, kctx);

	atomic_dec(&kbdev->faults_pending);
}

void kbase_flush_mmu_wqs(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbase_as *as = &kbdev->as[i];

		flush_workqueue(as->pf_wq);
	}
}
