// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>

#include <hwcnt/mali_kbase_hwcnt_gpu.h>
#include <hwcnt/mali_kbase_hwcnt_types.h>

#include <hwcnt/backend/mali_kbase_hwcnt_backend.h>
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm_watchdog.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if.h>

#if IS_ENABLED(CONFIG_MALI_IS_FPGA) && !IS_ENABLED(CONFIG_MALI_NO_MALI)

static const u32 hwcnt_backend_watchdog_timer_interval_ms = 18000;
#else

static const u32 hwcnt_backend_watchdog_timer_interval_ms = 1000;
#endif

enum backend_watchdog_state {
	HWCNT_JM_WD_ERROR,
	HWCNT_JM_WD_IDLE_BUFFER_EMPTY,
	HWCNT_JM_WD_IDLE_BUFFER_FULL,
	HWCNT_JM_WD_BUFFER_CLEARING,
	HWCNT_JM_WD_TIMER_DUMPING,
	HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED,
	HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR,
	HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY,
	HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL,
};

enum wd_init_state {
	HWCNT_JM_WD_INIT_START,
	HWCNT_JM_WD_INIT_BACKEND = HWCNT_JM_WD_INIT_START,
	HWCNT_JM_WD_INIT_ENABLE_MAP,
	HWCNT_JM_WD_INIT_DUMP_BUFFER,
	HWCNT_JM_WD_INIT_END
};

struct kbase_hwcnt_backend_jm_watchdog_info {
	struct kbase_hwcnt_backend_interface *jm_backend_iface;
	struct kbase_hwcnt_watchdog_interface *dump_watchdog_iface;
};

struct kbase_hwcnt_backend_jm_watchdog {
	const struct kbase_hwcnt_backend_jm_watchdog_info *info;
	struct kbase_hwcnt_backend *jm_backend;
	u32 timeout_ms;
	struct kbase_hwcnt_dump_buffer wd_dump_buffer;
	struct kbase_hwcnt_enable_map wd_enable_map;
	u64 wd_dump_timestamp;
	struct completion watchdog_complete;
	struct {
		spinlock_t watchdog_lock;
		bool is_enabled;
		enum backend_watchdog_state state;
	} locked;
};

static void kbasep_hwcnt_backend_jm_watchdog_timer_callback(void *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend = backend;
	unsigned long flags;
	bool wd_accumulate;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	if (!wd_backend->locked.is_enabled || wd_backend->locked.state == HWCNT_JM_WD_ERROR) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}

	if (!(wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_EMPTY ||
	      wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL)) {

		wd_backend->info->dump_watchdog_iface->modify(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms);
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}

	wd_accumulate = (wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL);

	wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING;

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (wd_backend->info->jm_backend_iface->dump_request(wd_backend->jm_backend) ||
	    wd_backend->info->jm_backend_iface->dump_wait(wd_backend->jm_backend) ||
	    wd_backend->info->jm_backend_iface->dump_get(
		    wd_backend->jm_backend, &wd_backend->wd_dump_buffer, &wd_backend->wd_enable_map,
		    wd_accumulate, &wd_backend->wd_dump_timestamp)) {
		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);
		wd_backend->locked.state = HWCNT_JM_WD_ERROR;
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

		complete_all(&wd_backend->watchdog_complete);
		return;
	}

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING &&
		wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
		wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);

	if (wd_backend->locked.state == HWCNT_JM_WD_TIMER_DUMPING) {

		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_FULL;
	}
	if (wd_backend->locked.state != HWCNT_JM_WD_ERROR && wd_backend->locked.is_enabled) {

		wd_backend->info->dump_watchdog_iface->modify(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms);
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	complete_all(&wd_backend->watchdog_complete);
}

static struct kbase_hwcnt_backend_jm_watchdog_info *
kbasep_hwcnt_backend_jm_watchdog_info_create(struct kbase_hwcnt_backend_interface *backend_iface,
					     struct kbase_hwcnt_watchdog_interface *watchdog_iface)
{
	struct kbase_hwcnt_backend_jm_watchdog_info *const info =
		kmalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return NULL;

	*info = (struct kbase_hwcnt_backend_jm_watchdog_info){
		.jm_backend_iface = backend_iface, .dump_watchdog_iface = watchdog_iface
	};

	return info;
}

static const struct kbase_hwcnt_metadata *
kbasep_hwcnt_backend_jm_watchdog_metadata(const struct kbase_hwcnt_backend_info *info)
{
	const struct kbase_hwcnt_backend_jm_watchdog_info *wd_info = (void *)info;

	if (WARN_ON(!info))
		return NULL;

	return wd_info->jm_backend_iface->metadata(wd_info->jm_backend_iface->info);
}

