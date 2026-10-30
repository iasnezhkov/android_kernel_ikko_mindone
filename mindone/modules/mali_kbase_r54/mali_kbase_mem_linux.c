// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/shrinker.h>
#include <linux/cache.h>
#include <linux/memory_group_manager.h>
#include <linux/math64.h>
#include <linux/migrate.h>
#include <linux/version.h>
#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include <tl/mali_kbase_tracepoints.h>
#include <uapi/gpu/arm/midgard/mali_kbase_ioctl.h>
#include <mmu/mali_kbase_mmu.h>
#include <mali_kbase_caps.h>
#include <mali_kbase_trace_gpu_mem.h>
#include <mali_kbase_reset_gpu.h>
#include <linux/version_compat_defs.h>

#if IS_ENABLED(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM) && IS_ENABLED(CONFIG_MTK_GZ_KREE) && IS_ENABLED(CONFIG_MALI_MTK_PROTECTED_PATCH)
#include <trusted_mem_api.h>
#include <mtk_heap.h>
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
#include <gpu_pdma.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif

#if ((KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE) || \
     (KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE))

#define KBASE_MEM_ION_SYNC_WORKAROUND
#endif

static bool fully_backed_gpf_memory;
module_param(fully_backed_gpf_memory, bool, 0444);
MODULE_PARM_DESC(
	fully_backed_gpf_memory,
	"Enable the full physical backing of all grow-on-GPU-page-fault allocations in the kernel."
	"Note that this should only be enabled for testing worst case memory usage "
	"and should not be enabled in production");

#if MALI_USE_CSF
static int kbase_csf_cpu_mmap_user_reg_page(struct kbase_context *kctx, struct vm_area_struct *vma);
static int kbase_csf_cpu_mmap_user_io_pages(struct kbase_context *kctx, struct vm_area_struct *vma);
#endif

static int kbase_vmap_phy_pages(struct kbase_context *kctx, struct kbase_va_region *reg,
				u64 offset_bytes, size_t size, struct kbase_vmap_struct *map,
				kbase_vmap_flag vmap_flags);
static void kbase_vunmap_phy_pages(struct kbase_context *kctx, struct kbase_vmap_struct *map);

static int kbase_tracking_page_setup(struct kbase_context *kctx, struct vm_area_struct *vma);

static bool is_process_exiting(struct vm_area_struct *vma)
{

	if (atomic_read(&vma->vm_mm->mm_users))
		return false;

	return true;
}

static struct kbase_va_region *kbase_find_event_mem_region(struct kbase_context *kctx, u64 gpu_addr)
{
#if MALI_USE_CSF
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct kbase_va_region *reg;

	lockdep_assert_held(&kctx->reg_lock);

	list_for_each_entry(reg, &kctx->csf.event_pages_head, link) {
		if ((reg->start_pfn <= gpu_pfn) && (gpu_pfn < (reg->start_pfn + reg->nr_pages))) {
			if (WARN_ON(reg->flags & KBASE_REG_FREE))
				return NULL;

			if (WARN_ON(!(reg->flags & KBASE_REG_CSF_EVENT)))
				return NULL;

			return reg;
		}
	}
#else
	CSTD_UNUSED(kctx);
	CSTD_UNUSED(gpu_addr);
#endif

	return NULL;
}

static int kbase_phy_alloc_mapping_init(struct kbase_context *kctx, struct kbase_va_region *reg,
					size_t vsize, size_t size)
{
	size_t size_bytes = (size << PAGE_SHIFT);
	struct kbase_vmap_struct *kern_mapping;
	int err = 0;

	if (vsize != size || reg->cpu_alloc->permanent_map != NULL ||
	    reg->cpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
		return -EINVAL;

	kern_mapping = kzalloc(sizeof(*kern_mapping), GFP_KERNEL);
	if (!kern_mapping)
		return -ENOMEM;

	err = kbase_vmap_phy_pages(kctx, reg, 0u, size_bytes, kern_mapping,
				   KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING);
	if (err < 0)
		goto vmap_fail;

	reg->flags &= ~KBASE_REG_GROWABLE;

	reg->cpu_alloc->permanent_map = kern_mapping;

	return 0;
vmap_fail:
	kfree(kern_mapping);
	return err;
}

void kbase_phy_alloc_mapping_term(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc)
{
	WARN_ON(!alloc->permanent_map);
	kbase_vunmap_phy_pages(kctx, alloc->permanent_map);
	kfree(alloc->permanent_map);

	alloc->permanent_map = NULL;
}

void *kbase_phy_alloc_mapping_get(struct kbase_context *kctx, u64 gpu_addr,
				  struct kbase_vmap_struct **out_kern_mapping)
{
	struct kbase_va_region *reg;
	void *kern_mem_ptr = NULL;
	struct kbase_vmap_struct *kern_mapping;
	u64 mapping_offset;

	WARN_ON(!kctx);
	WARN_ON(!out_kern_mapping);

	kbase_gpu_vm_lock(kctx);

	reg = kbase_find_event_mem_region(kctx, gpu_addr);

	if (!reg) {
		reg = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);
	}

	if (kbase_is_region_invalid_or_free(reg))
		goto out_unlock;

	kern_mapping = reg->cpu_alloc->permanent_map;
	if (kern_mapping == NULL)
		goto out_unlock;

	mapping_offset = gpu_addr - (reg->start_pfn << PAGE_SHIFT);

	WARN_ON(reg->cpu_alloc != kern_mapping->cpu_alloc);
	WARN_ON(reg->gpu_alloc != kern_mapping->gpu_alloc);
	(void)kbase_mem_phy_alloc_get(kern_mapping->cpu_alloc);
	(void)kbase_mem_phy_alloc_get(kern_mapping->gpu_alloc);

	kern_mem_ptr = (void *)(uintptr_t)((uintptr_t)kern_mapping->addr + mapping_offset);
	*out_kern_mapping = kern_mapping;
out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return kern_mem_ptr;
}

void kbase_phy_alloc_mapping_put(struct kbase_context *kctx, struct kbase_vmap_struct *kern_mapping)
{
	WARN_ON(!kctx);
	WARN_ON(!kern_mapping);

	WARN_ON(kctx != kern_mapping->cpu_alloc->imported.native.kctx);
	WARN_ON(kern_mapping != kern_mapping->cpu_alloc->permanent_map);

	kbase_mem_phy_alloc_put(kern_mapping->cpu_alloc);
	kbase_mem_phy_alloc_put(kern_mapping->gpu_alloc);

}

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx, u64 va_pages, u64 commit_pages,
					u64 extension, u64 *flags, u64 *gpu_va,
					enum kbase_caller_mmu_sync_info mmu_sync_info, enum kbase_memory_category category)
#else
struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx, u64 va_pages, u64 commit_pages,
					u64 extension, u64 *flags, u64 *gpu_va,
					enum kbase_caller_mmu_sync_info mmu_sync_info)
#endif
{
	struct kbase_va_region *reg;
	enum kbase_memory_zone zone;
	struct device *dev;
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	bool have_pbha_hint = false;
#endif

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(flags);
	KBASE_DEBUG_ASSERT(gpu_va);

	dev = kctx->kbdev->dev;
	dev_dbg(dev, "Allocating %lld va_pages, %lld commit_pages, %lld extension, 0x%llX flags\n",
		va_pages, commit_pages, extension, *flags);

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	if ((*gpu_va == 0xF0) && !(*flags & BASE_MEM_FIXED)) {
		have_pbha_hint = true;
		*gpu_va = 0;
	}
#endif

#if MALI_USE_CSF
	if (!(*flags & BASE_MEM_FIXED))
		*gpu_va = 0;
#else
	if (!(*flags & BASE_MEM_FLAG_MAP_FIXED))
		*gpu_va = 0;
#endif
	else
		dev_dbg(dev, "Keeping requested GPU VA of 0x%llx\n", (unsigned long long)*gpu_va);

	if (!kbase_check_alloc_flags(*flags)) {
		dev_warn(dev, "%s called with bad flags (%llx)", __func__,
			 (unsigned long long)*flags);
		goto bad_flags;
	}

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbase_ctx_flag(kctx, KCTX_INFINITE_CACHE))) {

		*flags &= ~(BASE_MEM_COHERENT_SYSTEM_REQUIRED | BASE_MEM_COHERENT_SYSTEM);
	}
#endif

	if ((*flags & BASE_MEM_UNCACHED_GPU) != 0 &&
	    (*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0) {

		*flags &= ~BASE_MEM_COHERENT_SYSTEM_REQUIRED;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0 &&
	    !kbase_device_is_cpu_coherent(kctx->kbdev)) {
		dev_warn(dev, "%s call required coherent mem when unavailable", __func__);
		goto bad_flags;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM) != 0 &&
	    !kbase_device_is_cpu_coherent(kctx->kbdev)) {

		*flags &= ~BASE_MEM_COHERENT_SYSTEM;
	}

	if (kbase_check_alloc_sizes(kctx, *flags, va_pages, commit_pages, extension))
		goto bad_sizes;

	if (unlikely(fully_backed_gpf_memory)) {
		if (*flags & BASE_MEM_GROW_ON_GPF)
			commit_pages = va_pages;
	}

	if (*flags & BASE_MEM_SAME_VA)
		zone = SAME_VA_ZONE;
#if MALI_USE_CSF

	else if (*flags & (BASE_MEM_FIXED | BASE_MEM_FIXABLE)) {
		if (*flags & BASE_MEM_PROT_GPU_EX) {
			zone = EXEC_FIXED_VA_ZONE;
		} else {
			zone = FIXED_VA_ZONE;
		}
	}
#endif
	else if ((*flags & BASE_MEM_PROT_GPU_EX) && kbase_has_exec_va_zone(kctx)) {
		zone = EXEC_VA_ZONE;
	} else {
		zone = CUSTOM_VA_ZONE;
	}

	reg = kbase_ctx_alloc_free_region(kctx, zone, PFN_DOWN(*gpu_va), va_pages);

	if (!reg) {
		dev_err(dev, "Failed to allocate free region");
		goto no_region;
	}

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	if (kbase_reg_prepare_native(reg, kctx, kbase_mem_group_id_get(*flags)) != 0) {
		dev_err(dev, "Failed to prepare region");
		goto prepare_failed;
	}

	if (unlikely(reg->cpu_alloc != reg->gpu_alloc))
		*flags |= BASE_MEM_KERNEL_SYNC;

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
	if ((*flags & BASE_MEM_COHERENT_SYSTEM) != 0 &&
		kbase_device_is_cpu_coherent(kctx->kbdev) &&
		kctx->kbdev->acp_dbg_force_sync)
		*flags |= BASE_MEM_KERNEL_SYNC;
#endif

	if (reg->flags & KBASE_REG_CPU_CACHED)
		*flags |= BASE_MEM_CACHED_CPU;
	else
		*flags &= ~BASE_MEM_CACHED_CPU;

	if (*flags & BASE_MEM_GROW_ON_GPF) {

		reg->extension = extension;
#if !MALI_USE_CSF
	} else if (*flags & BASE_MEM_TILER_ALIGN_TOP) {
		reg->extension = extension;
#endif
	} else {
		reg->extension = 0;
	}

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	reg->gpu_alloc->category = reg->cpu_alloc->category = ((*flags & BASE_MEM_GROW_ON_GPF) && (KBASE_MEM_JIT != category)) ? KBASE_MEM_GROW : category;
#endif
	if (kbase_alloc_phy_pages(reg, va_pages, commit_pages) != 0) {
		dev_warn(dev, "Failed to allocate %lld pages (va_pages=%lld)",
			 (unsigned long long)commit_pages, (unsigned long long)va_pages);
		goto no_mem;
	}
	reg->initial_commit = commit_pages;

	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	if (reg->flags & KBASE_REG_PERMANENT_KERNEL_MAPPING) {

		int err = kbase_phy_alloc_mapping_init(kctx, reg, va_pages, commit_pages);
		if (err < 0) {
			kbase_gpu_vm_unlock_with_pmode_sync(kctx);
			goto no_kern_mapping;
		}
	}

	if (*flags & BASE_MEM_SAME_VA) {
		unsigned long cookie, cookie_nr;
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
		if (have_pbha_hint) {
			reg->pbha_8bit = pdma_request_extended_pbha(kctx->id);
			reg->isFirstDmaBuf = false;
			reg->isImportedMemory = false;
		}
#endif

		if (bitmap_empty(kctx->cookies, BITS_PER_LONG)) {
			dev_err(dev, "No cookies available for allocation!");
			kbase_gpu_vm_unlock_with_pmode_sync(kctx);
			goto no_cookie;
		}

		cookie_nr = find_first_bit(kctx->cookies, BITS_PER_LONG);
		bitmap_clear(kctx->cookies, cookie_nr, 1);
		BUG_ON(kctx->pending_regions[cookie_nr]);
		kctx->pending_regions[cookie_nr] = reg;

		cookie = cookie_nr + PFN_DOWN(BASE_MEM_COOKIE_BASE);
		cookie <<= PAGE_SHIFT;

		*gpu_va = (u64)cookie;
	} else   {
		size_t align = 1;

		if (kbase_is_large_pages_enabled()) {

			if (kctx->kbdev->gpu_props.mmu.va_bits > 33) {
				if (va_pages >= NUM_PAGES_IN_2MB_LARGE_PAGE)
					align = NUM_PAGES_IN_2MB_LARGE_PAGE;
			}
			if (*gpu_va)
				align = 1;
#if !MALI_USE_CSF
			if (reg->flags & KBASE_REG_TILER_ALIGN_TOP)
				align = 1;
#endif
		}
		if (kbase_gpu_mmap(kctx, reg, *gpu_va, va_pages, align, mmu_sync_info) != 0) {
			dev_warn(dev, "Failed to map memory on GPU");
			kbase_gpu_vm_unlock_with_pmode_sync(kctx);
			goto no_mmap;
		}

		*gpu_va = reg->start_pfn << PAGE_SHIFT;
	}

