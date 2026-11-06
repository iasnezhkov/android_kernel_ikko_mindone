/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_H_
#define _KBASE_MEM_H_

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <uapi/gpu/arm/midgard/mali_base_kernel.h>
#include <mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"

#include "mali_kbase_mem_linux.h"
#include "mali_kbase_reg_track.h"
#include "mali_kbase_mem_migrate.h"

#include <linux/version_compat_defs.h>
#include <linux/sched/mm.h>
#include <linux/kref.h>
#include <linux/vmalloc.h>
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
#include <slbc_ops.h>
#include <linux/memory_group_manager.h>
#endif

static inline void kbase_process_page_usage_inc(struct kbase_context *kctx, int pages);

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316 (2)

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630 (3)

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2 (0)

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_8316 \
	(1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_9630 \
	(1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630)

#define KBASE_REG_FREE (1ul << 0)

#define KBASE_REG_CPU_WR (1ul << 1)

#define KBASE_REG_GPU_WR (1ul << 2)

#define KBASE_REG_GPU_NX (1ul << 3)

#define KBASE_REG_CPU_CACHED (1ul << 4)

#define KBASE_REG_GPU_CACHED (1ul << 5)

#define KBASE_REG_GROWABLE (1ul << 6)

#define KBASE_REG_PF_GROW (1ul << 7)

#define KBASE_REG_GPU_VA_SAME_4GB_PAGE (1ul << 8)

#define KBASE_REG_SHARE_IN (1ul << 9)

#define KBASE_REG_SHARE_BOTH (1ul << 10)

#define KBASE_REG_GPU_RD (1ul << 14)

#define KBASE_REG_CPU_RD (1ul << 15)

#define KBASE_REG_MEMATTR_MASK (7ul << 16)
#define KBASE_REG_MEMATTR_INDEX(x) (((x)&7) << 16)
#define KBASE_REG_MEMATTR_VALUE(x) (((x)&KBASE_REG_MEMATTR_MASK) >> 16)

#define KBASE_MEMATTR_IMPL_DEF_CACHE_POLICY                                         \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                  \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_IMPL) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_WRITE_BACK)))

#define KBASE_MEMATTR_FORCE_TO_CACHE_ALL                                             \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_W_MASK |                   \
			      AS_MEMATTR_ATTRIBUTE0_ALLOC_R_MASK |                   \
			      AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                   \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_ALLOC) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                 \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_WRITE_BACK)))

#define KBASE_MEMATTR_WRITE_ALLOC                                                    \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_W_MASK |                   \
			      AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                   \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_ALLOC) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                 \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_WRITE_BACK)))

#define KBASE_MEMATTR_AARCH64_OUTER_IMPL_DEF                                        \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                  \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_IMPL) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_WRITE_BACK)))

#define KBASE_MEMATTR_AARCH64_OUTER_WA                                               \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_W_MASK |                   \
			      AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                   \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_ALLOC) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                 \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_WRITE_BACK)))

#define KBASE_MEMATTR_AARCH64_NON_CACHEABLE                                          \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                   \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_ALLOC) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                 \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_NON_CACHEABLE)))

#define KBASE_MEMATTR_INDEX_DEFAULT 0
#define KBASE_MEMATTR_INDEX_DEFAULT_ACE 3

#define KBASE_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY 0

#define KBASE_MEMATTR_INDEX_FORCE_TO_CACHE_ALL 1

#define KBASE_MEMATTR_INDEX_WRITE_ALLOC 2

#define KBASE_MEMATTR_INDEX_OUTER_IMPL_DEF 3

#define KBASE_MEMATTR_INDEX_OUTER_WA 4

#define KBASE_MEMATTR_INDEX_NON_CACHEABLE 5

#if MALI_USE_CSF

#define KBASE_MEMATTR_AARCH64_SHARED                                                \
	((unsigned long long)(AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_SET(                  \
				      0ull, AS_MEMATTR_ATTRIBUTE0_ALLOC_SEL_IMPL) | \
			      AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SET(                \
				      0ull, AS_MEMATTR_ATTRIBUTE0_MEMORY_TYPE_SHARED)))

#define KBASE_MEMATTR_INDEX_SHARED 6
#endif

#define KBASE_REG_PROTECTED (1ul << 19)

#define KBASE_REG_DONT_NEED (1ul << 20)

#define KBASE_REG_IMPORT_PAD (1ul << 21)

#if MALI_USE_CSF

#define KBASE_REG_CSF_EVENT (1ul << 22)

#define KBASE_REG_RESERVED_BIT_23 (1ul << 23)
#else

#define KBASE_REG_RESERVED_BIT_22 (1ul << 22)

#define KBASE_REG_TILER_ALIGN_TOP (1ul << 23)
#endif

