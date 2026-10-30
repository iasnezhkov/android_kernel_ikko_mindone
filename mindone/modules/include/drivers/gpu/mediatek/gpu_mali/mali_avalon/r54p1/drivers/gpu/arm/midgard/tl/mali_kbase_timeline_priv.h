/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_TIMELINE_PRIV_H)
#define _KBASE_TIMELINE_PRIV_H

#include <mali_kbase.h>
#include "mali_kbase_tlstream.h"

#if MALI_USE_CSF
#include "csf/mali_kbase_csf_tl_reader.h"
#include "csf/mali_kbase_csf_trace_buffer.h"
#endif

#include <linux/timer.h>
#include <linux/atomic.h>
#include <linux/mutex.h>

#define TIMELINE_HYSTERESIS_TIMEOUT_MS ((s64)500)

struct kbase_timeline {
	struct kbase_tlstream streams[TL_STREAM_TYPE_COUNT];
	struct list_head tl_kctx_list;
	struct mutex tl_kctx_list_lock;
	struct timer_list autoflush_timer;
	atomic_t autoflush_timer_active;
	struct mutex reader_lock;
	wait_queue_head_t event_queue;
#if MALI_UNIT_TEST
	atomic_t bytes_collected;
#endif
	atomic_t *timeline_flags;
	size_t obj_header_btc;
	size_t aux_header_btc;
	ktime_t last_acquire_time;
#if MALI_USE_CSF
	struct kbase_csf_tl_reader csf_tl_reader;
#endif
};

void kbase_create_timeline_objects(struct kbase_device *kbdev);

int kbase_timeline_acquire(struct kbase_device *kbdev, u32 flags);

void kbase_timeline_release(struct kbase_timeline *timeline);

#endif