#if MALI_JIT_PRESSURE_LIMIT_BASE
	if (*flags & BASEP_MEM_PERFORM_JIT_TRIM) {
		kbase_jit_done_phys_increase(kctx, commit_pages);

		mutex_lock(&kctx->jit_evict_lock);
		WARN_ON(!list_empty(&reg->jit_node));
		list_add(&reg->jit_node, &kctx->jit_active_head);
		mutex_unlock(&kctx->jit_evict_lock);
	}
#endif

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);

#if MALI_USE_CSF
	if (*flags & BASE_MEM_FIXABLE)
		atomic64_inc(&kctx->num_fixable_allocs);
	else if (*flags & BASE_MEM_FIXED)
		atomic64_inc(&kctx->num_fixed_allocs);
#endif

	return reg;

no_mmap:
no_cookie:
no_kern_mapping:
no_mem:
#if MALI_JIT_PRESSURE_LIMIT_BASE
	if (*flags & BASEP_MEM_PERFORM_JIT_TRIM) {
		kbase_gpu_vm_lock(kctx);
		kbase_jit_done_phys_increase(kctx, commit_pages);
		kbase_gpu_vm_unlock(kctx);
	}
#endif
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
invalid_flags:
prepare_failed:
	kfree(reg);
no_region:
bad_sizes:
bad_flags:
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_mem_alloc);

int kbase_mem_query(struct kbase_context *kctx, u64 gpu_addr, u64 query, u64 *const out)
{
	struct kbase_va_region *reg;
	int ret = -EINVAL;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(out);

	if (gpu_addr & ~PAGE_MASK) {
		dev_warn(kctx->kbdev->dev, "mem_query: gpu_addr: passed parameter is invalid");
		return -EINVAL;
	}

	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (kbase_is_region_invalid_or_free(reg))
		goto out_unlock;

	switch (query) {
	case KBASE_MEM_QUERY_COMMIT_SIZE:
		if (reg->cpu_alloc->type != KBASE_MEM_TYPE_ALIAS) {
			*out = kbase_reg_current_backed_size(reg);
		} else {
			size_t i;
			struct kbase_aliased *aliased;
			*out = 0;
			aliased = reg->cpu_alloc->imported.alias.aliased;
			for (i = 0; i < reg->cpu_alloc->imported.alias.nents; i++)
				*out += aliased[i].length;
		}
		break;
	case KBASE_MEM_QUERY_VA_SIZE:
		*out = reg->nr_pages;
		break;
	case KBASE_MEM_QUERY_FLAGS: {
		*out = 0;
		if (KBASE_REG_CPU_WR & reg->flags)
			*out |= BASE_MEM_PROT_CPU_WR;
		if (KBASE_REG_CPU_RD & reg->flags)
			*out |= BASE_MEM_PROT_CPU_RD;
		if (KBASE_REG_CPU_CACHED & reg->flags)
			*out |= BASE_MEM_CACHED_CPU;
		if (KBASE_REG_GPU_WR & reg->flags)
			*out |= BASE_MEM_PROT_GPU_WR;
		if (KBASE_REG_GPU_RD & reg->flags)
			*out |= BASE_MEM_PROT_GPU_RD;
		if (!(KBASE_REG_GPU_NX & reg->flags))
			*out |= BASE_MEM_PROT_GPU_EX;
		if (KBASE_REG_SHARE_BOTH & reg->flags)
			*out |= BASE_MEM_COHERENT_SYSTEM;
		if (KBASE_REG_SHARE_IN & reg->flags)
			*out |= BASE_MEM_COHERENT_LOCAL;
		if (mali_kbase_supports_mem_dont_need(kctx->api_version)) {
			if (KBASE_REG_DONT_NEED & reg->flags)
				*out |= BASE_MEM_DONT_NEED;
		}
		if (mali_kbase_supports_mem_grow_on_gpf(kctx->api_version)) {

			if (KBASE_REG_PF_GROW & reg->flags)
				*out |= BASE_MEM_GROW_ON_GPF;
		}
		if (mali_kbase_supports_mem_protected(kctx->api_version)) {

			if (KBASE_REG_PROTECTED & reg->flags)
				*out |= BASE_MEM_PROTECTED;
		}
#if !MALI_USE_CSF
		if (KBASE_REG_TILER_ALIGN_TOP & reg->flags)
			*out |= BASE_MEM_TILER_ALIGN_TOP;
#endif
		if (!(KBASE_REG_GPU_CACHED & reg->flags))
			*out |= BASE_MEM_UNCACHED_GPU;
#if MALI_USE_CSF
		if (KBASE_REG_CSF_EVENT & reg->flags)
			*out |= BASE_MEM_CSF_EVENT;
		if ((kbase_bits_to_zone(reg->flags) == FIXED_VA_ZONE) ||
		    (kbase_bits_to_zone(reg->flags) == EXEC_FIXED_VA_ZONE)) {
			if (KBASE_REG_FIXED_ADDRESS & reg->flags)
				*out |= BASE_MEM_FIXED;
			else
				*out |= BASE_MEM_FIXABLE;
		}
#endif
		if (KBASE_REG_GPU_VA_SAME_4GB_PAGE & reg->flags)
			*out |= BASE_MEM_GPU_VA_SAME_4GB_PAGE;
		if (mali_kbase_supports_mem_import_sync_on_map_unmap(kctx->api_version)) {
			if (reg->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
				if (reg->gpu_alloc->imported.umm.need_sync)
					*out |= BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP;
			}
		}
		if (mali_kbase_supports_mem_kernel_sync(kctx->api_version)) {
			if (unlikely(reg->cpu_alloc != reg->gpu_alloc))
				*out |= BASE_MEM_KERNEL_SYNC;
		}
		if (mali_kbase_supports_mem_same_va(kctx->api_version)) {
			if (kbase_bits_to_zone(reg->flags) == SAME_VA_ZONE) {

				if (reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM &&
				    reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_USER_BUF) {
					*out |= BASE_MEM_SAME_VA;
				}
			}
		}

		*out |= kbase_mem_group_id_set(reg->cpu_alloc->group_id);

		WARN(*out & ~BASE_MEM_FLAGS_QUERYABLE, "BASE_MEM_FLAGS_QUERYABLE needs updating\n");
		*out &= BASE_MEM_FLAGS_QUERYABLE;
		break;
	}
	default:
		*out = 0;
		goto out_unlock;
	}

	ret = 0;

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;
}

static unsigned long kbase_mem_evictable_reclaim_count_objects(struct shrinker *s,
							       struct shrink_control *sc)
{
	struct kbase_context *kctx =
		KBASE_GET_KBASE_DATA_FROM_SHRINKER(s, struct kbase_context, reclaim);

	int evict_nents = atomic_read(&kctx->evict_nents);
	unsigned long nr_freeable_items;
#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	struct kbase_device *kbdev;
	u64 jit_reclaim_timeout_ns = 0;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	kbdev = kctx->kbdev;
	jit_reclaim_timeout_ns = kbdev->jit_reclaim_timeout_ms * 1000000ULL;
#endif

#if !IS_ENABLED(CONFIG_MALI_MTK_PREVENT_PRINTK_TOO_MUCH)
	WARN(in_atomic(),
	     "Shrinker called in atomic context. The caller must use GFP_ATOMIC or similar, then Shrinkers must not be called. gfp_mask==%x\n",
	     sc->gfp_mask);
#endif

	if (unlikely(evict_nents < 0)) {
		dev_err(kctx->kbdev->dev, "invalid evict_nents(%d)", evict_nents);
		nr_freeable_items = 0;
	} else {
		nr_freeable_items = (unsigned long)evict_nents;
	}

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	if (mutex_trylock(&kctx->jit_evict_lock)) {
		struct kbase_mem_phy_alloc *alloc, *tmp;
		u64 now_ns;

		now_ns = ktime_get_raw_ns();
		list_for_each_entry_safe(alloc, tmp, &kctx->evict_list, evict_node) {
			if (!alloc->reg)
				continue;

			if (alloc->reg->last_used_ts == 0 || now_ns - alloc->reg->last_used_ts > jit_reclaim_timeout_ns)
				continue;

			pr_debug("mem_evictable count_object: tgid=%d, jit_usage_id=%u, total=%lu, exclude=%lu",
				 kctx->tgid, alloc->reg->jit_usage_id,
				 nr_freeable_items, alloc->reg->gpu_alloc->nents);

			nr_freeable_items -= alloc->reg->gpu_alloc->nents;
		}

		mutex_unlock(&kctx->jit_evict_lock);
	} else {
		pr_debug("mem_evictable count_object: tgid=%d, total=%lu, exclude=%lu",
			 kctx->tgid, nr_freeable_items, nr_freeable_items);
		nr_freeable_items = 0;
	}

	trace_mali_mem_evictable_count(kctx, nr_freeable_items);
#endif

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	if (nr_freeable_items == 0)
		nr_freeable_items = SHRINK_EMPTY;
#endif

	return nr_freeable_items;
}

static unsigned long kbase_mem_evictable_reclaim_scan_objects(struct shrinker *s,
							      struct shrink_control *sc)
{
	struct kbase_context *kctx;
	struct kbase_mem_phy_alloc *alloc;
	struct kbase_mem_phy_alloc *tmp;
	unsigned long freed = 0;
#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	struct kbase_device *kbdev;
	u64 jit_reclaim_timeout_ns = 0;
	u64 now_ns;
#endif

	kctx = KBASE_GET_KBASE_DATA_FROM_SHRINKER(s, struct kbase_context, reclaim);
#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	kbdev = kctx->kbdev;
	jit_reclaim_timeout_ns = kbdev->jit_reclaim_timeout_ms * 1000000ULL;
	now_ns = ktime_get_raw_ns();
#endif

#if MALI_USE_CSF
	if (!down_read_trylock(&kctx->kbdev->csf.mmu_sync_sem)) {
		dev_warn(kctx->kbdev->dev,
			 "Can't shrink GPU memory when P.Mode entrance is in progress");
		return 0;
	}
#endif
	mutex_lock(&kctx->jit_evict_lock);

	list_for_each_entry_safe(alloc, tmp, &kctx->evict_list, evict_node) {
		int err;

		if (!alloc->reg)
			continue;

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
		if (alloc->reg->last_used_ts != 0 && now_ns - alloc->reg->last_used_ts <= jit_reclaim_timeout_ns) {
			trace_mali_mem_evictable_reclaim(kctx, alloc->reg->jit_usage_id,
				now_ns, alloc->reg->last_used_ts, true);

			continue;
		} else {
			trace_mali_mem_evictable_reclaim(kctx, alloc->reg->jit_usage_id,
				now_ns, alloc->reg->last_used_ts, false);
		}
#endif

		err = kbase_mem_shrink_gpu_mapping(kctx, alloc->reg, 0, alloc->nents);

		if (err != 0)
			continue;

		alloc->evicted = alloc->nents;

		kbase_free_phy_pages_helper(alloc, alloc->evicted);
		freed += alloc->evicted;
		WARN_ON(atomic_sub_return(alloc->evicted, &kctx->evict_nents) < 0);
		list_del_init(&alloc->evict_node);

		kbase_jit_backing_lost(alloc->reg);

		if (freed > sc->nr_to_scan)
			break;
	}

	mutex_unlock(&kctx->jit_evict_lock);
#if MALI_USE_CSF
	up_read(&kctx->kbdev->csf.mmu_sync_sem);
#endif
	return freed;
}

int kbase_mem_evictable_init(struct kbase_context *kctx)
{
	struct shrinker *reclaim;

	INIT_LIST_HEAD(&kctx->evict_list);
	mutex_init(&kctx->jit_evict_lock);

	reclaim = KBASE_INIT_RECLAIM(kctx, reclaim, "mali-mem");
	if (!reclaim)
		return -ENOMEM;
	KBASE_SET_RECLAIM(kctx, reclaim, reclaim);

	reclaim->count_objects = kbase_mem_evictable_reclaim_count_objects;
	reclaim->scan_objects = kbase_mem_evictable_reclaim_scan_objects;
	reclaim->seeks = DEFAULT_SEEKS;

	KBASE_REGISTER_SHRINKER(reclaim, "mali-mem", kctx);

	return 0;
}

void kbase_mem_evictable_deinit(struct kbase_context *kctx)
{
	KBASE_UNREGISTER_SHRINKER(kctx->reclaim);
}

void kbase_mem_evictable_mark_reclaim(struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_context *kctx = alloc->imported.native.kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	int __maybe_unused new_page_count;

	kbase_process_page_usage_dec(kctx, alloc->nents);
	new_page_count = atomic_sub_return(alloc->nents, &kctx->used_pages);
	atomic_sub(alloc->nents, &kctx->kbdev->memdev.used_pages);

	KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, kctx->id, (u64)new_page_count);
	kbase_trace_gpu_mem_usage_dec(kbdev, kctx, alloc->nents);
#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	kbase_trace_free_pages(kbdev->id, kctx, alloc->nents, (size_t)alloc->pages, alloc->category);
#endif
}

static void kbase_mem_evictable_unmark_reclaim(struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_context *kctx = alloc->imported.native.kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	int __maybe_unused new_page_count;

	new_page_count = atomic_add_return(alloc->nents, &kctx->used_pages);
	atomic_add(alloc->nents, &kctx->kbdev->memdev.used_pages);

	kbase_process_page_usage_inc(kctx, alloc->nents);

	KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, kctx->id, (u64)new_page_count);
	kbase_trace_gpu_mem_usage_inc(kbdev, kctx, alloc->nents);
#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	kbase_trace_alloc_pages(kbdev->id, kctx, alloc->nents, (size_t)alloc->pages, alloc->category);
#endif
}