#define KBASE_REG_PERMANENT_KERNEL_MAPPING (1ul << 25)

#define KBASE_REG_VA_FREED (1ul << 26)

#define KBASE_REG_HEAP_INFO_IS_SIZE (1ul << 27)

#define KBASE_REG_ACTIVE_JIT_ALLOC (1ul << 28)

#if MALI_USE_CSF

#define KBASE_REG_FIXED_ADDRESS (1ul << 29)
#else
#define KBASE_REG_RESERVED_BIT_29 (1ul << 29)
#endif

struct kbase_cpu_mapping {
	struct list_head mappings_list;
	struct kbase_mem_phy_alloc *alloc;
	struct kbase_context *kctx;
	struct kbase_va_region *region;
	int count;
	int free_on_close;
};

enum kbase_memory_type {
	KBASE_MEM_TYPE_NATIVE,
	KBASE_MEM_TYPE_IMPORTED_UMM,
	KBASE_MEM_TYPE_IMPORTED_USER_BUF,
	KBASE_MEM_TYPE_ALIAS,
	KBASE_MEM_TYPE_RAW
};

struct kbase_aliased {
	struct kbase_mem_phy_alloc *alloc;
	u64 offset;
	u64 length;
};

#define KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED (1u << 0)
#define KBASE_MEM_PHY_ALLOC_LARGE (1u << 1)

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)

#define JIT_RECLAIM_DEFAULT_TIMEOUT_MS (1000)
#endif

enum kbase_user_buf_state {
	KBASE_USER_BUF_STATE_EMPTY,
	KBASE_USER_BUF_STATE_PINNED,
	KBASE_USER_BUF_STATE_DMA_MAPPED,
	KBASE_USER_BUF_STATE_GPU_MAPPED,
	KBASE_USER_BUF_STATE_COUNT = 4
};

struct kbase_mem_phy_alloc {
	struct kref kref;
	atomic64_t gpu_mappings;
	atomic_t kernel_mappings;
	size_t nents;
	struct tagged_addr *pages;
	struct list_head mappings;
	struct list_head evict_node;
	size_t evicted;
	struct kbase_va_region *reg;
	enum kbase_memory_type type;
#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	enum kbase_memory_category category;
#endif
	struct kbase_vmap_struct *permanent_map;
	u8 properties;
	u8 group_id;

	union {
		struct {
			struct kbase_context *kctx;
			struct dma_buf *dma_buf;
			struct dma_buf_attachment *dma_attachment;
			unsigned int current_mapping_usage_count;
			struct sg_table *sgt;
			bool need_sync;
		} umm;
		struct {
			u64 stride;
			size_t nents;
			struct kbase_aliased *aliased;
		} alias;
		struct {
			struct kbase_context *kctx;

			size_t nr_struct_pages;
		} native;
		struct kbase_alloc_import_user_buf {
			unsigned long address;
			unsigned long size;
			unsigned long nr_pages;
			struct page **pages;
			u32 current_mapping_usage_count;
			struct mm_struct *mm;
			dma_addr_t *dma_addrs;
			enum kbase_user_buf_state state;
		} user_buf;
	} imported;
};

enum kbase_page_status {
	MEM_POOL = 0,
	ALLOCATE_IN_PROGRESS,
	SPILL_IN_PROGRESS,
	NOT_MOVABLE,
	ALLOCATED_MAPPED,
	PT_MAPPED,
	FREE_IN_PROGRESS,
	FREE_ISOLATED_IN_PROGRESS,
	FREE_PT_ISOLATED_IN_PROGRESS,
};

#define PGD_VPFN_LEVEL_MASK ((u64)0x3)
#define PGD_VPFN_LEVEL_GET_LEVEL(pgd_vpfn_level) (pgd_vpfn_level & PGD_VPFN_LEVEL_MASK)
#define PGD_VPFN_LEVEL_GET_VPFN(pgd_vpfn_level) (pgd_vpfn_level & ~PGD_VPFN_LEVEL_MASK)
#define PGD_VPFN_LEVEL_SET(pgd_vpfn, level) \
	((pgd_vpfn & ~PGD_VPFN_LEVEL_MASK) | (level & PGD_VPFN_LEVEL_MASK))

struct kbase_page_metadata {
	struct kbase_device *kbdev;
	dma_addr_t dma_addr;
	spinlock_t migrate_lock;
	struct semaphore cpu_map_lock;

	union {
		struct {

			struct kbase_mem_pool *pool;
		} mem_pool;
		struct {
			struct kbase_va_region *reg;
			struct kbase_mmu_table *mmut;

			u64 vpfn;
		} mapped;
		struct {
			struct kbase_mmu_table *mmut;

