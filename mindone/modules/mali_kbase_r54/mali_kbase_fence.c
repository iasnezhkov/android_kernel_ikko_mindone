// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <mali_kbase_fence.h>
#include <mali_kbase.h>

#include <linux/version_compat_defs.h>

static DEFINE_SPINLOCK(kbase_fence_lock);

struct dma_fence *kbase_fence_out_new(struct kbase_jd_atom *katom)
{
	struct dma_fence *fence;

	WARN_ON(katom->dma_fence.fence);

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	dma_fence_init(fence, &kbase_fence_ops, &kbase_fence_lock, katom->dma_fence.context,
		       (u64)atomic_inc_return(&katom->dma_fence.seqno));

	katom->dma_fence.fence = fence;

	return fence;
}
