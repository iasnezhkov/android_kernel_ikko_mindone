// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_timeline.h"
#include "mali_kbase_timeline_priv.h"
#include "mali_kbase_tracepoints.h"

#include <mali_kbase.h>
#include <linux/atomic.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/delay.h>

#define AUTOFLUSH_INTERVAL 1000

const size_t __obj_stream_offset = offsetof(struct kbase_timeline, streams) +
				   sizeof(struct kbase_tlstream) * TL_STREAM_TYPE_OBJ;

const size_t __aux_stream_offset = offsetof(struct kbase_timeline, streams) +
				   sizeof(struct kbase_tlstream) * TL_STREAM_TYPE_AUX;

static void kbasep_timeline_autoflush_timer_callback(struct timer_list *timer)
{
	enum tl_stream_type stype;
	int rcode;
	struct kbase_timeline *timeline =
		container_of(timer, struct kbase_timeline, autoflush_timer);

	CSTD_UNUSED(timer);

	for (stype = (enum tl_stream_type)0; stype < TL_STREAM_TYPE_COUNT; stype++) {
		struct kbase_tlstream *stream = &timeline->streams[stype];

		int af_cnt = atomic_read(&stream->autoflush_counter);

		if (af_cnt < 0)
			continue;

		if (af_cnt != atomic_cmpxchg(&stream->autoflush_counter, af_cnt, af_cnt + 1))
			continue;
		if (!af_cnt)
			continue;

		kbase_tlstream_flush_stream(stream);
	}

	if (atomic_read(&timeline->autoflush_timer_active))
		rcode = mod_timer(&timeline->autoflush_timer,
				  jiffies + msecs_to_jiffies(AUTOFLUSH_INTERVAL));
	CSTD_UNUSED(rcode);
}

int kbase_timeline_init(struct kbase_timeline **timeline, atomic_t *timeline_flags)
{
	enum tl_stream_type i;
	struct kbase_timeline *result;
#if MALI_USE_CSF
	struct kbase_tlstream *csffw_stream;
#endif

	if (!timeline || !timeline_flags)
		return -EINVAL;

	result = vzalloc(sizeof(*result));
	if (!result)
		return -ENOMEM;

	mutex_init(&result->reader_lock);
	init_waitqueue_head(&result->event_queue);

	for (i = 0; i < TL_STREAM_TYPE_COUNT; i++)
		kbase_tlstream_init(&result->streams[i], i, &result->event_queue);

	mutex_init(&result->tl_kctx_list_lock);
	INIT_LIST_HEAD(&result->tl_kctx_list);

	atomic_set(&result->autoflush_timer_active, 0);
	kbase_timer_setup(&result->autoflush_timer, kbasep_timeline_autoflush_timer_callback);
	result->timeline_flags = timeline_flags;

#if MALI_USE_CSF
	csffw_stream = &result->streams[TL_STREAM_TYPE_CSFFW];
	kbase_csf_tl_reader_init(&result->csf_tl_reader, csffw_stream);
#endif

	*timeline = result;
	return 0;
}

void kbase_timeline_term(struct kbase_timeline *timeline)
{
	enum tl_stream_type i;

	if (!timeline)
		return;

#if MALI_USE_CSF
	kbase_csf_tl_reader_term(&timeline->csf_tl_reader);
#endif

	WARN_ON(!list_empty(&timeline->tl_kctx_list));

	for (i = (enum tl_stream_type)0; i < TL_STREAM_TYPE_COUNT; i++)
		kbase_tlstream_term(&timeline->streams[i]);

	vfree(timeline);
}

#ifdef CONFIG_MALI_DEVFREQ
static void kbase_tlstream_current_devfreq_target(struct kbase_device *kbdev)
{
	struct devfreq *devfreq = kbdev->devfreq;

	if (devfreq) {
		unsigned long cur_freq = 0;

		mutex_lock(&devfreq->lock);
		cur_freq = devfreq->last_status.current_frequency;
		KBASE_TLSTREAM_AUX_DEVFREQ_TARGET(kbdev, (u64)cur_freq);
		mutex_unlock(&devfreq->lock);
	}
}
#endif

int kbase_timeline_acquire(struct kbase_device *kbdev, u32 flags)
{
	int err = 0;
	u32 timeline_flags = TLSTREAM_ENABLED | flags;
	struct kbase_timeline *timeline;
	int rcode;

	if (WARN_ON(!kbdev) || WARN_ON(flags & ~BASE_TLSTREAM_FLAGS_MASK))
		return -EINVAL;

	timeline = kbdev->timeline;
	if (WARN_ON(!timeline))
		return -EFAULT;

	if (atomic_cmpxchg(timeline->timeline_flags, 0, (int)timeline_flags))
		return -EBUSY;

#if MALI_USE_CSF
	if (flags & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS) {
		err = kbase_csf_tl_reader_start(&timeline->csf_tl_reader, kbdev);
		if (err) {
			atomic_set(timeline->timeline_flags, 0);
			return err;
		}
	}
#endif

	kbase_tlstream_reset(&timeline->streams[TL_STREAM_TYPE_OBJ_SUMMARY]);

	timeline->obj_header_btc = obj_desc_header_size;
	timeline->aux_header_btc = aux_desc_header_size;

#if !MALI_USE_CSF

	if (flags & BASE_TLSTREAM_JOB_DUMPING_ENABLED) {
		dev_info(kbdev->dev,
			 "Job dumping is enabled, readjusting the software event's timeout\n");
		atomic_set(&kbdev->js_data.soft_job_timeout_ms, 1800000);
	}
#endif

	kbase_create_timeline_objects(kbdev);

#ifdef CONFIG_MALI_DEVFREQ

	kbase_tlstream_current_devfreq_target(kbdev);
#endif

	atomic_set(&timeline->autoflush_timer_active, 1);
	rcode = mod_timer(&timeline->autoflush_timer,
			  jiffies + msecs_to_jiffies(AUTOFLUSH_INTERVAL));
	CSTD_UNUSED(rcode);

	timeline->last_acquire_time = ktime_get_raw();

	return err;
}