void kbase_mem_evictable_make(struct kbase_mem_phy_alloc *gpu_alloc)
{
	struct kbase_context *kctx = gpu_alloc->imported.native.kctx;

	lockdep_assert_held(&kctx->reg_lock);

	kbase_mem_shrink_cpu_mapping(kctx, gpu_alloc->reg, 0, gpu_alloc->nents);

	mutex_lock(&kctx->jit_evict_lock);

	WARN_ON(!list_empty(&gpu_alloc->evict_node));

	list_add(&gpu_alloc->evict_node, &kctx->evict_list);
	atomic_add(gpu_alloc->nents, &kctx->evict_nents);

	if (kbase_is_page_migration_enabled())
		kbase_set_phy_alloc_page_status(kctx, gpu_alloc, NOT_MOVABLE);

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	gpu_alloc->reg->last_used_ts = 0;
#endif

	mutex_unlock(&kctx->jit_evict_lock);
	kbase_mem_evictable_mark_reclaim(gpu_alloc);

	gpu_alloc->reg->flags |= KBASE_REG_DONT_NEED;
}

bool kbase_mem_evictable_unmake(struct kbase_mem_phy_alloc *gpu_alloc)
{
	struct kbase_context *kctx = gpu_alloc->imported.native.kctx;
	int err = 0;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	lockdep_assert_held(&kctx->reg_lock);

	mutex_lock(&kctx->jit_evict_lock);

	WARN_ON(atomic_sub_return(gpu_alloc->nents, &kctx->evict_nents) < 0);
	list_del_init(&gpu_alloc->evict_node);
	mutex_unlock(&kctx->jit_evict_lock);

	if (gpu_alloc->evicted == 0) {

		kbase_mem_evictable_unmark_reclaim(gpu_alloc);
	} else {

		if (gpu_alloc->reg) {

			err = kbase_alloc_phy_pages_helper(gpu_alloc, gpu_alloc->evicted);

			if (!err)
				err = kbase_mem_grow_gpu_mapping(
					kctx, gpu_alloc->reg, gpu_alloc->evicted, 0, mmu_sync_info);

			gpu_alloc->evicted = 0;

			if (kbase_is_page_migration_enabled())
				kbase_set_phy_alloc_page_status(kctx, gpu_alloc, ALLOCATED_MAPPED);
		}
	}

	if (gpu_alloc->reg)
		gpu_alloc->reg->flags &= ~KBASE_REG_DONT_NEED;

	return (err == 0);
}

static int kbase_mem_flags_change_imported_umm(struct kbase_context *kctx, unsigned int flags,
					       struct kbase_va_region *reg)
{
	unsigned int real_flags = 0;
	unsigned int new_flags = 0;
	int ret = -EINVAL;

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM)
		return ret;

	if (flags & ~(BASE_MEM_COHERENT_SYSTEM | BASE_MEM_COHERENT_LOCAL))
		return ret;

	if ((BASE_MEM_COHERENT_SYSTEM & flags) && !kbase_device_is_cpu_coherent(kctx->kbdev))
		return ret;

	if (!(reg->flags & KBASE_REG_GPU_CACHED))
		return 0;

	if (BASE_MEM_COHERENT_SYSTEM & flags)
		real_flags |= KBASE_REG_SHARE_BOTH;
	else if (BASE_MEM_COHERENT_LOCAL & flags)
		real_flags |= KBASE_REG_SHARE_IN;

	if (real_flags == (reg->flags & (KBASE_REG_SHARE_IN | KBASE_REG_SHARE_BOTH)))
		return 0;

	new_flags = reg->flags & ~(KBASE_REG_SHARE_IN | KBASE_REG_SHARE_BOTH);
	new_flags |= real_flags;

	if (IS_ENABLED(CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND)) {

		WARN_ON(reg->gpu_alloc->imported.umm.current_mapping_usage_count);
		ret = 0;
	} else if (reg->gpu_alloc->imported.umm.current_mapping_usage_count) {

		dev_dbg(kctx->kbdev->dev, "Updating page tables on mem flag change\n");
		ret = kbase_mmu_update_pages(kctx, reg->start_pfn, kbase_get_gpu_phy_pages(reg),
					     kbase_reg_current_backed_size(reg), new_flags,
					     reg->gpu_alloc->group_id);
		if (ret)
			dev_warn(kctx->kbdev->dev,
				 "Failed to update GPU page tables on flag change: %d\n", ret);
	} else
		WARN_ON(!reg->gpu_alloc->imported.umm.current_mapping_usage_count);

	if (!ret)
		reg->flags = new_flags;

	return ret;
}

static int kbase_mem_flags_change_native(struct kbase_context *kctx, unsigned int flags,
					 struct kbase_va_region *reg)
{
	bool kbase_reg_dont_need_flag = (KBASE_REG_DONT_NEED & reg->flags);
	bool requested_dont_need_flag = (BASE_MEM_DONT_NEED & flags);
	int ret = 0;

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
		return -EINVAL;

	if (kbase_reg_dont_need_flag ^ requested_dont_need_flag) {

		if (atomic64_read(&reg->gpu_alloc->gpu_mappings) > 1)
			return -EINVAL;

		if (atomic_read(&reg->cpu_alloc->kernel_mappings) > 0)
			return -EINVAL;

		if (requested_dont_need_flag)
			kbase_mem_evictable_make(reg->gpu_alloc);
		else
			ret = kbase_mem_evictable_unmake(reg->gpu_alloc) ? 0 : -ENOMEM;
	}

	return ret;
}

int kbase_mem_flags_change(struct kbase_context *kctx, u64 gpu_addr, unsigned int flags,
			   unsigned int mask)
{
	struct kbase_va_region *reg;
	int ret = -EINVAL;

	KBASE_DEBUG_ASSERT(kctx);

	if (!gpu_addr)
		return ret;

	if ((gpu_addr & ~PAGE_MASK) && (gpu_addr >= PAGE_SIZE))
		return ret;

	flags &= mask;

	down_write(kbase_mem_get_process_mmap_lock());
	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (kbase_is_region_invalid_or_free(reg))
		goto out_unlock;

	if (kbase_va_region_is_no_user_free(reg))
		goto out_unlock;

	switch (reg->gpu_alloc->type) {
	case KBASE_MEM_TYPE_NATIVE: {
		if ((flags & ~(BASE_MEM_FLAGS_MODIFIABLE_NATIVE)) ||
		    (mask & ~(BASE_MEM_FLAGS_MODIFIABLE_NATIVE))) {
			ret = -EINVAL;
			break;
		}

		ret = kbase_mem_flags_change_native(kctx, flags, reg);
		break;
	}
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		if ((flags & ~(BASE_MEM_FLAGS_MODIFIABLE_IMPORTED_UMM)) ||
		    (mask & ~(BASE_MEM_FLAGS_MODIFIABLE_IMPORTED_UMM))) {
			ret = -EINVAL;
			break;
		}

		ret = kbase_mem_flags_change_imported_umm(kctx, flags, reg);
		break;
	}
	default: {

		ret = -EINVAL;
		break;
	}
	}

out_unlock:
	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
	up_write(kbase_mem_get_process_mmap_lock());

	return ret;
}

#define KBASE_MEM_IMPORT_HAVE_PAGES (1UL << BASE_MEM_FLAGS_NR_BITS)

int kbase_sync_imported_umm(struct kbase_context *kctx, struct kbase_va_region *reg,
			    enum kbase_sync_type sync_fn)
{
	int ret = -EINVAL;
	struct dma_buf __maybe_unused *dma_buf;
	enum dma_data_direction dir = DMA_BIDIRECTIONAL;

	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(reg->cpu_alloc != reg->gpu_alloc);

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM)
		return ret;

	if (IS_ENABLED(CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND) &&
	    !reg->gpu_alloc->imported.umm.current_mapping_usage_count)
		return ret;

	dma_buf = reg->gpu_alloc->imported.umm.dma_buf;

	switch (sync_fn) {
	case KBASE_SYNC_TO_DEVICE:
		dev_dbg(kctx->kbdev->dev, "Syncing imported buffer at GPU VA %llx to GPU\n",
			reg->start_pfn);
#ifdef KBASE_MEM_ION_SYNC_WORKAROUND
		if (!WARN_ON(!reg->gpu_alloc->imported.umm.dma_attachment)) {
			struct dma_buf_attachment *attachment =
				reg->gpu_alloc->imported.umm.dma_attachment;
			struct sg_table *sgt = reg->gpu_alloc->imported.umm.sgt;

			dma_sync_sg_for_device(attachment->dev, sgt->sgl, (int)sgt->nents, dir);
			ret = 0;
		}
#else
		ret = dma_buf_end_cpu_access(dma_buf, dir);
#endif
		break;
	case KBASE_SYNC_TO_CPU:
		dev_dbg(kctx->kbdev->dev, "Syncing imported buffer at GPU VA %llx to CPU\n",
			reg->start_pfn);
#ifdef KBASE_MEM_ION_SYNC_WORKAROUND
		if (!WARN_ON(!reg->gpu_alloc->imported.umm.dma_attachment)) {
			struct dma_buf_attachment *attachment =
				reg->gpu_alloc->imported.umm.dma_attachment;
			struct sg_table *sgt = reg->gpu_alloc->imported.umm.sgt;

			dma_sync_sg_for_cpu(attachment->dev, sgt->sgl, (int)sgt->nents, dir);
			ret = 0;
		}
#else
		ret = dma_buf_begin_cpu_access(dma_buf, dir);
#endif
		break;
	}

	if (unlikely(ret))
		dev_warn(kctx->kbdev->dev, "Failed to sync mem region %pK at GPU VA %llx: %d\n",
			 reg, reg->start_pfn, ret);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			 "Failed to sync mem region %pK at GPU VA %llx: %d\n",
			 reg, reg->start_pfn, ret);
#endif

	return ret;
}

static void kbase_mem_umm_unmap_attachment(struct kbase_context *kctx,
					   struct kbase_mem_phy_alloc *alloc)
{
	struct tagged_addr *pa = alloc->pages;

#if (KERNEL_VERSION(6, 1, 55) <= LINUX_VERSION_CODE)
	dma_buf_unmap_attachment_unlocked(alloc->imported.umm.dma_attachment,
					  alloc->imported.umm.sgt, DMA_BIDIRECTIONAL);
#else
	dma_buf_unmap_attachment(alloc->imported.umm.dma_attachment, alloc->imported.umm.sgt,
				 DMA_BIDIRECTIONAL);
#endif
	alloc->imported.umm.sgt = NULL;

	kbase_remove_dma_buf_usage(kctx, alloc);

	memset(pa, 0xff, sizeof(*pa) * alloc->nents);
	alloc->nents = 0;
}

static int kbase_mem_umm_map_attachment(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	struct sg_table *sgt;
	struct scatterlist *s;
	int i;
	struct tagged_addr *pa;
	int err;
	size_t count = 0;
	struct kbase_mem_phy_alloc *alloc = reg->gpu_alloc;

	WARN_ON_ONCE(alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM);
	WARN_ON_ONCE(alloc->imported.umm.sgt);

#if (KERNEL_VERSION(6, 1, 55) <= LINUX_VERSION_CODE)
	sgt = dma_buf_map_attachment_unlocked(alloc->imported.umm.dma_attachment,
					      DMA_BIDIRECTIONAL);
#else
	sgt = dma_buf_map_attachment(alloc->imported.umm.dma_attachment, DMA_BIDIRECTIONAL);
#endif
	if (IS_ERR_OR_NULL(sgt))
		return -EINVAL;

	alloc->imported.umm.sgt = sgt;

	pa = kbase_get_gpu_phy_pages(reg);

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		size_t j, pages = PFN_UP(sg_dma_len(s));

#if IS_ENABLED(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM) && IS_ENABLED(CONFIG_MTK_GZ_KREE) && IS_ENABLED(CONFIG_MALI_MTK_PROTECTED_PATCH)
		uint64_t phy_addr = 0;

		if (reg->flags & KBASE_REG_PROTECTED) {
			struct dma_buf *dma_buf = reg->gpu_alloc->imported.umm.dma_buf;
			u64 sec_handle = dmabuf_to_secure_handle(dma_buf);

			if (sec_handle && is_support_secure_handle(dma_buf)) {

#if IS_ENABLED(CONFIG_ARM_FFA_TRANSPORT)
				trusted_mem_ffa_query_pa(&sec_handle, &phy_addr);
#else
				trusted_mem_api_query_pa(0, 0, 0, NULL, &sec_handle, NULL, 0, 0, &phy_addr);
#endif
			} else {

				phy_addr = sg_phys(s);
			}

			if (phy_addr == 0) {
				dev_warn(kctx->kbdev->dev,
					"can't get PA: sec_handle=%llx, phy_addr=%llx\n",
					(unsigned long long)sec_handle,
					(unsigned long long)phy_addr);
				err = -EINVAL;
				goto err_unmap_attachment;
			}
		} else
			phy_addr = sg_phys(s);
#endif

		WARN_ONCE(sg_dma_len(s) & (PAGE_SIZE - 1),
			  "sg_dma_len(s)=%u is not a multiple of PAGE_SIZE\n", sg_dma_len(s));

#if IS_ENABLED(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM) && IS_ENABLED(CONFIG_MTK_GZ_KREE) && IS_ENABLED(CONFIG_MALI_MTK_PROTECTED_PATCH)
		WARN_ONCE(phy_addr & (PAGE_SIZE-1),
		"sg_phys(s)=%llx is not aligned to PAGE_SIZE\n",
		(unsigned long long) phy_addr);

		for (j = 0; (j < pages) && (count < reg->nr_pages); j++, count++)
			*pa++ = as_tagged(phy_addr + (j << PAGE_SHIFT));
#else
		WARN_ONCE(sg_dma_address(s) & (PAGE_SIZE - 1),
			  "sg_dma_address(s)=%llx is not aligned to PAGE_SIZE\n",
			  (unsigned long long)sg_dma_address(s));

		for (j = 0; (j < pages) && (count < reg->nr_pages); j++, count++)
			*pa++ = as_tagged(sg_dma_address(s) + (j << PAGE_SHIFT));
#endif
		WARN_ONCE(j < pages, "sg list from dma_buf_map_attachment > dma_buf->size=%zu\n",
			  alloc->imported.umm.dma_buf->size);
	}

	if (!(reg->flags & KBASE_REG_IMPORT_PAD) &&
	    WARN_ONCE(count < reg->nr_pages,
		      "sg list from dma_buf_map_attachment < dma_buf->size=%zu\n",
		      alloc->imported.umm.dma_buf->size)) {
		err = -EINVAL;
		goto err_unmap_attachment;
	}

	alloc->nents = count;
	kbase_add_dma_buf_usage(kctx, alloc);

	return 0;

