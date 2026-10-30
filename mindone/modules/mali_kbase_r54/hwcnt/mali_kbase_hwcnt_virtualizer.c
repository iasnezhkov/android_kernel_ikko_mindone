// SPDX-License-Identifier: GPL-2.0
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "hwcnt/mali_kbase_hwcnt_accumulator.h"
#include "hwcnt/mali_kbase_hwcnt_context.h"
#include "hwcnt/mali_kbase_hwcnt_types.h"

#include <linux/mutex.h>
#include <linux/slab.h>

struct kbase_hwcnt_virtualizer {
	struct kbase_hwcnt_context *hctx;
	u64 dump_threshold_ns;
	const struct kbase_hwcnt_metadata *metadata;
	struct mutex lock;
	size_t client_count;
	struct list_head clients;
	struct kbase_hwcnt_accumulator *accum;
	struct kbase_hwcnt_enable_map scratch_map;
	struct kbase_hwcnt_dump_buffer scratch_buf;
	u64 ts_last_dump_ns;
};

struct kbase_hwcnt_virtualizer_client {
	struct list_head node;
	struct kbase_hwcnt_virtualizer *hvirt;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer accum_buf;
	bool has_accum;
	u64 ts_start_ns;
};

const struct kbase_hwcnt_metadata *
kbase_hwcnt_virtualizer_metadata(struct kbase_hwcnt_virtualizer *hvirt)
{
	if (!hvirt)
		return NULL;

	return hvirt->metadata;
}

static void kbasep_hwcnt_virtualizer_client_free(struct kbase_hwcnt_virtualizer_client *hvcli)
{
	if (!hvcli)
		return;

	kbase_hwcnt_dump_buffer_free(&hvcli->accum_buf);
	kbase_hwcnt_enable_map_free(&hvcli->enable_map);
	kfree(hvcli);
}

static int kbasep_hwcnt_virtualizer_client_alloc(const struct kbase_hwcnt_metadata *metadata,
						 struct kbase_hwcnt_virtualizer_client **out_hvcli)
{
	int errcode;
	struct kbase_hwcnt_virtualizer_client *hvcli = NULL;

	WARN_ON(!metadata);
	WARN_ON(!out_hvcli);

	hvcli = kzalloc(sizeof(*hvcli), GFP_KERNEL);
	if (!hvcli)
		return -ENOMEM;

	errcode = kbase_hwcnt_enable_map_alloc(metadata, &hvcli->enable_map);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_dump_buffer_alloc(metadata, &hvcli->accum_buf);
	if (errcode)
		goto error;

	*out_hvcli = hvcli;
	return 0;
error:
	kbasep_hwcnt_virtualizer_client_free(hvcli);
	return errcode;
}

static void
kbasep_hwcnt_virtualizer_client_accumulate(struct kbase_hwcnt_virtualizer_client *hvcli,
					   const struct kbase_hwcnt_dump_buffer *dump_buf)
{
	WARN_ON(!hvcli);
	WARN_ON(!dump_buf);
	lockdep_assert_held(&hvcli->hvirt->lock);

	if (hvcli->has_accum) {

		kbase_hwcnt_dump_buffer_accumulate(&hvcli->accum_buf, dump_buf, &hvcli->enable_map);
	} else {

		kbase_hwcnt_dump_buffer_copy(&hvcli->accum_buf, dump_buf, &hvcli->enable_map);
	}
	hvcli->has_accum = true;
}

static void kbasep_hwcnt_virtualizer_accumulator_term(struct kbase_hwcnt_virtualizer *hvirt)
{
	WARN_ON(!hvirt);
	lockdep_assert_held(&hvirt->lock);
	WARN_ON(hvirt->client_count);

	kbase_hwcnt_dump_buffer_free(&hvirt->scratch_buf);
	kbase_hwcnt_enable_map_free(&hvirt->scratch_map);
	kbase_hwcnt_accumulator_release(hvirt->accum);
	hvirt->accum = NULL;
}

