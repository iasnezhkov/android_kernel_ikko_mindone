// SPDX-License-Identifier: GPL-2.0
#include <tl/mali_kbase_tracepoints.h>
#include <tl/mali_kbase_timeline.h>
#include <tl/mali_kbase_timeline_priv.h>

#include <mali_kbase.h>

void kbase_create_timeline_objects(struct kbase_device *kbdev)
{
	unsigned int lpu_id;
	int as_nr;
	struct kbase_context *kctx;
	struct kbase_timeline *timeline = kbdev->timeline;
	struct kbase_tlstream *summary = &timeline->streams[TL_STREAM_TYPE_OBJ_SUMMARY];

	for (lpu_id = 0; lpu_id < kbdev->gpu_props.num_job_slots; lpu_id++) {
		void *lpu = &kbdev->gpu_props.js_features[lpu_id];

		__kbase_tlstream_tl_new_lpu(summary, lpu, lpu_id, 0);
	}

	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_new_as(summary, &kbdev->as[as_nr], (u32)as_nr);

	__kbase_tlstream_tl_new_gpu(summary, kbdev, kbdev->id, kbdev->gpu_props.num_cores);

	for (lpu_id = 0; lpu_id < kbdev->gpu_props.num_job_slots; lpu_id++) {
		void *lpu = &kbdev->gpu_props.js_features[lpu_id];
		__kbase_tlstream_tl_lifelink_lpu_gpu(summary, lpu, kbdev);
	}

	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_lifelink_as_gpu(summary, &kbdev->as[as_nr], kbdev);

	mutex_lock(&timeline->tl_kctx_list_lock);

	list_for_each_entry(kctx, &timeline->tl_kctx_list, tl_kctx_list_node) {

		__kbase_tlstream_tl_new_ctx(summary, kctx, kctx->id, (u32)(kctx->tgid));
	}

	kbase_timeline_streams_body_reset(timeline);

	mutex_unlock(&timeline->tl_kctx_list_lock);

	kbase_timeline_streams_flush(timeline);
}