err_unmap_attachment:
	kbase_mem_umm_unmap_attachment(kctx, alloc);

	return err;
}

int kbase_mem_umm_map(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	int err;
	struct kbase_mem_phy_alloc *alloc;
	unsigned long gwt_mask = ~0UL;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	lockdep_assert_held(&kctx->reg_lock);

	alloc = reg->gpu_alloc;

	alloc->imported.umm.current_mapping_usage_count++;
	if (alloc->imported.umm.current_mapping_usage_count != 1) {
		if (IS_ENABLED(CONFIG_MALI_DMA_BUF_LEGACY_COMPAT) ||
		    alloc->imported.umm.need_sync) {
			if (!kbase_is_region_invalid_or_free(reg)) {
				err = kbase_sync_imported_umm(kctx, reg, KBASE_SYNC_TO_DEVICE);
				WARN_ON_ONCE(err);
			}
		}
		return 0;
	}

	err = kbase_mem_umm_map_attachment(kctx, reg);
	if (err)
		goto bad_map_attachment;

#ifdef CONFIG_MALI_CINSTR_GWT
	if (kctx->gwt_enabled)
		gwt_mask = ~KBASE_REG_GPU_WR;
#endif

	err = kbase_mmu_insert_pages_skip_status_update(kctx->kbdev, &kctx->mmu, reg->start_pfn,
							kbase_get_gpu_phy_pages(reg),
							kbase_reg_current_backed_size(reg),
							reg->flags & gwt_mask, kctx->as_nr,
							alloc->group_id, mmu_sync_info, NULL);
	if (err)
		goto bad_insert;

	if (reg->flags & KBASE_REG_IMPORT_PAD && !WARN_ON(reg->nr_pages < alloc->nents)) {

		err = kbase_mmu_insert_single_imported_page(
			kctx, reg->start_pfn + alloc->nents, kctx->aliasing_sink_page,
			reg->nr_pages - alloc->nents,
			(reg->flags | KBASE_REG_GPU_RD) & ~KBASE_REG_GPU_WR, KBASE_MEM_GROUP_SINK,
			mmu_sync_info);
		if (err)
			goto bad_pad_insert;
	}

	return 0;

bad_pad_insert:
	kbase_mmu_teardown_imported_pages(kctx->kbdev, &kctx->mmu, reg->start_pfn, alloc->pages,
					  alloc->nents, alloc->nents, kctx->as_nr);
bad_insert:
	kbase_mem_umm_unmap_attachment(kctx, alloc);
bad_map_attachment:
	alloc->imported.umm.current_mapping_usage_count--;

	return err;
}

void kbase_mem_umm_unmap(struct kbase_context *kctx, struct kbase_va_region *reg,
			 struct kbase_mem_phy_alloc *alloc)
{
	alloc->imported.umm.current_mapping_usage_count--;
	if (alloc->imported.umm.current_mapping_usage_count) {
		if (IS_ENABLED(CONFIG_MALI_DMA_BUF_LEGACY_COMPAT) ||
		    alloc->imported.umm.need_sync) {
			if (!kbase_is_region_invalid_or_free(reg)) {
				int err = kbase_sync_imported_umm(kctx, reg, KBASE_SYNC_TO_CPU);
				WARN_ON_ONCE(err);
			}
		}
		return;
	}

	if (!kbase_is_region_invalid_or_free(reg) && reg->gpu_alloc == alloc) {
		int err;

		err = kbase_mmu_teardown_imported_pages(kctx->kbdev, &kctx->mmu, reg->start_pfn,
							alloc->pages, reg->nr_pages, reg->nr_pages,
							kctx->as_nr);
		WARN_ON(err);
	}

	kbase_mem_umm_unmap_attachment(kctx, alloc);
}

static int get_umm_memory_group_id(struct kbase_context *kctx, struct dma_buf *dma_buf)
{
	int group_id = BASE_MEM_GROUP_DEFAULT;

	if (kctx->kbdev->mgm_dev->ops.mgm_get_import_memory_id) {
		struct memory_group_manager_import_data mgm_import_data;

		mgm_import_data.type = MEMORY_GROUP_MANAGER_IMPORT_TYPE_DMA_BUF;
		mgm_import_data.u.dma_buf = dma_buf;

		group_id = kctx->kbdev->mgm_dev->ops.mgm_get_import_memory_id(kctx->kbdev->mgm_dev,
									      &mgm_import_data);
	}

	return group_id;
}

static struct kbase_va_region *kbase_mem_from_umm(struct kbase_context *kctx, int fd, u64 *va_pages,
						  u64 *flags, u32 padding)
{
	struct kbase_va_region *reg;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *dma_attachment;
	enum kbase_memory_zone zone;
	bool shared_zone = false;
	bool need_sync = false;
	int group_id;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma_buf))
		return NULL;

	dma_attachment = dma_buf_attach(dma_buf, kctx->kbdev->dev);
	if (IS_ERR_OR_NULL(dma_attachment)) {
		dma_buf_put(dma_buf);
		return NULL;
	}

	*va_pages = (PAGE_ALIGN(dma_buf->size) >> PAGE_SHIFT) + (u64)padding;
	if (!*va_pages)
		goto dma_buf_exit;

	if (*va_pages > (U64_MAX / PAGE_SIZE))
		goto dma_buf_exit;

	if (!kbase_import_size_is_valid(kctx->kbdev, *va_pages))
		goto dma_buf_exit;

	*flags &= ~BASE_MEM_SAME_VA;

	*flags |= BASE_MEM_CACHED_CPU;

	if (*flags & BASE_MEM_IMPORT_SHARED)
		shared_zone = true;

	if (*flags & BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP)
		need_sync = true;

	if (!kbase_ctx_compat_mode(kctx)) {

		shared_zone = true;
	}

	if (shared_zone) {
		*flags |= BASE_MEM_NEED_MMAP;
		zone = SAME_VA_ZONE;
	} else
		zone = CUSTOM_VA_ZONE;

	reg = kbase_ctx_alloc_free_region(kctx, zone, 0, *va_pages);

	if (!reg)
		goto dma_buf_exit;

	group_id = get_umm_memory_group_id(kctx, dma_buf);

	reg->gpu_alloc = kbase_alloc_create(kctx, *va_pages, KBASE_MEM_TYPE_IMPORTED_UMM, group_id);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto error_out;

	reg->gpu_alloc->nents = 0;

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_GPU_NX;
	reg->flags &= ~KBASE_REG_GROWABLE;

	if (*flags & BASE_MEM_PROTECTED)
		reg->flags |= KBASE_REG_PROTECTED;

	if (padding)
		reg->flags |= KBASE_REG_IMPORT_PAD;

	reg->gpu_alloc->type = KBASE_MEM_TYPE_IMPORTED_UMM;
	reg->gpu_alloc->imported.umm.sgt = NULL;
	reg->gpu_alloc->imported.umm.dma_buf = dma_buf;
	reg->gpu_alloc->imported.umm.dma_attachment = dma_attachment;
	reg->gpu_alloc->imported.umm.current_mapping_usage_count = 0;
	reg->gpu_alloc->imported.umm.need_sync = need_sync;
	reg->gpu_alloc->imported.umm.kctx = kctx;
	reg->extension = 0;

	if (!IS_ENABLED(CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND)) {
		int err;

		reg->gpu_alloc->imported.umm.current_mapping_usage_count = 1;

		err = kbase_mem_umm_map_attachment(kctx, reg);
		if (err) {
			dev_warn(kctx->kbdev->dev, "Failed to map dma-buf %pK on GPU: %d\n",
				 dma_buf, err);
			goto error_out;
		}

		*flags |= KBASE_MEM_IMPORT_HAVE_PAGES;
	}

	return reg;

error_out:
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
no_alloc:
	kfree(reg);

dma_buf_exit:
	dma_buf_detach(dma_buf, dma_attachment);
	dma_buf_put(dma_buf);

	return NULL;
}

u32 kbase_get_cache_line_alignment(struct kbase_device *kbdev)
{
	u32 cpu_cache_line_size = (u32)cache_line_size();
	u32 gpu_cache_line_size = (1UL << kbdev->gpu_props.log2_line_size);

	return ((cpu_cache_line_size > gpu_cache_line_size) ? cpu_cache_line_size :
								    gpu_cache_line_size);
}

static struct kbase_va_region *kbase_mem_from_user_buffer(struct kbase_context *kctx,
							  unsigned long address, unsigned long size,
							  u64 *va_pages, u64 *flags)
{
	struct kbase_va_region *reg;
	enum kbase_memory_zone zone = CUSTOM_VA_ZONE;
	bool shared_zone = false;
	u32 cache_line_alignment = kbase_get_cache_line_alignment(kctx->kbdev);
	struct kbase_alloc_import_user_buf *user_buf;
	int write;
	long faulted_pages;

	if (*flags & BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP)
		return NULL;

	if ((address & (cache_line_alignment - 1)) != 0 ||
	    (size & (cache_line_alignment - 1)) != 0) {
		if (*flags & BASE_MEM_UNCACHED_GPU) {
			dev_warn(
				kctx->kbdev->dev,
				"User buffer is not cache line aligned and marked as GPU uncached\n");
			goto bad_size;
		}

		if (*flags & (BASE_MEM_COHERENT_SYSTEM | BASE_MEM_COHERENT_SYSTEM_REQUIRED)) {

			*flags |= BASE_MEM_COHERENT_SYSTEM_REQUIRED;
		} else {
			dev_warn(
				kctx->kbdev->dev,
				"User buffer is not cache line aligned and no coherency enabled\n");
			goto bad_size;
		}
	}

	*va_pages = (PAGE_ALIGN(address + size) >> PAGE_SHIFT) - PFN_DOWN(address);
	if (!*va_pages)
		goto bad_size;

	if (*va_pages > (UINT64_MAX / PAGE_SIZE))

		goto bad_size;

	if (!kbase_import_size_is_valid(kctx->kbdev, *va_pages))
		goto bad_size;

	*flags &= ~BASE_MEM_SAME_VA;

	if (*flags & BASE_MEM_IMPORT_SHARED)
		shared_zone = true;

	if (!kbase_ctx_compat_mode(kctx)) {

		shared_zone = true;
	}

	if (shared_zone) {
		*flags |= BASE_MEM_NEED_MMAP;
		zone = SAME_VA_ZONE;
	}

	reg = kbase_ctx_alloc_free_region(kctx, zone, 0, *va_pages);
	if (!reg)
		goto no_region;

	reg->gpu_alloc = kbase_alloc_create(kctx, *va_pages, KBASE_MEM_TYPE_IMPORTED_USER_BUF,
					    BASE_MEM_GROUP_DEFAULT);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_GPU_NX;
	reg->flags &= ~KBASE_REG_GROWABLE;

	user_buf = &reg->gpu_alloc->imported.user_buf;

	user_buf->size = size;
	user_buf->address = address;
	user_buf->nr_pages = *va_pages;
	user_buf->mm = current->mm;
	user_buf->current_mapping_usage_count = 0;
	kbase_user_buf_empty_init(reg);
	kbase_mem_mmgrab();
	if (reg->gpu_alloc->properties & KBASE_MEM_PHY_ALLOC_LARGE)
		user_buf->pages = vmalloc(*va_pages * sizeof(struct page *));
	else
		user_buf->pages = kmalloc_array(*va_pages, sizeof(struct page *), GFP_KERNEL);

	if (!user_buf->pages)
		goto no_page_array;

	reg->gpu_alloc->nents = 0;
	reg->extension = 0;

	write = reg->flags & (KBASE_REG_CPU_WR | KBASE_REG_GPU_WR);

	down_read(kbase_mem_get_process_mmap_lock());
	faulted_pages =
		kbase_get_user_pages(address, *va_pages, write ? FOLL_WRITE : 0, NULL, NULL);
	up_read(kbase_mem_get_process_mmap_lock());

	if (faulted_pages != *va_pages)
		goto fault_mismatch;

	if (reg->flags & KBASE_REG_SHARE_BOTH)
		*flags |= KBASE_MEM_IMPORT_HAVE_PAGES;

	return reg;

fault_mismatch:
no_page_array:
invalid_flags:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc_obj:
	kfree(reg);
no_region:
bad_size:
	return NULL;
}

u64 kbase_mem_alias(struct kbase_context *kctx, u64 *flags, u64 stride, u64 nents,
		    struct base_mem_aliasing_info *ai, u64 *num_pages)
{
	struct kbase_va_region *reg;
	u64 gpu_va;
	size_t i;
	bool coherent;
	uint64_t max_stride;
	enum kbase_memory_zone zone;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(flags);
	KBASE_DEBUG_ASSERT(ai);
	KBASE_DEBUG_ASSERT(num_pages);

	*flags &= (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR | BASE_MEM_COHERENT_SYSTEM |
		   BASE_MEM_COHERENT_LOCAL | BASE_MEM_PROT_CPU_RD |
		   BASE_MEM_COHERENT_SYSTEM_REQUIRED);

