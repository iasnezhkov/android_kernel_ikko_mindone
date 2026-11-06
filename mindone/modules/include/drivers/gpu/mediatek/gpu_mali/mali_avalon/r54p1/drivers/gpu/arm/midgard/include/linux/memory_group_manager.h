/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MEMORY_GROUP_MANAGER_H_
#define _MEMORY_GROUP_MANAGER_H_

#include <linux/mm.h>
#include <linux/of.h>
#include <linux/version.h>

#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
typedef int vm_fault_t;
#endif

#define MEMORY_GROUP_MANAGER_NR_GROUPS (16)
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
#define GPU_ONLY_PBHA 6
#define GPU_TLS_PBHA 9
#endif

#define PTE_PBHA_SHIFT (59)
#define PTE_PBHA_MASK ((uint64_t)0xf << PTE_PBHA_SHIFT)
#define PTE_RES_BIT_MULTI_AS_SHIFT (63)

struct memory_group_manager_device;
struct memory_group_manager_import_data;

struct memory_group_manager_ops {

	struct page *(*mgm_alloc_page)(struct memory_group_manager_device *mgm_dev,
				       unsigned int group_id, gfp_t gfp_mask, unsigned int order);

	void (*mgm_free_page)(struct memory_group_manager_device *mgm_dev, unsigned int group_id,
			      struct page *page, unsigned int order);

	int (*mgm_get_import_memory_id)(struct memory_group_manager_device *mgm_dev,
					struct memory_group_manager_import_data *import_data);

	u64 (*mgm_update_gpu_pte)(struct memory_group_manager_device *mgm_dev,
				  unsigned int group_id, int mmu_level, u64 pte);

	u64 (*mgm_pte_to_original_pte)(struct memory_group_manager_device *mgm_dev,
				       unsigned int group_id, int mmu_level, u64 pte);

	vm_fault_t (*mgm_vmf_insert_pfn_prot)(struct memory_group_manager_device *mgm_dev,
					      unsigned int group_id, struct vm_area_struct *vma,
					      unsigned long addr, unsigned long pfn,
					      pgprot_t pgprot);

	bool (*mgm_get_import_memory_cached_access_permitted)(
		struct memory_group_manager_device *mgm_dev,
		struct memory_group_manager_import_data *import_data);

#if IS_ENABLED(CONFIG_MALI_MTK_MGMM)
	size_t (*mgm_mtk_cache_pool_size)(struct memory_group_manager_device *mgm_dev);
#endif
};

struct memory_group_manager_device {
	struct memory_group_manager_ops ops;
	void *data;
	struct module *owner;
};

enum memory_group_manager_import_type { MEMORY_GROUP_MANAGER_IMPORT_TYPE_DMA_BUF };

struct memory_group_manager_import_data {
	enum memory_group_manager_import_type type;
	union {
		struct dma_buf *dma_buf;
	} u;
};

#endif
