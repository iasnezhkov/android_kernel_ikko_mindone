/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_DMA_BUF_H
#define MINDONE_COMPAT_DMA_BUF_H

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_DMA_BUF_CACHE_SGT_MAPPING
#define MINDONE_DMA_HEAP_GET_POOL_SIZE(fn)
#else
#define MINDONE_DMA_BUF_CACHE_SGT_MAPPING .cache_sgt_mapping = 1,
#define MINDONE_DMA_HEAP_GET_POOL_SIZE(fn) .get_pool_size = fn,
#endif

#endif