	if (!(*flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR))) {
		dev_warn(kctx->kbdev->dev, "%s called with bad flags (%llx)", __func__,
			 (unsigned long long)*flags);
		goto bad_flags;
	}
	coherent = (*flags & BASE_MEM_COHERENT_SYSTEM) != 0 ||
		   (*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0;

	if (!stride)
		goto bad_stride;

	if (!nents)
		goto bad_nents;

	max_stride = div64_u64(U64_MAX, nents);

	if (stride > max_stride)
		goto bad_size;

	if ((nents * stride) > (U64_MAX / PAGE_SIZE))

		goto bad_size;

	*num_pages = nents * stride;

	if (!kbase_alias_size_is_valid(kctx->kbdev, *num_pages))
		goto bad_size;

	if (!kbase_ctx_compat_mode(kctx)) {

		zone = SAME_VA_ZONE;
		*flags |= BASE_MEM_NEED_MMAP;
	} else
		zone = CUSTOM_VA_ZONE;

	reg = kbase_ctx_alloc_free_region(kctx, zone, 0, *num_pages);

	if (!reg)
		goto no_reg;

	reg->gpu_alloc = kbase_alloc_create(kctx, 0, KBASE_MEM_TYPE_ALIAS, BASE_MEM_GROUP_DEFAULT);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	reg->gpu_alloc->imported.alias.nents = nents;
	reg->gpu_alloc->imported.alias.stride = stride;
	reg->gpu_alloc->imported.alias.aliased =
		vzalloc(sizeof(*reg->gpu_alloc->imported.alias.aliased) * nents);
	if (!reg->gpu_alloc->imported.alias.aliased)
		goto no_aliased_array;

	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	for (i = 0; i < nents; i++) {
		if (ai[i].handle.basep.handle < BASE_MEM_FIRST_FREE_ADDRESS) {
			if (ai[i].handle.basep.handle != BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE)
				goto bad_handle;
			if (!ai[i].length)
				goto bad_handle;
			if (ai[i].length > stride)
				goto bad_handle;

			reg->gpu_alloc->imported.alias.aliased[i].length = ai[i].length;
		} else {
			struct kbase_va_region *aliasing_reg;
			struct kbase_mem_phy_alloc *alloc;

			aliasing_reg = kbase_region_tracker_find_region_base_address(
				kctx, (ai[i].handle.basep.handle >> PAGE_SHIFT) << PAGE_SHIFT);

			if (kbase_is_region_invalid_or_free(aliasing_reg))
				goto bad_handle;
			if (kbase_is_region_shrinkable(aliasing_reg))
				goto bad_handle;
			if (kbase_va_region_is_no_user_free(aliasing_reg))
				goto bad_handle;

			if (!(aliasing_reg->flags & KBASE_REG_GPU_CACHED))
				goto bad_handle;
			if (!aliasing_reg->gpu_alloc)
				goto bad_handle;
			if (aliasing_reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
				goto bad_handle;
			if (coherent != ((aliasing_reg->flags & KBASE_REG_SHARE_BOTH) != 0))
				goto bad_handle;

			if (!ai[i].length)
				goto bad_handle;
			if (ai[i].length > stride)
				goto bad_handle;

			alloc = aliasing_reg->gpu_alloc;

			if (ai[i].offset > alloc->nents)
				goto bad_handle;
			if (ai[i].offset + ai[i].length > alloc->nents)
				goto bad_handle;

			reg->gpu_alloc->imported.alias.aliased[i].alloc =
				kbase_mem_phy_alloc_get(alloc);
			reg->gpu_alloc->imported.alias.aliased[i].length = ai[i].length;
			reg->gpu_alloc->imported.alias.aliased[i].offset = ai[i].offset;

			kbase_mem_phy_alloc_gpu_mapped(alloc);
		}
	}

	if (!kbase_ctx_compat_mode(kctx)) {

		if (bitmap_empty(kctx->cookies, BITS_PER_LONG)) {
			dev_err(kctx->kbdev->dev, "No cookies available for allocation!");
			goto no_cookie;
		}

		gpu_va = find_first_bit(kctx->cookies, BITS_PER_LONG);
		bitmap_clear(kctx->cookies, gpu_va, 1);
		BUG_ON(kctx->pending_regions[gpu_va]);
		kctx->pending_regions[gpu_va] = reg;

		gpu_va += PFN_DOWN(BASE_MEM_COOKIE_BASE);
		gpu_va <<= PAGE_SHIFT;
	} else {

		if (kbase_gpu_mmap(kctx, reg, 0, *num_pages, 1, mmu_sync_info) != 0) {
			dev_warn(kctx->kbdev->dev, "Failed to map memory on GPU");
			goto no_mmap;
		}

		gpu_va = reg->start_pfn << PAGE_SHIFT;
	}

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags &= ~KBASE_REG_GROWABLE;

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);

	return gpu_va;

no_cookie:
no_mmap:
bad_handle:

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
no_aliased_array:
invalid_flags:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc_obj:
	kfree(reg);
no_reg:
bad_size:
bad_nents:
bad_stride:
bad_flags:
	return 0;
}
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		     void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages, u64 *flags, u8 PBHA, bool isFirstDmaBuf)
#else
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		     void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages, u64 *flags)
#endif
{
	struct kbase_va_region *reg;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(gpu_va);
	KBASE_DEBUG_ASSERT(va_pages);
	KBASE_DEBUG_ASSERT(flags);

	if ((!kbase_ctx_flag(kctx, KCTX_COMPAT)) && kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA))
		*flags |= BASE_MEM_SAME_VA;

	if (!kbase_check_import_flags(*flags)) {
		dev_warn(kctx->kbdev->dev, "%s called with bad flags (%llx)", __func__,
			 (unsigned long long)*flags);
		goto bad_flags;
	}

	if ((*flags & BASE_MEM_UNCACHED_GPU) != 0 &&
	    (*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0) {

		*flags &= ~BASE_MEM_COHERENT_SYSTEM_REQUIRED;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0 &&
	    !kbase_device_is_cpu_coherent(kctx->kbdev)) {
		dev_warn(kctx->kbdev->dev, "%s call required coherent mem when unavailable",
			 __func__);
		goto bad_flags;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM) != 0 &&
	    !kbase_device_is_cpu_coherent(kctx->kbdev)) {

		*flags &= ~BASE_MEM_COHERENT_SYSTEM;
	}
	if (((*flags & BASE_MEM_CACHED_CPU) == 0) && (type == BASE_MEM_IMPORT_TYPE_USER_BUFFER)) {
		dev_warn(kctx->kbdev->dev, "USER_BUFFER must be CPU cached");
		goto bad_flags;
	}
	if ((padding != 0) && (type != BASE_MEM_IMPORT_TYPE_UMM)) {
		dev_warn(kctx->kbdev->dev, "padding is only supported for UMM");
		goto bad_flags;
	}

	switch (type) {
	case BASE_MEM_IMPORT_TYPE_UMM: {
		int fd;

		if (get_user(fd, (int __user *)phandle))
			reg = NULL;
		else
			reg = kbase_mem_from_umm(kctx, fd, va_pages, flags, padding);
	} break;
	case BASE_MEM_IMPORT_TYPE_USER_BUFFER: {
		struct base_mem_import_user_buffer user_buffer;
		void __user *uptr;

		if (copy_from_user(&user_buffer, phandle, sizeof(user_buffer))) {
			reg = NULL;
		} else {
#if IS_ENABLED(CONFIG_COMPAT)
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				uptr = compat_ptr(user_buffer.ptr);
			else
#endif
				uptr = u64_to_user_ptr(user_buffer.ptr);

			reg = kbase_mem_from_user_buffer(kctx, (unsigned long)uptr,
							 user_buffer.length, va_pages, flags);
		}
		break;
	}
	default: {
		reg = NULL;
		break;
	}
	}

	if (!reg)
		goto no_reg;

	kbase_gpu_vm_lock_with_pmode_sync(kctx);

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	reg->pbha_8bit = PBHA;
	reg->isFirstDmaBuf = isFirstDmaBuf;
	reg->isImportedMemory = true;
#endif

	if (*flags & (BASE_MEM_SAME_VA | BASE_MEM_NEED_MMAP)) {

		if (bitmap_empty(kctx->cookies, BITS_PER_LONG))
			goto no_cookie;

		*gpu_va = find_first_bit(kctx->cookies, BITS_PER_LONG);
		bitmap_clear(kctx->cookies, *gpu_va, 1);
		BUG_ON(kctx->pending_regions[*gpu_va]);
		kctx->pending_regions[*gpu_va] = reg;

		*gpu_va += PFN_DOWN(BASE_MEM_COOKIE_BASE);
		*gpu_va <<= PAGE_SHIFT;

	} else if (*flags & KBASE_MEM_IMPORT_HAVE_PAGES) {

		if (kbase_gpu_mmap(kctx, reg, 0, *va_pages, 1, mmu_sync_info) != 0)
			goto no_gpu_va;

		*gpu_va = reg->start_pfn << PAGE_SHIFT;
	} else {

		if (kbase_add_va_region(kctx, reg, 0, *va_pages, 1) != 0)
			goto no_gpu_va;

		*gpu_va = reg->start_pfn << PAGE_SHIFT;
	}

	*flags &= ((1UL << BASE_MEM_FLAGS_NR_BITS) - 1);

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);

	return 0;

no_gpu_va:
no_cookie:
	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
	kfree(reg);
no_reg:
bad_flags:
	*gpu_va = 0;
	*va_pages = 0;
	*flags = 0;
	return -ENOMEM;
}

int kbase_mem_grow_gpu_mapping(struct kbase_context *kctx, struct kbase_va_region *reg,
			       u64 new_pages, u64 old_pages,
			       enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	struct tagged_addr *phy_pages;
	u64 delta = new_pages - old_pages;
	int ret = 0;

	lockdep_assert_held(&kctx->reg_lock);

	phy_pages = kbase_get_gpu_phy_pages(reg);
	ret = kbase_mmu_insert_pages(kctx->kbdev, &kctx->mmu, reg->start_pfn + old_pages,
				     phy_pages + old_pages, delta, reg->flags, kctx->as_nr,
				     reg->gpu_alloc->group_id, mmu_sync_info, reg);

	return ret;
}

void kbase_mem_shrink_cpu_mapping(struct kbase_context *kctx, struct kbase_va_region *reg,
				  u64 new_pages, u64 old_pages)
{
	u64 gpu_va_start = reg->start_pfn;

	if (new_pages == old_pages)

		return;

	unmap_mapping_range(kctx->filp->f_inode->i_mapping,
			    (loff_t)(gpu_va_start + new_pages) << PAGE_SHIFT,
			    (loff_t)(old_pages - new_pages) << PAGE_SHIFT, 1);
}

int kbase_mem_shrink_gpu_mapping(struct kbase_context *const kctx,
				 struct kbase_va_region *const reg, u64 const new_pages,
				 u64 const old_pages)
{
	u64 delta = old_pages - new_pages;
	struct kbase_mem_phy_alloc *alloc = reg->gpu_alloc;
	int ret = 0;

	ret = kbase_mmu_teardown_pages(kctx->kbdev, &kctx->mmu, reg->start_pfn + new_pages,
				       alloc->pages + new_pages, delta, delta, kctx->as_nr);

	return ret;
}

int kbase_mem_commit(struct kbase_context *kctx, u64 gpu_addr, u64 new_pages)
{
	u64 old_pages;
	u64 delta = 0;
	int res = -EINVAL;
	struct kbase_va_region *reg;
	bool read_locked = false;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	KBASE_DEBUG_ASSERT(kctx);

	if (unlikely(gpu_addr == 0)) {
		dev_warn(kctx->kbdev->dev, "kbase:mem_commit: gpu_addr is 0");
		return -EINVAL;
	}

	if (gpu_addr & ~PAGE_MASK) {
		dev_warn(kctx->kbdev->dev,
			 "kbase:mem_commit: gpu_addr: passed parameter is invalid");
		return -EINVAL;
	}

	down_write(kbase_mem_get_process_mmap_lock());
	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (kbase_is_region_invalid_or_free(reg))
		goto out_unlock;

	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
		goto out_unlock;

	if (0 == (reg->flags & KBASE_REG_GROWABLE))
		goto out_unlock;

	if (reg->flags & KBASE_REG_ACTIVE_JIT_ALLOC)
		goto out_unlock;

	if (new_pages > reg->nr_pages)
		goto out_unlock;

	if (atomic64_read(&reg->gpu_alloc->gpu_mappings) > 1)
		goto out_unlock;

	if (atomic_read(&reg->cpu_alloc->kernel_mappings) > 0)
		goto out_unlock;

	if (kbase_is_region_shrinkable(reg))
		goto out_unlock;

	if (kbase_va_region_is_no_user_free(reg))
		goto out_unlock;

	if (unlikely(fully_backed_gpf_memory)) {
		if ((reg->flags & KBASE_REG_PF_GROW) && (reg->nr_pages != reg->initial_commit)) {
			dev_err(kctx->kbdev->dev,
				"GPF region has different physical and VA page size!");
			res = -EINVAL;
		} else {

			res = 0;
		}
		goto out_unlock;
	}

	if (new_pages == reg->gpu_alloc->nents) {
		res = 0;
		goto out_unlock;
	}

	old_pages = kbase_reg_current_backed_size(reg);
	if (new_pages > old_pages) {
		delta = new_pages - old_pages;

		downgrade_write(kbase_mem_get_process_mmap_lock());
		read_locked = true;

		if (kbase_alloc_phy_pages_helper(reg->cpu_alloc, delta) != 0) {
			res = -ENOMEM;
			goto out_unlock;
		}
		if (reg->cpu_alloc != reg->gpu_alloc) {
			if (kbase_alloc_phy_pages_helper(reg->gpu_alloc, delta) != 0) {
				res = -ENOMEM;
				kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
				goto out_unlock;
			}
		}

		res = kbase_mem_grow_gpu_mapping(kctx, reg, new_pages, old_pages, mmu_sync_info);

		if (res) {
			kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
			if (reg->cpu_alloc != reg->gpu_alloc)
				kbase_free_phy_pages_helper(reg->gpu_alloc, delta);
			res = -ENOMEM;
			goto out_unlock;
		}
	} else {
		res = kbase_mem_shrink(kctx, reg, new_pages);
		if (res)
			res = -ENOMEM;
	}

out_unlock:
	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
	if (read_locked)
		up_read(kbase_mem_get_process_mmap_lock());
	else
		up_write(kbase_mem_get_process_mmap_lock());

	return res;
}

