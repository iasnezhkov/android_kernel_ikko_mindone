/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_TIMELINE_H)
#define _KBASE_TIMELINE_H

#include <mali_kbase.h>

struct kbase_timeline;

int kbase_timeline_init(struct kbase_timeline **timeline, atomic_t *timeline_flags);

void kbase_timeline_term(struct kbase_timeline *timeline);

int kbase_timeline_io_acquire(struct kbase_device *kbdev, u32 flags);

int kbase_timeline_streams_flush(struct kbase_timeline *timeline);

void kbase_timeline_streams_body_reset(struct kbase_timeline *timeline);

void kbase_timeline_post_kbase_context_create(struct kbase_context *kctx);

void kbase_timeline_pre_kbase_context_destroy(struct kbase_context *kctx);

void kbase_timeline_post_kbase_context_destroy(struct kbase_context *kctx);

#if MALI_UNIT_TEST

void kbase_timeline_stats(struct kbase_timeline *timeline, u32 *bytes_collected,
			  u32 *bytes_generated);
#endif

void kbase_timeline_io_debugfs_init(struct kbase_device *kbdev);

#endif
