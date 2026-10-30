// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>

#include <linux/dma-buf.h>
#include <asm/cacheflush.h>
#if IS_ENABLED(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif
#include <linux/dma-mapping.h>
#include <uapi/gpu/arm/midgard/mali_base_kernel.h>
#include <mali_kbase_hwaccess_time.h>
#include <mali_kbase_kinstr_jm.h>
#include <mali_kbase_mem_linux.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>
#include <linux/version.h>
#include <linux/ktime.h>
#include <linux/pfn.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <linux/version_compat_defs.h>

#if !MALI_USE_CSF

struct kbase_external_resource_list {
	u64 count;
	struct base_external_resource *ext_res;
};

static void kbasep_add_waiting_soft_job(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_add_tail(&katom->queue, &kctx->waiting_soft_jobs);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

void kbasep_remove_waiting_soft_job(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_del(&katom->queue);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

static void kbasep_add_waiting_with_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	katom->start_timestamp = ktime_get_raw();

	kbasep_add_waiting_soft_job(katom);

	if (!timer_pending(&kctx->soft_job_timeout)) {
		unsigned int timeout_ms =
			(unsigned int)atomic_read(&kctx->kbdev->js_data.soft_job_timeout_ms);
		mod_timer(&kctx->soft_job_timeout, jiffies + msecs_to_jiffies(timeout_ms));
	}
}

static int kbasep_read_soft_event_status(struct kbase_context *kctx, u64 evt, unsigned char *status)
{
	unsigned char *mapped_evt;
	struct kbase_vmap_struct map;

	mapped_evt = kbase_vmap_prot(kctx, evt, sizeof(*mapped_evt), KBASE_REG_CPU_RD, &map);
	if (!mapped_evt)
		return -EFAULT;

	*status = *mapped_evt;

	kbase_vunmap(kctx, &map);

	return 0;
}

static int kbasep_write_soft_event_status(struct kbase_context *kctx, u64 evt,
					  unsigned char new_status)
{
	unsigned char *mapped_evt;
	struct kbase_vmap_struct map;

	if ((new_status != BASE_JD_SOFT_EVENT_SET) && (new_status != BASE_JD_SOFT_EVENT_RESET))
		return -EINVAL;

	mapped_evt = kbase_vmap_prot(kctx, evt, sizeof(*mapped_evt), KBASE_REG_CPU_WR, &map);
	if (!mapped_evt)
		return -EFAULT;

	*mapped_evt = new_status;

	kbase_vunmap(kctx, &map);

	return 0;
}

static int kbase_dump_cpu_gpu_time(struct kbase_jd_atom *katom)
{
	struct kbase_vmap_struct map;
	void *user_result;
	struct timespec64 ts;
	struct base_dump_cpu_gpu_counters data;
	u64 system_time = 0ULL;
	u64 cycle_counter;
	u64 jc = katom->jc;
	struct kbase_context *kctx = katom->kctx;
	int pm_active_err;

	memset(&data, 0, sizeof(data));

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	atomic_inc(&kctx->kbdev->pm.gpu_users_waiting);
#endif
	pm_active_err = kbase_pm_context_active_handle_suspend(
		kctx->kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE);
	if (pm_active_err) {
		struct kbasep_js_device_data *js_devdata = &kctx->kbdev->js_data;

		mutex_lock(&js_devdata->runpool_mutex);
		list_add_tail(&katom->dep_item[1], &js_devdata->suspended_soft_jobs_list);
		mutex_unlock(&js_devdata->runpool_mutex);

		kbasep_add_waiting_soft_job(katom);

		return pm_active_err;
	}
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	else
		atomic_dec(&kctx->kbdev->pm.gpu_users_waiting);
#endif

	kbase_backend_get_gpu_time(kctx->kbdev, &cycle_counter, &system_time, &ts);

	kbase_pm_context_idle(kctx->kbdev);

	data.sec = (__u64)ts.tv_sec;
	data.usec = ts.tv_nsec / 1000;
	data.system_time = system_time;
	data.cycle_counter = cycle_counter;

	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	user_result = kbase_vmap_prot(kctx, jc, sizeof(data), KBASE_REG_GPU_WR, &map);
	if (!user_result)
		return 0;

	memcpy(user_result, &data, sizeof(data));

	kbase_vunmap(kctx, &map);

	katom->event_code = BASE_JD_EVENT_DONE;

	return 0;
}

#if IS_ENABLED(CONFIG_SYNC_FILE)

void kbase_soft_event_wait_callback(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	mutex_lock(&kctx->jctx.lock);
	kbasep_remove_waiting_soft_job(katom);
	kbase_finish_soft_job(katom);
	if (kbase_jd_done_nolock(katom, true))
		kbase_js_sched_all(kctx->kbdev);
	mutex_unlock(&kctx->jctx.lock);
}
#endif

static void kbasep_soft_event_complete_job(struct work_struct *work)
{
	struct kbase_jd_atom *katom = container_of(work, struct kbase_jd_atom, work);
	struct kbase_context *kctx = katom->kctx;
	int resched;

	mutex_lock(&kctx->jctx.lock);
	resched = kbase_jd_done_nolock(katom, true);
	mutex_unlock(&kctx->jctx.lock);

	if (resched)
		kbase_js_sched_all(kctx->kbdev);
}

void kbasep_complete_triggered_soft_events(struct kbase_context *kctx, u64 evt)
{
	int cancel_timer = 1;
	struct list_head *entry, *tmp;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		struct kbase_jd_atom *katom = list_entry(entry, struct kbase_jd_atom, queue);

		switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
		case BASE_JD_REQ_SOFT_EVENT_WAIT:
			if (katom->jc == evt) {
				list_del(&katom->queue);

				katom->event_code = BASE_JD_EVENT_DONE;
				INIT_WORK(&katom->work, kbasep_soft_event_complete_job);
				queue_work(kctx->jctx.job_done_wq, &katom->work);
			} else {

				cancel_timer = 0;
			}
			break;
#ifdef CONFIG_MALI_FENCE_DEBUG
		case BASE_JD_REQ_SOFT_FENCE_WAIT:

			cancel_timer = 0;
			break;
#endif
		}
	}

	if (cancel_timer)
		del_timer(&kctx->soft_job_timeout);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

