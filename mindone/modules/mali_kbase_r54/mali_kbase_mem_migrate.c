// SPDX-License-Identifier: GPL-2.0
#include <linux/migrate.h>

#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_mem_migrate.h>
#include <mmu/mali_kbase_mmu.h>

static DEFINE_STATIC_KEY_FALSE(page_migration_static_key);

#ifdef __ANDROID_COMMON_KERNEL__
static int kbase_page_migration_enabled;
#else
static int kbase_page_migration_enabled = -1;
#endif
module_param(kbase_page_migration_enabled, int, 0444);
MODULE_PARM_DESC(kbase_page_migration_enabled,
		 "Explicitly enable or disable page migration with 1 or 0 respectively.");

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static const struct movable_operations movable_ops;
#endif

bool kbase_is_page_migration_enabled(void)
{
	return static_branch_unlikely(&page_migration_static_key);
}
KBASE_EXPORT_TEST_API(kbase_is_page_migration_enabled);

bool kbase_alloc_page_metadata(struct kbase_device *kbdev, struct page *p, dma_addr_t dma_addr,
			       u8 group_id)
{
	struct kbase_page_metadata *page_md;

	if (!kbase_is_page_migration_enabled())
		return false;

	if (is_huge_head(as_tagged(page_to_phys(p))) || is_partial(as_tagged(page_to_phys(p))))
		dev_WARN(kbdev->dev, "%s: migration-metadata attempted on large-page.", __func__);

	page_md = kmem_cache_zalloc(kbdev->page_metadata_slab, GFP_KERNEL);
	if (!page_md)
		return false;

	SetPagePrivate(p);
	set_page_private(p, (unsigned long)page_md);
	page_md->kbdev = kbdev;
	page_md->dma_addr = dma_addr;
	page_md->status = PAGE_STATUS_SET(page_md->status, (u8)ALLOCATE_IN_PROGRESS);
	page_md->vmap_count = 0;
	page_md->group_id = group_id;
	spin_lock_init(&page_md->migrate_lock);
	sema_init(&page_md->cpu_map_lock, 1);

	lock_page(p);
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	__SetPageMovable(p, &movable_ops);
	page_md->status = PAGE_MOVABLE_SET(page_md->status);
#else

	if (kbdev->mem_migrate.inode && kbdev->mem_migrate.inode->i_mapping) {
		__SetPageMovable(p, kbdev->mem_migrate.inode->i_mapping);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
	}
#endif
	unlock_page(p);

	return true;
}

static void kbase_free_page_metadata(struct kbase_device *kbdev, struct page *p, u8 *group_id)
{
	struct device *const dev = kbdev->dev;
	struct kbase_page_metadata *page_md;
	dma_addr_t dma_addr;

	if (!kbase_is_page_migration_enabled())
		return;

	page_md = kbase_page_private(p);
	if (!page_md)
		return;

	if (group_id)
		*group_id = page_md->group_id;
	dma_addr = kbase_dma_addr(p);
	dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	kmem_cache_free(kbdev->page_metadata_slab, page_md);
	set_page_private(p, 0);
	ClearPagePrivate(p);
}

static void kbase_free_pages_worker(struct work_struct *work)
{
	struct kbase_mem_migrate *mem_migrate =
		container_of(work, struct kbase_mem_migrate, free_pages_work);
	struct kbase_device *kbdev = container_of(mem_migrate, struct kbase_device, mem_migrate);
	struct page *p, *tmp;
	struct kbase_page_metadata *page_md;
	LIST_HEAD(free_list);

	spin_lock(&mem_migrate->free_pages_lock);
	list_splice_init(&mem_migrate->free_pages_list, &free_list);
	spin_unlock(&mem_migrate->free_pages_lock);
	list_for_each_entry_safe(p, tmp, &free_list, lru) {
		u8 group_id = 0;
		list_del_init(&p->lru);

		lock_page(p);
		page_md = kbase_page_private(p);
		if (page_md && IS_PAGE_MOVABLE(page_md->status)) {
			__ClearPageMovable(p);
			page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		}
		kbase_free_page_metadata(kbdev, p, &group_id);
		unlock_page(p);
		kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev, group_id, p, 0);
	}
}

void kbase_free_page_later(struct kbase_device *kbdev, struct page *p)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (!kbase_is_page_migration_enabled())
		return;

	spin_lock(&mem_migrate->free_pages_lock);
	list_add(&p->lru, &mem_migrate->free_pages_list);
	spin_unlock(&mem_migrate->free_pages_lock);
}