void kbase_timeline_release(struct kbase_timeline *timeline)
{
	ktime_t elapsed_time;
	s64 elapsed_time_ms, time_to_sleep;

	if (WARN_ON(!timeline) || WARN_ON(!atomic_read(timeline->timeline_flags)))
		return;

	elapsed_time = ktime_sub(ktime_get_raw(), timeline->last_acquire_time);
	elapsed_time_ms = ktime_to_ms(elapsed_time);
	time_to_sleep = (elapsed_time_ms < 0 ? TIMELINE_HYSTERESIS_TIMEOUT_MS :
						     TIMELINE_HYSTERESIS_TIMEOUT_MS - elapsed_time_ms);
	if (time_to_sleep > 0)
		msleep_interruptible(time_to_sleep);

#if MALI_USE_CSF
	kbase_csf_tl_reader_stop(&timeline->csf_tl_reader);
#endif

	atomic_set(&timeline->autoflush_timer_active, 0);
	del_timer_sync(&timeline->autoflush_timer);

	atomic_set(timeline->timeline_flags, 0);
}

int kbase_timeline_streams_flush(struct kbase_timeline *timeline)
{
	enum tl_stream_type stype;
	bool has_bytes = false;
	size_t nbytes = 0;

	if (WARN_ON(!timeline))
		return -EINVAL;

#if MALI_USE_CSF
	{
		int ret = kbase_csf_tl_reader_flush_buffer(&timeline->csf_tl_reader);

		if (ret > 0)
			has_bytes = true;
	}
#endif

	for (stype = 0; stype < TL_STREAM_TYPE_COUNT; stype++) {
		nbytes = kbase_tlstream_flush_stream(&timeline->streams[stype]);
		if (nbytes > 0)
			has_bytes = true;
	}
	return has_bytes ? 0 : -EIO;
}

void kbase_timeline_streams_body_reset(struct kbase_timeline *timeline)
{
	kbase_tlstream_reset(&timeline->streams[TL_STREAM_TYPE_OBJ]);
	kbase_tlstream_reset(&timeline->streams[TL_STREAM_TYPE_AUX]);
#if MALI_USE_CSF
	kbase_tlstream_reset(&timeline->streams[TL_STREAM_TYPE_CSFFW]);
#endif
}

void kbase_timeline_pre_kbase_context_destroy(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_timeline *timeline = kbdev->timeline;

	mutex_lock(&timeline->tl_kctx_list_lock);
	list_del_init(&kctx->tl_kctx_list_node);
	mutex_unlock(&timeline->tl_kctx_list_lock);
}

void kbase_timeline_post_kbase_context_create(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_timeline *timeline = kbdev->timeline;

	mutex_lock(&timeline->tl_kctx_list_lock);

	list_add(&kctx->tl_kctx_list_node, &timeline->tl_kctx_list);

#if MALI_USE_CSF
	KBASE_TLSTREAM_TL_KBASE_NEW_CTX(kbdev, kctx->id, kbdev->id);
#endif

	KBASE_TLSTREAM_TL_NEW_CTX(kbdev, kctx, kctx->id, 0);

	mutex_unlock(&timeline->tl_kctx_list_lock);
}

void kbase_timeline_post_kbase_context_destroy(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;

	KBASE_TLSTREAM_TL_DEL_CTX(kbdev, kctx);
#if MALI_USE_CSF
	KBASE_TLSTREAM_TL_KBASE_DEL_CTX(kbdev, kctx->id);
#endif

	if (atomic_read(&kbdev->timeline_flags) != 0)
		kbase_timeline_streams_flush(kbdev->timeline);
}

#if MALI_UNIT_TEST
void kbase_timeline_stats(struct kbase_timeline *timeline, u32 *bytes_collected,
			  u32 *bytes_generated)
{
	enum tl_stream_type stype;

	KBASE_DEBUG_ASSERT(bytes_collected);

	*bytes_generated = 0;
	for (stype = (enum tl_stream_type)0; stype < TL_STREAM_TYPE_COUNT; stype++)
		*bytes_generated += (u32)atomic_read(&timeline->streams[stype].bytes_generated);

	*bytes_collected = (u32)atomic_read(&timeline->bytes_collected);
}
#endif