			u64 pgd_vpfn_level[GPU_PAGES_PER_CPU_PAGE];
#if GPU_PAGES_PER_CPU_PAGE > 1

			struct list_head pgd_link;

			struct page *pgd_page;

			DECLARE_BITMAP(allocated_sub_pages, GPU_PAGES_PER_CPU_PAGE);

			s8 num_allocated_sub_pages;
#endif
		} pt_mapped;
	} data;

	u8 status;
	u8 vmap_count;
	u8 group_id;
};

enum kbase_jit_report_flags { KBASE_JIT_REPORT_ON_ALLOC_OR_FREE = (1u << 0) };

void kbase_set_phy_alloc_page_status(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc,
				     enum kbase_page_status status);

static inline void kbase_mem_phy_alloc_gpu_mapped(struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(alloc);

	if (alloc->type == KBASE_MEM_TYPE_NATIVE)
		atomic64_inc(&alloc->gpu_mappings);
}

static inline void kbase_mem_phy_alloc_gpu_unmapped(struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(alloc);

	if (alloc->type == KBASE_MEM_TYPE_NATIVE)
		if (atomic64_dec_return(&alloc->gpu_mappings) < 0) {
			pr_err("Mismatched %s:\n", __func__);
			dump_stack();
		}
}

static inline void kbase_mem_phy_alloc_kernel_mapped(struct kbase_mem_phy_alloc *alloc)
{
	atomic_inc(&alloc->kernel_mappings);
}

static inline void kbase_mem_phy_alloc_kernel_unmapped(struct kbase_mem_phy_alloc *alloc)
{
	WARN_ON(atomic_dec_return(&alloc->kernel_mappings) < 0);
}

static inline bool kbase_mem_is_imported(enum kbase_memory_type type)
{
	return (type == KBASE_MEM_TYPE_IMPORTED_UMM) || (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF);
}

void kbase_mem_kref_free(struct kref *kref);

int kbase_mem_init(struct kbase_device *kbdev);
void kbase_mem_halt(struct kbase_device *kbdev);
void kbase_mem_term(struct kbase_device *kbdev);

static inline struct kbase_mem_phy_alloc *kbase_mem_phy_alloc_get(struct kbase_mem_phy_alloc *alloc)
{
	kref_get(&alloc->kref);
	return alloc;
}

static inline struct kbase_mem_phy_alloc *kbase_mem_phy_alloc_put(struct kbase_mem_phy_alloc *alloc)
{
	kref_put(&alloc->kref, kbase_mem_kref_free);
	return NULL;
}

struct kbase_va_region {
	struct rb_node rblink;
	struct list_head link;
	struct rb_root *rbtree;
	u64 start_pfn;
	void *user_data;
	size_t nr_pages;
	size_t initial_commit;
	unsigned long flags;
	size_t extension;
	struct kbase_mem_phy_alloc *cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc;
	struct list_head jit_node;
	u16 jit_usage_id;
	u8 jit_bin_id;

#if MALI_JIT_PRESSURE_LIMIT_BASE

	u64 heap_info_gpu_addr;

	size_t used_pages;
#endif

	kbase_refcount_t va_refcnt;
	atomic64_t no_user_free_count;

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	u64 last_used_ts;
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	u32 pbha_8bit;
	bool isFirstDmaBuf;
	bool isImportedMemory;
#endif
};

#define KBASE_RESERVED_REG_JIT_ALLOC ((struct kbase_va_region *)-1)

static inline bool kbase_is_region_free(struct kbase_va_region *reg)
{
	return (!reg || reg->flags & KBASE_REG_FREE);
}

static inline bool kbase_is_region_invalid(struct kbase_va_region *reg)
{
	return (!reg || reg->flags & KBASE_REG_VA_FREED);
}

static inline bool kbase_is_region_invalid_or_free(struct kbase_va_region *reg)
{

	return (kbase_is_region_invalid(reg) || kbase_is_region_free(reg));
}

static inline bool kbase_is_region_shrinkable(struct kbase_va_region *reg)
{
	return (reg->flags & KBASE_REG_DONT_NEED) || (reg->flags & KBASE_REG_ACTIVE_JIT_ALLOC);
}

void kbase_remove_va_region(struct kbase_device *kbdev, struct kbase_va_region *reg);
static inline void kbase_region_refcnt_free(struct kbase_device *kbdev, struct kbase_va_region *reg)
{

	if (reg->start_pfn)
		kbase_remove_va_region(kbdev, reg);

	KBASE_DEBUG_CODE(reg->flags |= KBASE_REG_FREE);
	kfree(reg);
}

static inline struct kbase_va_region *kbase_va_region_alloc_get(struct kbase_context *kctx,
								struct kbase_va_region *region)
{
	WARN_ON(!kbase_refcount_read(&region->va_refcnt));
	WARN_ON(kbase_refcount_read(&region->va_refcnt) == INT_MAX);

