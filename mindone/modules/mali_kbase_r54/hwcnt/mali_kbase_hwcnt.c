// SPDX-License-Identifier: GPL-2.0
#include "hwcnt/mali_kbase_hwcnt_context.h"
#include "hwcnt/mali_kbase_hwcnt_accumulator.h"
#include "hwcnt/backend/mali_kbase_hwcnt_backend.h"
#include "hwcnt/mali_kbase_hwcnt_types.h"

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

enum kbase_hwcnt_accum_state { ACCUM_STATE_ERROR, ACCUM_STATE_DISABLED, ACCUM_STATE_ENABLED };

struct kbase_hwcnt_accumulator {
	const struct kbase_hwcnt_metadata *metadata;
	struct kbase_hwcnt_backend *backend;
	enum kbase_hwcnt_accum_state state;
	struct kbase_hwcnt_enable_map enable_map;
	bool enable_map_any_enabled;
	struct kbase_hwcnt_enable_map scratch_map;
	struct kbase_hwcnt_dump_buffer accum_buf;
	bool accumulated;
	u64 ts_last_dump_ns;
};

struct kbase_hwcnt_context {
	const struct kbase_hwcnt_backend_interface *iface;
	spinlock_t state_lock;
	size_t disable_count;
	struct mutex accum_lock;
	bool accum_inited;
	struct kbase_hwcnt_accumulator accum;
	struct workqueue_struct *wq;
};

int kbase_hwcnt_context_init(const struct kbase_hwcnt_backend_interface *iface,
			     struct kbase_hwcnt_context **out_hctx)
{
	struct kbase_hwcnt_context *hctx = NULL;

	if (!iface || !out_hctx)
		return -EINVAL;

	hctx = kzalloc(sizeof(*hctx), GFP_KERNEL);
	if (!hctx)
		goto err_alloc_hctx;

	hctx->iface = iface;
	spin_lock_init(&hctx->state_lock);
	hctx->disable_count = 1;
	mutex_init(&hctx->accum_lock);
	hctx->accum_inited = false;

	hctx->wq = alloc_workqueue("mali_kbase_hwcnt", WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!hctx->wq)
		goto err_alloc_workqueue;

	*out_hctx = hctx;

	return 0;

err_alloc_workqueue:
	kfree(hctx);
err_alloc_hctx:
	return -ENOMEM;
}

void kbase_hwcnt_context_term(struct kbase_hwcnt_context *hctx)
{
	if (!hctx)
		return;

	WARN_ON(hctx->accum_inited);

	destroy_workqueue(hctx->wq);
	kfree(hctx);
}

static void kbasep_hwcnt_accumulator_term(struct kbase_hwcnt_context *hctx)
{
	WARN_ON(!hctx);
	WARN_ON(!hctx->accum_inited);

	kbase_hwcnt_enable_map_free(&hctx->accum.scratch_map);
	kbase_hwcnt_dump_buffer_free(&hctx->accum.accum_buf);
	kbase_hwcnt_enable_map_free(&hctx->accum.enable_map);
	hctx->iface->term(hctx->accum.backend);
	memset(&hctx->accum, 0, sizeof(hctx->accum));
}

static int kbasep_hwcnt_accumulator_init(struct kbase_hwcnt_context *hctx)
{
	int errcode;

	WARN_ON(!hctx);
	WARN_ON(!hctx->accum_inited);

	errcode = hctx->iface->init(hctx->iface->info, &hctx->accum.backend);
	if (errcode)
		goto error;

	hctx->accum.metadata = hctx->iface->metadata(hctx->iface->info);
	hctx->accum.state = ACCUM_STATE_ERROR;

	errcode = kbase_hwcnt_enable_map_alloc(hctx->accum.metadata, &hctx->accum.enable_map);
	if (errcode)
		goto error;

	hctx->accum.enable_map_any_enabled = false;

	errcode = kbase_hwcnt_dump_buffer_alloc(hctx->accum.metadata, &hctx->accum.accum_buf);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_enable_map_alloc(hctx->accum.metadata, &hctx->accum.scratch_map);
	if (errcode)
		goto error;

	hctx->accum.accumulated = false;

	hctx->accum.ts_last_dump_ns = hctx->iface->timestamp_ns(hctx->accum.backend);

	return 0;

error:
	kbasep_hwcnt_accumulator_term(hctx);
	return errcode;
}