static int kbasep_hwcnt_virtualizer_accumulator_init(struct kbase_hwcnt_virtualizer *hvirt)
{
	int errcode;

	WARN_ON(!hvirt);
	lockdep_assert_held(&hvirt->lock);
	WARN_ON(hvirt->client_count);
	WARN_ON(hvirt->accum);

	errcode = kbase_hwcnt_accumulator_acquire(hvirt->hctx, &hvirt->accum);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_enable_map_alloc(hvirt->metadata, &hvirt->scratch_map);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_dump_buffer_alloc(hvirt->metadata, &hvirt->scratch_buf);
	if (errcode)
		goto error;

	return 0;
error:
	kbasep_hwcnt_virtualizer_accumulator_term(hvirt);
	return errcode;
}

static int kbasep_hwcnt_virtualizer_client_add(struct kbase_hwcnt_virtualizer *hvirt,
					       struct kbase_hwcnt_virtualizer_client *hvcli,
					       const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode = 0;
	u64 ts_start_ns;
	u64 ts_end_ns;

	WARN_ON(!hvirt);
	WARN_ON(!hvcli);
	WARN_ON(!enable_map);
	lockdep_assert_held(&hvirt->lock);

	if (hvirt->client_count == 0)

		errcode = kbasep_hwcnt_virtualizer_accumulator_init(hvirt);
	if (errcode)
		return errcode;

	hvirt->client_count += 1;

	if (hvirt->client_count == 1) {

		errcode = kbase_hwcnt_accumulator_set_counters(hvirt->accum, enable_map,
							       &ts_start_ns, &ts_end_ns, NULL);
	} else {
		struct kbase_hwcnt_virtualizer_client *pos;

		kbase_hwcnt_enable_map_copy(&hvirt->scratch_map, enable_map);
		list_for_each_entry(pos, &hvirt->clients, node)
			kbase_hwcnt_enable_map_union(&hvirt->scratch_map, &pos->enable_map);

		errcode = kbase_hwcnt_accumulator_set_counters(hvirt->accum, &hvirt->scratch_map,
							       &ts_start_ns, &ts_end_ns,
							       &hvirt->scratch_buf);

		if (!errcode)
			list_for_each_entry(pos, &hvirt->clients, node)
				kbasep_hwcnt_virtualizer_client_accumulate(pos,
									   &hvirt->scratch_buf);
	}
	if (errcode)
		goto error;

	list_add(&hvcli->node, &hvirt->clients);
	hvcli->hvirt = hvirt;
	kbase_hwcnt_enable_map_copy(&hvcli->enable_map, enable_map);
	hvcli->has_accum = false;
	hvcli->ts_start_ns = ts_end_ns;

	hvirt->ts_last_dump_ns = ts_end_ns;

	return 0;
error:
	hvirt->client_count -= 1;
	if (hvirt->client_count == 0)
		kbasep_hwcnt_virtualizer_accumulator_term(hvirt);
	return errcode;
}

static void kbasep_hwcnt_virtualizer_client_remove(struct kbase_hwcnt_virtualizer *hvirt,
						   struct kbase_hwcnt_virtualizer_client *hvcli)
{
	int errcode = 0;
	u64 ts_start_ns;
	u64 ts_end_ns;

	WARN_ON(!hvirt);
	WARN_ON(!hvcli);
	lockdep_assert_held(&hvirt->lock);

	list_del(&hvcli->node);
	hvirt->client_count -= 1;

	if (hvirt->client_count == 0) {

		kbasep_hwcnt_virtualizer_accumulator_term(hvirt);
	} else {
		struct kbase_hwcnt_virtualizer_client *pos;

		kbase_hwcnt_enable_map_disable_all(&hvirt->scratch_map);
		list_for_each_entry(pos, &hvirt->clients, node)
			kbase_hwcnt_enable_map_union(&hvirt->scratch_map, &pos->enable_map);

		errcode = kbase_hwcnt_accumulator_set_counters(hvirt->accum, &hvirt->scratch_map,
							       &ts_start_ns, &ts_end_ns,
							       &hvirt->scratch_buf);

		if (!errcode) {
			list_for_each_entry(pos, &hvirt->clients, node)
				kbasep_hwcnt_virtualizer_client_accumulate(pos,
									   &hvirt->scratch_buf);

			hvirt->ts_last_dump_ns = ts_end_ns;
		}
	}
	WARN_ON(errcode);
}