	dev_dbg(kctx->kbdev->dev, "va_refcnt %d before get %pK\n",
		kbase_refcount_read(&region->va_refcnt), (void *)region);
	kbase_refcount_inc(&region->va_refcnt);

	return region;
}

static inline struct kbase_va_region *kbase_va_region_alloc_put(struct kbase_context *kctx,
								struct kbase_va_region *region)
{
	WARN_ON(kbase_refcount_read(&region->va_refcnt) <= 0);
	WARN_ON(region->flags & KBASE_REG_FREE);

	if (kbase_refcount_dec_and_test(&region->va_refcnt))
		kbase_region_refcnt_free(kctx->kbdev, region);
	else
		dev_dbg(kctx->kbdev->dev, "va_refcnt %d after put %pK\n",
			kbase_refcount_read(&region->va_refcnt), (void *)region);

	return NULL;
}

static inline bool kbase_va_region_is_no_user_free(struct kbase_va_region *region)
{
	return atomic64_read(&region->no_user_free_count) > 0;
}

static inline void kbase_va_region_no_user_free_inc(struct kbase_va_region *region)
{
	WARN_ON(kbase_is_region_shrinkable(region));
	WARN_ON(atomic64_read(&region->no_user_free_count) == S64_MAX);

	atomic64_inc(&region->no_user_free_count);
}

static inline void kbase_va_region_no_user_free_dec(struct kbase_va_region *region)
{
	WARN_ON(!kbase_va_region_is_no_user_free(region));

	atomic64_dec(&region->no_user_free_count);
}

static inline struct tagged_addr *kbase_get_cpu_phy_pages(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->cpu_alloc->pages;
}

static inline struct tagged_addr *kbase_get_gpu_phy_pages(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc->pages);

	return reg->gpu_alloc->pages;
}

static inline size_t kbase_reg_current_backed_size(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);

	if (!reg->cpu_alloc)
		return 0;

	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->cpu_alloc->nents;
}

#define KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD \
	((size_t)(4 * 1024))

static inline struct kbase_mem_phy_alloc *kbase_alloc_create(struct kbase_context *kctx,
							     size_t nr_pages,
							     enum kbase_memory_type type,
							     int group_id)
{
	struct kbase_mem_phy_alloc *alloc;
	size_t alloc_size = sizeof(*alloc) + sizeof(*alloc->pages) * nr_pages;
	size_t per_page_size = sizeof(*alloc->pages);
	size_t i;

	if (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF) {
		alloc_size += nr_pages * sizeof(*alloc->imported.user_buf.dma_addrs);
		per_page_size += sizeof(*alloc->imported.user_buf.dma_addrs);
	}

	if (nr_pages > ((((size_t)-1) - sizeof(*alloc)) / per_page_size))
		return ERR_PTR(-ENOMEM);

	if (alloc_size > KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD)
		alloc = vmalloc(alloc_size);
	else
		alloc = kmalloc(alloc_size, GFP_KERNEL);

	if (!alloc)
		return ERR_PTR(-ENOMEM);

	memset(alloc, 0, sizeof(struct kbase_mem_phy_alloc));

	if (type == KBASE_MEM_TYPE_NATIVE) {
		alloc->imported.native.nr_struct_pages = (alloc_size + (PAGE_SIZE - 1)) >>
							 PAGE_SHIFT;
		kbase_process_page_usage_inc(kctx, alloc->imported.native.nr_struct_pages);
	}

	if (alloc_size > KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD)
		alloc->properties |= KBASE_MEM_PHY_ALLOC_LARGE;

	kref_init(&alloc->kref);
	atomic64_set(&alloc->gpu_mappings, 0);
	atomic_set(&alloc->kernel_mappings, 0);
	alloc->nents = 0;
	if (type != KBASE_MEM_TYPE_ALIAS) {
		alloc->pages = (void *)(alloc + 1);

		for (i = 0; i < nr_pages; i++)
			alloc->pages[i] = as_tagged(KBASE_INVALID_PHYSICAL_ADDRESS);
	}
	INIT_LIST_HEAD(&alloc->mappings);
	alloc->type = type;
#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	alloc->category = KBASE_MEM_UNKNOWN;
#endif
	alloc->group_id = group_id;

	if (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF)
		alloc->imported.user_buf.dma_addrs = (void *)(alloc->pages + nr_pages);

	return alloc;
}