#ifdef CONFIG_MALI_FENCE_DEBUG
static void kbase_fence_debug_check_atom(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct device *dev = kctx->kbdev->dev;
	int i;

	for (i = 0; i < 2; i++) {
		struct kbase_jd_atom *dep;

		list_for_each_entry(dep, &katom->dep_head[i], dep_item[i]) {
			if (dep->status == KBASE_JD_ATOM_STATE_UNUSED ||
			    dep->status == KBASE_JD_ATOM_STATE_COMPLETED)
				continue;

			if ((dep->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) ==
			    BASE_JD_REQ_SOFT_FENCE_TRIGGER) {

				struct kbase_sync_fence_info info;

				if (!kbase_sync_fence_in_info_get(dep, &info)) {
					dev_warn(dev,
						 "\tVictim trigger atom %d fence [%pK] %s: %s\n",
						 kbase_jd_atom_id(kctx, dep), info.fence, info.name,
						 kbase_sync_status_string(info.status));
				}
			}

			kbase_fence_debug_check_atom(dep);
		}
	}
}

static void kbase_fence_debug_wait_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct device *dev = katom->kctx->kbdev->dev;
	int timeout_ms = atomic_read(&kctx->kbdev->js_data.soft_job_timeout_ms);
	unsigned long lflags;
	struct kbase_sync_fence_info info;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);

	if (kbase_sync_fence_in_info_get(katom, &info)) {

		spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
		return;
	}

	dev_warn(dev, "ctx %d_%d: Atom %d still waiting for fence [%pK] after %dms\n", kctx->tgid,
		 kctx->id, kbase_jd_atom_id(kctx, katom), info.fence, timeout_ms);
	dev_warn(dev, "\tGuilty fence [%pK] %s: %s\n", info.fence, info.name,
		 kbase_sync_status_string(info.status));

	kbase_fence_debug_check_atom(katom);

	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);

	kbase_sync_fence_in_dump(katom);
}

struct kbase_fence_debug_work {
	struct kbase_jd_atom *katom;
	struct work_struct work;
};

static void kbase_fence_debug_wait_timeout_worker(struct work_struct *work)
{
	struct kbase_fence_debug_work *w = container_of(work, struct kbase_fence_debug_work, work);
	struct kbase_jd_atom *katom = w->katom;
	struct kbase_context *kctx = katom->kctx;

	mutex_lock(&kctx->jctx.lock);
	kbase_fence_debug_wait_timeout(katom);
	mutex_unlock(&kctx->jctx.lock);

	kfree(w);
}

static void kbase_fence_debug_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_fence_debug_work *work;
	struct kbase_context *kctx = katom->kctx;

	work = kzalloc(sizeof(struct kbase_fence_debug_work), GFP_ATOMIC);

	if (work) {
		work->katom = katom;
		INIT_WORK(&work->work, kbase_fence_debug_wait_timeout_worker);
		queue_work(kctx->jctx.job_done_wq, &work->work);
	}
}
#endif

void kbasep_soft_job_timeout_worker(struct timer_list *timer)
{
	struct kbase_context *kctx = container_of(timer, struct kbase_context, soft_job_timeout);
	u32 timeout_ms = (u32)atomic_read(&kctx->kbdev->js_data.soft_job_timeout_ms);
	ktime_t cur_time = ktime_get_raw();
	bool restarting = false;
	unsigned long lflags;
	struct list_head *entry, *tmp;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		struct kbase_jd_atom *katom = list_entry(entry, struct kbase_jd_atom, queue);
		s64 elapsed_time = ktime_to_ms(ktime_sub(cur_time, katom->start_timestamp));

		if (elapsed_time < (s64)timeout_ms) {
			restarting = true;
			continue;
		}

		switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
		case BASE_JD_REQ_SOFT_EVENT_WAIT:

			list_del(&katom->queue);

			katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
			INIT_WORK(&katom->work, kbasep_soft_event_complete_job);
			queue_work(kctx->jctx.job_done_wq, &katom->work);
			break;
#ifdef CONFIG_MALI_FENCE_DEBUG
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			kbase_fence_debug_timeout(katom);
			break;
#endif
		}
	}

	if (restarting)
		mod_timer(timer, jiffies + msecs_to_jiffies(timeout_ms));
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

static int kbasep_soft_event_wait(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned char status;

	if (kbasep_read_soft_event_status(kctx, katom->jc, &status)) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return 0;
	}

	if (status == BASE_JD_SOFT_EVENT_SET)
		return 0;

	kbasep_add_waiting_with_timeout(katom);

	return 1;
}

static void kbasep_soft_event_update_locked(struct kbase_jd_atom *katom, unsigned char new_status)
{

	struct kbase_context *kctx = katom->kctx;

	if (kbasep_write_soft_event_status(kctx, katom->jc, new_status) != 0) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return;
	}

	if (new_status == BASE_JD_SOFT_EVENT_SET)
		kbasep_complete_triggered_soft_events(kctx, katom->jc);
}