static int kbasep_hwcnt_virtualizer_client_set_counters(
	struct kbase_hwcnt_virtualizer *hvirt, struct kbase_hwcnt_virtualizer_client *hvcli,
	const struct kbase_hwcnt_enable_map *enable_map, u64 *ts_start_ns, u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_virtualizer_client *pos;

	WARN_ON(!hvirt);
	WARN_ON(!hvcli);
	WARN_ON(!enable_map);
	WARN_ON(!ts_start_ns);
	WARN_ON(!ts_end_ns);
	WARN_ON(enable_map->metadata != hvirt->metadata);
	WARN_ON(dump_buf && (dump_buf->metadata != hvirt->metadata));
	lockdep_assert_held(&hvirt->lock);

	kbase_hwcnt_enable_map_copy(&hvirt->scratch_map, enable_map);
	list_for_each_entry(pos, &hvirt->clients, node)

		if (pos != hvcli)
			kbase_hwcnt_enable_map_union(&hvirt->scratch_map, &pos->enable_map);

	errcode = kbase_hwcnt_accumulator_set_counters(hvirt->accum, &hvirt->scratch_map,
						       ts_start_ns, ts_end_ns, &hvirt->scratch_buf);
	if (errcode)
		return errcode;

	list_for_each_entry(pos, &hvirt->clients, node)
		if (pos != hvcli)
			kbasep_hwcnt_virtualizer_client_accumulate(pos, &hvirt->scratch_buf);

	if (dump_buf) {
		const struct kbase_hwcnt_dump_buffer *src = &hvirt->scratch_buf;

		if (hvcli->has_accum) {
			kbase_hwcnt_dump_buffer_accumulate(&hvcli->accum_buf, src,
							   &hvcli->enable_map);
			src = &hvcli->accum_buf;
		}
		kbase_hwcnt_dump_buffer_copy(dump_buf, src, &hvcli->enable_map);
	}
	hvcli->has_accum = false;

	kbase_hwcnt_enable_map_copy(&hvcli->enable_map, enable_map);

	*ts_start_ns = hvcli->ts_start_ns;
	hvcli->ts_start_ns = *ts_end_ns;

	hvirt->ts_last_dump_ns = *ts_end_ns;

	return errcode;
}

