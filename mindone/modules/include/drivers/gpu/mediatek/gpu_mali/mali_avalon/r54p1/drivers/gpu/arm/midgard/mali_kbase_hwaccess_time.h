/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_BACKEND_TIME_H_
#define _KBASE_BACKEND_TIME_H_

struct kbase_backend_time {
#if MALI_USE_CSF
	u64 multiplier;
	u64 divisor;
	s64 gpu_timestamp_offset;
#endif
	unsigned int device_scaled_timeouts[KBASE_TIMEOUT_SELECTOR_COUNT];
};

#if MALI_USE_CSF

u64 __maybe_unused kbase_backend_time_convert_gpu_to_cpu(struct kbase_device *kbdev, u64 gpu_ts);

void kbase_backend_update_gpu_timestamp_offset(struct kbase_device *kbdev);

void kbase_backend_invalidate_gpu_timestamp_offset(struct kbase_device *kbdev);

#if MALI_UNIT_TEST

u64 kbase_backend_read_gpu_timestamp_offset_reg(struct kbase_device *kbdev);
#endif
#endif

void kbase_backend_get_gpu_time(struct kbase_device *kbdev, u64 *cycle_counter, u64 *system_time,
				struct timespec64 *ts);

void kbase_backend_get_gpu_time_norequest(struct kbase_device *kbdev, u64 *cycle_counter,
					  u64 *system_time, struct timespec64 *ts);

void kbase_device_set_timeout_ms(struct kbase_device *kbdev, enum kbase_timeout_selector selector,
				 unsigned int timeout_ms);

void kbase_device_set_timeout(struct kbase_device *kbdev, enum kbase_timeout_selector selector,
			      u64 timeout_cycles, u32 cycle_multiplier);

unsigned int kbase_get_timeout_ms(struct kbase_device *kbdev, enum kbase_timeout_selector selector);

u64 kbase_backend_get_cycle_cnt(struct kbase_device *kbdev);

u64 kbase_arch_timer_get_cntfrq(struct kbase_device *kbdev);

int kbase_backend_time_init(struct kbase_device *kbdev);

#endif