int kbase_soft_event_update(struct kbase_context *kctx, u64 event, unsigned char new_status)
{
	int err = 0;

	mutex_lock(&kctx->jctx.lock);

	if (kbasep_write_soft_event_status(kctx, event, new_status)) {
		err = -ENOENT;
		goto out;
	}

	if (new_status == BASE_JD_SOFT_EVENT_SET)
		kbasep_complete_triggered_soft_events(kctx, event);

out:
	mutex_unlock(&kctx->jctx.lock);

	return err;
}

static void kbasep_soft_event_cancel_job(struct kbase_jd_atom *katom)
{
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
	if (kbase_jd_done_nolock(katom, true))
		kbase_js_sched_all(katom->kctx->kbdev);
}

#if IS_ENABLED(CONFIG_MALI_VECTOR_DUMP) || MALI_UNIT_TEST
static void kbase_debug_copy_finish(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers = katom->softjob_data;
	unsigned int i;
	unsigned int nr = katom->nr_extres;

	if (!buffers)
		return;

	kbase_gpu_vm_lock(katom->kctx);
	for (i = 0; i < nr; i++) {
		uint p;
		struct kbase_mem_phy_alloc *gpu_alloc = buffers[i].gpu_alloc;

		if (!buffers[i].pages)
			break;
		for (p = 0; p < buffers[i].nr_pages; p++) {
			struct page *pg = buffers[i].pages[p];

			if (pg)
				put_page(pg);
		}
		if (buffers[i].is_vmalloc)
			vfree(buffers[i].pages);
		else
			kfree(buffers[i].pages);
		if (gpu_alloc) {
			switch (gpu_alloc->type) {
			case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
				kbase_free_user_buffer(&buffers[i]);
				break;
			}
			default:

				break;
			}
			kbase_mem_phy_alloc_put(gpu_alloc);
		}
	}
	kbase_gpu_vm_unlock(katom->kctx);
	kfree(buffers);

	katom->softjob_data = NULL;
}

static int kbase_debug_copy_prepare(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers;
	struct base_jd_debug_copy_buffer *user_buffers = NULL;
	unsigned int i;
	unsigned int nr = katom->nr_extres;
	int ret = 0;
	void __user *user_structs = (void __user *)(uintptr_t)katom->jc;
	size_t copy_size;

	if (!user_structs)
		return -EINVAL;

	buffers = kcalloc(nr, sizeof(*buffers), GFP_KERNEL);
	if (!buffers) {
		ret = -ENOMEM;
		goto out_cleanup;
	}
	katom->softjob_data = buffers;

	user_buffers = kmalloc_array(nr, sizeof(*user_buffers), GFP_KERNEL);

	if (!user_buffers) {
		ret = -ENOMEM;
		goto out_cleanup;
	}

	if (check_mul_overflow(sizeof(*user_buffers), (size_t)nr, &copy_size)) {
		ret = -EINVAL;
		goto out_cleanup;
	}

	ret = copy_from_user(user_buffers, user_structs, copy_size);
	if (ret) {
		ret = -EFAULT;
		goto out_cleanup;
	}

	for (i = 0; i < nr; i++) {
		u64 addr = user_buffers[i].address;
		u64 page_addr = addr & PAGE_MASK;
		u64 end_page_addr = addr + user_buffers[i].size - 1;
		u64 last_page_addr = end_page_addr & PAGE_MASK;
		size_t nr_pages = (last_page_addr - page_addr) / PAGE_SIZE + 1;
		int pinned_pages;
		struct kbase_va_region *reg;
		struct base_external_resource user_extres;

		if (!addr)
			continue;

		if (last_page_addr < page_addr) {
			ret = -EINVAL;
			goto out_cleanup;
		}

		buffers[i].nr_pages = nr_pages;
		buffers[i].offset = addr & ~PAGE_MASK;
		if (buffers[i].offset >= PAGE_SIZE) {
			ret = -EINVAL;
			goto out_cleanup;
		}
		buffers[i].size = user_buffers[i].size;

		if (nr_pages > (KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD / sizeof(struct page *))) {
			buffers[i].is_vmalloc = true;
			buffers[i].pages = vzalloc(nr_pages * sizeof(struct page *));
		} else {
			buffers[i].is_vmalloc = false;
			buffers[i].pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
		}

		if (!buffers[i].pages) {
			ret = -ENOMEM;
			goto out_cleanup;
		}

		pinned_pages = get_user_pages_fast(page_addr, nr_pages, 1,
						   buffers[i].pages);
		if (pinned_pages < 0) {

			buffers[i].nr_pages = 0;

			ret = pinned_pages;
			goto out_cleanup;
		}
		if ((size_t)pinned_pages != nr_pages) {

			buffers[i].nr_pages = (unsigned int)pinned_pages;

			ret = -EINVAL;
			goto out_cleanup;
		}

		user_extres = user_buffers[i].extres;
		if (user_extres.ext_resource == 0ULL) {
			ret = -EINVAL;
			goto out_cleanup;
		}

		kbase_gpu_vm_lock(katom->kctx);
		reg = kbase_region_tracker_find_region_enclosing_address(
			katom->kctx,
			user_extres.ext_resource & ~(__u64)BASE_EXT_RES_ACCESS_EXCLUSIVE);

		if (kbase_is_region_invalid_or_free(reg) || reg->gpu_alloc == NULL) {
			ret = -EINVAL;
			goto out_unlock;
		}

		buffers[i].gpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);
		buffers[i].nr_extres_pages = reg->nr_pages;

		if (reg->nr_pages * PAGE_SIZE != buffers[i].size)
			dev_warn(
				katom->kctx->kbdev->dev,
				"Copy buffer is not of same size as the external resource to copy.\n");

		switch (reg->gpu_alloc->type) {
		case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
			struct kbase_mem_phy_alloc *alloc = reg->gpu_alloc;
			const unsigned long nr_pages = alloc->imported.user_buf.nr_pages;
			const unsigned long start = alloc->imported.user_buf.address;

			if (alloc->imported.user_buf.mm != current->mm) {
				ret = -EINVAL;
				goto out_unlock;
			}
			buffers[i].extres_pages =
				kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
			if (!buffers[i].extres_pages) {
				ret = -ENOMEM;
				goto out_unlock;
			}
			kbase_gpu_vm_unlock(katom->kctx);
			ret = get_user_pages_fast(start, nr_pages, 0, buffers[i].extres_pages);
			kbase_gpu_vm_lock(katom->kctx);
			if (ret != (int)nr_pages) {

				if (ret < 0)
					buffers[i].nr_extres_pages = 0;
				else
					buffers[i].nr_extres_pages = (unsigned int)ret;

				goto out_unlock;
			}
			ret = 0;
			break;
		}
		default:

			break;
		}
		kbase_gpu_vm_unlock(katom->kctx);
	}
	kfree(user_buffers);

	return ret;