int kbase_mem_shrink(struct kbase_context *const kctx, struct kbase_va_region *const reg,
		     u64 new_pages)
{
	u64 delta, old_pages;
	int err;

	lockdep_assert_held(&kctx->reg_lock);

	if (WARN_ON(!kctx))
		return -EINVAL;

	if (WARN_ON(!reg))
		return -EINVAL;

	old_pages = kbase_reg_current_backed_size(reg);
	if (old_pages < new_pages) {
		dev_warn(
			kctx->kbdev->dev,
			"Requested number of pages (%llu) is larger than the current number of pages (%llu)",
			new_pages, old_pages);
		return -EINVAL;
	}

	delta = old_pages - new_pages;
	if (kbase_is_large_pages_enabled()) {
		struct tagged_addr *start_free = reg->gpu_alloc->pages + new_pages;

		while (delta && is_huge(*start_free) && !is_huge_head(*start_free)) {
			start_free++;
			new_pages++;
			delta--;
		}

		if (!delta)
			return 0;
	}

	err = kbase_mem_shrink_gpu_mapping(kctx, reg, new_pages, old_pages);
	if (err >= 0) {

		kbase_mem_shrink_cpu_mapping(kctx, reg, new_pages, old_pages);

		kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
		if (reg->cpu_alloc != reg->gpu_alloc)
			kbase_free_phy_pages_helper(reg->gpu_alloc, delta);
	}

	return err;
}

static void kbase_cpu_vm_open(struct vm_area_struct *vma)
{
	struct kbase_cpu_mapping *map = vma->vm_private_data;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);

	map->count++;
}

static void kbase_cpu_vm_close(struct vm_area_struct *vma)
{
	struct kbase_cpu_mapping *map = vma->vm_private_data;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);

	if (--map->count)
		return;

	KBASE_DEBUG_ASSERT(map->kctx);
	KBASE_DEBUG_ASSERT(map->alloc);

	kbase_gpu_vm_lock_with_pmode_sync(map->kctx);

	if (map->free_on_close) {
		KBASE_DEBUG_ASSERT(kbase_bits_to_zone(map->region->flags) == SAME_VA_ZONE);

		if (!is_process_exiting(vma))
			kbase_mem_free_region(map->kctx, map->region);
	}

	list_del(&map->mappings_list);

	kbase_va_region_alloc_put(map->kctx, map->region);
	kbase_gpu_vm_unlock_with_pmode_sync(map->kctx);

	kbase_mem_phy_alloc_put(map->alloc);
	kfree(map);
}

static struct kbase_aliased *get_aliased_alloc(struct vm_area_struct *vma,
					       struct kbase_va_region *reg, pgoff_t *start_off,
					       size_t nr_pages)
{
	struct kbase_aliased *aliased = reg->cpu_alloc->imported.alias.aliased;

	CSTD_UNUSED(vma);

	if (!reg->cpu_alloc->imported.alias.stride || reg->nr_pages < (*start_off + nr_pages))
		return NULL;

	while (*start_off >= reg->cpu_alloc->imported.alias.stride) {
		aliased++;
		*start_off -= reg->cpu_alloc->imported.alias.stride;
	}

	if (!aliased->alloc) {

		return NULL;
	}

	if ((*start_off + nr_pages) > aliased->length) {

		return NULL;
	}

	return aliased;
}

#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)
static vm_fault_t kbase_cpu_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static vm_fault_t kbase_cpu_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct kbase_cpu_mapping *map = vma->vm_private_data;
	pgoff_t map_start_pgoff;
	pgoff_t fault_pgoff;
	size_t i;
	pgoff_t addr;
	size_t nents;
	struct tagged_addr *pages;
	vm_fault_t ret = VM_FAULT_SIGBUS;
	struct memory_group_manager_device *mgm_dev;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);
	KBASE_DEBUG_ASSERT(map->kctx);
	KBASE_DEBUG_ASSERT(map->alloc);

	kbase_gpu_vm_lock(map->kctx);

	if ((map->alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) && map->free_on_close) {
		dev_warn(map->kctx->kbdev->dev, "Invalid CPU access to UMM memory for ctx %d_%d",
			 map->kctx->tgid, map->kctx->id);
		goto exit;
	}

	map_start_pgoff = vma->vm_pgoff - map->region->start_pfn;

	if (unlikely(map->region->cpu_alloc->type == KBASE_MEM_TYPE_ALIAS)) {
		struct kbase_aliased *aliased =
			get_aliased_alloc(vma, map->region, &map_start_pgoff, 1);

		if (!aliased)
			goto exit;

		nents = aliased->length;
		pages = aliased->alloc->pages + aliased->offset;
	} else {
		nents = map->alloc->nents;
		pages = map->alloc->pages;
	}

	fault_pgoff = map_start_pgoff + (vmf->pgoff - vma->vm_pgoff);

	if (fault_pgoff >= nents)
		goto exit;

	if (map->alloc->reg && (map->alloc->reg->flags & KBASE_REG_DONT_NEED))
		goto exit;

	i = map_start_pgoff;
	addr = (pgoff_t)(vma->vm_start >> PAGE_SHIFT);
	mgm_dev = map->kctx->kbdev->mgm_dev;
	while (i < nents && (addr < vma->vm_end >> PAGE_SHIFT)) {
		ret = mgm_dev->ops.mgm_vmf_insert_pfn_prot(mgm_dev, map->alloc->group_id, vma,
							   addr << PAGE_SHIFT,
							   PFN_DOWN(as_phys_addr_t(pages[i])),
							   vma->vm_page_prot);

		if (ret != VM_FAULT_NOPAGE)
			goto exit;

		i++;
		addr++;
	}

exit:
	kbase_gpu_vm_unlock(map->kctx);
	return ret;
}

const struct vm_operations_struct kbase_vm_ops = { .open = kbase_cpu_vm_open,
						   .close = kbase_cpu_vm_close,
						   .fault = kbase_cpu_vm_fault };

static int kbase_cpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg,
			  struct vm_area_struct *vma, void *kaddr, size_t nr_pages,
			  unsigned long aligned_offset, int free_on_close)
{
	struct kbase_cpu_mapping *map;
	int err = 0;

	map = kzalloc(sizeof(*map), GFP_KERNEL);

	if (!map) {
		WARN_ON(1);
		err = -ENOMEM;
		goto out;
	}

	__vm_flags_mod(vma, VM_DONTCOPY | VM_DONTDUMP | VM_DONTEXPAND | VM_IO, 0);

	vma->vm_ops = &kbase_vm_ops;
	vma->vm_private_data = map;

	if (reg->cpu_alloc->type == KBASE_MEM_TYPE_ALIAS && nr_pages) {
		pgoff_t rel_pgoff = vma->vm_pgoff - reg->start_pfn + (aligned_offset >> PAGE_SHIFT);
		struct kbase_aliased *aliased = get_aliased_alloc(vma, reg, &rel_pgoff, nr_pages);

		if (!aliased) {
			err = -EINVAL;
			kfree(map);
			goto out;
		}
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED) &&
	    (reg->flags & (KBASE_REG_CPU_WR | KBASE_REG_CPU_RD))) {

		BUG_ON(kaddr);
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	}

	if (!kaddr)
		__vm_flags_mod(vma, VM_PFNMAP, 0);
	else {
		WARN_ON(aligned_offset);

		__vm_flags_mod(vma, VM_MIXEDMAP, 0);

		err = remap_vmalloc_range(vma, kaddr, 0);
		WARN_ON(err);
	}

	if (err) {
		kfree(map);
		goto out;
	}

	map->region = kbase_va_region_alloc_get(kctx, reg);
	map->free_on_close = free_on_close;
	map->kctx = kctx;
	map->alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	map->count = 1;

	if (reg->flags & KBASE_REG_CPU_CACHED)
		map->alloc->properties |= KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED;

	list_add(&map->mappings_list, &map->alloc->mappings);

out:
	return err;
}

#ifdef CONFIG_MALI_VECTOR_DUMP
static void kbase_free_unused_jit_allocations(struct kbase_context *kctx)
{

	while (kbase_jit_evict(kctx))
		;
}

static int kbase_mmu_dump_mmap(struct kbase_context *kctx, struct vm_area_struct *vma,
			       struct kbase_va_region **const reg, void **const kmap_addr)
{
	struct kbase_va_region *new_reg;
	void *kaddr;
	size_t nr_pages;
	size_t size;
	int err = 0;

	lockdep_assert_held(&kctx->reg_lock);

	dev_dbg(kctx->kbdev->dev, "%s\n", __func__);
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size >> PAGE_SHIFT;

	kbase_free_unused_jit_allocations(kctx);

	kaddr = kbase_mmu_dump(kctx, nr_pages);

	if (!kaddr) {
		err = -ENOMEM;
		goto out;
	}

	new_reg = kbase_ctx_alloc_free_region(kctx, SAME_VA_ZONE, 0, nr_pages);
	if (!new_reg) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out;
	}

	new_reg->cpu_alloc =
		kbase_alloc_create(kctx, 0, KBASE_MEM_TYPE_RAW, BASE_MEM_GROUP_DEFAULT);
	if (IS_ERR_OR_NULL(new_reg->cpu_alloc)) {
		err = -ENOMEM;
		new_reg->cpu_alloc = NULL;
		WARN_ON(1);
		goto out_no_alloc;
	}

	new_reg->gpu_alloc = kbase_mem_phy_alloc_get(new_reg->cpu_alloc);

	new_reg->flags &= ~KBASE_REG_FREE;
	new_reg->flags |= KBASE_REG_CPU_CACHED;
	if (kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1) != 0) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out_va_region;
	}

	*kmap_addr = kaddr;
	*reg = new_reg;

	dev_dbg(kctx->kbdev->dev, "%s done\n", __func__);
	return 0;

out_no_alloc:
out_va_region:
	kbase_free_alloced_region(new_reg);
out:
	return err;
}
#endif

void kbase_os_mem_map_lock(struct kbase_context *kctx)
{
	(void)kctx;
	down_read(kbase_mem_get_process_mmap_lock());
}

void kbase_os_mem_map_unlock(struct kbase_context *kctx)
{
	(void)kctx;
	up_read(kbase_mem_get_process_mmap_lock());
}

static int kbasep_reg_mmap(struct kbase_context *kctx, struct vm_area_struct *vma,
			   struct kbase_va_region **regm, size_t *nr_pages, size_t *aligned_offset)

{
	unsigned int cookie = vma->vm_pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);
	struct kbase_va_region *reg;
	int err = 0;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	*aligned_offset = 0;

	dev_dbg(kctx->kbdev->dev, "%s\n", __func__);

	reg = kctx->pending_regions[cookie];
	if (!reg) {
		err = -ENOMEM;
		goto out;
	}

	if ((reg->flags & KBASE_REG_GPU_NX) && (reg->nr_pages != *nr_pages)) {

		err = -ENOMEM;
		goto out;
	}

	if ((vma->vm_flags & VM_READ && !(reg->flags & KBASE_REG_CPU_RD)) ||
	    (vma->vm_flags & VM_WRITE && !(reg->flags & KBASE_REG_CPU_WR))) {

		err = -EPERM;
		dev_err(kctx->kbdev->dev, "%s:%d inconsistent VM flags\n", __FILE__, __LINE__);
		goto out;
	}

	*nr_pages = kbase_reg_current_backed_size(reg);
	if (kbase_gpu_mmap(kctx, reg, vma->vm_start + *aligned_offset, reg->nr_pages, 1,
			   mmu_sync_info) != 0) {

		dev_err(kctx->kbdev->dev, "%s:%d\n", __FILE__, __LINE__);
		err = -ENOMEM;
		goto out;
	}

	kctx->pending_regions[cookie] = NULL;
	bitmap_set(kctx->cookies, cookie, 1);

#if MALI_USE_CSF
	if (reg->flags & KBASE_REG_CSF_EVENT)
		kbase_link_event_mem_page(kctx, reg);
#endif

	vma->vm_pgoff = reg->start_pfn - ((*aligned_offset) >> PAGE_SHIFT);
out:
	*regm = reg;
	dev_dbg(kctx->kbdev->dev, "%s done\n", __func__);

	return err;
}