static inline int kbase_reg_prepare_native(struct kbase_va_region *reg, struct kbase_context *kctx,
					   int group_id)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(!reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(!reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->flags & KBASE_REG_FREE);

	reg->cpu_alloc = kbase_alloc_create(kctx, reg->nr_pages, KBASE_MEM_TYPE_NATIVE, group_id);
	if (IS_ERR(reg->cpu_alloc))
		return PTR_ERR(reg->cpu_alloc);
	else if (!reg->cpu_alloc)
		return -ENOMEM;

	reg->cpu_alloc->imported.native.kctx = kctx;
	if (kbase_ctx_flag(kctx, KCTX_INFINITE_CACHE) && (reg->flags & KBASE_REG_CPU_CACHED)) {
		reg->gpu_alloc =
			kbase_alloc_create(kctx, reg->nr_pages, KBASE_MEM_TYPE_NATIVE, group_id);
		if (IS_ERR_OR_NULL(reg->gpu_alloc)) {
			kbase_mem_phy_alloc_put(reg->cpu_alloc);
			return -ENOMEM;
		}
		reg->gpu_alloc->imported.native.kctx = kctx;
	} else {
		reg->gpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	}

	mutex_lock(&kctx->jit_evict_lock);
	INIT_LIST_HEAD(&reg->cpu_alloc->evict_node);
	INIT_LIST_HEAD(&reg->gpu_alloc->evict_node);
	mutex_unlock(&kctx->jit_evict_lock);

	reg->flags &= ~KBASE_REG_FREE;

	return 0;
}

#define KBASE_MEM_POOL_MAX_SIZE_KBDEV (SZ_64M >> PAGE_SHIFT)

#define KBASE_MEM_POOL_MAX_SIZE_KCTX (SZ_64M >> PAGE_SHIFT)

#define KBASE_MEM_POOL_2MB_PAGE_TABLE_ORDER (__builtin_ffs(NUM_PAGES_IN_2MB_LARGE_PAGE) - 1)

#define KBASE_MEM_POOL_SMALL_PAGE_TABLE_ORDER 0

static inline void kbase_mem_pool_config_set_max_size(struct kbase_mem_pool_config *const config,
						      size_t const max_size)
{
	WRITE_ONCE(config->max_size, max_size);
}

static inline size_t
kbase_mem_pool_config_get_max_size(const struct kbase_mem_pool_config *const config)
{
	return READ_ONCE(config->max_size);
}

int kbase_mem_pool_init(struct kbase_mem_pool *pool, const struct kbase_mem_pool_config *config,
			unsigned int order, int group_id, struct kbase_device *kbdev,
			struct kbase_mem_pool *next_pool);

void kbase_mem_pool_term(struct kbase_mem_pool *pool);

struct page *kbase_mem_pool_alloc(struct kbase_mem_pool *pool);

struct page *kbase_mem_pool_alloc_locked(struct kbase_mem_pool *pool);

void kbase_mem_pool_free(struct kbase_mem_pool *pool, struct page *page, bool dirty);

void kbase_mem_pool_free_locked(struct kbase_mem_pool *pool, struct page *p, bool dirty);

int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_small_pages,
			       struct tagged_addr *pages, bool partial_allowed,
			       struct task_struct *page_owner);

int kbase_mem_pool_alloc_pages_locked(struct kbase_mem_pool *pool, size_t nr_small_pages,
				      struct tagged_addr *pages);

void kbase_mem_pool_free_pages(struct kbase_mem_pool *pool, size_t nr_pages,
			       struct tagged_addr *pages, bool dirty, bool reclaimed);

void kbase_mem_pool_free_pages_locked(struct kbase_mem_pool *pool, size_t nr_pages,
				      struct tagged_addr *pages, bool dirty, bool reclaimed);

static inline size_t kbase_mem_pool_size(struct kbase_mem_pool *pool)
{
	return READ_ONCE(pool->cur_size);
}

static inline size_t kbase_mem_pool_max_size(struct kbase_mem_pool *pool)
{
	return pool->max_size;
}

void kbase_mem_pool_set_max_size(struct kbase_mem_pool *pool, size_t max_size);

int kbase_mem_pool_grow(struct kbase_mem_pool *pool, size_t nr_to_grow,
			struct task_struct *page_owner);

void kbase_mem_pool_trim(struct kbase_mem_pool *pool, size_t new_size);

void kbase_mem_pool_mark_dying(struct kbase_mem_pool *pool);

struct page *kbase_mem_alloc_page(struct kbase_mem_pool *pool);

void kbase_mem_pool_free_page(struct kbase_mem_pool *pool, struct page *p);

bool kbase_check_alloc_flags(unsigned long flags);
bool kbase_check_import_flags(unsigned long flags);

static inline bool kbase_import_size_is_valid(struct kbase_device *kbdev, u64 va_pages)
{
	if (va_pages > KBASE_MEM_ALLOC_MAX_SIZE) {
		dev_dbg(kbdev->dev,
			"Import attempted with va_pages==%lld larger than KBASE_MEM_ALLOC_MAX_SIZE!",
			(unsigned long long)va_pages);
		return false;
	}

	return true;
}