static void kbasep_hwcnt_accumulator_disable(struct kbase_hwcnt_context *hctx, bool accumulate)
{
	int errcode = 0;
	bool backend_enabled = false;
	struct kbase_hwcnt_accumulator *accum;
	unsigned long flags;
	u64 dump_time_ns;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->accum_lock);
	WARN_ON(!hctx->accum_inited);

	accum = &hctx->accum;

	spin_lock_irqsave(&hctx->state_lock, flags);

	WARN_ON(hctx->disable_count != 0);
	WARN_ON(hctx->accum.state == ACCUM_STATE_DISABLED);

	if ((hctx->accum.state == ACCUM_STATE_ENABLED) && (accum->enable_map_any_enabled))
		backend_enabled = true;

	if (!backend_enabled)
		hctx->accum.state = ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	if (!backend_enabled)
		return;

	if (!accumulate)
		goto disable;

	errcode = hctx->iface->dump_request(accum->backend);
	if (errcode)
		goto disable;

	errcode = hctx->iface->dump_wait(accum->backend);
	if (errcode)
		goto disable;

	errcode = hctx->iface->dump_get(accum->backend, &accum->accum_buf, &accum->enable_map,
					accum->accumulated, &dump_time_ns);
	if (errcode)
		goto disable;

	accum->accumulated = true;

disable:
	hctx->iface->dump_disable(accum->backend, (accum->accumulated) ? &accum->accum_buf : NULL,
				  &accum->enable_map);

	spin_lock_irqsave(&hctx->state_lock, flags);

	hctx->accum.state = ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
}

static void kbasep_hwcnt_accumulator_enable(struct kbase_hwcnt_context *hctx)
{
	int errcode = 0;
	struct kbase_hwcnt_accumulator *accum;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->state_lock);
	WARN_ON(!hctx->accum_inited);
	WARN_ON(hctx->accum.state != ACCUM_STATE_DISABLED);

	accum = &hctx->accum;

	if (accum->enable_map_any_enabled)
		errcode = hctx->iface->dump_enable_nolock(accum->backend, &accum->enable_map);

	if (!errcode)
		accum->state = ACCUM_STATE_ENABLED;
	else
		accum->state = ACCUM_STATE_ERROR;
}