out_unlock:
	kbase_gpu_vm_unlock(katom->kctx);

out_cleanup:

	kbase_debug_copy_finish(katom);
	kfree(user_buffers);

	return ret;
}

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
static void *dma_buf_kmap_page(struct kbase_mem_phy_alloc *gpu_alloc, unsigned long page_num,
			       struct page **page)
{
	struct sg_table *sgt = gpu_alloc->imported.umm.sgt;
	struct sg_page_iter sg_iter;
	unsigned long page_index = 0;

	if (WARN_ON(gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM))
		return NULL;

	if (!sgt)
		return NULL;

	if (WARN_ON(page_num >= gpu_alloc->nents))
		return NULL;

	for_each_sg_page(sgt->sgl, &sg_iter, sgt->nents, 0) {
		if (page_index == page_num) {
			*page = sg_page_iter_page(&sg_iter);

			return kbase_kmap(*page);
		}
		page_index++;
	}

	return NULL;
}
#endif

static int kbase_mem_copy_from_extres(struct kbase_context *kctx,
				      struct kbase_debug_copy_buffer *buf_data)
{
	unsigned int i;
	unsigned int target_page_nr = 0;
	struct page **pages = buf_data->pages;
	u64 offset = buf_data->offset;
	size_t extres_size = buf_data->nr_extres_pages * PAGE_SIZE;
	size_t to_copy = min(extres_size, buf_data->size);
	struct kbase_mem_phy_alloc *gpu_alloc = buf_data->gpu_alloc;
	int ret = 0;
	size_t dma_to_copy;

	KBASE_DEBUG_ASSERT(pages != NULL);

	kbase_gpu_vm_lock(kctx);
	if (!gpu_alloc) {
		ret = -EINVAL;
		goto out_unlock;
	}

	switch (gpu_alloc->type) {
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
		for (i = 0; i < buf_data->nr_extres_pages && target_page_nr < buf_data->nr_pages;
		     i++) {
			struct page *pg = buf_data->extres_pages[i];
			void *extres_page = kbase_kmap(pg);
			if (extres_page) {
				ret = kbase_mem_copy_to_pinned_user_pages(pages, extres_page,
									  &to_copy,
									  buf_data->nr_pages,
									  &target_page_nr, offset);
				kbase_kunmap(pg, extres_page);
				if (ret)
					goto out_unlock;
			}
		}
	} break;
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		struct dma_buf *dma_buf = gpu_alloc->imported.umm.dma_buf;

		KBASE_DEBUG_ASSERT(dma_buf != NULL);
		if (dma_buf->size > buf_data->nr_extres_pages * PAGE_SIZE)
			dev_warn(kctx->kbdev->dev, "External resources buffer size mismatch");

		dma_to_copy = min(dma_buf->size, (size_t)(buf_data->nr_extres_pages * PAGE_SIZE));
		ret = dma_buf_begin_cpu_access(dma_buf, DMA_FROM_DEVICE);
		if (ret)
			goto out_unlock;

		for (i = 0; i < dma_to_copy / PAGE_SIZE && target_page_nr < buf_data->nr_pages;
		     i++) {
#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
			struct page *pg;
			void *extres_page = dma_buf_kmap_page(gpu_alloc, i, &pg);
#else
			void *extres_page = dma_buf_kmap(dma_buf, i);
#endif
			if (extres_page) {
				ret = kbase_mem_copy_to_pinned_user_pages(pages, extres_page,
									  &to_copy,
									  buf_data->nr_pages,
									  &target_page_nr, offset);

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
				kbase_kunmap(pg, extres_page);
#else
				dma_buf_kunmap(dma_buf, i, extres_page);
#endif
				if (ret)
					break;
			}
		}
		dma_buf_end_cpu_access(dma_buf, DMA_FROM_DEVICE);
		break;
	}
	default:
		ret = -EINVAL;
	}
out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;
}

static int kbase_debug_copy(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers = katom->softjob_data;
	unsigned int i;

	if (WARN_ON(!buffers))
		return -EINVAL;

	for (i = 0; i < katom->nr_extres; i++) {
		int res = kbase_mem_copy_from_extres(katom->kctx, &buffers[i]);

		if (res)
			return res;
	}

	return 0;
}
#endif
#endif

#define KBASEP_JIT_ALLOC_GPU_ADDR_ALIGNMENT ((u32)0x7)