static void
kbasep_hwcnt_backend_jm_watchdog_term_partial(struct kbase_hwcnt_backend_jm_watchdog *wd_backend,
					      enum wd_init_state state)
{
	if (!wd_backend)
		return;

	WARN_ON(state > HWCNT_JM_WD_INIT_END);

	while (state-- > HWCNT_JM_WD_INIT_START) {
		switch (state) {
		case HWCNT_JM_WD_INIT_BACKEND:
			wd_backend->info->jm_backend_iface->term(wd_backend->jm_backend);
			break;
		case HWCNT_JM_WD_INIT_ENABLE_MAP:
			kbase_hwcnt_enable_map_free(&wd_backend->wd_enable_map);
			break;
		case HWCNT_JM_WD_INIT_DUMP_BUFFER:
			kbase_hwcnt_dump_buffer_free(&wd_backend->wd_dump_buffer);
			break;
		case HWCNT_JM_WD_INIT_END:
			break;
		}
	}

	kfree(wd_backend);
}

static void kbasep_hwcnt_backend_jm_watchdog_acquire(const struct kbase_hwcnt_backend *backend)
{
}

static void kbasep_hwcnt_backend_jm_watchdog_release(const struct kbase_hwcnt_backend *backend)
{
}

static void kbasep_hwcnt_backend_jm_watchdog_term(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend =
		(struct kbase_hwcnt_backend_jm_watchdog *)backend;

	if (!backend)
		return;

	wd_backend->info->dump_watchdog_iface->disable(
		wd_backend->info->dump_watchdog_iface->timer);

	kbasep_hwcnt_backend_jm_watchdog_term_partial(wd_backend, HWCNT_JM_WD_INIT_END);
}

static int kbasep_hwcnt_backend_jm_watchdog_init(const struct kbase_hwcnt_backend_info *info,
						 struct kbase_hwcnt_backend **out_backend)
{
	int errcode = 0;
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend = NULL;
	struct kbase_hwcnt_backend_jm_watchdog_info *const wd_info = (void *)info;
	const struct kbase_hwcnt_backend_info *jm_info;
	const struct kbase_hwcnt_metadata *metadata;
	enum wd_init_state state = HWCNT_JM_WD_INIT_START;

	if (WARN_ON(!info) || WARN_ON(!out_backend))
		return -EINVAL;

	jm_info = wd_info->jm_backend_iface->info;
	metadata = wd_info->jm_backend_iface->metadata(wd_info->jm_backend_iface->info);

	wd_backend = kmalloc(sizeof(*wd_backend), GFP_KERNEL);
	if (!wd_backend) {
		*out_backend = NULL;
		return -ENOMEM;
	}

	*wd_backend = (struct kbase_hwcnt_backend_jm_watchdog){
		.info = wd_info,
		.timeout_ms = hwcnt_backend_watchdog_timer_interval_ms,
		.locked = { .state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY, .is_enabled = false }
	};

	while (state < HWCNT_JM_WD_INIT_END && !errcode) {
		switch (state) {
		case HWCNT_JM_WD_INIT_BACKEND:
			errcode = wd_info->jm_backend_iface->init(jm_info, &wd_backend->jm_backend);
			break;
		case HWCNT_JM_WD_INIT_ENABLE_MAP:
			errcode =
				kbase_hwcnt_enable_map_alloc(metadata, &wd_backend->wd_enable_map);
			break;
		case HWCNT_JM_WD_INIT_DUMP_BUFFER:
			errcode = kbase_hwcnt_dump_buffer_alloc(metadata,
								&wd_backend->wd_dump_buffer);
			break;
		case HWCNT_JM_WD_INIT_END:
			break;
		}
		if (!errcode)
			state++;
	}

	if (errcode) {
		kbasep_hwcnt_backend_jm_watchdog_term_partial(wd_backend, state);
		*out_backend = NULL;
		return errcode;
	}

	WARN_ON(state != HWCNT_JM_WD_INIT_END);

	spin_lock_init(&wd_backend->locked.watchdog_lock);
	init_completion(&wd_backend->watchdog_complete);

	*out_backend = (struct kbase_hwcnt_backend *)wd_backend;
	return 0;
}