int kbase_context_mmap(struct kbase_context *const kctx, struct vm_area_struct *const vma)
{
	struct kbase_va_region *reg = NULL;
	void *kaddr = NULL;
	size_t nr_pages = vma_pages(vma);
	int err = 0;
	int free_on_close = 0;
	struct device *dev = kctx->kbdev->dev;
	size_t aligned_offset = 0;

	dev_dbg(dev, "kbase_mmap\n");

	if (!(vma->vm_flags & VM_READ))
		vm_flags_clear(vma, VM_MAYREAD);
	if (!(vma->vm_flags & VM_WRITE))
		vm_flags_clear(vma, VM_MAYWRITE);

	if (nr_pages == 0) {
		err = -EINVAL;
		goto out;
	}

	if (!(vma->vm_flags & VM_SHARED)) {
		err = -EINVAL;
		goto out;
	}

	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	if (vma->vm_pgoff == PFN_DOWN(BASE_MEM_MAP_TRACKING_HANDLE)) {

		err = kbase_tracking_page_setup(kctx, vma);
		goto out_unlock;
	}

	if (!kbase_mem_allow_alloc(kctx)) {
		err = -EINVAL;
		goto out_unlock;
	}

	switch (vma->vm_pgoff) {
	case PFN_DOWN(BASEP_MEM_INVALID_HANDLE):
	case PFN_DOWN(BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE):

		err = -EINVAL;
		goto out_unlock;
	case PFN_DOWN(BASE_MEM_MMU_DUMP_HANDLE):
#if defined(CONFIG_MALI_VECTOR_DUMP)

		err = kbase_mmu_dump_mmap(kctx, vma, &reg, &kaddr);
		if (err != 0)
			goto out_unlock;

		free_on_close = 1;
		break;
#else

		err = -EINVAL;
		goto out_unlock;
#endif
#if MALI_USE_CSF
	case PFN_DOWN(BASEP_MEM_CSF_USER_REG_PAGE_HANDLE):
		kbase_gpu_vm_unlock_with_pmode_sync(kctx);
		err = kbase_csf_cpu_mmap_user_reg_page(kctx, vma);
		goto out;
	case PFN_DOWN(BASEP_MEM_CSF_USER_IO_PAGES_HANDLE)... PFN_DOWN(BASE_MEM_COOKIE_BASE) - 1: {
		kbase_gpu_vm_unlock_with_pmode_sync(kctx);
		mutex_lock(&kctx->csf.lock);
		err = kbase_csf_cpu_mmap_user_io_pages(kctx, vma);
		mutex_unlock(&kctx->csf.lock);
		goto out;
	}
#endif
	case PFN_DOWN(BASE_MEM_COOKIE_BASE)... PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) - 1: {
		err = kbasep_reg_mmap(kctx, vma, &reg, &nr_pages, &aligned_offset);
		if (err != 0)
			goto out_unlock;

		free_on_close = 1;
		break;
	}
	default: {
		reg = kbase_region_tracker_find_region_enclosing_address(
			kctx, (u64)vma->vm_pgoff << PAGE_SHIFT);

		if (!kbase_is_region_invalid_or_free(reg)) {

			if (nr_pages > (reg->nr_pages - (vma->vm_pgoff - reg->start_pfn))) {
				err = -ENOMEM;
				goto out_unlock;
			}

			if ((vma->vm_flags & VM_READ && !(reg->flags & KBASE_REG_CPU_RD)) ||
			    (vma->vm_flags & VM_WRITE && !(reg->flags & KBASE_REG_CPU_WR))) {

				err = -EPERM;
				dev_err(dev, "%s:%d inconsistent VM flags\n", __FILE__, __LINE__);
				goto out_unlock;
			}

			if (reg->cpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
				if (0 != (vma->vm_pgoff - reg->start_pfn)) {
					err = -EINVAL;
					dev_warn(
						dev,
						"%s:%d attempt to do a partial map in a dma_buf: non-zero offset to dma_buf mapping!\n",
						__FILE__, __LINE__);
					goto out_unlock;
				}
				err = dma_buf_mmap(reg->cpu_alloc->imported.umm.dma_buf, vma,
						   vma->vm_pgoff - reg->start_pfn);
				goto out_unlock;
			}

			if (reg->cpu_alloc->type == KBASE_MEM_TYPE_ALIAS) {

				if (nr_pages > reg->gpu_alloc->imported.alias.stride ||
				    !reg->gpu_alloc->imported.alias.stride || !nr_pages) {
					err = -EINVAL;
					dev_warn(dev, "mmap aliased: invalid params!\n");
					goto out_unlock;
				}
			} else if (reg->cpu_alloc->nents <
				   (vma->vm_pgoff - reg->start_pfn + nr_pages)) {

				if ((vma->vm_pgoff - reg->start_pfn) >= reg->cpu_alloc->nents)
					nr_pages = 0;
				else
					nr_pages = reg->cpu_alloc->nents -
						   (vma->vm_pgoff - reg->start_pfn);
			}
		} else {
			err = -ENOMEM;
			goto out_unlock;
		}
	}
	}

	err = kbase_cpu_mmap(kctx, reg, vma, kaddr, nr_pages, aligned_offset, free_on_close);
#if defined(CONFIG_MALI_VECTOR_DUMP)
	if (vma->vm_pgoff == PFN_DOWN(BASE_MEM_MMU_DUMP_HANDLE)) {

		vfree(kaddr);

		vma->vm_pgoff = PFN_DOWN(vma->vm_start);
	}
#endif
out_unlock:
	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
out:
	if (err)
		dev_err(dev, "mmap failed %d\n", err);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_context_mmap);

void kbase_sync_mem_regions(struct kbase_context *kctx, struct kbase_vmap_struct *map,
			    enum kbase_sync_type dest)
{
	size_t i;
	off_t const offset = map->offset_in_page;
	size_t const page_count = PFN_UP((size_t)offset + map->size);

	size_t sz = MIN((PAGE_SIZE - (size_t)offset), map->size);
	struct tagged_addr cpu_pa = map->cpu_pages[0];
	struct tagged_addr gpu_pa = map->gpu_pages[0];

	kbase_sync_single(kctx, cpu_pa, gpu_pa, offset, sz, dest);

	for (i = 1; page_count > 2 && i < page_count - 1; i++) {
		cpu_pa = map->cpu_pages[i];
		gpu_pa = map->gpu_pages[i];
		kbase_sync_single(kctx, cpu_pa, gpu_pa, 0, PAGE_SIZE, dest);
	}

	if (page_count > 1) {
		cpu_pa = map->cpu_pages[page_count - 1];
		gpu_pa = map->gpu_pages[page_count - 1];
		sz = (((size_t)offset + map->size - 1) & ~PAGE_MASK) + 1;
		kbase_sync_single(kctx, cpu_pa, gpu_pa, 0, sz, dest);
	}
}

static void kbase_vmap_phy_pages_migrate_count_increment(struct tagged_addr *pages,
							 size_t page_count, unsigned long flags)
{
	size_t i;

	if (!kbase_is_page_migration_enabled())
		return;

	for (i = 0; i < page_count; i++) {
		struct page *p = as_page(pages[i]);
		struct kbase_page_metadata *page_md = kbase_page_private(p);

		if (is_huge(pages[i]) || is_partial(pages[i]))
			continue;

		spin_lock(&page_md->migrate_lock);

		if (flags & KBASE_REG_PERMANENT_KERNEL_MAPPING)
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
		if (page_md->vmap_count < U8_MAX)
			page_md->vmap_count++;
		else
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
		spin_unlock(&page_md->migrate_lock);
	}
}

static void kbase_vunmap_phy_pages_migrate_count_decrement(struct tagged_addr *pages,
							   size_t page_count)
{
	size_t i;

	if (!kbase_is_page_migration_enabled())
		return;

	for (i = 0; i < page_count; i++) {
		struct page *p = as_page(pages[i]);
		struct kbase_page_metadata *page_md = kbase_page_private(p);

		if (is_huge(pages[i]) || is_partial(pages[i]))
			continue;

		spin_lock(&page_md->migrate_lock);

		if (page_md->vmap_count == 0)
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
		else
			page_md->vmap_count--;
		spin_unlock(&page_md->migrate_lock);
	}
}

static int kbase_vmap_phy_pages(struct kbase_context *kctx, struct kbase_va_region *reg,
				u64 offset_bytes, size_t size, struct kbase_vmap_struct *map,
				kbase_vmap_flag vmap_flags)
{
	unsigned long page_index;
	unsigned int offset_in_page = offset_bytes & ~PAGE_MASK;
	size_t page_count = PFN_UP(offset_in_page + size);
	struct tagged_addr *page_array;
	struct page **pages;
	void *cpu_addr = NULL;
	pgprot_t prot;
	size_t i;

	if (WARN_ON(vmap_flags & ~KBASE_VMAP_INPUT_FLAGS))
		return -EINVAL;

	if (WARN_ON(kbase_is_region_invalid_or_free(reg)))
		return -EINVAL;

	if (!size || !map || !reg->cpu_alloc || !reg->gpu_alloc)
		return -EINVAL;

	if (size > ((size_t)-1 / PAGE_SIZE))
		return -EINVAL;

	page_index = offset_bytes >> PAGE_SHIFT;

	if (-1UL - page_count < page_index)
		return -EINVAL;

	if (page_index + page_count > kbase_reg_current_backed_size(reg))
		return -ENOMEM;

	if ((vmap_flags & KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING) &&
	    (page_count > (KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES -
			   (size_t)atomic_read(&kctx->permanent_mapped_pages)))) {
		dev_warn(
			kctx->kbdev->dev,
			"Request for %llu more pages mem needing a permanent mapping would breach limit %lu, currently at %d pages",
			(u64)page_count, KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES,
			atomic_read(&kctx->permanent_mapped_pages));
		return -ENOMEM;
	}

	if (kbase_is_region_shrinkable(reg))
		return -EINVAL;

	prot = PAGE_KERNEL;
	if (!(reg->flags & KBASE_REG_CPU_CACHED)) {

		prot = pgprot_writecombine(prot);
	}

	page_array = kbase_get_cpu_phy_pages(reg);
	if (!page_array)
		return -ENOMEM;

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < page_count; i++)
		pages[i] = as_page(page_array[page_index + i]);

	cpu_addr = vmap(pages, page_count, VM_MAP, prot);

	if (kbase_is_page_migration_enabled() && !kbase_mem_is_imported(reg->gpu_alloc->type))
		kbase_vmap_phy_pages_migrate_count_increment(page_array, page_count, reg->flags);

	kfree(pages);

	if (!cpu_addr)
		return -ENOMEM;

	map->offset_in_page = offset_in_page;
	map->cpu_alloc = reg->cpu_alloc;
	map->cpu_pages = &kbase_get_cpu_phy_pages(reg)[page_index];
	map->gpu_alloc = reg->gpu_alloc;
	map->gpu_pages = &kbase_get_gpu_phy_pages(reg)[page_index];
	map->addr = (void *)((uintptr_t)cpu_addr + offset_in_page);
	map->size = size;
	map->flags = vmap_flags;
	if ((reg->flags & KBASE_REG_CPU_CACHED) && !kbase_mem_is_imported(map->gpu_alloc->type))
		map->flags |= KBASE_VMAP_FLAG_SYNC_NEEDED;

	if (map->flags & KBASE_VMAP_FLAG_SYNC_NEEDED)
		kbase_sync_mem_regions(kctx, map, KBASE_SYNC_TO_CPU);

	if (vmap_flags & KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING)
		atomic_add(page_count, &kctx->permanent_mapped_pages);

	kbase_mem_phy_alloc_kernel_mapped(reg->cpu_alloc);

	return 0;
}

void *kbase_vmap_reg(struct kbase_context *kctx, struct kbase_va_region *reg, u64 gpu_addr,
		     size_t size, unsigned long prot_request, struct kbase_vmap_struct *map,
		     kbase_vmap_flag vmap_flags)
{
	u64 offset_bytes;
	struct kbase_mem_phy_alloc *cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc;
	int err;

	lockdep_assert_held(&kctx->reg_lock);

	if (WARN_ON(kbase_is_region_invalid_or_free(reg)))
		return NULL;

	if ((reg->flags & prot_request) != prot_request)
		return NULL;

	offset_bytes = gpu_addr - (reg->start_pfn << PAGE_SHIFT);
	cpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	gpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	err = kbase_vmap_phy_pages(kctx, reg, offset_bytes, size, map, vmap_flags);
	if (err < 0)
		goto fail_vmap_phy_pages;

	return map->addr;

fail_vmap_phy_pages:
	kbase_mem_phy_alloc_put(cpu_alloc);
	kbase_mem_phy_alloc_put(gpu_alloc);
	return NULL;
}

void *kbase_vmap_prot(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		      unsigned long prot_request, struct kbase_vmap_struct *map)
{
	struct kbase_va_region *reg;
	void *addr = NULL;

	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);
	if (kbase_is_region_invalid_or_free(reg))
		goto out_unlock;

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
		goto out_unlock;

	addr = kbase_vmap_reg(kctx, reg, gpu_addr, size, prot_request, map, 0u);

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return addr;
}

void *kbase_vmap(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		 struct kbase_vmap_struct *map)
{

	return kbase_vmap_prot(kctx, gpu_addr, size, 0u, map);
}
KBASE_EXPORT_TEST_API(kbase_vmap);

static void kbase_vunmap_phy_pages(struct kbase_context *kctx, struct kbase_vmap_struct *map)
{
	void *addr = (void *)((uintptr_t)map->addr & PAGE_MASK);

	vunmap(addr);

	if (kbase_is_page_migration_enabled() && !kbase_mem_is_imported(map->gpu_alloc->type)) {
		const size_t page_count = PFN_UP((size_t)map->offset_in_page + map->size);
		struct tagged_addr *pages_array = map->cpu_pages;

		kbase_vunmap_phy_pages_migrate_count_decrement(pages_array, page_count);
	}

	if (map->flags & KBASE_VMAP_FLAG_SYNC_NEEDED)
		kbase_sync_mem_regions(kctx, map, KBASE_SYNC_TO_DEVICE);
	if (map->flags & KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING) {
		size_t page_count = PFN_UP((size_t)map->offset_in_page + map->size);

		WARN_ON(page_count > (size_t)atomic_read(&kctx->permanent_mapped_pages));
		atomic_sub(page_count, &kctx->permanent_mapped_pages);
	}

	kbase_mem_phy_alloc_kernel_unmapped(map->cpu_alloc);

	map->offset_in_page = 0;
	map->cpu_pages = NULL;
	map->gpu_pages = NULL;
	map->addr = NULL;
	map->size = 0;
	map->flags = 0;
}

void kbase_vunmap(struct kbase_context *kctx, struct kbase_vmap_struct *map)
{
	kbase_vunmap_phy_pages(kctx, map);
	map->cpu_alloc = kbase_mem_phy_alloc_put(map->cpu_alloc);
	map->gpu_alloc = kbase_mem_phy_alloc_put(map->gpu_alloc);
}
KBASE_EXPORT_TEST_API(kbase_vunmap);

static void kbasep_add_mm_counter(struct mm_struct *mm, int member, long value)
{
#if (KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE)

	percpu_counter_add(&mm->rss_stat[member], value);
#elif (KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE)

	atomic_long_add(value, &mm->rss_stat.count[member]);
#else
	add_mm_counter(mm, member, value);
#endif
}