int kbasep_jit_alloc_validate(struct kbase_context *kctx, struct base_jit_alloc_info *info)
{
	size_t j;

	CSTD_UNUSED(kctx);

	if (info->id == 0)
		return -EINVAL;

	if (info->va_pages < info->commit_pages)
		return -EINVAL;

	if ((info->gpu_alloc_addr & KBASEP_JIT_ALLOC_GPU_ADDR_ALIGNMENT) != 0)
		return -EINVAL;

	for (j = 0; j < sizeof(info->padding); j++) {
		if (info->padding[j] != 0)
			return -EINVAL;
	}

	if (info->flags & ~(BASE_JIT_ALLOC_VALID_FLAGS))
		return -EINVAL;

#if !MALI_JIT_PRESSURE_LIMIT_BASE

	if (info->heap_info_gpu_addr)
		return -EINVAL;
#endif

#if !MALI_USE_CSF

	if ((info->flags & BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE) && !info->heap_info_gpu_addr)
		return -EINVAL;
#endif

	return 0;
}

#if !MALI_USE_CSF

static int kbase_jit_allocate_prepare(struct kbase_jd_atom *katom)
{
	__user u8 *data = (__user u8 *)(uintptr_t)katom->jc;
	struct base_jit_alloc_info *info;
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	u32 count;
	int ret;
	u32 i;

	if (!kbase_mem_allow_alloc(kctx)) {
		dev_dbg(kbdev->dev, "Invalid attempt to allocate JIT memory by %s/%d for ctx %d_%d",
			current->comm, current->pid, kctx->tgid, kctx->id);
		ret = -EINVAL;
		goto fail;
	}

	if (katom->nr_extres == 0)
		katom->nr_extres = 1;
	count = katom->nr_extres;

	if (!data || count > kctx->jit_max_allocations || count > ARRAY_SIZE(kctx->jit_alloc)) {
		ret = -EINVAL;
		goto fail;
	}

	info = kmalloc_array(count, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto fail;
	}

	katom->softjob_data = info;

	for (i = 0; i < count; i++, info++, data += sizeof(*info)) {
		if (copy_from_user(info, data, sizeof(*info)) != 0) {
			ret = -EINVAL;
			goto free_info;
		}

		ret = kbasep_jit_alloc_validate(kctx, info);
		if (ret)
			goto free_info;
		KBASE_TLSTREAM_TL_ATTRIB_ATOM_JITALLOCINFO(
			kbdev, katom, info->va_pages, info->commit_pages, info->extension, info->id,
			info->bin_id, info->max_allocations, info->flags, info->usage_id);
	}

	katom->jit_blocked = false;

	lockdep_assert_held(&kctx->jctx.lock);
	list_add_tail(&katom->jit_node, &kctx->jctx.jit_atoms_head);

	return 0;

free_info:
	kfree(katom->softjob_data);
	katom->softjob_data = NULL;
fail:
	return ret;
}

static u8 *kbase_jit_free_get_ids(struct kbase_jd_atom *katom)
{
	if (WARN_ON((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) != BASE_JD_REQ_SOFT_JIT_FREE))
		return NULL;

	return (u8 *)katom->softjob_data;
}

static void kbase_jit_add_to_pending_alloc_list(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct list_head *target_list_head = NULL;
	struct kbase_jd_atom *entry;

	list_for_each_entry(entry, &kctx->jctx.jit_pending_alloc, queue) {
		if (katom->age < entry->age) {
			target_list_head = &entry->queue;
			break;
		}
	}

	if (target_list_head == NULL)
		target_list_head = &kctx->jctx.jit_pending_alloc;

	list_add_tail(&katom->queue, target_list_head);
}