static u64 kbasep_hwcnt_backend_jm_watchdog_timestamp_ns(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return wd_backend->info->jm_backend_iface->timestamp_ns(wd_backend->jm_backend);
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend,
	const struct kbase_hwcnt_enable_map *enable_map, kbase_hwcnt_backend_dump_enable_fn enabler)
{
	int errcode = -EPERM;
	unsigned long flags;

	if (WARN_ON(!wd_backend) || WARN_ON(!enable_map))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	if (wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	kbase_hwcnt_enable_map_copy(&wd_backend->wd_enable_map, enable_map);

	errcode = enabler(wd_backend->jm_backend, enable_map);
	if (!errcode) {

		errcode = wd_backend->info->dump_watchdog_iface->enable(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms,
			kbasep_hwcnt_backend_jm_watchdog_timer_callback, wd_backend);
		if (!errcode) {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(wd_backend->locked.is_enabled);
			wd_backend->locked.is_enabled = true;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		} else

			wd_backend->info->jm_backend_iface->dump_disable(wd_backend->jm_backend,
									 NULL, NULL);
	}

	return errcode;
}

static int
kbasep_hwcnt_backend_jm_watchdog_dump_enable(struct kbase_hwcnt_backend *backend,
					     const struct kbase_hwcnt_enable_map *enable_map)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
		wd_backend, enable_map, wd_backend->info->jm_backend_iface->dump_enable);
}

static int
kbasep_hwcnt_backend_jm_watchdog_dump_enable_nolock(struct kbase_hwcnt_backend *backend,
						    const struct kbase_hwcnt_enable_map *enable_map)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
		wd_backend, enable_map, wd_backend->info->jm_backend_iface->dump_enable_nolock);
}

static void
kbasep_hwcnt_backend_jm_watchdog_dump_disable(struct kbase_hwcnt_backend *backend,
					      struct kbase_hwcnt_dump_buffer *dump_buffer,
					      const struct kbase_hwcnt_enable_map *buf_enable_map)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;

	if (WARN_ON(!backend))
		return;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}

	wd_backend->locked.is_enabled = false;

	if (wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL)
		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	wd_backend->info->dump_watchdog_iface->disable(
		wd_backend->info->dump_watchdog_iface->timer);

	wd_backend->info->jm_backend_iface->dump_disable(wd_backend->jm_backend, dump_buffer,
							 buf_enable_map);
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_clear(struct kbase_hwcnt_backend *backend)
{
	int errcode = -EPERM;
	bool clear_wd_wait_completion = false;
	unsigned long flags;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	if (WARN_ON(!backend))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_IDLE_BUFFER_FULL:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:
	case HWCNT_JM_WD_IDLE_BUFFER_EMPTY:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
		wd_backend->locked.state = HWCNT_JM_WD_BUFFER_CLEARING;
		errcode = 0;
		break;
	case HWCNT_JM_WD_TIMER_DUMPING:

		clear_wd_wait_completion = true;

		reinit_completion(&wd_backend->watchdog_complete);
		wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR;
		errcode = 0;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (!errcode) {
		if (clear_wd_wait_completion) {

			wait_for_completion(&wd_backend->watchdog_complete);
		}

		errcode = wd_backend->info->jm_backend_iface->dump_clear(wd_backend->jm_backend);

		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
			wd_backend->locked.state != HWCNT_JM_WD_BUFFER_CLEARING &&
			wd_backend->locked.state != HWCNT_JM_WD_ERROR);

		WARN_ON(!wd_backend->locked.is_enabled);

		if (!errcode && wd_backend->locked.state != HWCNT_JM_WD_ERROR) {

			wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;

			wd_backend->info->dump_watchdog_iface->modify(
				wd_backend->info->dump_watchdog_iface->timer,
				wd_backend->timeout_ms);
		} else {
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			errcode = -EPERM;
		}

		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
	}

	return errcode;
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_request(struct kbase_hwcnt_backend *backend)
{
	bool call_dump_request = false;
	int errcode = 0;
	unsigned long flags;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	if (WARN_ON(!backend))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_IDLE_BUFFER_EMPTY:

		wd_backend->locked.state = HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY;
		call_dump_request = true;
		break;
	case HWCNT_JM_WD_IDLE_BUFFER_FULL:
		wd_backend->locked.state = HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL;
		call_dump_request = true;
		break;
	case HWCNT_JM_WD_TIMER_DUMPING:

		reinit_completion(&wd_backend->watchdog_complete);
		wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (call_dump_request) {
		errcode = wd_backend->info->jm_backend_iface->dump_request(wd_backend->jm_backend);
		if (!errcode) {

			wd_backend->info->dump_watchdog_iface->modify(
				wd_backend->info->dump_watchdog_iface->timer,
				wd_backend->timeout_ms);
		} else {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(!wd_backend->locked.is_enabled);
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		}
	}

	return errcode;
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_wait(struct kbase_hwcnt_backend *backend)
{
	int errcode = -EPERM;
	bool wait_for_auto_dump = false, wait_for_user_dump = false;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;

	if (WARN_ON(!backend))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED:
		wait_for_auto_dump = true;
		errcode = 0;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:
		wait_for_user_dump = true;
		errcode = 0;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (wait_for_auto_dump)
		wait_for_completion(&wd_backend->watchdog_complete);
	else if (wait_for_user_dump) {
		errcode = wd_backend->info->jm_backend_iface->dump_wait(wd_backend->jm_backend);
		if (errcode) {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(!wd_backend->locked.is_enabled);
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		}
	}

	return errcode;
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_get(
	struct kbase_hwcnt_backend *backend, struct kbase_hwcnt_dump_buffer *dump_buffer,
	const struct kbase_hwcnt_enable_map *enable_map, bool accumulate, u64 *dump_time_ns)
{
	bool call_dump_get = false;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;
	int errcode = 0;

	if (WARN_ON(!backend) || WARN_ON(!dump_buffer) || WARN_ON(!enable_map) ||
	    WARN_ON(!dump_time_ns))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED:

		if (accumulate)
			kbase_hwcnt_dump_buffer_accumulate(dump_buffer, &wd_backend->wd_dump_buffer,
							   enable_map);
		else
			kbase_hwcnt_dump_buffer_copy(dump_buffer, &wd_backend->wd_dump_buffer,
						     enable_map);

		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;

		*dump_time_ns = wd_backend->wd_dump_timestamp;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:

		if (accumulate)
			kbase_hwcnt_dump_buffer_accumulate(dump_buffer, &wd_backend->wd_dump_buffer,
							   enable_map);
		else
			kbase_hwcnt_dump_buffer_copy(dump_buffer, &wd_backend->wd_dump_buffer,
						     enable_map);

		accumulate = true;
		call_dump_get = true;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
		call_dump_get = true;
		break;
	default:
		errcode = -EPERM;
		break;
	}

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (call_dump_get && !errcode) {

		errcode = wd_backend->info->jm_backend_iface->dump_get(
			wd_backend->jm_backend, dump_buffer, enable_map, accumulate, dump_time_ns);

		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY &&
			wd_backend->locked.state != HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);

		if (!errcode)
			wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;
		else
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;

		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
	}

	return errcode;
}

