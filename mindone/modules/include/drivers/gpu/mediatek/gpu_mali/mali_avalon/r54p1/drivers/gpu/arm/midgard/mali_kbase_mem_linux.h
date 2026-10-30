/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_LINUX_H_
#define _KBASE_MEM_LINUX_H_

struct kbase_hwc_dma_mapping {
	void *cpu_va;
	dma_addr_t dma_pa;
	size_t size;
};

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx, u64 va_pages, u64 commit_pages,
					u64 extension, u64 *flags, u64 *gpu_va,
					enum kbase_caller_mmu_sync_info mmu_sync_info, enum kbase_memory_category category);
#else
struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx, u64 va_pages, u64 commit_pages,
					u64 extension, u64 *flags, u64 *gpu_va,
					enum kbase_caller_mmu_sync_info mmu_sync_info);
#endif

int kbase_mem_query(struct kbase_context *kctx, u64 gpu_addr, u64 query, u64 *const out);

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		     void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages, u64 *flags, u8 PBHA, bool isFirstDmaBuf);
#else
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		     void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages, u64 *flags);
#endif

u64 kbase_mem_alias(struct kbase_context *kctx, u64 *flags, u64 stride, u64 nents,
		    struct base_mem_aliasing_info *ai, u64 *num_pages);

int kbase_mem_flags_change(struct kbase_context *kctx, u64 gpu_addr, unsigned int flags,
			   unsigned int mask);

int kbase_mem_commit(struct kbase_context *kctx, u64 gpu_addr, u64 new_pages);

int kbase_mem_shrink(struct kbase_context *kctx, struct kbase_va_region *reg, u64 new_pages);

int kbase_context_mmap(struct kbase_context *kctx, struct vm_area_struct *vma);

int kbase_mem_evictable_init(struct kbase_context *kctx);

void kbase_mem_evictable_deinit(struct kbase_context *kctx);

int kbase_mem_grow_gpu_mapping(struct kbase_context *kctx, struct kbase_va_region *reg,
			       u64 new_pages, u64 old_pages,
			       enum kbase_caller_mmu_sync_info mmu_sync_info);

void kbase_mem_evictable_make(struct kbase_mem_phy_alloc *gpu_alloc);

bool kbase_mem_evictable_unmake(struct kbase_mem_phy_alloc *alloc);

typedef unsigned int kbase_vmap_flag;

#define KBASE_VMAP_FLAG_SYNC_NEEDED (((kbase_vmap_flag)1) << 0)

#define KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING (((kbase_vmap_flag)1) << 1)

#define KBASE_VMAP_INPUT_FLAGS (KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING)

struct kbase_vmap_struct {
	off_t offset_in_page;
	struct kbase_mem_phy_alloc *cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc;
	struct tagged_addr *cpu_pages;
	struct tagged_addr *gpu_pages;
	void *addr;
	size_t size;
	kbase_vmap_flag flags;
};

int kbase_mem_shrink_gpu_mapping(struct kbase_context *kctx, struct kbase_va_region *reg,
				 u64 new_pages, u64 old_pages);

void *kbase_vmap_reg(struct kbase_context *kctx, struct kbase_va_region *reg, u64 gpu_addr,
		     size_t size, unsigned long prot_request, struct kbase_vmap_struct *map,
		     kbase_vmap_flag vmap_flags);

void *kbase_vmap_prot(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		      unsigned long prot_request, struct kbase_vmap_struct *map);

void *kbase_vmap(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		 struct kbase_vmap_struct *map);

void kbase_vunmap(struct kbase_context *kctx, struct kbase_vmap_struct *map);

extern const struct vm_operations_struct kbase_vm_ops;

void kbase_sync_mem_regions(struct kbase_context *kctx, struct kbase_vmap_struct *map,
			    enum kbase_sync_type dest);

void kbase_mem_shrink_cpu_mapping(struct kbase_context *kctx, struct kbase_va_region *reg,
				  u64 new_pages, u64 old_pages);

void kbase_phy_alloc_mapping_term(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc);

void *kbase_phy_alloc_mapping_get(struct kbase_context *kctx, u64 gpu_addr,
				  struct kbase_vmap_struct **out_kern_mapping);

void kbase_phy_alloc_mapping_put(struct kbase_context *kctx,
				 struct kbase_vmap_struct *kern_mapping);

u32 kbase_get_cache_line_alignment(struct kbase_device *kbdev);

static inline struct rw_semaphore *kbase_mem_get_process_mmap_lock(void)
{
#if KERNEL_VERSION(5, 8, 0) > LINUX_VERSION_CODE
	return &current->mm->mmap_sem;
#else
	return &current->mm->mmap_lock;
#endif
}

#endif