static int kbase_jit_allocate_process(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct base_jit_alloc_info *info;
	struct kbase_va_region *reg;
	struct kbase_vmap_struct mapping;
	u64 *ptr, new_addr;
	u32 count = katom->nr_extres;
	u32 i;
	bool ignore_pressure_limit = false;

	trace_sysgraph(SGR_SUBMIT, kctx->id, kbase_jd_atom_id(kctx, katom));

	if (katom->jit_blocked) {
		list_del(&katom->queue);
		katom->jit_blocked = false;
	}

	info = katom->softjob_data;
	if (WARN_ON(!info)) {
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return 0;
	}

	for (i = 0; i < count; i++, info++) {

		if (kctx->jit_alloc[info->id]) {
			katom->event_code = BASE_JD_EVENT_MEM_GROWTH_FAILED;
			return 0;
		}
	}

#if MALI_JIT_PRESSURE_LIMIT_BASE

	if (!kbase_ctx_flag(kctx, KCTX_JPL_ENABLED) || (kctx->jit_current_allocations == 0)) {
		ignore_pressure_limit = true;
	}
#else
	ignore_pressure_limit = true;
#endif

	for (i = 0, info = katom->softjob_data; i < count; i++, info++) {
		if (kctx->jit_alloc[info->id]) {

			u32 j;

			info = katom->softjob_data;
			for (j = 0; j < i; j++, info++) {
				kbase_jit_free(kctx, kctx->jit_alloc[info->id]);
				kctx->jit_alloc[info->id] = KBASE_RESERVED_REG_JIT_ALLOC;
			}

			katom->event_code = BASE_JD_EVENT_MEM_GROWTH_FAILED;
			return 0;
		}

		reg = kbase_jit_allocate(kctx, info, ignore_pressure_limit);
		if (!reg) {
			struct kbase_jd_atom *jit_atom;
			bool can_block = false;

			lockdep_assert_held(&kctx->jctx.lock);

			list_for_each_entry(jit_atom, &kctx->jctx.jit_atoms_head, jit_node) {
				if (jit_atom == katom)
					break;

				if ((jit_atom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) ==
				    BASE_JD_REQ_SOFT_JIT_FREE) {
					u8 *free_ids = kbase_jit_free_get_ids(jit_atom);

					if (free_ids && *free_ids && kctx->jit_alloc[*free_ids]) {

						can_block = true;
						break;
					}
				}
			}

			if (!can_block) {

				for (; i < count; i++, info++) {
					kctx->jit_alloc[info->id] = KBASE_RESERVED_REG_JIT_ALLOC;
				}

				katom->event_code = BASE_JD_EVENT_MEM_GROWTH_FAILED;
				dev_warn_ratelimited(kbdev->dev,
						     "JIT alloc softjob failed: atom id %d\n",
						     kbase_jd_atom_id(kctx, katom));
				return 0;
			}

			kbase_jit_add_to_pending_alloc_list(katom);
			katom->jit_blocked = true;

			while (i-- > 0) {
				info--;
				kbase_jit_free(kctx, kctx->jit_alloc[info->id]);
				kctx->jit_alloc[info->id] = NULL;
			}

			return 1;
		}

		kctx->jit_alloc[info->id] = reg;
	}

	for (i = 0, info = katom->softjob_data; i < count; i++, info++) {
		u64 entry_mmu_flags = 0;

		ptr = kbase_vmap_prot(kctx, info->gpu_alloc_addr, sizeof(*ptr), KBASE_REG_CPU_WR,
				      &mapping);
		if (!ptr) {

			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			return 0;
		}

		reg = kctx->jit_alloc[info->id];
		new_addr = reg->start_pfn << PAGE_SHIFT;
		*ptr = new_addr;

#if defined(CONFIG_MALI_VECTOR_DUMP)

		entry_mmu_flags = kbase_mmu_create_ate(kbdev, (struct tagged_addr){ 0 }, reg->flags,
						       MIDGARD_MMU_BOTTOMLEVEL, kctx->jit_group_id);
#endif

		KBASE_TLSTREAM_TL_ATTRIB_ATOM_JIT(kbdev, katom, info->gpu_alloc_addr, new_addr,
						  info->flags, entry_mmu_flags, info->id,
						  info->commit_pages, info->extension,
						  info->va_pages);
		kbase_vunmap(kctx, &mapping);

		kbase_trace_jit_report_gpu_mem(kctx, reg, KBASE_JIT_REPORT_ON_ALLOC_OR_FREE);
	}

	katom->event_code = BASE_JD_EVENT_DONE;

	return 0;
}

static void kbase_jit_allocate_finish(struct kbase_jd_atom *katom)
{
	struct base_jit_alloc_info *info;

	lockdep_assert_held(&katom->kctx->jctx.lock);

	if (WARN_ON(!katom->softjob_data))
		return;

	list_del(&katom->jit_node);

	if (katom->jit_blocked) {
		list_del(&katom->queue);
		katom->jit_blocked = false;
	}

	info = katom->softjob_data;

	kfree(info);
}

static int kbase_jit_free_prepare(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	__user void *data = (__user void *)(uintptr_t)katom->jc;
	u8 *ids;
	u32 count = MAX(katom->nr_extres, 1);
	u32 i;
	int ret;
	size_t copy_size;

	if (count > ARRAY_SIZE(kctx->jit_alloc)) {
		ret = -EINVAL;
		goto fail;
	}

	ids = kmalloc_array(count, sizeof(*ids), GFP_KERNEL);
	if (!ids) {
		ret = -ENOMEM;
		goto fail;
	}

	lockdep_assert_held(&kctx->jctx.lock);
	katom->softjob_data = ids;

	if (katom->nr_extres) {

		if (!data) {
			ret = -EINVAL;
			goto free_info;
		}
		if (check_mul_overflow(sizeof(*ids), (size_t)count, &copy_size)) {
			ret = -EINVAL;
			goto free_info;
		}

		if (copy_from_user(ids, data, copy_size) != 0) {
			ret = -EINVAL;
			goto free_info;
		}
	} else {
		katom->nr_extres = 1;
		*ids = (u8)katom->jc;
	}
	for (i = 0; i < count; i++)
		KBASE_TLSTREAM_TL_ATTRIB_ATOM_JITFREEINFO(kbdev, katom, ids[i]);

	list_add_tail(&katom->jit_node, &kctx->jctx.jit_atoms_head);

	return 0;

free_info:
	kfree(katom->softjob_data);
	katom->softjob_data = NULL;
fail:
	return ret;
}

static void kbase_jit_free_process(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	u8 *ids = kbase_jit_free_get_ids(katom);
	u32 count = katom->nr_extres;
	u32 i;

	if (ids == NULL) {
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return;
	}

	for (i = 0; i < count; i++, ids++) {

		if ((*ids == 0) || (kctx->jit_alloc[*ids] == NULL)) {
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			return;
		}
	}
}

static void kbasep_jit_finish_worker(struct work_struct *work)
{
	struct kbase_jd_atom *katom = container_of(work, struct kbase_jd_atom, work);
	struct kbase_context *kctx = katom->kctx;
	int resched;

	mutex_lock(&kctx->jctx.lock);
	kbase_finish_soft_job(katom);
	resched = kbase_jd_done_nolock(katom, true);
	mutex_unlock(&kctx->jctx.lock);

	if (resched)
		kbase_js_sched_all(kctx->kbdev);
}

