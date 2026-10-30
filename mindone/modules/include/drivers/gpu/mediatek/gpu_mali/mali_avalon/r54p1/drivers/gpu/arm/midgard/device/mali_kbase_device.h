/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_KBASE_DEVICE_H_
#define _MALI_KBASE_DEVICE_H_

#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access.h>

const struct list_head *kbase_device_get_list(void);

void kbase_device_put_list(const struct list_head *dev_list);

void kbase_increment_device_id(void);

int kbase_device_firmware_init_once(struct kbase_device *kbdev);

int kbase_device_init(struct kbase_device *kbdev);

void kbase_device_term(struct kbase_device *kbdev);

bool kbase_is_gpu_removed(struct kbase_device *kbdev);

#if MALI_USE_CSF
int kbase_gpu_cache_flush_pa_range_and_busy_wait(struct kbase_device *kbdev, phys_addr_t phys,
						 size_t nr_bytes, u32 flush_op);
#endif

int kbase_gpu_cache_flush_and_busy_wait(struct kbase_device *kbdev, u32 flush_op);

void kbase_gpu_start_cache_clean(struct kbase_device *kbdev, u32 flush_op);

void kbase_gpu_start_cache_clean_nolock(struct kbase_device *kbdev, u32 flush_op);

void kbase_gpu_wait_cache_clean(struct kbase_device *kbdev);

int kbase_gpu_wait_cache_clean_timeout(struct kbase_device *kbdev, unsigned int wait_timeout_ms);

void kbase_gpu_cache_clean_wait_complete(struct kbase_device *kbdev);

void kbase_clean_caches_done(struct kbase_device *kbdev);

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val);

#endif
