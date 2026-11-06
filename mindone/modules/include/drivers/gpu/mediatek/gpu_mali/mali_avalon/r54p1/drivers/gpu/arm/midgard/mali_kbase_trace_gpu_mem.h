/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_TRACE_GPU_MEM_H_
#define _KBASE_TRACE_GPU_MEM_H_

#if IS_ENABLED(CONFIG_TRACE_GPU_MEM)
#include <trace/events/gpu_mem.h>
#endif

#define DEVICE_TGID ((u32)0U)

static void kbase_trace_gpu_mem_usage(struct kbase_device *kbdev, struct kbase_context *kctx)
{
#if IS_ENABLED(CONFIG_TRACE_GPU_MEM)
	lockdep_assert_held(&kbdev->gpu_mem_usage_lock);

	trace_gpu_mem_total(kbdev->id, DEVICE_TGID, kbdev->total_gpu_pages << PAGE_SHIFT);

	if (likely(kctx))
		trace_gpu_mem_total(kbdev->id, kctx->kprcs->tgid,
				    kctx->kprcs->total_gpu_pages << PAGE_SHIFT);
#else
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
#endif
}

static inline void kbase_trace_gpu_mem_usage_dec(struct kbase_device *kbdev,
						 struct kbase_context *kctx, size_t pages)
{
	spin_lock(&kbdev->gpu_mem_usage_lock);

	if (likely(kctx))
		kctx->kprcs->total_gpu_pages -= pages;

	kbdev->total_gpu_pages -= pages;

	kbase_trace_gpu_mem_usage(kbdev, kctx);

	spin_unlock(&kbdev->gpu_mem_usage_lock);
}

static inline void kbase_trace_gpu_mem_usage_inc(struct kbase_device *kbdev,
						 struct kbase_context *kctx, size_t pages)
{
	spin_lock(&kbdev->gpu_mem_usage_lock);

	if (likely(kctx))
		kctx->kprcs->total_gpu_pages += pages;

	kbdev->total_gpu_pages += pages;

	kbase_trace_gpu_mem_usage(kbdev, kctx);

	spin_unlock(&kbdev->gpu_mem_usage_lock);
}

void kbase_remove_dma_buf_usage(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc);

void kbase_add_dma_buf_usage(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc);

#endif