void kbase_jit_retry_pending_alloc(struct kbase_context *kctx)
{
	LIST_HEAD(jit_pending_alloc_list);
	struct list_head *i, *tmp;

	list_splice_tail_init(&kctx->jctx.jit_pending_alloc, &jit_pending_alloc_list);

	list_for_each_safe(i, tmp, &jit_pending_alloc_list) {
		struct kbase_jd_atom *pending_atom = list_entry(i, struct kbase_jd_atom, queue);
		KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_START(kctx->kbdev, pending_atom);
		kbase_kinstr_jm_atom_sw_start(pending_atom);
		if (kbase_jit_allocate_process(pending_atom) == 0) {

			INIT_WORK(&pending_atom->work, kbasep_jit_finish_worker);
			queue_work(kctx->jctx.job_done_wq, &pending_atom->work);
		}
		KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_END(kctx->kbdev, pending_atom);
		kbase_kinstr_jm_atom_sw_stop(pending_atom);
	}
}

static void kbase_jit_free_finish(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	u8 *ids;
	size_t j;

	lockdep_assert_held(&kctx->jctx.lock);

	ids = kbase_jit_free_get_ids(katom);
	if (WARN_ON(ids == NULL))
		return;

	list_del(&katom->jit_node);

	for (j = 0; j != katom->nr_extres; ++j) {
		if ((ids[j] != 0) && (kctx->jit_alloc[ids[j]] != NULL)) {

			if (kctx->jit_alloc[ids[j]] != KBASE_RESERVED_REG_JIT_ALLOC) {
				KBASE_TLSTREAM_TL_JIT_USEDPAGES(
					kctx->kbdev, kctx->jit_alloc[ids[j]]->gpu_alloc->nents,
					ids[j]);
				kbase_jit_free(kctx, kctx->jit_alloc[ids[j]]);
			}
			kctx->jit_alloc[ids[j]] = NULL;
		}
	}

	kfree(ids);

	kbase_jit_retry_pending_alloc(kctx);
}

static int kbase_ext_res_prepare(struct kbase_jd_atom *katom)
{
	__user struct base_external_resource_list *user_ext_res;
	struct kbase_external_resource_list *ext_res;
	u64 count = 0;
	size_t copy_size;

	user_ext_res = (__user struct base_external_resource_list *)(uintptr_t)katom->jc;

	if (!user_ext_res)
		return -EINVAL;

	if (copy_from_user(&count, &user_ext_res->count, sizeof(u64)) != 0)
		return -EINVAL;

	if (!count || count > BASE_EXT_RES_COUNT_MAX)
		return -EINVAL;

	copy_size = sizeof(struct base_external_resource) * count;
	ext_res = kmalloc(sizeof(*ext_res) + copy_size, GFP_KERNEL);
	if (!ext_res)
		return -ENOMEM;

	ext_res->count = count;
	ext_res->ext_res = (struct base_external_resource *)(ext_res + 1);
	if (copy_from_user(ext_res->ext_res, user_ext_res->ext_res, copy_size) != 0) {
		kfree(ext_res);
		return -EINVAL;
	}

	katom->softjob_data = ext_res;

	return 0;
}

static void kbase_ext_res_process(struct kbase_jd_atom *katom, bool map)
{
	struct kbase_external_resource_list *ext_res;
	uint64_t i;
	bool failed = false;

	ext_res = katom->softjob_data;
	if (!ext_res)
		goto failed_jc;

	kbase_gpu_vm_lock(katom->kctx);

	for (i = 0; i < ext_res->count; i++) {
		u64 gpu_addr;

		gpu_addr = ext_res->ext_res[i].ext_resource & ~(__u64)BASE_EXT_RES_ACCESS_EXCLUSIVE;
		if (map) {
			if (!kbase_sticky_resource_acquire(katom->kctx, gpu_addr, NULL))
				goto failed_loop;
		} else {
			if (!kbase_sticky_resource_release_force(katom->kctx, NULL, gpu_addr))
				failed = true;
		}
	}

	if (failed)
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
	else
		katom->event_code = BASE_JD_EVENT_DONE;

	kbase_gpu_vm_unlock(katom->kctx);

	return;

failed_loop:
	while (i > 0) {
		u64 const gpu_addr = ext_res->ext_res[i - 1].ext_resource &
				     ~(__u64)BASE_EXT_RES_ACCESS_EXCLUSIVE;

		kbase_sticky_resource_release_force(katom->kctx, NULL, gpu_addr);

		--i;
	}

	katom->event_code = BASE_JD_EVENT_JOB_INVALID;
	kbase_gpu_vm_unlock(katom->kctx);

failed_jc:
	return;
}

static void kbase_ext_res_finish(struct kbase_jd_atom *katom)
{
	struct kbase_external_resource_list *ext_res;

	ext_res = katom->softjob_data;

	kfree(ext_res);
}

int kbase_process_soft_job(struct kbase_jd_atom *katom)
{
	int ret = 0;
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;

	KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_START(kbdev, katom);
	kbase_kinstr_jm_atom_sw_start(katom);

	trace_sysgraph(SGR_SUBMIT, kctx->id, kbase_jd_atom_id(kctx, katom));

	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
		ret = kbase_dump_cpu_gpu_time(katom);
		break;

#if IS_ENABLED(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		katom->event_code = kbase_sync_fence_out_trigger(
			katom, katom->event_code == BASE_JD_EVENT_DONE ? 0 : -EFAULT);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT: {
		ret = kbase_sync_fence_in_wait(katom);

		if (ret == 1) {
#ifdef CONFIG_MALI_FENCE_DEBUG
			kbasep_add_waiting_with_timeout(katom);
#else
			kbasep_add_waiting_soft_job(katom);
#endif
		}
		break;
	}