static inline bool kbase_alias_size_is_valid(struct kbase_device *kbdev, u64 va_pages)
{
	if (va_pages > KBASE_MEM_ALLOC_MAX_SIZE) {
		dev_dbg(kbdev->dev,
			"Alias attempted with va_pages==%lld larger than KBASE_MEM_ALLOC_MAX_SIZE!",
			(unsigned long long)va_pages);
		return false;
	}

	return true;
}

int kbase_check_alloc_sizes(struct kbase_context *kctx, unsigned long flags, u64 va_pages,
			    u64 commit_pages, u64 extension);

int kbase_update_region_flags(struct kbase_context *kctx, struct kbase_va_region *reg,
			      unsigned long flags);

void kbase_gpu_vm_lock(struct kbase_context *kctx);

void kbase_gpu_vm_lock_with_pmode_sync(struct kbase_context *kctx);

void kbase_gpu_vm_unlock(struct kbase_context *kctx);

void kbase_gpu_vm_unlock_with_pmode_sync(struct kbase_context *kctx);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size);

int kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr,
		   size_t nr_pages, size_t align, enum kbase_caller_mmu_sync_info mmu_sync_info);

int kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_mmu_update(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, int as_nr);

void kbase_mmu_disable(struct kbase_context *kctx);

void kbase_mmu_disable_as(struct kbase_device *kbdev, int as_nr);

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat);

#if defined(CONFIG_MALI_VECTOR_DUMP)

void *kbase_mmu_dump(struct kbase_context *kctx, size_t nr_pages);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MMU_DUMP)
size_t kbasep_mmu_dump_table_size(struct kbase_device *kbdev, int level, struct kbase_mmu_table *mmu);
#endif

int kbase_sync_now(struct kbase_context *kctx, struct basep_syncset *sset);
void kbase_sync_single(struct kbase_context *kctx, struct tagged_addr cpu_pa,
		       struct tagged_addr gpu_pa, off_t offset, size_t size,
		       enum kbase_sync_type sync_fn);

int kbase_mem_free(struct kbase_context *kctx, u64 gpu_addr);
int kbase_mem_free_region(struct kbase_context *kctx, struct kbase_va_region *reg);
void kbase_os_mem_map_lock(struct kbase_context *kctx);
void kbase_os_mem_map_unlock(struct kbase_context *kctx);

void kbasep_os_process_page_usage_update(struct kbase_context *kctx, int pages);

static inline void kbase_process_page_usage_inc(struct kbase_context *kctx, int pages)
{
	kbasep_os_process_page_usage_update(kctx, pages);
}

static inline void kbase_process_page_usage_dec(struct kbase_context *kctx, int pages)
{
	kbasep_os_process_page_usage_update(kctx, 0 - pages);
}

int kbasep_find_enclosing_cpu_mapping_offset(struct kbase_context *kctx, unsigned long uaddr,
					     size_t size, u64 *offset);

int kbasep_find_enclosing_gpu_mapping_start_and_offset(struct kbase_context *kctx, u64 gpu_addr,
						       size_t size, u64 *start, u64 *offset);

int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc *alloc, size_t nr_pages_requested);

struct tagged_addr *kbase_alloc_phy_pages_helper_locked(struct kbase_mem_phy_alloc *alloc,
							struct kbase_mem_pool *pool,
							size_t nr_pages_requested,
							struct kbase_sub_alloc **prealloc_sa);

int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc *alloc, size_t nr_pages_to_free);

void kbase_free_phy_pages_helper_locked(struct kbase_mem_phy_alloc *alloc,
					struct kbase_mem_pool *pool, struct tagged_addr *pages,
					size_t nr_pages_to_free);

static inline void kbase_set_dma_addr_as_priv(struct page *p, dma_addr_t dma_addr)
{
	SetPagePrivate(p);
	if (sizeof(dma_addr_t) > sizeof(p->private)) {

		KBASE_DEBUG_ASSERT(!(dma_addr & (PAGE_SIZE - 1)));
		set_page_private(p, dma_addr >> PAGE_SHIFT);
	} else {
		set_page_private(p, dma_addr);
	}
}

static inline dma_addr_t kbase_dma_addr_as_priv(struct page *p)
{
	if (sizeof(dma_addr_t) > sizeof(p->private))
		return ((dma_addr_t)page_private(p)) << PAGE_SHIFT;

	return (dma_addr_t)page_private(p);
}

static inline void kbase_clear_dma_addr_as_priv(struct page *p)
{
	ClearPagePrivate(p);
}

static inline struct kbase_page_metadata *kbase_page_private(struct page *p)
{
	return (struct kbase_page_metadata *)page_private(p);
}

