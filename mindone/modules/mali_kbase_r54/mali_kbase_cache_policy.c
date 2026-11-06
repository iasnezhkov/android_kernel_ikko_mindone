// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase_cache_policy.h>
#include <mali_kbase.h>

u32 kbase_cache_enabled(u32 flags, u32 nr_pages)
{
	u32 cache_flags = 0;

	CSTD_UNUSED(nr_pages);

	if (!(flags & BASE_MEM_UNCACHED_GPU))
		cache_flags |= KBASE_REG_GPU_CACHED;

	if (flags & BASE_MEM_CACHED_CPU)
		cache_flags |= KBASE_REG_CPU_CACHED;

	return cache_flags;
}

void kbase_sync_single_for_device(struct kbase_device *kbdev, dma_addr_t handle, size_t size,
				  enum dma_data_direction dir)
{
	dma_sync_single_for_device(kbdev->dev, handle, size, dir);
}
KBASE_EXPORT_TEST_API(kbase_sync_single_for_device);

void kbase_sync_single_for_cpu(struct kbase_device *kbdev, dma_addr_t handle, size_t size,
			       enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(kbdev->dev, handle, size, dir);
}
KBASE_EXPORT_TEST_API(kbase_sync_single_for_cpu);