#endif
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
		ret = kbasep_soft_event_wait(katom);
		break;
	case BASE_JD_REQ_SOFT_EVENT_SET:
		kbasep_soft_event_update_locked(katom, BASE_JD_SOFT_EVENT_SET);
		break;
	case BASE_JD_REQ_SOFT_EVENT_RESET:
		kbasep_soft_event_update_locked(katom, BASE_JD_SOFT_EVENT_RESET);
		break;
#if IS_ENABLED(CONFIG_MALI_VECTOR_DUMP) || MALI_UNIT_TEST
	case BASE_JD_REQ_SOFT_DEBUG_COPY: {
		int res = kbase_debug_copy(katom);

		if (res)
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		break;
	}
#endif
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		ret = kbase_jit_allocate_process(katom);
		break;
	case BASE_JD_REQ_SOFT_JIT_FREE:
		kbase_jit_free_process(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		kbase_ext_res_process(katom, true);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		kbase_ext_res_process(katom, false);
		break;
	}

	KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_END(kbdev, katom);
	kbase_kinstr_jm_atom_sw_stop(katom);
	return ret;
}

void kbase_cancel_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
#if IS_ENABLED(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		kbase_sync_fence_in_cancel_wait(katom);
		break;
#endif
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
		kbasep_soft_event_cancel_job(katom);
		break;
	default:

		KBASE_DEBUG_ASSERT(0);
	}
}

int kbase_prepare_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME: {
		if (!IS_ALIGNED(katom->jc, cache_line_size()))
			return -EINVAL;
	} break;
#if IS_ENABLED(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER: {
		struct base_fence fence;
		int fd;

		if (copy_from_user(&fence, (__user void *)(uintptr_t)katom->jc, sizeof(fence)) != 0)
			return -EINVAL;

		fd = kbase_sync_fence_out_create(katom, fence.basep.stream_fd);
		if (fd < 0)
			return -EINVAL;

		fence.basep.fd = fd;
		if (copy_to_user((__user void *)(uintptr_t)katom->jc, &fence, sizeof(fence)) != 0) {
			kbase_sync_fence_out_remove(katom);

			fence.basep.fd = -EINVAL;
			return -EINVAL;
		}
	} break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT: {
		struct base_fence fence;
		int ret;

		if (copy_from_user(&fence, (__user void *)(uintptr_t)katom->jc, sizeof(fence)) != 0)
			return -EINVAL;

		ret = kbase_sync_fence_in_from_fd(katom, fence.basep.fd);
		if (ret < 0)
			return ret;
	} break;
#endif
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		return kbase_jit_allocate_prepare(katom);
	case BASE_JD_REQ_SOFT_JIT_FREE:
		return kbase_jit_free_prepare(katom);
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
	case BASE_JD_REQ_SOFT_EVENT_SET:
	case BASE_JD_REQ_SOFT_EVENT_RESET:
		if (katom->jc == 0)
			return -EINVAL;
		break;
#if IS_ENABLED(CONFIG_MALI_VECTOR_DUMP) || MALI_UNIT_TEST
	case BASE_JD_REQ_SOFT_DEBUG_COPY:
		return kbase_debug_copy_prepare(katom);
#endif
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		return kbase_ext_res_prepare(katom);
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		return kbase_ext_res_prepare(katom);
	default:

		return -EINVAL;
	}
	return 0;
}

void kbase_finish_soft_job(struct kbase_jd_atom *katom)
{
	trace_sysgraph(SGR_COMPLETE, katom->kctx->id, kbase_jd_atom_id(katom->kctx, katom));

	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:

		break;
#if IS_ENABLED(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:

		kbase_sync_fence_out_trigger(katom,
					     katom->event_code == BASE_JD_EVENT_DONE ? 0 : -EFAULT);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:

		kbase_sync_fence_in_remove(katom);
		break;
#endif
#if IS_ENABLED(CONFIG_MALI_VECTOR_DUMP) || MALI_UNIT_TEST
	case BASE_JD_REQ_SOFT_DEBUG_COPY:
		kbase_debug_copy_finish(katom);
		break;
#endif
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		kbase_jit_allocate_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		kbase_ext_res_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		kbase_ext_res_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_JIT_FREE:
		kbase_jit_free_finish(katom);
		break;
	}
}

void kbase_resume_suspended_soft_jobs(struct kbase_device *kbdev)
{
	LIST_HEAD(local_suspended_soft_jobs);
	struct kbase_jd_atom *tmp_iter;
	struct kbase_jd_atom *katom_iter;
	struct kbasep_js_device_data *js_devdata;
	bool resched = false;

	KBASE_DEBUG_ASSERT(kbdev);

	js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->runpool_mutex);
	list_splice_init(&js_devdata->suspended_soft_jobs_list, &local_suspended_soft_jobs);
	mutex_unlock(&js_devdata->runpool_mutex);

	list_for_each_entry_safe(katom_iter, tmp_iter, &local_suspended_soft_jobs, dep_item[1]) {
		struct kbase_context *kctx = katom_iter->kctx;

		mutex_lock(&kctx->jctx.lock);

		list_del(&katom_iter->dep_item[1]);

		kbasep_remove_waiting_soft_job(katom_iter);

		if (kbase_process_soft_job(katom_iter) == 0) {
			kbase_finish_soft_job(katom_iter);
			resched |= kbase_jd_done_nolock(katom_iter, true);
#ifdef CONFIG_MALI_ARBITER_SUPPORT
			atomic_dec(&kbdev->pm.gpu_users_waiting);
#endif
		}
		mutex_unlock(&kctx->jctx.lock);
	}

	if (resched)
		kbase_js_sched_all(kbdev);
}
#endif
