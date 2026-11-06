// SPDX-License-Identifier: GPL-2.0
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/memory_group_manager.h>

#include <mali_kbase.h>
#include <mali_kbase_native_mgm.h>

#include <thirdparty/mm.h>

static struct page *kbase_native_mgm_alloc(struct memory_group_manager_device *mgm_dev,
					   unsigned int group_id, gfp_t gfp_mask,
					   unsigned int order)
{

	BUILD_BUG_ON(BASE_MEM_GROUP_COUNT != MEMORY_GROUP_MANAGER_NR_GROUPS);

	BUILD_BUG_ON((BASEP_CONTEXT_MMU_GROUP_ID_MASK >> BASEP_CONTEXT_MMU_GROUP_ID_SHIFT) <
		     (BASE_MEM_GROUP_COUNT - 1));

	CSTD_UNUSED(mgm_dev);
	CSTD_UNUSED(group_id);

	return alloc_pages(gfp_mask, order);
}

static void kbase_native_mgm_free(struct memory_group_manager_device *mgm_dev,
				  unsigned int group_id, struct page *page, unsigned int order)
{
	CSTD_UNUSED(mgm_dev);
	CSTD_UNUSED(group_id);

	__free_pages(page, order);
}

static vm_fault_t kbase_native_mgm_vmf_insert_pfn_prot(struct memory_group_manager_device *mgm_dev,
						       unsigned int group_id,
						       struct vm_area_struct *vma,
						       unsigned long addr, unsigned long pfn,
						       pgprot_t pgprot)
{
	CSTD_UNUSED(mgm_dev);
	CSTD_UNUSED(group_id);

	return vmf_insert_pfn_prot(vma, addr, pfn, pgprot);
}

static u64 kbase_native_mgm_update_gpu_pte(struct memory_group_manager_device *mgm_dev,
					   unsigned int group_id, int mmu_level, u64 pte)
{
	if (WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return pte;

	pte |= ((u64)group_id << PTE_PBHA_SHIFT) & PTE_PBHA_MASK;

	pte |= ((u64)1 << PTE_RES_BIT_MULTI_AS_SHIFT);

	return pte;
}

static u64 kbase_native_mgm_pte_to_original_pte(struct memory_group_manager_device *mgm_dev,
						unsigned int group_id, int mmu_level, u64 pte)
{
	CSTD_UNUSED(mgm_dev);
	CSTD_UNUSED(group_id);
	CSTD_UNUSED(mmu_level);

	pte &= ~PTE_PBHA_MASK;

	pte &= ~((u64)1 << PTE_RES_BIT_MULTI_AS_SHIFT);

	return pte;
}

struct memory_group_manager_device kbase_native_mgm_dev = {
	.ops = {
		.mgm_alloc_page = kbase_native_mgm_alloc,
		.mgm_free_page = kbase_native_mgm_free,
		.mgm_get_import_memory_id = NULL,
		.mgm_vmf_insert_pfn_prot = kbase_native_mgm_vmf_insert_pfn_prot,
		.mgm_update_gpu_pte = kbase_native_mgm_update_gpu_pte,
		.mgm_pte_to_original_pte = kbase_native_mgm_pte_to_original_pte,
#if IS_ENABLED(CONFIG_MALI_MTK_MGMM)
		 .mgm_mtk_cache_pool_size = NULL,
#endif
	},
	.data = NULL
};