int kbase_hwcnt_virtualizer_client_set_counters(struct kbase_hwcnt_virtualizer_client *hvcli,
						const struct kbase_hwcnt_enable_map *enable_map,
						u64 *ts_start_ns, u64 *ts_end_ns,
						struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_virtualizer *hvirt;

	if (!hvcli || !enable_map || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hvirt = hvcli->hvirt;

	if ((enable_map->metadata != hvirt->metadata) ||
	    (dump_buf && (dump_buf->metadata != hvirt->metadata)))
		return -EINVAL;

	mutex_lock(&hvirt->lock);

	if ((hvirt->client_count == 1) && (!hvcli->has_accum)) {

		errcode = kbase_hwcnt_accumulator_set_counters(hvirt->accum, enable_map,
							       ts_start_ns, ts_end_ns, dump_buf);

		if (!errcode) {

			kbase_hwcnt_enable_map_copy(&hvcli->enable_map, enable_map);

			*ts_start_ns = hvcli->ts_start_ns;
			hvcli->ts_start_ns = *ts_end_ns;

			hvirt->ts_last_dump_ns = *ts_end_ns;
		}
	} else {

		errcode = kbasep_hwcnt_virtualizer_client_set_counters(
			hvirt, hvcli, enable_map, ts_start_ns, ts_end_ns, dump_buf);
	}

	mutex_unlock(&hvirt->lock);

	return errcode;
}

static int kbasep_hwcnt_virtualizer_client_dump(struct kbase_hwcnt_virtualizer *hvirt,
						struct kbase_hwcnt_virtualizer_client *hvcli,
						u64 *ts_start_ns, u64 *ts_end_ns,
						struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_virtualizer_client *pos;

	WARN_ON(!hvirt);
	WARN_ON(!hvcli);
	WARN_ON(!ts_start_ns);
	WARN_ON(!ts_end_ns);
	WARN_ON(dump_buf && (dump_buf->metadata != hvirt->metadata));
	lockdep_assert_held(&hvirt->lock);

	errcode = kbase_hwcnt_accumulator_dump(hvirt->accum, ts_start_ns, ts_end_ns,
					       &hvirt->scratch_buf);
	if (errcode)
		return errcode;

	list_for_each_entry(pos, &hvirt->clients, node)
		if (pos != hvcli)
			kbasep_hwcnt_virtualizer_client_accumulate(pos, &hvirt->scratch_buf);

	if (dump_buf) {
		const struct kbase_hwcnt_dump_buffer *src = &hvirt->scratch_buf;

		if (hvcli->has_accum) {
			kbase_hwcnt_dump_buffer_accumulate(&hvcli->accum_buf, src,
							   &hvcli->enable_map);
			src = &hvcli->accum_buf;
		}
		kbase_hwcnt_dump_buffer_copy(dump_buf, src, &hvcli->enable_map);
	}
	hvcli->has_accum = false;

	*ts_start_ns = hvcli->ts_start_ns;
	hvcli->ts_start_ns = *ts_end_ns;

	hvirt->ts_last_dump_ns = *ts_end_ns;

	return errcode;
}

static int kbasep_hwcnt_virtualizer_client_dump_rate_limited(
	struct kbase_hwcnt_virtualizer *hvirt, struct kbase_hwcnt_virtualizer_client *hvcli,
	u64 *ts_start_ns, u64 *ts_end_ns, struct kbase_hwcnt_dump_buffer *dump_buf)
{
	bool rate_limited = true;

	WARN_ON(!hvirt);
	WARN_ON(!hvcli);
	WARN_ON(!ts_start_ns);
	WARN_ON(!ts_end_ns);
	WARN_ON(dump_buf && (dump_buf->metadata != hvirt->metadata));
	lockdep_assert_held(&hvirt->lock);

	if (hvirt->dump_threshold_ns == 0) {

		rate_limited = false;
	} else if (hvirt->ts_last_dump_ns == hvcli->ts_start_ns) {

		rate_limited = false;
	} else {
		const u64 ts_ns = kbase_hwcnt_accumulator_timestamp_ns(hvirt->accum);
		const u64 time_since_last_dump_ns = ts_ns - hvirt->ts_last_dump_ns;

		if (time_since_last_dump_ns >= hvirt->dump_threshold_ns)
			rate_limited = false;
	}

	if (!rate_limited)
		return kbasep_hwcnt_virtualizer_client_dump(hvirt, hvcli, ts_start_ns, ts_end_ns,
							    dump_buf);

	WARN_ON(!hvcli->has_accum);

	if (dump_buf)
		kbase_hwcnt_dump_buffer_copy(dump_buf, &hvcli->accum_buf, &hvcli->enable_map);
	hvcli->has_accum = false;

	*ts_start_ns = hvcli->ts_start_ns;
	*ts_end_ns = hvirt->ts_last_dump_ns;
	hvcli->ts_start_ns = hvirt->ts_last_dump_ns;

	return 0;
}

int kbase_hwcnt_virtualizer_client_dump(struct kbase_hwcnt_virtualizer_client *hvcli,
					u64 *ts_start_ns, u64 *ts_end_ns,
					struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_virtualizer *hvirt;

	if (!hvcli || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hvirt = hvcli->hvirt;

	if (dump_buf && (dump_buf->metadata != hvirt->metadata))
		return -EINVAL;

	mutex_lock(&hvirt->lock);

	if ((hvirt->client_count == 1) && (!hvcli->has_accum)) {

		errcode = kbase_hwcnt_accumulator_dump(hvirt->accum, ts_start_ns, ts_end_ns,
						       dump_buf);

		if (!errcode) {

			*ts_start_ns = hvcli->ts_start_ns;
			hvcli->ts_start_ns = *ts_end_ns;

			hvirt->ts_last_dump_ns = *ts_end_ns;
		}
	} else {

		errcode = kbasep_hwcnt_virtualizer_client_dump_rate_limited(
			hvirt, hvcli, ts_start_ns, ts_end_ns, dump_buf);
	}

	mutex_unlock(&hvirt->lock);

	return errcode;
}

int kbase_hwcnt_virtualizer_client_create(struct kbase_hwcnt_virtualizer *hvirt,
					  const struct kbase_hwcnt_enable_map *enable_map,
					  struct kbase_hwcnt_virtualizer_client **out_hvcli)
{
	int errcode;
	struct kbase_hwcnt_virtualizer_client *hvcli;

	if (!hvirt || !enable_map || !out_hvcli || (enable_map->metadata != hvirt->metadata))
		return -EINVAL;

	errcode = kbasep_hwcnt_virtualizer_client_alloc(hvirt->metadata, &hvcli);
	if (errcode)
		return errcode;

	mutex_lock(&hvirt->lock);

	errcode = kbasep_hwcnt_virtualizer_client_add(hvirt, hvcli, enable_map);

	mutex_unlock(&hvirt->lock);

	if (errcode) {
		kbasep_hwcnt_virtualizer_client_free(hvcli);
		return errcode;
	}

	*out_hvcli = hvcli;
	return 0;
}

void kbase_hwcnt_virtualizer_client_destroy(struct kbase_hwcnt_virtualizer_client *hvcli)
{
	if (!hvcli)
		return;

	mutex_lock(&hvcli->hvirt->lock);

	kbasep_hwcnt_virtualizer_client_remove(hvcli->hvirt, hvcli);

	mutex_unlock(&hvcli->hvirt->lock);

	kbasep_hwcnt_virtualizer_client_free(hvcli);
}

int kbase_hwcnt_virtualizer_init(struct kbase_hwcnt_context *hctx, u64 dump_threshold_ns,
				 struct kbase_hwcnt_virtualizer **out_hvirt)
{
	struct kbase_hwcnt_virtualizer *virt;
	const struct kbase_hwcnt_metadata *metadata;

	if (!hctx || !out_hvirt)
		return -EINVAL;

	metadata = kbase_hwcnt_context_metadata(hctx);
	if (!metadata)
		return -EINVAL;

	virt = kzalloc(sizeof(*virt), GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	virt->hctx = hctx;
	virt->dump_threshold_ns = dump_threshold_ns;
	virt->metadata = metadata;

	mutex_init(&virt->lock);
	INIT_LIST_HEAD(&virt->clients);

	*out_hvirt = virt;
	return 0;
}

void kbase_hwcnt_virtualizer_term(struct kbase_hwcnt_virtualizer *hvirt)
{
	if (!hvirt)
		return;

	if (WARN_ON(hvirt->client_count != 0)) {
		struct kbase_hwcnt_virtualizer_client *pos, *n;

		list_for_each_entry_safe(pos, n, &hvirt->clients, node)
			kbase_hwcnt_virtualizer_client_destroy(pos);
	}

	WARN_ON(hvirt->client_count != 0);
	WARN_ON(hvirt->accum);

	kfree(hvirt);
}

bool kbase_hwcnt_virtualizer_queue_work(struct kbase_hwcnt_virtualizer *hvirt,
					struct work_struct *work)
{
	if (WARN_ON(!hvirt) || WARN_ON(!work))
		return false;

	return kbase_hwcnt_context_queue_work(hvirt->hctx, work);
}
