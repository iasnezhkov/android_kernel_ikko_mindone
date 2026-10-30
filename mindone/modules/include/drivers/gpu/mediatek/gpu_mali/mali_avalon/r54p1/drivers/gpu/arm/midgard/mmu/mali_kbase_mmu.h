/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MMU_H_
#define _KBASE_MMU_H_

#include <uapi/gpu/arm/midgard/mali_base_kernel.h>
#include <mali_kbase_debug.h>

#define KBASE_MMU_PAGE_ENTRIES 512

struct kbase_context;
struct kbase_device;
struct kbase_mmu_table;
struct kbase_va_region;
struct tagged_addr;

enum kbase_caller_mmu_sync_info {
	CALLER_MMU_UNSET_SYNCHRONICITY,
	CALLER_MMU_SYNC = 0x02,
	CALLER_MMU_ASYNC
};

enum kbase_mmu_op_type {
	KBASE_MMU_OP_NONE = 0,
	KBASE_MMU_OP_FIRST,
	KBASE_MMU_OP_LOCK = KBASE_MMU_OP_FIRST,
	KBASE_MMU_OP_UNLOCK,
	KBASE_MMU_OP_FLUSH_PT,
	KBASE_MMU_OP_FLUSH_MEM,
	KBASE_MMU_OP_COUNT
};

int kbase_mmu_as_init(struct kbase_device *kbdev, unsigned int i);

void kbase_mmu_as_term(struct kbase_device *kbdev, unsigned int i);

int kbase_mmu_init(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
		   struct kbase_context *kctx, int group_id);

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat);

void kbase_mmu_term(struct kbase_device *kbdev, struct kbase_mmu_table *mmut);

u64 kbase_mmu_create_ate(struct kbase_device *kbdev, struct tagged_addr phy, unsigned long flags,
			 int level, int group_id);

int kbase_mmu_insert_pages_no_flush(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				    u64 vpfn, struct tagged_addr *phys, size_t nr,
				    unsigned long flags, int group_id, u64 *dirty_pgds,
				    struct kbase_va_region *reg);
int kbase_mmu_insert_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			   struct tagged_addr *phys, size_t nr, unsigned long flags, int as_nr,
			   int group_id, enum kbase_caller_mmu_sync_info mmu_sync_info,
			   struct kbase_va_region *reg);

int kbase_mmu_insert_pages_skip_status_update(struct kbase_device *kbdev,
					      struct kbase_mmu_table *mmut, u64 vpfn,
					      struct tagged_addr *phys, size_t nr,
					      unsigned long flags, int as_nr, int group_id,
					      enum kbase_caller_mmu_sync_info mmu_sync_info,
					      struct kbase_va_region *reg);
int kbase_mmu_insert_aliased_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				   u64 vpfn, struct tagged_addr *phys, size_t nr,
				   unsigned long flags, int as_nr, int group_id,
				   enum kbase_caller_mmu_sync_info mmu_sync_info,
				   struct kbase_va_region *reg);
int kbase_mmu_insert_single_imported_page(struct kbase_context *kctx, u64 vpfn,
					  struct tagged_addr phys, size_t nr, unsigned long flags,
					  int group_id,
					  enum kbase_caller_mmu_sync_info mmu_sync_info);
int kbase_mmu_insert_single_aliased_page(struct kbase_context *kctx, u64 vpfn,
					 struct tagged_addr phys, size_t nr, unsigned long flags,
					 int group_id,
					 enum kbase_caller_mmu_sync_info mmu_sync_info);

int kbase_mmu_teardown_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, u64 vpfn,
			     struct tagged_addr *phys, size_t nr_phys_pages, size_t nr_virt_pages,
			     int as_nr);
int kbase_mmu_teardown_imported_pages(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
				      u64 vpfn, struct tagged_addr *phys, size_t nr_phys_pages,
				      size_t nr_virt_pages, int as_nr);
#define kbase_mmu_teardown_firmware_pages(kbdev, mmut, vpfn, phys, nr_phys_pages, nr_virt_pages, \
					  as_nr)                                                 \
	kbase_mmu_teardown_imported_pages(kbdev, mmut, vpfn, phys, nr_phys_pages, nr_virt_pages, \
					  as_nr)

int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, struct tagged_addr *phys,
			   size_t nr, unsigned long flags, int const group_id);
#if MALI_USE_CSF

int kbase_mmu_update_csf_mcu_pages(struct kbase_device *kbdev, u64 vpfn, struct tagged_addr *phys,
				   size_t nr, unsigned long flags, int const group_id);
#endif

int kbase_mmu_migrate_data_page(struct tagged_addr old_phys, struct tagged_addr new_phys,
				dma_addr_t old_dma_addr, dma_addr_t new_dma_addr);

int kbase_mmu_migrate_pgd_page(struct tagged_addr old_pgd_phys, struct tagged_addr new_pgd_phys,
			       dma_addr_t old_pgd_dma_addr, dma_addr_t new_pgd_dma_addr);

void kbase_mmu_flush_pa_range(struct kbase_device *kbdev, struct kbase_context *kctx,
			      phys_addr_t phys, size_t size, enum kbase_mmu_op_type flush_op);

int kbase_mmu_bus_fault_interrupt(struct kbase_device *kbdev, u32 status, u32 as_nr);

void kbase_mmu_gpu_fault_interrupt(struct kbase_device *kbdev, u32 status, u32 as_nr, u64 address,
				   bool as_valid);

static inline int kbase_context_mmu_group_id_get(base_context_create_flags const flags)
{
	KBASE_DEBUG_ASSERT(flags == (flags & BASEP_CONTEXT_CREATE_ALLOWED_FLAGS));
	return (int)BASE_CONTEXT_MMU_GROUP_ID_GET(flags);
}

#endif