static int kbasep_hwcnt_accumulator_dump(struct kbase_hwcnt_context *hctx, u64 *ts_start_ns,
					 u64 *ts_end_ns, struct kbase_hwcnt_dump_buffer *dump_buf,
					 const struct kbase_hwcnt_enable_map *new_map)
{
	int errcode = 0;
	unsigned long flags;
	enum kbase_hwcnt_accum_state state;
	bool dump_requested = false;
	bool dump_written = false;
	bool cur_map_any_enabled;
	struct kbase_hwcnt_enable_map *cur_map;
	bool new_map_any_enabled = false;
	u64 dump_time_ns = 0;
	struct kbase_hwcnt_accumulator *accum;

	WARN_ON(!hctx);
	WARN_ON(!ts_start_ns);
	WARN_ON(!ts_end_ns);
	WARN_ON(dump_buf && (dump_buf->metadata != hctx->accum.metadata));
	WARN_ON(new_map && (new_map->metadata != hctx->accum.metadata));
	WARN_ON(!hctx->accum_inited);
	lockdep_assert_held(&hctx->accum_lock);

	accum = &hctx->accum;
	cur_map = &accum->scratch_map;

	cur_map_any_enabled = accum->enable_map_any_enabled;
	kbase_hwcnt_enable_map_copy(cur_map, &accum->enable_map);

	if (new_map)
		new_map_any_enabled = kbase_hwcnt_enable_map_any_enabled(new_map);

	spin_lock_irqsave(&hctx->state_lock, flags);

	state = accum->state;

	if (new_map) {
		kbase_hwcnt_enable_map_copy(&accum->enable_map, new_map);
		accum->enable_map_any_enabled = new_map_any_enabled;
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	if (state == ACCUM_STATE_ERROR)
		return -EIO;

	if ((state == ACCUM_STATE_ENABLED) && cur_map_any_enabled) {
		if (dump_buf) {
			errcode = hctx->iface->dump_request(accum->backend);
			dump_requested = true;
		} else {
			dump_time_ns = hctx->iface->timestamp_ns(accum->backend);
			errcode = hctx->iface->dump_clear(accum->backend);
		}

		if (errcode)
			goto error;
	} else {
		dump_time_ns = hctx->iface->timestamp_ns(accum->backend);
	}

	if (accum->accumulated && dump_buf) {
		kbase_hwcnt_dump_buffer_copy(dump_buf, &accum->accum_buf, cur_map);
		dump_written = true;
	}

	if (dump_requested) {
		WARN_ON(state != ACCUM_STATE_ENABLED);
		errcode = hctx->iface->dump_wait(accum->backend);
		if (errcode)
			goto error;
	}

	if ((state == ACCUM_STATE_ENABLED) && new_map) {

		if (cur_map_any_enabled) {

			hctx->iface->dump_disable(accum->backend, NULL, NULL);
		}

		if (new_map_any_enabled) {
			errcode = hctx->iface->dump_enable(accum->backend, new_map);
			if (errcode)
				goto error;
		}
	}

	if (dump_buf) {

		if (dump_requested) {
			WARN_ON(state != ACCUM_STATE_ENABLED);
			errcode = hctx->iface->dump_get(accum->backend, dump_buf, cur_map,
							dump_written, &dump_time_ns);
			if (errcode)
				goto error;
			dump_written = true;
		}

		if (!dump_written) {
			kbase_hwcnt_dump_buffer_zero(dump_buf, cur_map);
			kbase_hwcnt_dump_buffer_block_state_update(dump_buf, cur_map,
								   KBASE_HWCNT_STATE_OFF);
		}
	}

	*ts_start_ns = accum->ts_last_dump_ns;
	*ts_end_ns = dump_time_ns;

	accum->accumulated = false;
	accum->ts_last_dump_ns = dump_time_ns;

	return 0;
error:

	WARN_ON(state != ACCUM_STATE_ENABLED);

	hctx->iface->dump_disable(accum->backend, (accum->accumulated) ? &accum->accum_buf : NULL,
				  cur_map);
	spin_lock_irqsave(&hctx->state_lock, flags);

	accum->state = ACCUM_STATE_ERROR;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	return errcode;
}

static void kbasep_hwcnt_context_disable(struct kbase_hwcnt_context *hctx, bool accumulate)
{
	unsigned long flags;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->accum_lock);

	if (!kbase_hwcnt_context_disable_atomic(hctx)) {
		kbasep_hwcnt_accumulator_disable(hctx, accumulate);

		spin_lock_irqsave(&hctx->state_lock, flags);

		WARN_ON(hctx->disable_count != 0);
		hctx->disable_count++;

		spin_unlock_irqrestore(&hctx->state_lock, flags);
	}
}

int kbase_hwcnt_accumulator_acquire(struct kbase_hwcnt_context *hctx,
				    struct kbase_hwcnt_accumulator **accum)
{
	int errcode = 0;
	unsigned long flags;

	if (!hctx || !accum)
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);
	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!hctx->accum_inited)

		hctx->accum_inited = true;
	else

		errcode = -EBUSY;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
	mutex_unlock(&hctx->accum_lock);

	if (errcode)
		return errcode;

	errcode = kbasep_hwcnt_accumulator_init(hctx);

	if (errcode) {
		mutex_lock(&hctx->accum_lock);
		spin_lock_irqsave(&hctx->state_lock, flags);

		hctx->accum_inited = false;

		spin_unlock_irqrestore(&hctx->state_lock, flags);
		mutex_unlock(&hctx->accum_lock);

		return errcode;
	}

	hctx->iface->acquire(hctx->accum.backend);

	spin_lock_irqsave(&hctx->state_lock, flags);

	WARN_ON(hctx->disable_count == 0);
	WARN_ON(hctx->accum.enable_map_any_enabled);

	hctx->disable_count--;

	hctx->accum.state = (hctx->disable_count == 0) ? ACCUM_STATE_ENABLED : ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	*accum = &hctx->accum;

	return 0;
}