static inline dma_addr_t kbase_dma_addr(struct page *p)
{
	if (kbase_is_page_migration_enabled())
		return kbase_page_private(p)->dma_addr;

	return kbase_dma_addr_as_priv(p);
}

static inline dma_addr_t kbase_dma_addr_from_tagged(struct tagged_addr tagged_pa)
{
	phys_addr_t pa = as_phys_addr_t(tagged_pa);
	struct page *page = pfn_to_page(PFN_DOWN(pa));
	dma_addr_t dma_addr = (is_huge(tagged_pa) || is_partial(tagged_pa)) ?
					    kbase_dma_addr_as_priv(page) :
					    kbase_dma_addr(page);

	return dma_addr;
}

void kbase_flush_mmu_wqs(struct kbase_device *kbdev);

void kbase_sync_single_for_device(struct kbase_device *kbdev, dma_addr_t handle, size_t size,
				  enum dma_data_direction dir);

void kbase_sync_single_for_cpu(struct kbase_device *kbdev, dma_addr_t handle, size_t size,
			       enum dma_data_direction dir);

#if IS_ENABLED(CONFIG_DEBUG_FS)

void kbase_jit_debugfs_init(struct kbase_context *kctx);
#endif

int kbase_jit_init(struct kbase_context *kctx);

struct kbase_va_region *kbase_jit_allocate(struct kbase_context *kctx,
					   const struct base_jit_alloc_info *info,
					   bool ignore_pressure_limit);