int kbase_hwcnt_backend_jm_watchdog_create(struct kbase_hwcnt_backend_interface *backend_iface,
					   struct kbase_hwcnt_watchdog_interface *watchdog_iface,
					   struct kbase_hwcnt_backend_interface *out_iface)
{
	struct kbase_hwcnt_backend_jm_watchdog_info *info = NULL;

	if (WARN_ON(!backend_iface) || WARN_ON(!watchdog_iface) || WARN_ON(!out_iface))
		return -EINVAL;

	info = kbasep_hwcnt_backend_jm_watchdog_info_create(backend_iface, watchdog_iface);
	if (!info)
		return -ENOMEM;

	*out_iface = (struct kbase_hwcnt_backend_interface){
		.info = (void *)info,
		.metadata = kbasep_hwcnt_backend_jm_watchdog_metadata,
		.init = kbasep_hwcnt_backend_jm_watchdog_init,
		.term = kbasep_hwcnt_backend_jm_watchdog_term,
		.acquire = kbasep_hwcnt_backend_jm_watchdog_acquire,
		.release = kbasep_hwcnt_backend_jm_watchdog_release,
		.timestamp_ns = kbasep_hwcnt_backend_jm_watchdog_timestamp_ns,
		.dump_enable = kbasep_hwcnt_backend_jm_watchdog_dump_enable,
		.dump_enable_nolock = kbasep_hwcnt_backend_jm_watchdog_dump_enable_nolock,
		.dump_disable = kbasep_hwcnt_backend_jm_watchdog_dump_disable,
		.dump_clear = kbasep_hwcnt_backend_jm_watchdog_dump_clear,
		.dump_request = kbasep_hwcnt_backend_jm_watchdog_dump_request,
		.dump_wait = kbasep_hwcnt_backend_jm_watchdog_dump_wait,
		.dump_get = kbasep_hwcnt_backend_jm_watchdog_dump_get
	};

	return 0;
}

void kbase_hwcnt_backend_jm_watchdog_destroy(struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface || !iface->info)
		return;

	kfree((struct kbase_hwcnt_backend_jm_watchdog_info *)iface->info);

	memset(iface, 0, sizeof(*iface));
}
