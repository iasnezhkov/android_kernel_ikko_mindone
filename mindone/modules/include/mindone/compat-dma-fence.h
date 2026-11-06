/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_DMA_FENCE_H
#define MINDONE_COMPAT_DMA_FENCE_H

#include <linux/dma-fence.h>
#include <linux/version.h>

static inline bool mindone_dma_fence_is_later(struct dma_fence *fence, u64 f1, u64 f2)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return __dma_fence_is_later(fence, f1, f2);
#else
	return __dma_fence_is_later(f1, f2, fence->ops);
#endif
}

#endif