void kbasep_os_process_page_usage_update(struct kbase_context *kctx, int pages)
{
	struct mm_struct *mm = kctx->process_mm;

	if (unlikely(!mm))
		return;

	atomic_add(pages, &kctx->nonmapped_pages);
#ifdef SPLIT_RSS_COUNTING
	kbasep_add_mm_counter(mm, MM_FILEPAGES, pages);
#else
	spin_lock(&mm->page_table_lock);
	kbasep_add_mm_counter(mm, MM_FILEPAGES, pages);
	spin_unlock(&mm->page_table_lock);
#endif
}

static int kbase_tracking_page_setup(struct kbase_context *kctx, struct vm_area_struct *vma)
{
	if (vma_pages(vma) != 1)
		return -EINVAL;

	__vm_flags_mod(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP | VM_IO,
		       VM_READ | VM_MAYREAD | VM_WRITE | VM_MAYWRITE | VM_EXEC | VM_MAYEXEC);

	return 0;
}

#if MALI_USE_CSF
static unsigned long get_queue_doorbell_pfn(struct kbase_device *kbdev, struct kbase_queue *queue)
{
	lockdep_assert_held(&kbdev->csf.reg_lock);

#if IS_ENABLED(CONFIG_MALI_NO_MALI)
	return PFN_DOWN(as_phys_addr_t(kbdev->csf.dummy_db_page));
#else
	if (queue->doorbell_nr == KBASEP_USER_DB_NR_INVALID)
		return PFN_DOWN(as_phys_addr_t(kbdev->csf.dummy_db_page));
#endif
	return (PFN_DOWN(kbdev->reg_start + CSF_HW_DOORBELL_PAGE_OFFSET +
			 (u64)queue->doorbell_nr * CSF_HW_DOORBELL_PAGE_SIZE));
}

static int
#if (KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE || \
     KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
kbase_csf_user_io_pages_vm_mremap(struct vm_area_struct *vma)
{
#else
kbase_csf_user_io_pages_vm_mremap(struct vm_area_struct *vma, unsigned long flags)
{
	CSTD_UNUSED(flags);
#endif
	CSTD_UNUSED(vma);

	pr_debug("Unexpected call to mremap method for User IO pages mapping vma\n");
	return -EINVAL;
}

static int kbase_csf_user_io_pages_vm_split(struct vm_area_struct *vma, unsigned long addr)
{
	CSTD_UNUSED(vma);
	CSTD_UNUSED(addr);

	pr_debug("Unexpected call to split method for User IO pages mapping vma\n");
	return -EINVAL;
}

static void kbase_csf_user_io_pages_vm_open(struct vm_area_struct *vma)
{
	pr_debug("Unexpected call to the open method for User IO pages mapping vma\n");
	vma->vm_private_data = NULL;
}

static void kbase_csf_user_io_pages_vm_close(struct vm_area_struct *vma)
{
	struct kbase_queue *queue = vma->vm_private_data;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	int err;
	bool reset_prevented = false;

	if (!queue) {
		pr_debug("Close method called for the new User IO pages mapping vma\n");
		return;
	}

	kctx = queue->kctx;
	kbdev = kctx->kbdev;

	err = kbase_reset_gpu_prevent_and_wait(kbdev);
	if (err)
		dev_warn(
			kbdev->dev,
			"Unsuccessful GPU reset detected when unbinding queue (csi_index=%d), attempting to unbind regardless",
			queue->csi_index);
	else
		reset_prevented = true;

	mutex_lock(&kctx->csf.lock);
	kbase_csf_queue_unbind(queue, is_process_exiting(vma));
	mutex_unlock(&kctx->csf.lock);

	if (reset_prevented)
		kbase_reset_gpu_allow(kbdev);

	fput(kctx->filp);
}

#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)
static vm_fault_t kbase_csf_user_io_pages_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static vm_fault_t kbase_csf_user_io_pages_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct kbase_queue *queue = vma->vm_private_data;
	unsigned long doorbell_cpu_addr, input_cpu_addr, output_cpu_addr;
	unsigned long doorbell_page_pfn, input_page_pfn, output_page_pfn;
	pgprot_t doorbell_pgprot, input_page_pgprot, output_page_pgprot;
	size_t nr_pages = PFN_DOWN(vma->vm_end - vma->vm_start);
	vm_fault_t ret;
	struct kbase_device *kbdev;
	struct memory_group_manager_device *mgm_dev;

	if (!queue || (nr_pages != BASEP_QUEUE_NR_MMAP_USER_PAGES) ||
	    (vma->vm_pgoff != queue->db_file_offset)) {
		pr_warn("Unexpected CPU page fault on User IO pages mapping for process %s tgid %d pid %d\n",
			current->comm, current->tgid, current->pid);
		return VM_FAULT_SIGBUS;
	}

	kbdev = queue->kctx->kbdev;
	mgm_dev = kbdev->mgm_dev;

	mutex_lock(&kbdev->csf.reg_lock);

	doorbell_pgprot = pgprot_device(vma->vm_page_prot);

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
	if (kbdev->system_coherency == COHERENCY_NONE ||
		kbdev->acp_dbg_force_sync) {
#else
	if (kbdev->system_coherency == COHERENCY_NONE) {
#endif
		input_page_pgprot = pgprot_writecombine(vma->vm_page_prot);
		output_page_pgprot = pgprot_writecombine(vma->vm_page_prot);
	} else {
		input_page_pgprot = vma->vm_page_prot;
		output_page_pgprot = vma->vm_page_prot;
	}

	doorbell_cpu_addr = vma->vm_start;

#if KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE
	if ((unsigned long)vmf->virtual_address == doorbell_cpu_addr) {
#else
	if (vmf->address == doorbell_cpu_addr) {
#endif
		doorbell_page_pfn = get_queue_doorbell_pfn(kbdev, queue);
		ret = mgm_dev->ops.mgm_vmf_insert_pfn_prot(mgm_dev, KBASE_MEM_GROUP_CSF_IO, vma,
							   doorbell_cpu_addr, doorbell_page_pfn,
							   doorbell_pgprot);
	} else {

		input_cpu_addr = doorbell_cpu_addr + PAGE_SIZE;
		input_page_pfn = PFN_DOWN(as_phys_addr_t(queue->phys[0]));
		ret = mgm_dev->ops.mgm_vmf_insert_pfn_prot(mgm_dev, KBASE_MEM_GROUP_CSF_IO, vma,
							   input_cpu_addr, input_page_pfn,
							   input_page_pgprot);
		if (ret != VM_FAULT_NOPAGE)
			goto exit;

		output_cpu_addr = input_cpu_addr + PAGE_SIZE;
		output_page_pfn = PFN_DOWN(as_phys_addr_t(queue->phys[1]));
		ret = mgm_dev->ops.mgm_vmf_insert_pfn_prot(mgm_dev, KBASE_MEM_GROUP_CSF_IO, vma,
							   output_cpu_addr, output_page_pfn,
							   output_page_pgprot);
	}

exit:
	mutex_unlock(&kbdev->csf.reg_lock);
	return ret;
}

static const struct vm_operations_struct kbase_csf_user_io_pages_vm_ops = {
	.open = kbase_csf_user_io_pages_vm_open,
	.close = kbase_csf_user_io_pages_vm_close,
#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
	.may_split = kbase_csf_user_io_pages_vm_split,
#else
	.split = kbase_csf_user_io_pages_vm_split,
#endif
	.mremap = kbase_csf_user_io_pages_vm_mremap,
	.fault = kbase_csf_user_io_pages_vm_fault
};

static int kbase_csf_cpu_mmap_user_io_pages(struct kbase_context *kctx, struct vm_area_struct *vma)
{
	unsigned long cookie = vma->vm_pgoff - PFN_DOWN(BASEP_MEM_CSF_USER_IO_PAGES_HANDLE);
	size_t nr_pages = vma_pages(vma);
	struct kbase_queue *queue;
	int err = 0;

	lockdep_assert_held(&kctx->csf.lock);

	queue = kctx->csf.user_pages_info[cookie];

	if (!queue)
		return -EINVAL;

	if (WARN_ON(test_bit(cookie, kctx->csf.cookies)))
		return -EINVAL;

	kctx->csf.user_pages_info[cookie] = NULL;
	bitmap_set(kctx->csf.cookies, cookie, 1);

	queue->handle = BASEP_MEM_INVALID_HANDLE;

	if (nr_pages != BASEP_QUEUE_NR_MMAP_USER_PAGES) {
		err = -EINVAL;
		goto map_failed;
	}

	err = kbase_csf_alloc_command_stream_user_pages(kctx, queue);
	if (err)
		goto map_failed;

	__vm_flags_mod(vma, VM_DONTCOPY | VM_DONTDUMP | VM_DONTEXPAND | VM_IO, 0);

	__vm_flags_mod(vma, VM_PFNMAP, 0);

	vma->vm_ops = &kbase_csf_user_io_pages_vm_ops;
	vma->vm_private_data = queue;

	vma->vm_file = kctx->kbdev->csf.db_filp;
	get_file(vma->vm_file);

	vma->vm_pgoff = queue->db_file_offset;

	return 0;

map_failed:

	kbase_csf_queue_unbind_stopped(queue);

	return err;
}

static void kbase_csf_user_reg_vm_open(struct vm_area_struct *vma)
{
	pr_debug("Unexpected call to the open method for USER register mapping");
	vma->vm_private_data = NULL;
}

static void kbase_csf_user_reg_vm_close(struct vm_area_struct *vma)
{
	struct kbase_context *kctx = vma->vm_private_data;
	struct kbase_device *kbdev;

	if (unlikely(!kctx)) {
		pr_debug("Close function called for the unexpected mapping");
		return;
	}

	kbdev = kctx->kbdev;

	if (unlikely(!kctx->csf.user_reg.vma))
		dev_warn(kbdev->dev, "user_reg VMA pointer unexpectedly NULL for ctx %d_%d",
			 kctx->tgid, kctx->id);

	mutex_lock(&kbdev->csf.reg_lock);
	list_del_init(&kctx->csf.user_reg.link);
	mutex_unlock(&kbdev->csf.reg_lock);

	kctx->csf.user_reg.vma = NULL;

	fput(kctx->filp);
}

static int
#if ((KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE) || \
     (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE))
kbase_csf_user_reg_vm_mremap(struct vm_area_struct *vma)
{
#else
kbase_csf_user_reg_vm_mremap(struct vm_area_struct *vma, unsigned long flags)
{
	CSTD_UNUSED(flags);
#endif
	CSTD_UNUSED(vma);

	pr_debug("Unexpected call to mremap method for USER page mapping vma\n");
	return -EINVAL;
}

#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)
static vm_fault_t kbase_csf_user_reg_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static vm_fault_t kbase_csf_user_reg_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct kbase_context *kctx = vma->vm_private_data;
	struct kbase_device *kbdev;
	struct memory_group_manager_device *mgm_dev;
	unsigned long pfn;
	size_t nr_pages = PFN_DOWN(vma->vm_end - vma->vm_start);
	vm_fault_t ret = VM_FAULT_SIGBUS;
	unsigned long flags;

	if (!kctx || (nr_pages != 1) || (vma != kctx->csf.user_reg.vma) ||
	    (vma->vm_pgoff != kctx->csf.user_reg.file_offset)) {
		pr_err("Unexpected CPU page fault on USER page mapping for process %s tgid %d pid %d\n",
		       current->comm, current->tgid, current->pid);
		return VM_FAULT_SIGBUS;
	}

	kbdev = kctx->kbdev;
	mgm_dev = kbdev->mgm_dev;
	pfn = PFN_DOWN(kbdev->reg_start + USER_BASE);

	mutex_lock(&kbdev->csf.reg_lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (IS_ENABLED(CONFIG_MALI_NO_MALI) || !kbdev->pm.backend.gpu_powered)
		pfn = PFN_DOWN(as_phys_addr_t(kbdev->csf.user_reg.dummy_page));
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	list_move_tail(&kctx->csf.user_reg.link, &kbdev->csf.user_reg.list);
	ret = mgm_dev->ops.mgm_vmf_insert_pfn_prot(mgm_dev, KBASE_MEM_GROUP_CSF_FW, vma,
						   vma->vm_start, pfn, vma->vm_page_prot);

	mutex_unlock(&kbdev->csf.reg_lock);

	return ret;
}

static const struct vm_operations_struct kbase_csf_user_reg_vm_ops = {
	.open = kbase_csf_user_reg_vm_open,
	.close = kbase_csf_user_reg_vm_close,
	.mremap = kbase_csf_user_reg_vm_mremap,
	.fault = kbase_csf_user_reg_vm_fault
};

static int kbase_csf_cpu_mmap_user_reg_page(struct kbase_context *kctx, struct vm_area_struct *vma)
{
	size_t nr_pages = PFN_DOWN(vma->vm_end - vma->vm_start);
	struct kbase_device *kbdev = kctx->kbdev;

	if (kctx->csf.user_reg.vma)
		return -EBUSY;

	if (nr_pages != 1)
		return -EINVAL;

	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		return -EPERM;

	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);

	vm_flags_set(vma, VM_DONTCOPY | VM_DONTDUMP | VM_DONTEXPAND | VM_IO);

	vm_flags_set(vma, VM_PFNMAP);

	kctx->csf.user_reg.vma = vma;

	mutex_lock(&kbdev->csf.reg_lock);
	kctx->csf.user_reg.file_offset = kbdev->csf.user_reg.file_offset++;
	mutex_unlock(&kbdev->csf.reg_lock);

	vma->vm_file = kctx->kbdev->csf.user_reg.filp;
	get_file(vma->vm_file);

	vma->vm_pgoff = kctx->csf.user_reg.file_offset;
	vma->vm_ops = &kbase_csf_user_reg_vm_ops;
	vma->vm_private_data = kctx;

	return 0;
}

#endif