void kbase_jit_free(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_jit_backing_lost(struct kbase_va_region *reg);

bool kbase_jit_evict(struct kbase_context *kctx);

void kbase_jit_term(struct kbase_context *kctx);

#if MALI_JIT_PRESSURE_LIMIT_BASE

void kbase_trace_jit_report_gpu_mem_trace_enabled(struct kbase_context *kctx,
						  struct kbase_va_region *reg, unsigned int flags);
#endif

#if MALI_JIT_PRESSURE_LIMIT_BASE
#define kbase_trace_jit_report_gpu_mem(kctx, reg, flags)                                      \
	do {                                                                                  \
		if (trace_mali_jit_report_gpu_mem_enabled())                                  \
			kbase_trace_jit_report_gpu_mem_trace_enabled((kctx), (reg), (flags)); \
	} while (0)
#else
#define kbase_trace_jit_report_gpu_mem(kctx, reg, flags) CSTD_NOP(kctx, reg, flags)
#endif

#if MALI_JIT_PRESSURE_LIMIT_BASE

void kbase_jit_report_update_pressure(struct kbase_context *kctx, struct kbase_va_region *reg,
				      u64 new_used_pages, unsigned int flags);

void kbase_jit_trim_necessary_pages(struct kbase_context *kctx, size_t needed_pages);

static inline void kbase_jit_request_phys_increase_locked(struct kbase_context *kctx,
							  size_t needed_pages)
{
#if !MALI_USE_CSF
	lockdep_assert_held(&kctx->jctx.lock);
#endif
	lockdep_assert_held(&kctx->reg_lock);
	lockdep_assert_held(&kctx->jit_evict_lock);

	kctx->jit_phys_pages_to_be_allocated += needed_pages;

	kbase_jit_trim_necessary_pages(kctx, kctx->jit_phys_pages_to_be_allocated);
}

static inline void kbase_jit_request_phys_increase(struct kbase_context *kctx, size_t needed_pages)
{
#if !MALI_USE_CSF
	lockdep_assert_held(&kctx->jctx.lock);
#endif
	lockdep_assert_held(&kctx->reg_lock);

	mutex_lock(&kctx->jit_evict_lock);
	kbase_jit_request_phys_increase_locked(kctx, needed_pages);
	mutex_unlock(&kctx->jit_evict_lock);
}

static inline void kbase_jit_done_phys_increase(struct kbase_context *kctx, size_t needed_pages)
{
	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(kctx->jit_phys_pages_to_be_allocated < needed_pages);

	kctx->jit_phys_pages_to_be_allocated -= needed_pages;
}
#endif

bool kbase_has_exec_va_zone(struct kbase_context *kctx);

int kbase_map_external_resource(struct kbase_context *kctx, struct kbase_va_region *reg,
				struct mm_struct *locked_mm);

void kbase_unmap_external_resource(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_unpin_user_buf_page(struct page *page);

int kbase_user_buf_pin_pages(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_user_buf_unpin_pages(struct kbase_mem_phy_alloc *alloc);

int kbase_user_buf_dma_map_pages(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_user_buf_dma_unmap_pages(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_user_buf_empty_init(struct kbase_va_region *reg);

int kbase_user_buf_from_empty_to_pinned(struct kbase_context *kctx, struct kbase_va_region *reg);

int kbase_user_buf_from_empty_to_dma_mapped(struct kbase_context *kctx,
					    struct kbase_va_region *reg);

int kbase_user_buf_from_empty_to_gpu_mapped(struct kbase_context *kctx,
					    struct kbase_va_region *reg);

int kbase_user_buf_from_pinned_to_gpu_mapped(struct kbase_context *kctx,
					     struct kbase_va_region *reg);

void kbase_user_buf_from_dma_mapped_to_pinned(struct kbase_context *kctx,
					      struct kbase_va_region *reg);

int kbase_user_buf_from_dma_mapped_to_gpu_mapped(struct kbase_context *kctx,
						 struct kbase_va_region *reg);

void kbase_user_buf_from_gpu_mapped_to_pinned(struct kbase_context *kctx,
					      struct kbase_va_region *reg);

int kbase_sticky_resource_init(struct kbase_context *kctx);

struct kbase_ctx_ext_res_meta *kbase_sticky_resource_acquire(struct kbase_context *kctx,
							     u64 gpu_addr,
							     struct mm_struct *locked_mm);

bool kbase_sticky_resource_release(struct kbase_context *kctx, struct kbase_ctx_ext_res_meta *meta,
				   u64 gpu_addr);

bool kbase_sticky_resource_release_force(struct kbase_context *kctx,
					 struct kbase_ctx_ext_res_meta *meta, u64 gpu_addr);

void kbase_sticky_resource_term(struct kbase_context *kctx);

static inline void kbase_mem_pool_lock(struct kbase_mem_pool *pool)
{
	spin_lock(&pool->pool_lock);
}

static inline void kbase_mem_pool_unlock(struct kbase_mem_pool *pool)
{
	spin_unlock(&pool->pool_lock);
}

void kbase_mem_evictable_mark_reclaim(struct kbase_mem_phy_alloc *alloc);

#if MALI_USE_CSF

static inline void kbase_link_event_mem_page(struct kbase_context *kctx,
					     struct kbase_va_region *reg)
{
	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(reg->flags & KBASE_REG_FREE);
	WARN_ON(!(reg->flags & KBASE_REG_CSF_EVENT));

	list_add(&reg->link, &kctx->csf.event_pages_head);
}

static inline void kbase_unlink_event_mem_page(struct kbase_context *kctx,
					       struct kbase_va_region *reg)
{
	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(reg->flags & KBASE_REG_FREE);
	WARN_ON(!(reg->flags & KBASE_REG_CSF_EVENT));

	list_del(&reg->link);
}

int kbase_mcu_shared_interface_region_tracker_init(struct kbase_device *kbdev);

void kbase_mcu_shared_interface_region_tracker_term(struct kbase_device *kbdev);
#endif

int kbase_mem_umm_map(struct kbase_context *kctx, struct kbase_va_region *reg);

void kbase_mem_umm_unmap(struct kbase_context *kctx, struct kbase_va_region *reg,
			 struct kbase_mem_phy_alloc *alloc);

int kbase_sync_imported_umm(struct kbase_context *kctx, struct kbase_va_region *reg,
			    enum kbase_sync_type sync_fn);

int kbase_mem_copy_to_pinned_user_pages(struct page **dest_pages, void *src_page, size_t *to_copy,
					unsigned int nr_pages, unsigned int *target_page_nr,
					size_t offset);

static inline bool kbase_mem_allow_alloc(struct kbase_context *kctx)
{
	return (kctx->process_mm == current->mm);
}

static inline void kbase_mem_mmgrab(void)
{

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	atomic_inc(&current->mm->mm_count);
#else
	mmgrab(current->mm);
#endif
}

static inline int kbase_mem_group_id_get(base_mem_alloc_flags flags)
{
	KBASE_DEBUG_ASSERT((flags & ~BASE_MEM_FLAGS_INPUT_MASK) == 0);
	return (int)BASE_MEM_GROUP_ID_GET(flags);
}

static inline base_mem_alloc_flags kbase_mem_group_id_set(int id)
{
	return BASE_MEM_GROUP_ID_SET(id);
}

bool kbase_is_large_pages_enabled(void);

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
void kbase_trace_alloc_pages(int32_t gpu_id, struct kbase_context *kctx, uint64_t size, uint64_t gpu_addr, enum kbase_memory_category category);
void kbase_trace_free_pages(int32_t gpu_id, struct kbase_context *kctx, uint64_t size, uint64_t gpu_addr, enum kbase_memory_category category);
void kbase_trace_update_pages(int32_t gpu_id, struct kbase_context *kctx, uint64_t size_a, uint64_t size_b, uint64_t gpu_addr, enum kbase_memory_category category);
 #endif

#endif