void kbase_hwcnt_accumulator_release(struct kbase_hwcnt_accumulator *accum)
{
	unsigned long flags;
	struct kbase_hwcnt_context *hctx;

	if (!accum)
		return;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	mutex_lock(&hctx->accum_lock);

	WARN_ON(!hctx->accum_inited);

	kbasep_hwcnt_context_disable(hctx, false);

	mutex_unlock(&hctx->accum_lock);

	hctx->iface->release(hctx->accum.backend);

	kbasep_hwcnt_accumulator_term(hctx);

	mutex_lock(&hctx->accum_lock);
	spin_lock_irqsave(&hctx->state_lock, flags);

	hctx->accum_inited = false;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
	mutex_unlock(&hctx->accum_lock);
}

void kbase_hwcnt_context_disable(struct kbase_hwcnt_context *hctx)
{
	if (WARN_ON(!hctx))
		return;

	if (kbase_hwcnt_context_disable_atomic(hctx))
		return;

	mutex_lock(&hctx->accum_lock);

	kbasep_hwcnt_context_disable(hctx, true);

	mutex_unlock(&hctx->accum_lock);
}

bool kbase_hwcnt_context_disable_atomic(struct kbase_hwcnt_context *hctx)
{
	unsigned long flags;
	bool atomic_disabled = false;

	if (WARN_ON(!hctx))
		return false;

	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!WARN_ON(hctx->disable_count == SIZE_MAX)) {

		if (hctx->disable_count != 0) {
			hctx->disable_count++;
			atomic_disabled = true;
		}
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	return atomic_disabled;
}

void kbase_hwcnt_context_enable(struct kbase_hwcnt_context *hctx)
{
	unsigned long flags;

	if (WARN_ON(!hctx))
		return;

	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!WARN_ON(hctx->disable_count == 0)) {
		if (hctx->disable_count == 1)
			kbasep_hwcnt_accumulator_enable(hctx);

		hctx->disable_count--;
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);
}

const struct kbase_hwcnt_metadata *kbase_hwcnt_context_metadata(struct kbase_hwcnt_context *hctx)
{
	if (!hctx)
		return NULL;

	return hctx->iface->metadata(hctx->iface->info);
}

bool kbase_hwcnt_context_queue_work(struct kbase_hwcnt_context *hctx, struct work_struct *work)
{
	if (WARN_ON(!hctx) || WARN_ON(!work))
		return false;

	return queue_work(hctx->wq, work);
}

int kbase_hwcnt_accumulator_set_counters(struct kbase_hwcnt_accumulator *accum,
					 const struct kbase_hwcnt_enable_map *new_map,
					 u64 *ts_start_ns, u64 *ts_end_ns,
					 struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_context *hctx;

	if (!accum || !new_map || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	if ((new_map->metadata != hctx->accum.metadata) ||
	    (dump_buf && (dump_buf->metadata != hctx->accum.metadata)))
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);

	errcode = kbasep_hwcnt_accumulator_dump(hctx, ts_start_ns, ts_end_ns, dump_buf, new_map);

	mutex_unlock(&hctx->accum_lock);

	return errcode;
}

int kbase_hwcnt_accumulator_dump(struct kbase_hwcnt_accumulator *accum, u64 *ts_start_ns,
				 u64 *ts_end_ns, struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_context *hctx;

	if (!accum || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	if (dump_buf && (dump_buf->metadata != hctx->accum.metadata))
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);

	errcode = kbasep_hwcnt_accumulator_dump(hctx, ts_start_ns, ts_end_ns, dump_buf, NULL);

	mutex_unlock(&hctx->accum_lock);

	return errcode;
}

u64 kbase_hwcnt_accumulator_timestamp_ns(struct kbase_hwcnt_accumulator *accum)
{
	struct kbase_hwcnt_context *hctx;

	if (WARN_ON(!accum))
		return 0;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);
	return hctx->iface->timestamp_ns(accum->backend);
}