static int kbasep_migrate_page_pt_mapped(struct page *old_page, struct page *new_page)
{
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_device *kbdev;
	dma_addr_t old_dma_addr, new_dma_addr;
	int ret;

	if (!kbase_is_page_migration_enabled())
		return -EINVAL;

	spin_lock(&page_md->migrate_lock);

	if (PAGE_STATUS_GET(page_md->status) == FREE_PT_ISOLATED_IN_PROGRESS) {
		ret = -EAGAIN;
		goto early_exit;
	}

	if (WARN_ONCE(PAGE_STATUS_GET(page_md->status) != PT_MAPPED,
		      "Page metadata status %d doesn't match expected value %d",
		      PAGE_STATUS_GET(page_md->status), PT_MAPPED)) {
		ret = -EINVAL;
		goto early_exit;
	}

	kbdev = page_md->kbdev;

	old_dma_addr = page_md->dma_addr;

	spin_unlock(&page_md->migrate_lock);

	new_dma_addr = dma_map_page(kbdev->dev, new_page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kbdev->dev, new_dma_addr))
		return -ENOMEM;

	ret = kbase_mmu_migrate_pgd_page(as_tagged(page_to_phys(old_page)),
					 as_tagged(page_to_phys(new_page)), old_dma_addr,
					 new_dma_addr);

	if (ret == 0) {
		dma_unmap_page(kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
		__ClearPageMovable(old_page);
		ClearPagePrivate(old_page);
		put_page(old_page);

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		__SetPageMovable(new_page, &movable_ops);
		spin_lock(&page_md->migrate_lock);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
		spin_unlock(&page_md->migrate_lock);
#else
		if (kbdev->mem_migrate.inode->i_mapping) {
			__SetPageMovable(new_page, kbdev->mem_migrate.inode->i_mapping);
			spin_lock(&page_md->migrate_lock);
			page_md->status = PAGE_MOVABLE_SET(page_md->status);
			spin_unlock(&page_md->migrate_lock);
		}
#endif
		SetPagePrivate(new_page);
		set_page_private(new_page, (unsigned long)page_md);
		get_page(new_page);
	} else
		dma_unmap_page(kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	return ret;

early_exit:
	spin_unlock(&page_md->migrate_lock);
	return ret;
}

static int kbasep_migrate_page_allocated_mapped(struct page *old_page, struct page *new_page)
{
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_device *kbdev;
	dma_addr_t old_dma_addr, new_dma_addr;
	u64 vpfn;
	int ret;
	struct file *filp;

	if (!kbase_is_page_migration_enabled())
		return -EINVAL;

	spin_lock(&page_md->migrate_lock);

	if (PAGE_STATUS_GET(page_md->status) == FREE_ISOLATED_IN_PROGRESS) {
		ret = -EAGAIN;
		goto early_exit;
	}

	if (WARN_ONCE(PAGE_STATUS_GET(page_md->status) != ALLOCATED_MAPPED,
		      "Page metadata status %d doesn't match expected value %d",
		      PAGE_STATUS_GET(page_md->status), ALLOCATED_MAPPED)) {
		ret = -EINVAL;
		goto early_exit;
	}

	kbdev = page_md->kbdev;
	old_dma_addr = page_md->dma_addr;
	vpfn = page_md->data.mapped.vpfn;
	filp = page_md->data.mapped.mmut->kctx->filp;

	if (file_count(filp) <= 0) {

		ret = -EINVAL;
		goto early_exit;
	}

	get_file(filp);

	spin_unlock(&page_md->migrate_lock);

	new_dma_addr = dma_map_page(kbdev->dev, new_page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kbdev->dev, new_dma_addr)) {
		ret = -ENOMEM;
		goto err_exit_with_fput;
	}

	down(&page_md->cpu_map_lock);

	unmap_mapping_range(filp->f_inode->i_mapping,
			    (loff_t)(vpfn / GPU_PAGES_PER_CPU_PAGE) << PAGE_SHIFT, PAGE_SIZE, 1);

	ret = kbase_mmu_migrate_data_page(as_tagged(page_to_phys(old_page)),
					  as_tagged(page_to_phys(new_page)), old_dma_addr,
					  new_dma_addr);
	up(&page_md->cpu_map_lock);

	if (ret == 0) {
		dma_unmap_page(kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

		SetPagePrivate(new_page);
		set_page_private(new_page, (unsigned long)page_md);
		get_page(new_page);

		ClearPagePrivate(old_page);
		__ClearPageMovable(old_page);
		put_page(old_page);

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		__SetPageMovable(new_page, &movable_ops);
		spin_lock(&page_md->migrate_lock);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
		spin_unlock(&page_md->migrate_lock);
#else
		if (kbdev->mem_migrate.inode->i_mapping) {
			__SetPageMovable(new_page, kbdev->mem_migrate.inode->i_mapping);
			spin_lock(&page_md->migrate_lock);
			page_md->status = PAGE_MOVABLE_SET(page_md->status);
			spin_unlock(&page_md->migrate_lock);
		}
#endif
	} else
		dma_unmap_page(kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

err_exit_with_fput:
	fput(filp);
	return ret;

early_exit:
	spin_unlock(&page_md->migrate_lock);
	return ret;
}

#if MALI_UNIT_TEST
int kbase_migrate_page_allocated_mapped(struct page *old_page, struct page *new_page)
{
	return kbasep_migrate_page_allocated_mapped(old_page, new_page);
}
KBASE_EXPORT_TEST_API(kbase_migrate_page_allocated_mapped);
#endif

static bool kbase_page_isolate(struct page *p, isolate_mode_t mode)
{
	bool status_mem_pool = false;
	struct kbase_mem_pool *mem_pool = NULL;
	struct kbase_page_metadata *page_md = kbase_page_private(p);

	if (!kbase_is_page_migration_enabled())
		return false;

	CSTD_UNUSED(mode);

	if (!page_md || !IS_PAGE_MOVABLE(page_md->status))
		return false;

	if (!spin_trylock(&page_md->migrate_lock))
		return false;

	if (WARN_ON(IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return false;
	}

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:

		mem_pool = page_md->data.mem_pool.pool;
		status_mem_pool = true;
		preempt_disable();
		atomic_inc(&mem_pool->isolation_in_progress_cnt);
		break;
	case ALLOCATED_MAPPED:

		if (page_md->vmap_count == 0)
			page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
		break;
	case PT_MAPPED:

		page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
		break;
	case SPILL_IN_PROGRESS:
	case ALLOCATE_IN_PROGRESS:
	case FREE_IN_PROGRESS:
		break;
	case NOT_MOVABLE:

		__ClearPageMovable(p);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		break;
	default:

		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	if (status_mem_pool) {
		if (!spin_trylock(&mem_pool->pool_lock)) {
			atomic_dec(&mem_pool->isolation_in_progress_cnt);
			preempt_enable();
			return false;
		}

		spin_lock(&page_md->migrate_lock);

		if (PAGE_STATUS_GET(page_md->status) == MEM_POOL) {
			page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
			list_del_init(&p->lru);
			mem_pool->cur_size--;
		}
		spin_unlock(&page_md->migrate_lock);
		spin_unlock(&mem_pool->pool_lock);
		atomic_dec(&mem_pool->isolation_in_progress_cnt);
		preempt_enable();
	}

	return IS_PAGE_ISOLATED(page_md->status);
}

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
static int kbase_page_migrate(struct address_space *mapping, struct page *new_page,
			      struct page *old_page, enum migrate_mode mode)
#else
static int kbase_page_migrate(struct page *new_page, struct page *old_page, enum migrate_mode mode)
#endif
{
	int err = 0;
	bool status_mem_pool = false;
	bool status_free_pt_isolated_in_progress = false;
	bool status_free_isolated_in_progress = false;
	bool status_pt_mapped = false;
	bool status_mapped = false;
	bool status_not_movable = false;
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_device *kbdev = NULL;

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	CSTD_UNUSED(mapping);
#endif
	CSTD_UNUSED(mode);

	if (!kbase_is_page_migration_enabled() || !page_md || !IS_PAGE_MOVABLE(page_md->status))
		return -EINVAL;

	if (!spin_trylock(&page_md->migrate_lock))
		return -EAGAIN;

	if (WARN_ON(!IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return -EINVAL;
	}

	kbdev = page_md->kbdev;

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		status_mem_pool = true;
		break;
	case ALLOCATED_MAPPED:
		status_mapped = true;
		break;
	case PT_MAPPED:
		status_pt_mapped = true;
		break;
	case FREE_ISOLATED_IN_PROGRESS:
		status_free_isolated_in_progress = true;
		break;
	case FREE_PT_ISOLATED_IN_PROGRESS:
		status_free_pt_isolated_in_progress = true;
		break;
	case NOT_MOVABLE:
		status_not_movable = true;
		break;
	default:

		err = -EAGAIN;
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	if (status_mem_pool || status_free_isolated_in_progress ||
	    status_free_pt_isolated_in_progress) {
		struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

		kbase_free_page_metadata(kbdev, old_page, NULL);
		__ClearPageMovable(old_page);
		put_page(old_page);

		INIT_LIST_HEAD(&new_page->lru);
		get_page(new_page);
		set_page_private(new_page, 0);
		kbase_free_page_later(kbdev, new_page);
		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
	} else if (status_not_movable) {
		err = -EINVAL;
	} else if (status_mapped) {
		err = kbasep_migrate_page_allocated_mapped(old_page, new_page);
	} else if (status_pt_mapped) {
		err = kbasep_migrate_page_pt_mapped(old_page, new_page);
	}

	if (err < 0 && err != -EAGAIN) {
		__ClearPageMovable(old_page);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
	}

	return err;
}

static void kbase_page_putback(struct page *p)
{
	bool status_mem_pool = false;
	bool status_free_isolated_in_progress = false;
	bool status_free_pt_isolated_in_progress = false;
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	struct kbase_device *kbdev = NULL;

	if (!kbase_is_page_migration_enabled())
		return;

	if (!page_md)
		return;

	spin_lock(&page_md->migrate_lock);

	if (WARN_ON(!IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return;
	}

	kbdev = page_md->kbdev;

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		status_mem_pool = true;
		break;
	case ALLOCATED_MAPPED:
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	case PT_MAPPED:
	case NOT_MOVABLE:

		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	case FREE_ISOLATED_IN_PROGRESS:
		status_free_isolated_in_progress = true;
		break;
	case FREE_PT_ISOLATED_IN_PROGRESS:
		status_free_pt_isolated_in_progress = true;
		break;
	default:

		break;
	}

	spin_unlock(&page_md->migrate_lock);

	if (status_mem_pool || status_free_isolated_in_progress ||
	    status_free_pt_isolated_in_progress) {
		__ClearPageMovable(p);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		if (!WARN_ON_ONCE(!kbdev)) {
			struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

			kbase_free_page_later(kbdev, p);
			queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
		}
	}
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static const struct movable_operations movable_ops = {
	.isolate_page = kbase_page_isolate,
	.migrate_page = kbase_page_migrate,
	.putback_page = kbase_page_putback,
};
#else
static const struct address_space_operations kbase_address_space_ops = {
	.isolate_page = kbase_page_isolate,
	.migratepage = kbase_page_migrate,
	.putback_page = kbase_page_putback,
};
#endif

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
void kbase_mem_migrate_set_address_space_ops(struct kbase_device *kbdev, struct file *const filp)
{
	if (!kbase_is_page_migration_enabled())
		return;

	mutex_lock(&kbdev->fw_load_lock);

	if (filp) {
		filp->f_inode->i_mapping->a_ops = &kbase_address_space_ops;

		if (!kbdev->mem_migrate.inode) {
			kbdev->mem_migrate.inode = filp->f_inode;

			atomic_inc(&filp->f_inode->i_count);
		} else {
			WARN_ON(kbdev->mem_migrate.inode != filp->f_inode);
		}
	}

	mutex_unlock(&kbdev->fw_load_lock);
}
#endif

void kbase_mem_migrate_init(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (kbase_page_migration_enabled < 0) {
		if (kbase_is_large_pages_enabled())
			static_branch_inc(&page_migration_static_key);
	} else {
		dev_info(kbdev->dev, "Page migration support explicitly %s at insmod.",
			 kbase_page_migration_enabled ? "enabled" : "disabled");
		if (kbase_page_migration_enabled)
			static_branch_inc(&page_migration_static_key);
	}

	spin_lock_init(&mem_migrate->free_pages_lock);
	INIT_LIST_HEAD(&mem_migrate->free_pages_list);

	mem_migrate->free_pages_workq =
		alloc_workqueue("free_pages_workq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	INIT_WORK(&mem_migrate->free_pages_work, kbase_free_pages_worker);
}

void kbase_mem_migrate_term(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (mem_migrate->free_pages_workq)
		destroy_workqueue(mem_migrate->free_pages_workq);
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	iput(mem_migrate->inode);
#endif
}
