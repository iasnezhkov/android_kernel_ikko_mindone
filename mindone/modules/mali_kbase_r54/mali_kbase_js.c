// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_js.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_ctx_sched.h>

#include <mali_kbase_defs.h>
#include <mali_kbase_config_defaults.h>

#include "mali_kbase_jm.h"
#include "mali_kbase_hwaccess_jm.h"
#include <mali_kbase_hwaccess_time.h>
#include <linux/priority_control_manager.h>
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
#include <mali_kbase_gpu_metrics.h>
#endif

enum {

	KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED = (1u << 0),

	KBASEP_JS_RELEASE_RESULT_SCHED_ALL = (1u << 1)
};

typedef u32 kbasep_js_release_result;

const int kbasep_js_atom_priority_to_relative[BASE_JD_NR_PRIO_LEVELS] = {
	KBASE_JS_ATOM_SCHED_PRIO_MED,
	KBASE_JS_ATOM_SCHED_PRIO_HIGH,
	KBASE_JS_ATOM_SCHED_PRIO_LOW,
	KBASE_JS_ATOM_SCHED_PRIO_REALTIME
};

const base_jd_prio kbasep_js_relative_priority_to_atom[KBASE_JS_ATOM_SCHED_PRIO_COUNT] = {
	BASE_JD_PRIO_REALTIME,
	BASE_JD_PRIO_HIGH,
	BASE_JD_PRIO_MEDIUM,
	BASE_JD_PRIO_LOW
};

static kbasep_js_release_result
kbasep_js_runpool_release_ctx_internal(struct kbase_device *kbdev, struct kbase_context *kctx,
				       struct kbasep_js_atom_retained_state *katom_retained_state);

static unsigned int kbase_js_get_slot(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

static void kbase_js_foreach_ctx_job(struct kbase_context *kctx, kbasep_js_ctx_job_cb *callback);

#if KBASE_KTRACE_ENABLE
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	return atomic_read(&kctx->refcount);
}
#else
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	CSTD_UNUSED(kctx);
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

static enum hrtimer_restart gpu_metrics_timer_callback(struct hrtimer *timer)
{
	struct kbasep_js_device_data *js_devdata =
		container_of(timer, struct kbasep_js_device_data, gpu_metrics_timer);
	struct kbase_device *kbdev = container_of(js_devdata, struct kbase_device, js_data);
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_gpu_metrics_emit_tracepoint(kbdev, ktime_get_raw_ns());
	WARN_ON_ONCE(!js_devdata->gpu_metrics_timer_running);
	if (js_devdata->gpu_metrics_timer_needed) {
		hrtimer_start(&js_devdata->gpu_metrics_timer,
			      HR_TIMER_DELAY_NSEC(kbase_gpu_metrics_get_tp_emit_interval()),
			      HRTIMER_MODE_REL);
	} else
		js_devdata->gpu_metrics_timer_running = false;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return HRTIMER_NORESTART;
}

static inline int gpu_metrics_ctx_init(struct kbase_context *kctx)
{
	struct kbase_gpu_metrics_ctx *gpu_metrics_ctx;
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long flags;
	int ret = 0;

	const struct cred *cred = get_current_cred();
	const unsigned int aid = cred->euid.val;

	put_cred(cred);

	if (unlikely(!kctx->filp))
		return 0;

	mutex_lock(&kbdev->kctx_list_lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	gpu_metrics_ctx = kbase_gpu_metrics_ctx_get(kbdev, aid);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (!gpu_metrics_ctx) {
		gpu_metrics_ctx = kmalloc(sizeof(*gpu_metrics_ctx), GFP_KERNEL);

		if (gpu_metrics_ctx) {
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			kbase_gpu_metrics_ctx_init(kbdev, gpu_metrics_ctx, aid);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {
			dev_err(kbdev->dev, "Allocation for gpu_metrics_ctx failed");
			ret = -ENOMEM;
		}
	}

	kctx->gpu_metrics_ctx = gpu_metrics_ctx;
	mutex_unlock(&kbdev->kctx_list_lock);

	return ret;
}

static inline void gpu_metrics_ctx_term(struct kbase_context *kctx)
{
	unsigned long flags;

	if (unlikely(!kctx->filp))
		return;

	mutex_lock(&kctx->kbdev->kctx_list_lock);
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	kbase_gpu_metrics_ctx_put(kctx->kbdev, kctx->gpu_metrics_ctx);
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	mutex_unlock(&kctx->kbdev->kctx_list_lock);
}
#endif

static base_jd_core_req core_reqs_from_jsn_features(struct kbase_js_features_props *features)
{
	base_jd_core_req core_req = 0u;

	if (features->write_value)
		core_req |= BASE_JD_REQ_V;

	if (features->cache_flush)
		core_req |= BASE_JD_REQ_CF;

	if (features->compute_shader)
		core_req |= BASE_JD_REQ_CS;

	if (features->tiler)
		core_req |= BASE_JD_REQ_T;

	if (features->fragment_shader)
		core_req |= BASE_JD_REQ_FS;

	return core_req;
}

static void kbase_js_sync_timers(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->js_data.runpool_mutex);
	kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&kbdev->js_data.runpool_mutex);
}

static inline bool jsctx_rb_none_to_pull_prio(struct kbase_context *kctx, unsigned int js, int prio)
{
	bool none_to_pull;
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	none_to_pull = RB_EMPTY_ROOT(&rb->runnable_tree);

	dev_dbg(kctx->kbdev->dev, "Slot %u (prio %d) is %spullable in kctx %pK\n", js, prio,
		none_to_pull ? "not " : "", kctx);

	return none_to_pull;
}

static inline bool jsctx_rb_none_to_pull(struct kbase_context *kctx, unsigned int js)
{
	int prio;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
		if (!jsctx_rb_none_to_pull_prio(kctx, js, prio))
			return false;
	}

	return true;
}

static void jsctx_queue_foreach_prio(struct kbase_context *kctx, unsigned int js, int prio,
				     kbasep_js_ctx_job_cb *callback)
{
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	while (!RB_EMPTY_ROOT(&queue->runnable_tree)) {
		struct rb_node *node = rb_first(&queue->runnable_tree);
		struct kbase_jd_atom *entry =
			rb_entry(node, struct kbase_jd_atom, runnable_tree_node);

		rb_erase(node, &queue->runnable_tree);
		callback(kctx->kbdev, entry);
	}

	while (!list_empty(&queue->x_dep_head)) {
		struct kbase_jd_atom *entry =
			list_entry(queue->x_dep_head.next, struct kbase_jd_atom, queue);

		WARN_ON(!(entry->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST));
		dev_dbg(kctx->kbdev->dev, "Del blocked atom %pK from X_DEP list\n", (void *)entry);

		list_del(queue->x_dep_head.next);
		entry->atom_flags &= ~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;

		callback(kctx->kbdev, entry);
	}
}

static inline void jsctx_queue_foreach(struct kbase_context *kctx, unsigned int js,
				       kbasep_js_ctx_job_cb *callback)
{
	int prio;

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++)
		jsctx_queue_foreach_prio(kctx, js, prio, callback);
}

static inline struct kbase_jd_atom *jsctx_rb_peek_prio(struct kbase_context *kctx, unsigned int js,
						       int prio)
{
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];
	struct rb_node *node;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	dev_dbg(kctx->kbdev->dev, "Peeking runnable tree of kctx %pK for prio %d (s:%u)\n",
		(void *)kctx, prio, js);

	node = rb_first(&rb->runnable_tree);
	if (!node) {
		dev_dbg(kctx->kbdev->dev, "Tree is empty\n");
		return NULL;
	}

	return rb_entry(node, struct kbase_jd_atom, runnable_tree_node);
}

static inline struct kbase_jd_atom *jsctx_rb_peek(struct kbase_context *kctx, unsigned int js)
{
	int prio;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
		struct kbase_jd_atom *katom;

		katom = jsctx_rb_peek_prio(kctx, js, prio);
		if (katom)
			return katom;
	}

	return NULL;
}

static inline void jsctx_rb_pull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	int prio = katom->sched_priority;
	unsigned int js = katom->slot_nr;
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	dev_dbg(kctx->kbdev->dev, "Erasing atom %pK from runnable tree of kctx %pK\n",
		(void *)katom, (void *)kctx);

	WARN_ON(katom != jsctx_rb_peek_prio(kctx, js, prio));

	rb_erase(&katom->runnable_tree_node, &rb->runnable_tree);
}

static void jsctx_tree_add(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int prio = katom->sched_priority;
	unsigned int js = katom->slot_nr;
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];
	struct rb_node **new = &(queue->runnable_tree.rb_node), *parent = NULL;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	dev_dbg(kbdev->dev, "Adding atom %pK to runnable tree of kctx %pK (s:%u)\n", (void *)katom,
		(void *)kctx, js);

	while (*new) {
		struct kbase_jd_atom *entry =
			container_of(*new, struct kbase_jd_atom, runnable_tree_node);

		parent = *new;
		if (kbase_jd_atom_is_younger(katom, entry))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&katom->runnable_tree_node, parent, new);
	rb_insert_color(&katom->runnable_tree_node, &queue->runnable_tree);

	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(kbdev, katom, TL_ATOM_STATE_READY);
}

static inline void jsctx_rb_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	KBASE_KTRACE_ADD_JM(kctx->kbdev, JS_UNPULL_JOB, kctx, katom, katom->jc, 0u);

	jsctx_tree_add(kctx, katom);
}

static bool kbase_js_ctx_pullable(struct kbase_context *kctx, unsigned int js, bool is_scheduled);
static bool kbase_js_ctx_list_add_pullable_nolock(struct kbase_device *kbdev,
						  struct kbase_context *kctx, unsigned int js);
static bool kbase_js_ctx_list_add_unpullable_nolock(struct kbase_device *kbdev,
						    struct kbase_context *kctx, unsigned int js);

typedef bool(katom_ordering_func)(const struct kbase_jd_atom *, const struct kbase_jd_atom *);

bool kbase_js_atom_runs_before(struct kbase_device *kbdev, const struct kbase_jd_atom *katom_a,
			       const struct kbase_jd_atom *katom_b,
			       const kbase_atom_ordering_flag_t order_flags)
{
	struct kbase_context *kctx_a = katom_a->kctx;
	struct kbase_context *kctx_b = katom_b->kctx;
	katom_ordering_func *samectxatomprio_ordering_func = kbase_jd_atom_is_younger;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (order_flags & KBASE_ATOM_ORDERING_FLAG_SEQNR)
		samectxatomprio_ordering_func = kbase_jd_atom_is_earlier;

	WARN_ON(katom_a->slot_nr != katom_b->slot_nr);

	if (kbdev->js_ctx_scheduling_mode == KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE) {

		if (kctx_a != kctx_b)
			return false;
	} else {

		if (kctx_a != kctx_b) {
			if (kctx_a->priority < kctx_b->priority)
				return true;
			if (kctx_a->priority > kctx_b->priority)
				return false;
		}
	}

	if (katom_a->sched_priority < katom_b->sched_priority)
		return true;
	if (katom_a->sched_priority > katom_b->sched_priority)
		return false;

	if (kctx_a == kctx_b && samectxatomprio_ordering_func(katom_a, katom_b))
		return true;

	return false;
}

int kbasep_js_devdata_init(struct kbase_device *const kbdev)
{
	struct kbasep_js_device_data *jsdd;
	int i, j;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	jsdd = &kbdev->js_data;

	INIT_LIST_HEAD(&jsdd->suspended_soft_jobs_list);

	jsdd->scheduling_period_ns = DEFAULT_JS_SCHEDULING_PERIOD_NS;
	jsdd->soft_stop_ticks = DEFAULT_JS_SOFT_STOP_TICKS;
	jsdd->soft_stop_ticks_cl = DEFAULT_JS_SOFT_STOP_TICKS_CL;
	jsdd->hard_stop_ticks_ss = DEFAULT_JS_HARD_STOP_TICKS_SS;
	jsdd->hard_stop_ticks_cl = DEFAULT_JS_HARD_STOP_TICKS_CL;
	jsdd->hard_stop_ticks_dumping = DEFAULT_JS_HARD_STOP_TICKS_DUMPING;
	jsdd->gpu_reset_ticks_ss = DEFAULT_JS_RESET_TICKS_SS;
	jsdd->gpu_reset_ticks_cl = DEFAULT_JS_RESET_TICKS_CL;

	jsdd->gpu_reset_ticks_dumping = DEFAULT_JS_RESET_TICKS_DUMPING;
	jsdd->ctx_timeslice_ns = DEFAULT_JS_CTX_TIMESLICE_NS;
	atomic_set(&jsdd->soft_job_timeout_ms, DEFAULT_JS_SOFT_JOB_TIMEOUT);
	jsdd->js_free_wait_time_ms = kbase_get_timeout_ms(kbdev, JM_DEFAULT_JS_FREE_TIMEOUT);

	dev_dbg(kbdev->dev, "JS Config Attribs: ");
	dev_dbg(kbdev->dev, "\tscheduling_period_ns:%u", jsdd->scheduling_period_ns);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks:%u", jsdd->soft_stop_ticks);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks_cl:%u", jsdd->soft_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_ss:%u", jsdd->hard_stop_ticks_ss);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_cl:%u", jsdd->hard_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_dumping:%u", jsdd->hard_stop_ticks_dumping);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_ss:%u", jsdd->gpu_reset_ticks_ss);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_cl:%u", jsdd->gpu_reset_ticks_cl);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_dumping:%u", jsdd->gpu_reset_ticks_dumping);
	dev_dbg(kbdev->dev, "\tctx_timeslice_ns:%u", jsdd->ctx_timeslice_ns);
	dev_dbg(kbdev->dev, "\tsoft_job_timeout:%i", atomic_read(&jsdd->soft_job_timeout_ms));
	dev_dbg(kbdev->dev, "\tjs_free_wait_time_ms:%u", jsdd->js_free_wait_time_ms);

	if (!(jsdd->soft_stop_ticks < jsdd->hard_stop_ticks_ss &&
	      jsdd->hard_stop_ticks_ss < jsdd->gpu_reset_ticks_ss &&
	      jsdd->soft_stop_ticks < jsdd->hard_stop_ticks_dumping &&
	      jsdd->hard_stop_ticks_dumping < jsdd->gpu_reset_ticks_dumping)) {
		dev_err(kbdev->dev,
			"Job scheduler timeouts invalid; soft/hard/reset tick counts should be in increasing order\n");
		return -EINVAL;
	}

#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS
	dev_dbg(kbdev->dev,
		"Job Scheduling Soft-stops disabled, ignoring value for soft_stop_ticks==%u at %uns per tick. Other soft-stops may still occur.",
		jsdd->soft_stop_ticks, jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev,
		"Job Scheduling Hard-stops disabled, ignoring values for hard_stop_ticks_ss==%d and hard_stop_ticks_dumping==%u at %uns per tick. Other hard-stops may still occur.",
		jsdd->hard_stop_ticks_ss, jsdd->hard_stop_ticks_dumping,
		jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS && KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev,
		"Note: The JS tick timer (if coded) will still be run, but do nothing.");
#endif

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i)
		jsdd->js_reqs[i] = core_reqs_from_jsn_features(&kbdev->gpu_props.js_features[i]);

	mutex_init(&jsdd->runpool_mutex);
	mutex_init(&jsdd->queue_mutex);
	sema_init(&jsdd->schedule_sem, 1);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i) {
		for (j = KBASE_JS_ATOM_SCHED_PRIO_FIRST; j < KBASE_JS_ATOM_SCHED_PRIO_COUNT; ++j) {
			INIT_LIST_HEAD(&jsdd->ctx_list_pullable[i][j]);
			INIT_LIST_HEAD(&jsdd->ctx_list_unpullable[i][j]);
		}
	}

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	hrtimer_init(&jsdd->gpu_metrics_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	jsdd->gpu_metrics_timer.function = gpu_metrics_timer_callback;
	jsdd->gpu_metrics_timer_needed = false;
	jsdd->gpu_metrics_timer_running = false;
#endif

	return 0;
}

void kbasep_js_devdata_halt(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_js_devdata_term(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	s8 zero_ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT] = {
		0,
	};
	CSTD_UNUSED(js_devdata);

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running == 0);
	KBASE_DEBUG_ASSERT(memcmp(js_devdata->runpool_irq.ctx_attr_ref_count,
				  zero_ctx_attr_ref_count, sizeof(zero_ctx_attr_ref_count)) == 0);
	CSTD_UNUSED(zero_ctx_attr_ref_count);

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	js_devdata->gpu_metrics_timer_needed = false;
	hrtimer_cancel(&js_devdata->gpu_metrics_timer);
#endif
}

int kbasep_js_kctx_init(struct kbase_context *const kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	int i, j;
	int ret;
	CSTD_UNUSED(js_kctx_info);

	KBASE_DEBUG_ASSERT(kctx != NULL);

	CSTD_UNUSED(ret);
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	ret = gpu_metrics_ctx_init(kctx);
	if (ret)
		return ret;
#endif

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; ++i)
		INIT_LIST_HEAD(&kctx->jctx.sched_info.ctx.ctx_list_entry[i]);

	js_kctx_info = &kctx->jctx.sched_info;

	kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);
	kbase_ctx_flag_clear(kctx, KCTX_DYING);

	kbase_ctx_flag_set(kctx, KCTX_SUBMIT_DISABLED);

	mutex_init(&js_kctx_info->ctx.jsctx_mutex);

	init_waitqueue_head(&js_kctx_info->ctx.is_scheduled_wait);

	for (i = KBASE_JS_ATOM_SCHED_PRIO_FIRST; i < KBASE_JS_ATOM_SCHED_PRIO_COUNT; i++) {
		for (j = 0; j < BASE_JM_MAX_NR_SLOTS; j++) {
			INIT_LIST_HEAD(&kctx->jsctx_queue[i][j].x_dep_head);
			kctx->jsctx_queue[i][j].runnable_tree = RB_ROOT;
		}
	}

	return 0;
}

void kbasep_js_kctx_term(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned int js;
	bool update_ctx_count = false;
	unsigned long flags;
	CSTD_UNUSED(js_kctx_info);

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs == 0);

	mutex_lock(&kbdev->js_data.queue_mutex);
	mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF)) {
		WARN_ON(atomic_read(&kbdev->js_data.nr_contexts_runnable) <= 0);
		atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		update_ctx_count = true;
		kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
	}

	mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kbdev->js_data.queue_mutex);

	if (update_ctx_count) {
		mutex_lock(&kbdev->js_data.runpool_mutex);
		kbase_backend_ctx_count_changed(kbdev);
		mutex_unlock(&kbdev->js_data.runpool_mutex);
	}

	while (true) {
		unsigned long flags;
		int refcount;

		mutex_lock(&kbdev->mmu_hw_mutex);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		refcount = atomic_read(&kctx->refcount);
		if ((refcount != 0) && !WARN_ON_ONCE(kctx->as_nr == KBASEP_AS_NR_INVALID)) {
			struct kbase_as *as = &kctx->kbdev->as[kctx->as_nr];
			int new_refcount;

			dev_dbg(kbdev->dev,
				"Waiting for pending fault worker to complete when terminating context (%d_%d)",
				kctx->tgid, kctx->id);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kbdev->mmu_hw_mutex);
			flush_workqueue(as->pf_wq);
			new_refcount = atomic_read(&kctx->refcount);
			if (refcount != new_refcount) {

				continue;
			} else {

				dev_warn(
					kbdev->dev,
					"No fault workers executed, %d refs remain for terminating context (%d_%d)",
					new_refcount, kctx->tgid, kctx->id);
				kbase_ctx_sched_remove_ctx(kctx);
			}
		} else {
			kbase_ctx_sched_remove_ctx_nolock(kctx);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kbdev->mmu_hw_mutex);
		}
		break;
	}

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
	gpu_metrics_ctx_term(kctx);
#endif
}

static void kbase_jsctx_slot_prio_blocked_clear(struct kbase_context *kctx, unsigned int js,
						int sched_prio)
{
	struct kbase_jsctx_slot_tracking *slot_tracking = &kctx->slot_tracking[js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	slot_tracking->blocked &= ~(((kbase_js_prio_bitmap_t)1) << sched_prio);
	KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev, JS_SLOT_PRIO_UNBLOCKED, kctx, NULL, 0, js,
				      (unsigned int)sched_prio);
}

static int kbase_jsctx_slot_atoms_pulled(struct kbase_context *kctx, unsigned int js)
{
	return atomic_read(&kctx->slot_tracking[js].atoms_pulled);
}

static bool kbase_jsctx_slot_prio_is_blocked(struct kbase_context *kctx, unsigned int js,
					     int sched_prio)
{
	struct kbase_jsctx_slot_tracking *slot_tracking = &kctx->slot_tracking[js];
	kbase_js_prio_bitmap_t prio_bit, higher_prios_mask;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	prio_bit = (((kbase_js_prio_bitmap_t)1) << sched_prio);

	higher_prios_mask = (prio_bit << 1u) - 1u;
	return (slot_tracking->blocked & higher_prios_mask) != 0u;
}

static int kbase_jsctx_slot_atom_pulled_inc(struct kbase_context *kctx,
					    const struct kbase_jd_atom *katom)
{
	unsigned int js = katom->slot_nr;
	int sched_prio = katom->sched_priority;
	struct kbase_jsctx_slot_tracking *slot_tracking = &kctx->slot_tracking[js];
	int nr_atoms_pulled;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	WARN(kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio),
	     "Should not have pulled atoms for slot %u from a context that is blocked at priority %d or higher",
	     js, sched_prio);

	nr_atoms_pulled = atomic_inc_return(&kctx->atoms_pulled_all_slots);
	atomic_inc(&slot_tracking->atoms_pulled);
	slot_tracking->atoms_pulled_pri[sched_prio]++;

	return nr_atoms_pulled;
}

static bool kbase_jsctx_slot_atom_pulled_dec(struct kbase_context *kctx,
					     const struct kbase_jd_atom *katom)
{
	unsigned int js = katom->slot_nr;
	int sched_prio = katom->sched_priority;
	int atoms_pulled_pri;
	struct kbase_jsctx_slot_tracking *slot_tracking = &kctx->slot_tracking[js];
	bool slot_prio_became_unblocked = false;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	atomic_dec(&kctx->atoms_pulled_all_slots);
	atomic_dec(&slot_tracking->atoms_pulled);

	atoms_pulled_pri = --(slot_tracking->atoms_pulled_pri[sched_prio]);

	if (!atoms_pulled_pri && kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio)) {
		kbase_jsctx_slot_prio_blocked_clear(kctx, js, sched_prio);

		if (!kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio))
			slot_prio_became_unblocked = true;
	}

	if (slot_prio_became_unblocked)
		KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev, JS_SLOT_PRIO_AND_HIGHER_UNBLOCKED, kctx,
					      katom, katom->jc, js, (unsigned int)sched_prio);

	return slot_prio_became_unblocked;
}

static bool kbase_js_ctx_list_add_pullable_nolock(struct kbase_device *kbdev,
						  struct kbase_context *kctx, unsigned int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "Add pullable tail kctx %pK (s:%u)\n", (void *)kctx, js);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
		      &kbdev->js_data.ctx_list_pullable[js][kctx->priority]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
			WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
			atomic_inc(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable |= (1UL << js);

	return ret;
}

static bool kbase_js_ctx_list_add_pullable_head_nolock(struct kbase_device *kbdev,
						       struct kbase_context *kctx, unsigned int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "Add pullable head kctx %pK (s:%u)\n", (void *)kctx, js);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
		 &kbdev->js_data.ctx_list_pullable[js][kctx->priority]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
			WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
			atomic_inc(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable |= (1UL << js);

	return ret;
}

static bool kbase_js_ctx_list_add_pullable_head(struct kbase_device *kbdev,
						struct kbase_context *kctx, unsigned int js)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	ret = kbase_js_ctx_list_add_pullable_head_nolock(kbdev, kctx, js);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

static bool kbase_js_ctx_list_add_unpullable_nolock(struct kbase_device *kbdev,
						    struct kbase_context *kctx, unsigned int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "Add unpullable tail kctx %pK (s:%u)\n", (void *)kctx, js);

	list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
		       &kbdev->js_data.ctx_list_unpullable[js][kctx->priority]);

	if (kctx->slots_pullable == (1UL << js)) {
		kbdev->js_data.nr_contexts_pullable--;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable &= ~(1UL << js);

	return ret;
}

static bool kbase_js_ctx_list_remove_nolock(struct kbase_device *kbdev, struct kbase_context *kctx,
					    unsigned int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]));

	list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	if (kctx->slots_pullable == (1UL << js)) {
		kbdev->js_data.nr_contexts_pullable--;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable &= ~(1UL << js);

	return ret;
}

static struct kbase_context *kbase_js_ctx_list_pop_head_nolock(struct kbase_device *kbdev,
							       unsigned int js)
{
	struct kbase_context *kctx;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = KBASE_JS_ATOM_SCHED_PRIO_FIRST; i < KBASE_JS_ATOM_SCHED_PRIO_COUNT; i++) {
		if (list_empty(&kbdev->js_data.ctx_list_pullable[js][i]))
			continue;

		kctx = list_entry(kbdev->js_data.ctx_list_pullable[js][i].next,
				  struct kbase_context, jctx.sched_info.ctx.ctx_list_entry[js]);

		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
		dev_dbg(kbdev->dev, "Popped %pK from the pullable queue (s:%u)\n", (void *)kctx,
			js);
		return kctx;
	}
	return NULL;
}

static struct kbase_context *kbase_js_ctx_list_pop_head(struct kbase_device *kbdev, unsigned int js)
{
	struct kbase_context *kctx;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kctx = kbase_js_ctx_list_pop_head_nolock(kbdev, js);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return kctx;
}

static bool kbase_js_ctx_pullable(struct kbase_context *kctx, unsigned int js, bool is_scheduled)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_jd_atom *katom;
	struct kbase_device *kbdev = kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	js_devdata = &kbdev->js_data;

	if (is_scheduled) {
		if (!kbasep_js_is_submit_allowed(js_devdata, kctx)) {
			dev_dbg(kbdev->dev, "JS: No submit allowed for kctx %pK\n", (void *)kctx);
			return false;
		}
	}
	katom = jsctx_rb_peek(kctx, js);
	if (!katom) {
		dev_dbg(kbdev->dev, "JS: No pullable atom in kctx %pK (s:%u)\n", (void *)kctx, js);
		return false;
	}
	if (kbase_jsctx_slot_prio_is_blocked(kctx, js, katom->sched_priority)) {
		KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev, JS_SLOT_PRIO_IS_BLOCKED, kctx, katom,
					      katom->jc, js, (unsigned int)katom->sched_priority);
		dev_dbg(kbdev->dev,
			"JS: kctx %pK is blocked from submitting atoms at priority %d and lower (s:%u)\n",
			(void *)kctx, katom->sched_priority, js);
		return false;
	}
	if (atomic_read(&katom->blocked)) {
		dev_dbg(kbdev->dev, "JS: Atom %pK is blocked in js_ctx_pullable\n", (void *)katom);
		return false;
	}
	if (katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) {
		if (katom->x_pre_dep->gpu_rb_state == KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
		    katom->x_pre_dep->will_fail_event_code) {
			dev_dbg(kbdev->dev,
				"JS: X pre-dep %pK is not present in slot FIFO or will fail\n",
				(void *)katom->x_pre_dep);
			return false;
		}
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
		    kbase_backend_nr_atoms_on_slot(kctx->kbdev, js)) {
			dev_dbg(kbdev->dev,
				"JS: Atom %pK has cross-slot fail dependency and atoms on slot (s:%u)\n",
				(void *)katom, js);
			return false;
		}
	}

	dev_dbg(kbdev->dev, "JS: Atom %pK is pullable in kctx %pK (s:%u)\n", (void *)katom,
		(void *)kctx, js);

	return true;
}

static bool kbase_js_dep_validate(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool ret = true;
	bool has_dep = false, has_x_dep = false;
	unsigned int js = kbase_js_get_slot(kbdev, katom);
	int prio = katom->sched_priority;
	int i;

	for (i = 0; i < 2; i++) {
		struct kbase_jd_atom *dep_atom = katom->dep[i].atom;

		if (dep_atom) {
			unsigned int dep_js = kbase_js_get_slot(kbdev, dep_atom);
			int dep_prio = dep_atom->sched_priority;

			dev_dbg(kbdev->dev, "Checking dep %d of atom %pK (s:%d) on %pK (s:%d)\n", i,
				(void *)katom, js, (void *)dep_atom, dep_js);

			if (!(dep_atom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE)) {
				dev_dbg(kbdev->dev, "Blocker not submitted yet\n");
				ret = false;
				break;
			}

			if (prio != dep_prio) {
				dev_dbg(kbdev->dev, "Different atom priorities\n");
				ret = false;
				break;
			}

			if (js == dep_js) {

				if (has_dep) {
					dev_dbg(kbdev->dev, "Too many same-slot deps\n");
					ret = false;
					break;
				}

				if (dep_atom->post_dep) {
					dev_dbg(kbdev->dev, "Too many same-slot successors\n");
					ret = false;
					break;
				}
				has_dep = true;
			} else {

				if (has_x_dep) {
					dev_dbg(kbdev->dev, "Too many cross-slot deps\n");
					ret = false;
					break;
				}

				if (dep_atom->x_post_dep) {
					dev_dbg(kbdev->dev, "Too many cross-slot successors\n");
					ret = false;
					break;
				}

				if (dep_atom->gpu_rb_state != KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
					dev_dbg(kbdev->dev,
						"Blocker already in ringbuffer (state:%d)\n",
						dep_atom->gpu_rb_state);
					ret = false;
					break;
				}

				if (dep_atom->status != KBASE_JD_ATOM_STATE_IN_JS) {
					dev_dbg(kbdev->dev,
						"Blocker already completed (status:%d)\n",
						dep_atom->status);
					ret = false;
					break;
				}

				has_x_dep = true;
			}

		}
	}

	if (ret) {
		for (i = 0; i < 2; i++) {
			struct kbase_jd_atom *dep_atom = katom->dep[i].atom;

			if (dep_atom) {
				unsigned int dep_js = kbase_js_get_slot(kbdev, dep_atom);

				dev_dbg(kbdev->dev,
					"Clearing dep %d of atom %pK (s:%d) on %pK (s:%d)\n", i,
					(void *)katom, js, (void *)dep_atom, dep_js);

				if ((js != dep_js) &&
				    (dep_atom->status != KBASE_JD_ATOM_STATE_COMPLETED) &&
				    (dep_atom->status != KBASE_JD_ATOM_STATE_HW_COMPLETED) &&
				    (dep_atom->status != KBASE_JD_ATOM_STATE_UNUSED)) {
					katom->atom_flags |= KBASE_KATOM_FLAG_X_DEP_BLOCKED;

					katom->x_pre_dep = dep_atom;
					dep_atom->x_post_dep = katom;
					if (kbase_jd_katom_dep_type(&katom->dep[i]) ==
					    BASE_JD_DEP_TYPE_DATA)
						katom->atom_flags |= KBASE_KATOM_FLAG_FAIL_BLOCKER;
				}
				if ((kbase_jd_katom_dep_type(&katom->dep[i]) ==
				     BASE_JD_DEP_TYPE_DATA) &&
				    (js == dep_js)) {
					katom->pre_dep = dep_atom;
					dep_atom->post_dep = katom;
				}

				list_del(&katom->dep_item[i]);
				kbase_jd_katom_dep_clear(&katom->dep[i]);
			}
		}
	} else {
		dev_dbg(kbdev->dev, "Deps of atom %pK (s:%d) could not be represented\n",
			(void *)katom, js);
	}

	return ret;
}

void kbase_js_set_ctx_priority(struct kbase_context *kctx, int new_priority)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (new_priority != kctx->priority) {
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kctx->slots_pullable & (1UL << js))
				list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					       &kbdev->js_data.ctx_list_pullable[js][new_priority]);
			else
				list_move_tail(
					&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					&kbdev->js_data.ctx_list_unpullable[js][new_priority]);
		}

		kctx->priority = new_priority;
	}
}

void kbase_js_update_ctx_priority(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int new_priority = KBASE_JS_ATOM_SCHED_PRIO_LOW;
	int prio;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->js_ctx_scheduling_mode == KBASE_JS_SYSTEM_PRIORITY_MODE) {

		for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT;
		     prio++) {
			if (kctx->atoms_count[prio]) {
				new_priority = prio;
				break;
			}
		}
	}

	kbase_js_set_ctx_priority(kctx, new_priority);
}
KBASE_EXPORT_TEST_API(kbase_js_update_ctx_priority);

bool kbasep_js_add_job(struct kbase_context *kctx, struct kbase_jd_atom *atom)
{
	unsigned long flags;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;

	bool enqueue_required = false;
	bool timer_sync = false;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);
	lockdep_assert_held(&kctx->jctx.lock);

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&js_devdata->runpool_mutex);

	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs < U32_MAX);
	++(js_kctx_info->ctx.nr_jobs);
	dev_dbg(kbdev->dev, "Add atom %pK to kctx %pK; now %d in ctx\n", (void *)atom, (void *)kctx,
		js_kctx_info->ctx.nr_jobs);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (++kctx->atoms_count[atom->sched_priority] == 1)
		kbase_js_update_ctx_priority(kctx);

	if (!kbase_js_dep_validate(kctx, atom)) {

		--(js_kctx_info->ctx.nr_jobs);
		dev_dbg(kbdev->dev, "Remove atom %pK from kctx %pK; now %d in ctx\n", (void *)atom,
			(void *)kctx, js_kctx_info->ctx.nr_jobs);

		atom->status = KBASE_JD_ATOM_STATE_QUEUED;
		dev_dbg(kbdev->dev, "Atom %pK status to queued\n", (void *)atom);

		kctx->atoms_count[atom->sched_priority]--;

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_devdata->runpool_mutex);

		goto out_unlock;
	}

	enqueue_required = kbase_js_dep_resolved_submit(kctx, atom);

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_ADD_JOB, kctx, atom, atom->jc,
				     kbase_ktrace_get_ctx_refcnt(kctx));

	kbasep_js_ctx_attr_ctx_retain_atom(kbdev, kctx, atom);

	if (enqueue_required) {
		if (kbase_js_ctx_pullable(kctx, atom->slot_nr, false))
			timer_sync =
				kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, atom->slot_nr);
		else
			timer_sync =
				kbase_js_ctx_list_add_unpullable_nolock(kbdev, kctx, atom->slot_nr);
	}

	if (enqueue_required && kctx == kbdev->hwaccess.active_kctx[atom->slot_nr])
		kbase_jm_try_kick(kbdev, 1UL << atom->slot_nr);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);

	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		if (kbase_ctx_flag(kctx, KCTX_DYING)) {

			kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, false);
		} else if (js_kctx_info->ctx.nr_jobs == 1) {

			KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
			dev_dbg(kbdev->dev, "JS: Enqueue Context %pK", kctx);

			WARN_ON(!enqueue_required);
		}
	}
out_unlock:
	dev_dbg(kbdev->dev, "Enqueue of kctx %pK is %srequired\n", kctx,
		enqueue_required ? "" : "not ");

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_unlock(&js_devdata->queue_mutex);

	return enqueue_required;
}

void kbasep_js_remove_job(struct kbase_device *kbdev, struct kbase_context *kctx,
			  struct kbase_jd_atom *atom)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);

	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_REMOVE_JOB, kctx, atom, atom->jc,
				     kbase_ktrace_get_ctx_refcnt(kctx));

	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs > 0);
	--(js_kctx_info->ctx.nr_jobs);
	dev_dbg(kbdev->dev, "Remove atom %pK from kctx %pK; now %d in ctx\n", (void *)atom,
		(void *)kctx, js_kctx_info->ctx.nr_jobs);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (--kctx->atoms_count[atom->sched_priority] == 0)
		kbase_js_update_ctx_priority(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

bool kbasep_js_remove_cancelled_job(struct kbase_device *kbdev, struct kbase_context *kctx,
				    struct kbase_jd_atom *katom)
{
	unsigned long flags;
	struct kbasep_js_atom_retained_state katom_retained_state;
	bool attr_state_changed;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);
	kbasep_js_remove_job(kbdev, kctx, katom);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	attr_state_changed =
		kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx, &katom_retained_state);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return attr_state_changed;
}

static kbasep_js_release_result kbasep_js_run_jobs_after_ctx_and_atom_release(
	struct kbase_device *kbdev, struct kbase_context *kctx,
	struct kbasep_js_atom_retained_state *katom_retained_state, bool runpool_ctx_attr_change)
{
	struct kbasep_js_device_data *js_devdata;
	kbasep_js_release_result result = 0;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom_retained_state != NULL);
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (js_devdata->nr_user_contexts_running != 0 && runpool_ctx_attr_change) {

		result = KBASEP_JS_RELEASE_RESULT_SCHED_ALL;

		KBASE_KTRACE_ADD_JM_SLOT(kbdev, JD_DONE_TRY_RUN_NEXT_JOB, kctx, NULL, 0u, 0);
	}
	return result;
}

static kbasep_js_release_result
kbasep_js_runpool_release_ctx_internal(struct kbase_device *kbdev, struct kbase_context *kctx,
				       struct kbasep_js_atom_retained_state *katom_retained_state)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	kbasep_js_release_result release_result = 0u;
	bool runpool_ctx_attr_change = false;
	int kctx_as_nr;
	int new_ref_count;
	CSTD_UNUSED(kctx_as_nr);

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	kctx_as_nr = kctx->as_nr;
	KBASE_DEBUG_ASSERT(kctx_as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(atomic_read(&kctx->refcount) > 0);

	mutex_lock(&kbdev->pm.lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_DEBUG_ASSERT(kctx_as_nr == kctx->as_nr);
	KBASE_DEBUG_ASSERT(atomic_read(&kctx->refcount) > 0);

	kbase_ctx_sched_release_ctx(kctx);
	new_ref_count = atomic_read(&kctx->refcount);

	if (kbasep_js_has_atom_finished(katom_retained_state))
		runpool_ctx_attr_change |=
			kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx, katom_retained_state);

	if (new_ref_count == 2 && kbase_ctx_flag(kctx, KCTX_PRIVILEGED) &&
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	    !kbase_pm_is_gpu_lost(kbdev) &&
#endif
	    !kbase_pm_is_suspending(kbdev)) {

		kbasep_js_set_submit_allowed(js_devdata, kctx);
	}

	if (new_ref_count == 1 && (!kbasep_js_is_submit_allowed(js_devdata, kctx) ||
#ifdef CONFIG_MALI_ARBITER_SUPPORT
				   kbase_pm_is_gpu_lost(kbdev) ||
#endif
				   kbase_pm_is_suspending(kbdev))) {
		int num_slots = kbdev->gpu_props.num_job_slots;
		unsigned int slot;

		dev_dbg(kbdev->dev,
			"JS: RunPool Remove Context %pK because refcount=%d, jobs=%d, allowed=%d",
			kctx, new_ref_count, js_kctx_info->ctx.nr_jobs,
			kbasep_js_is_submit_allowed(js_devdata, kctx));

		KBASE_TLSTREAM_TL_NRET_AS_CTX(kbdev, &kbdev->as[kctx->as_nr], kctx);

		kbase_backend_release_ctx_irq(kbdev, kctx);

		for (slot = 0; slot < num_slots; slot++) {
			if (kbdev->hwaccess.active_kctx[slot] == kctx) {
				dev_dbg(kbdev->dev, "Marking kctx %pK as inactive (s:%d)\n",
					(void *)kctx, slot);
				kbdev->hwaccess.active_kctx[slot] = NULL;
			}
		}

		runpool_ctx_attr_change |= kbasep_js_ctx_attr_runpool_release_ctx(kbdev, kctx);

		release_result |= kbasep_js_run_jobs_after_ctx_and_atom_release(
			kbdev, kctx, katom_retained_state, runpool_ctx_attr_change);

		for (slot = 0; slot < num_slots; slot++) {
			if (kbase_js_ctx_pullable(kctx, slot, false))
				kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, slot);
		}

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		kbase_backend_release_ctx_noirq(kbdev, kctx);

		mutex_unlock(&kbdev->pm.lock);

		kbase_backend_ctx_count_changed(kbdev);

		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);

		wake_up(&js_kctx_info->ctx.is_scheduled_wait);

		release_result |= KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED |
				  KBASEP_JS_RELEASE_RESULT_SCHED_ALL;
	} else {
		kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev, kctx, katom_retained_state,
							      runpool_ctx_attr_change);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->pm.lock);
	}

	return release_result;
}

void kbasep_js_runpool_release_ctx_nolock(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_atom_retained_state katom_retained_state;

	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	kbasep_js_runpool_release_ctx_internal(kbdev, kctx, &katom_retained_state);
}

void kbasep_js_runpool_requeue_or_kill_ctx(struct kbase_device *kbdev, struct kbase_context *kctx,
					   bool has_pm_ref)
{
	CSTD_UNUSED(has_pm_ref);

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {

		dev_dbg(kbdev->dev, "JS: ** Killing Context %pK on RunPool Remove **", kctx);
		kbase_js_foreach_ctx_job(kctx, &kbase_jd_cancel);
	}
}

void kbasep_js_runpool_release_ctx_and_katom_retained_state(
	struct kbase_device *kbdev, struct kbase_context *kctx,
	struct kbasep_js_atom_retained_state *katom_retained_state)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx, katom_retained_state);

	mutex_unlock(&js_devdata->runpool_mutex);

	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, true);

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);

	if (release_result & KBASEP_JS_RELEASE_RESULT_SCHED_ALL)
		kbase_js_sched_all(kbdev);
}

void kbasep_js_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_atom_retained_state katom_retained_state;

	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &katom_retained_state);
}

static void kbasep_js_runpool_release_ctx_no_schedule(struct kbase_device *kbdev,
						      struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;
	struct kbasep_js_atom_retained_state katom_retained_state_struct;
	struct kbasep_js_atom_retained_state *katom_retained_state = &katom_retained_state_struct;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	kbasep_js_atom_retained_state_init_invalid(katom_retained_state);

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx, katom_retained_state);

	mutex_unlock(&js_devdata->runpool_mutex);
	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, true);

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

}

void kbase_js_set_timeouts(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_backend_timeouts_changed(kbdev);
}

static bool kbasep_js_schedule_ctx(struct kbase_device *kbdev, struct kbase_context *kctx,
				   unsigned int js)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;
	bool kctx_suspended = false;
	int as_nr;

	dev_dbg(kbdev->dev, "Scheduling kctx %pK (s:%u)\n", kctx, js);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	as_nr = kbase_ctx_sched_retain_ctx(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
	if (as_nr == KBASEP_AS_NR_INVALID) {
		as_nr = kbase_backend_find_and_release_free_address_space(kbdev, kctx);
		if (as_nr != KBASEP_AS_NR_INVALID) {

			mutex_lock(&kbdev->mmu_hw_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			as_nr = kbase_ctx_sched_retain_ctx(kctx);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kbdev->mmu_hw_mutex);

			WARN_ON(as_nr == KBASEP_AS_NR_INVALID);
		}
	}
	if ((as_nr < 0) || (as_nr >= BASE_MAX_NR_AS))
		return false;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {

		kbase_ctx_sched_release_ctx(kctx);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return false;
	}

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_TRY_SCHEDULE_HEAD_CTX, kctx, NULL, 0u,
				     kbase_ktrace_get_ctx_refcnt(kctx));

	kbase_ctx_flag_set(kctx, KCTX_SCHEDULED);

	if (!kbase_backend_use_ctx(kbdev, kctx, as_nr)) {

		kbase_ctx_sched_release_ctx(kctx);
		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return false;
	}

	kbdev->hwaccess.active_kctx[js] = kctx;

	KBASE_TLSTREAM_TL_RET_AS_CTX(kbdev, &kbdev->as[kctx->as_nr], kctx);

	wake_up(&js_kctx_info->ctx.is_scheduled_wait);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_suspending(kbdev) || kbase_pm_is_gpu_lost(kbdev)) {
#else
	if (kbase_pm_is_suspending(kbdev)) {
#endif

		bool retained;
		CSTD_UNUSED(retained);

		retained = kbase_ctx_sched_inc_refcount_nolock(kctx);
		KBASE_DEBUG_ASSERT(retained);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);
		kctx_suspended = true;
	}

	kbase_ctx_flag_clear(kctx, KCTX_PULLED_SINCE_ACTIVE_JS0 << js);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_backend_ctx_count_changed(kbdev);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	if (kctx_suspended) {

		kbasep_js_runpool_release_ctx_no_schedule(kbdev, kctx);
		return false;
	}
	return true;
}

static bool kbase_js_use_ctx(struct kbase_device *kbdev, struct kbase_context *kctx,
			     unsigned int js)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) && kbase_backend_use_ctx_sched(kbdev, kctx, js)) {
		dev_dbg(kbdev->dev, "kctx %pK already has ASID - mark as active (s:%u)\n",
			(void *)kctx, js);

		if (kbdev->hwaccess.active_kctx[js] != kctx) {
			kbdev->hwaccess.active_kctx[js] = kctx;
			kbase_ctx_flag_clear(kctx, KCTX_PULLED_SINCE_ACTIVE_JS0 << js);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return true;
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	return kbasep_js_schedule_ctx(kbdev, kctx, js);
}

void kbasep_js_schedule_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbasep_js_device_data *js_devdata;
	bool is_scheduled;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

#ifdef CONFIG_MALI_ARBITER_SUPPORT

	if (kbdev->arb.arb_if == NULL)
		if (WARN_ON(kbase_pm_is_suspending(kbdev)))
			return;
#else

	if (WARN_ON(kbase_pm_is_suspending(kbdev)))
		return;
#endif

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	kbase_ctx_flag_set(kctx, KCTX_PRIVILEGED);

	is_scheduled = kbase_ctx_flag(kctx, KCTX_SCHEDULED);
	if (!is_scheduled) {

		if (kbase_js_ctx_list_add_pullable_head(kbdev, kctx, 0))
			kbase_js_sync_timers(kbdev);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);

		kbase_js_sched_all(kbdev);

		wait_event(kctx->jctx.sched_info.ctx.is_scheduled_wait,
			   kbase_ctx_flag(kctx, KCTX_SCHEDULED));
	} else {

		WARN_ON(!kbase_ctx_sched_inc_refcount(kctx));
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
	}
}
KBASE_EXPORT_TEST_API(kbasep_js_schedule_privileged_ctx);

void kbasep_js_release_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbase_ctx_flag_clear(kctx, KCTX_PRIVILEGED);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	kbasep_js_runpool_release_ctx(kbdev, kctx);

	kbase_js_sched_all(kbdev);
}
KBASE_EXPORT_TEST_API(kbasep_js_release_privileged_ctx);

void kbasep_js_suspend(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	int i;
	u16 retained = 0u;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kbase_pm_is_suspending(kbdev));
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	js_devdata->runpool_irq.submit_allowed = 0;

	for (i = BASE_MAX_NR_AS - 1; i >= 0; --i) {
		struct kbase_context *kctx = kbdev->as_to_kctx[i];

		retained = retained << 1;

		if (kctx && !(kbdev->as_free & (1u << i))) {
			kbase_ctx_sched_retain_ctx_refcount(kctx);
			retained |= 1u;

		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	for (i = 0; i < BASE_MAX_NR_AS; ++i, retained = retained >> 1) {
		struct kbase_context *kctx = kbdev->as_to_kctx[i];

		if (retained & 1u)
			kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

}

void kbasep_js_resume(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	unsigned int js;
	int prio;

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	mutex_lock(&js_devdata->queue_mutex);
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT;
		     prio++) {
			struct kbase_context *kctx, *n;
			unsigned long flags;

#ifndef CONFIG_MALI_ARBITER_SUPPORT
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			list_for_each_entry_safe(kctx, n,
						 &kbdev->js_data.ctx_list_unpullable[js][prio],
						 jctx.sched_info.ctx.ctx_list_entry[js]) {
				struct kbasep_js_kctx_info *js_kctx_info;
				bool timer_sync = false;

				spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

				js_kctx_info = &kctx->jctx.sched_info;

				mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
				mutex_lock(&js_devdata->runpool_mutex);
				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

				if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
				    kbase_js_ctx_pullable(kctx, js, false))
					timer_sync = kbase_js_ctx_list_add_pullable_nolock(
						kbdev, kctx, js);

				spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

				if (timer_sync)
					kbase_backend_ctx_count_changed(kbdev);

				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			}
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#else
			bool timer_sync = false;

			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			list_for_each_entry_safe(kctx, n,
						 &kbdev->js_data.ctx_list_unpullable[js][prio],
						 jctx.sched_info.ctx.ctx_list_entry[js]) {
				if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
				    kbase_js_ctx_pullable(kctx, js, false))
					timer_sync |= kbase_js_ctx_list_add_pullable_nolock(
						kbdev, kctx, js);
			}

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

			if (timer_sync) {
				mutex_lock(&js_devdata->runpool_mutex);
				kbase_backend_ctx_count_changed(kbdev);
				mutex_unlock(&js_devdata->runpool_mutex);
			}
#endif
		}
	}
	mutex_unlock(&js_devdata->queue_mutex);

	kbase_js_sched_all(kbdev);

}

bool kbase_js_is_atom_valid(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	CSTD_UNUSED(kbdev);

	if ((katom->core_req & BASE_JD_REQ_FS) &&
	    (katom->core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T)))
		return false;

	if ((katom->core_req & BASE_JD_REQ_JOB_SLOT) && (katom->jobslot >= BASE_JM_MAX_NR_SLOTS))
		return false;

	return true;
}

static unsigned int kbase_js_get_slot(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	if (katom->core_req & BASE_JD_REQ_JOB_SLOT)
		return katom->jobslot;

	if (katom->core_req & BASE_JD_REQ_FS)
		return 0;

	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		if (katom->device_nr == 1 && kbdev->gpu_props.num_core_groups == 2)
			return 2;
	}

	return 1;
}

bool kbase_js_dep_resolved_submit(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	bool enqueue_required;

	katom->slot_nr = kbase_js_get_slot(kctx->kbdev, katom);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->jctx.lock);

	enqueue_required = jsctx_rb_none_to_pull(kctx, katom->slot_nr);

	if ((katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) ||
	    (katom->pre_dep &&
	     (katom->pre_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST))) {
		int prio = katom->sched_priority;
		unsigned int js = katom->slot_nr;
		struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

		dev_dbg(kctx->kbdev->dev, "Add atom %pK to X_DEP list (s:%u)\n", (void *)katom, js);

		list_add_tail(&katom->queue, &queue->x_dep_head);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
		enqueue_required = false;
	} else {
		dev_dbg(kctx->kbdev->dev, "Atom %pK not added to X_DEP list\n", (void *)katom);

		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		jsctx_tree_add(kctx, katom);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_TREE;
	}

	dev_dbg(kctx->kbdev->dev, "Enqueue of kctx %pK is %srequired to submit atom %pK\n", kctx,
		enqueue_required ? "" : "not ", katom);

	return enqueue_required;
}

static void kbase_js_move_to_tree(struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&katom->kctx->kbdev->hwaccess_lock);

	while (katom) {
		WARN_ON(!(katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST));

		if (!(katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED)) {
			dev_dbg(katom->kctx->kbdev->dev,
				"Del atom %pK from X_DEP list in js_move_to_tree\n", (void *)katom);

			list_del(&katom->queue);
			katom->atom_flags &= ~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
			jsctx_tree_add(katom->kctx, katom);
			katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_TREE;
		} else {
			dev_dbg(katom->kctx->kbdev->dev,
				"Atom %pK blocked on x-dep in js_move_to_tree\n", (void *)katom);
			break;
		}

		katom = katom->post_dep;
	}
}

static void kbase_js_evict_deps(struct kbase_context *kctx, struct kbase_jd_atom *katom,
				unsigned int js, int prio)
{
	struct kbase_jd_atom *x_dep = katom->x_post_dep;
	struct kbase_jd_atom *next_katom = katom->post_dep;

	CSTD_UNUSED(js);
	CSTD_UNUSED(prio);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (next_katom) {
		KBASE_DEBUG_ASSERT(next_katom->status != KBASE_JD_ATOM_STATE_HW_COMPLETED);
		next_katom->will_fail_event_code = katom->event_code;
	}

	if (x_dep && (x_dep->atom_flags &
		      (KBASE_KATOM_FLAG_JSCTX_IN_TREE | KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST))) {

		x_dep->atom_flags &= ~KBASE_KATOM_FLAG_X_DEP_BLOCKED;

		dev_dbg(kctx->kbdev->dev, "Cleared X_DEP flag on atom %pK\n", (void *)x_dep);

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER)
			x_dep->will_fail_event_code = katom->event_code;

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST)
			kbase_js_move_to_tree(x_dep);
	}
}

struct kbase_jd_atom *kbase_js_pull(struct kbase_context *kctx, unsigned int js)
{
	struct kbase_jd_atom *katom;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_device *kbdev;
	int pulled;

	KBASE_DEBUG_ASSERT(kctx);

	kbdev = kctx->kbdev;
	dev_dbg(kbdev->dev, "JS: pulling an atom from kctx %pK (s:%u)\n", (void *)kctx, js);

	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kbasep_js_is_submit_allowed(js_devdata, kctx)) {
		dev_dbg(kbdev->dev, "JS: No submit allowed for kctx %pK\n", (void *)kctx);
		return NULL;
	}
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_suspending(kbdev) || kbase_pm_is_gpu_lost(kbdev))
#else
	if (kbase_pm_is_suspending(kbdev))
#endif
		return NULL;

	katom = jsctx_rb_peek(kctx, js);
	if (!katom) {
		dev_dbg(kbdev->dev, "JS: No pullable atom in kctx %pK (s:%u)\n", (void *)kctx, js);
		return NULL;
	}
	if (kbase_jsctx_slot_prio_is_blocked(kctx, js, katom->sched_priority)) {
		dev_dbg(kbdev->dev,
			"JS: kctx %pK is blocked from submitting atoms at priority %d and lower (s:%u)\n",
			(void *)kctx, katom->sched_priority, js);
		return NULL;
	}
	if (atomic_read(&katom->blocked)) {
		dev_dbg(kbdev->dev, "JS: Atom %pK is blocked in js_pull\n", (void *)katom);
		return NULL;
	}

	if (katom->pre_dep && kbase_jsctx_slot_atoms_pulled(kctx, js)) {
		struct kbase_jd_atom *prev_atom = kbase_backend_inspect_tail(kbdev, js);

		if (prev_atom && prev_atom->kctx != kctx)
			return NULL;
	}

	if (katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) {
		if (katom->x_pre_dep->gpu_rb_state == KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
		    katom->x_pre_dep->will_fail_event_code) {
			dev_dbg(kbdev->dev,
				"JS: X pre-dep %pK is not present in slot FIFO or will fail\n",
				(void *)katom->x_pre_dep);
			return NULL;
		}
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
		    kbase_backend_nr_atoms_on_slot(kbdev, js)) {
			dev_dbg(kbdev->dev,
				"JS: Atom %pK has cross-slot fail dependency and atoms on slot (s:%u)\n",
				(void *)katom, js);
			return NULL;
		}
	}

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JS_PULL_JOB, kctx, katom, katom->jc, js,
				      (u64)katom->sched_priority);
	kbase_ctx_flag_set(kctx, KCTX_PULLED);
	kbase_ctx_flag_set(kctx, (KCTX_PULLED_SINCE_ACTIVE_JS0 << js));

	pulled = kbase_jsctx_slot_atom_pulled_inc(kctx, katom);
	if (pulled == 1 && !kctx->slots_pullable) {
		WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
		kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
		atomic_inc(&kbdev->js_data.nr_contexts_runnable);
	}
	jsctx_rb_pull(kctx, katom);

	kbase_ctx_sched_retain_ctx_refcount(kctx);

	katom->ticks = 0;

	dev_dbg(kbdev->dev, "JS: successfully pulled atom %pK from kctx %pK (s:%u)\n",
		(void *)katom, (void *)kctx, js);

	return katom;
}

static void js_return_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;
	struct kbasep_js_atom_retained_state retained_state;
	unsigned int js = katom->slot_nr;
	bool slot_became_unblocked;
	bool timer_sync = false;
	bool context_idle = false;
	unsigned long flags;
	base_jd_core_req core_req = katom->core_req;
	u64 cache_jc = katom->jc;

	dev_dbg(kbdev->dev, "%s for atom %pK with event code 0x%x\n", __func__, (void *)katom,
		katom->event_code);

	KBASE_KTRACE_ADD_JM(kbdev, JS_RETURN_WORKER, kctx, katom, katom->jc, 0);
	KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTSTOP_EX(kbdev, katom);

	kbase_backend_complete_wq(kbdev, katom);

	kbasep_js_atom_retained_state_copy(&retained_state, katom);

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	atomic_dec(&katom->blocked);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	slot_became_unblocked = kbase_jsctx_slot_atom_pulled_dec(kctx, katom);

	if (!kbase_jsctx_slot_atoms_pulled(kctx, js) && jsctx_rb_none_to_pull(kctx, js))
		timer_sync |= kbase_js_ctx_list_remove_nolock(kbdev, kctx, js);

	if (slot_became_unblocked && kbase_jsctx_atoms_pulled(kctx) &&
	    kbase_js_ctx_pullable(kctx, js, true))
		timer_sync |= kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, js);

	if (!kbase_jsctx_atoms_pulled(kctx)) {
		dev_dbg(kbdev->dev, "No atoms currently pulled from context %pK\n", (void *)kctx);

		if (!kctx->slots_pullable) {
			dev_dbg(kbdev->dev, "Context %pK %s counted as runnable\n", (void *)kctx,
				kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF) ? "is" : "isn't");

			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
			timer_sync = true;
		}

		if (kctx->as_nr != KBASEP_AS_NR_INVALID && !kbase_ctx_flag(kctx, KCTX_DYING)) {
			unsigned int num_slots = kbdev->gpu_props.num_job_slots;
			unsigned int slot;

			if (!kbasep_js_is_submit_allowed(js_devdata, kctx))
				kbasep_js_set_submit_allowed(js_devdata, kctx);

			for (slot = 0; slot < num_slots; slot++) {
				if (kbase_js_ctx_pullable(kctx, slot, true))
					timer_sync |= kbase_js_ctx_list_add_pullable_nolock(
						kbdev, kctx, slot);
			}
		}

		kbase_jm_idle_ctx(kbdev, kctx);

		context_idle = true;
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (context_idle) {
		dev_dbg(kbdev->dev, "Context %pK %s counted as active\n", (void *)kctx,
			kbase_ctx_flag(kctx, KCTX_ACTIVE) ? "is" : "isn't");
		WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
		kbase_pm_context_idle(kbdev);
	}

	if (timer_sync)
		kbase_js_sync_timers(kbdev);

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);

	dev_dbg(kbdev->dev, "JS: retained state %s finished",
		kbasep_js_has_atom_finished(&retained_state) ? "has" : "hasn't");

	WARN_ON(kbasep_js_has_atom_finished(&retained_state));

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &retained_state);

	kbase_js_sched_all(kbdev);

	kbase_backend_complete_wq_post_sched(kbdev, core_req);

	KBASE_KTRACE_ADD_JM(kbdev, JS_RETURN_WORKER_END, kctx, NULL, cache_jc, 0);

	dev_dbg(kbdev->dev, "Leaving %s for atom %pK\n", __func__, (void *)katom);
}

void kbase_js_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	dev_dbg(kctx->kbdev->dev, "Unpulling atom %pK in kctx %pK\n", (void *)katom, (void *)kctx);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	jsctx_rb_unpull(kctx, katom);

	WARN_ON(work_pending(&katom->work));

	atomic_inc(&katom->blocked);

	kbase_job_check_leave_disjoint(kctx->kbdev, katom);

	INIT_WORK(&katom->work, js_return_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

bool kbase_js_complete_atom_wq(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_device *kbdev;
	unsigned long flags;
	bool timer_sync = false;
	unsigned int atom_slot;
	bool context_idle = false;
	int prio = katom->sched_priority;

	kbdev = kctx->kbdev;
	atom_slot = katom->slot_nr;

	dev_dbg(kbdev->dev, "%s for atom %pK (s:%u)\n", __func__, (void *)katom, atom_slot);

	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&js_devdata->runpool_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE) {
		bool slot_became_unblocked;

		dev_dbg(kbdev->dev, "Atom %pK is in runnable_tree\n", (void *)katom);

		slot_became_unblocked = kbase_jsctx_slot_atom_pulled_dec(kctx, katom);
		context_idle = !kbase_jsctx_atoms_pulled(kctx);

		if (!kbase_jsctx_atoms_pulled(kctx) && !kctx->slots_pullable) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
			timer_sync = true;
		}

		if (slot_became_unblocked) {
			dev_dbg(kbdev->dev,
				"kctx %pK is no longer blocked from submitting on slot %u at priority %d or higher\n",
				(void *)kctx, atom_slot, prio);

			if (kbase_js_ctx_pullable(kctx, atom_slot, true))
				timer_sync |= kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx,
										    atom_slot);
		}
	}
	WARN_ON(!(katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE));

	if (!kbase_jsctx_slot_atoms_pulled(kctx, atom_slot) &&
	    jsctx_rb_none_to_pull(kctx, atom_slot)) {
		if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[atom_slot]))
			timer_sync |= kbase_js_ctx_list_remove_nolock(kctx->kbdev, kctx, atom_slot);
	}

	if (!kbasep_js_is_submit_allowed(js_devdata, kctx) && !kbase_jsctx_atoms_pulled(kctx) &&
	    !kbase_ctx_flag(kctx, KCTX_DYING)) {
		unsigned int js;

		kbasep_js_set_submit_allowed(js_devdata, kctx);

		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, js);
		}
	} else if (katom->x_post_dep && kbasep_js_is_submit_allowed(js_devdata, kctx)) {
		unsigned int js;

		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, js);
		}
	}

	if (context_idle) {
		dev_dbg(kbdev->dev, "kctx %pK is no longer active\n", (void *)kctx);
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);

	dev_dbg(kbdev->dev, "Leaving %s\n", __func__);
	return context_idle;
}

struct kbase_jd_atom *kbase_js_complete_atom(struct kbase_jd_atom *katom, ktime_t *end_timestamp)
{
	struct kbase_device *kbdev;
	struct kbase_context *kctx = katom->kctx;
	struct kbase_jd_atom *x_dep = katom->x_post_dep;

	kbdev = kctx->kbdev;
	dev_dbg(kbdev->dev, "Atom %pK complete in kctx %pK (post-dep %pK)\n", (void *)katom,
		(void *)kctx, (void *)x_dep);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (katom->will_fail_event_code)
		katom->event_code = katom->will_fail_event_code;

	katom->status = KBASE_JD_ATOM_STATE_HW_COMPLETED;
	dev_dbg(kbdev->dev, "Atom %pK status to HW completed\n", (void *)katom);

	if (katom->event_code != BASE_JD_EVENT_DONE) {
		kbase_js_evict_deps(kctx, katom, katom->slot_nr, katom->sched_priority);
	}

	KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(kbdev, NULL, katom->slot_nr, 0, TL_JS_EVENT_STOP);

	trace_sysgraph_gpu(SGR_COMPLETE, kctx->id, kbase_jd_atom_id(katom->kctx, katom),
			   katom->slot_nr);

	KBASE_TLSTREAM_TL_JD_DONE_START(kbdev, katom);
	kbase_jd_done(katom, katom->slot_nr, end_timestamp, 0);
	KBASE_TLSTREAM_TL_JD_DONE_END(kbdev, katom);

	if (x_dep &&
	    (katom->event_code == BASE_JD_EVENT_DONE ||
	     !(x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER)) &&
	    (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST)) {
		bool was_pullable = kbase_js_ctx_pullable(kctx, x_dep->slot_nr, false);
		x_dep->atom_flags &= ~KBASE_KATOM_FLAG_X_DEP_BLOCKED;
		dev_dbg(kbdev->dev, "Cleared X_DEP flag on atom %pK\n", (void *)x_dep);

		kbase_js_move_to_tree(x_dep);

		if (!was_pullable && kbase_js_ctx_pullable(kctx, x_dep->slot_nr, false))
			kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, x_dep->slot_nr);

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE) {
			dev_dbg(kbdev->dev, "Atom %pK is in runnable tree\n", (void *)x_dep);
			return x_dep;
		}
	} else {
		dev_dbg(kbdev->dev, "No cross-slot dep to unblock for atom %pK\n", (void *)katom);
	}

	return NULL;
}

void kbase_js_sched(struct kbase_device *kbdev, unsigned int js_mask)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_context *last_active_kctx[BASE_JM_MAX_NR_SLOTS];
	bool timer_sync = false;
	bool ctx_waiting[BASE_JM_MAX_NR_SLOTS];
	unsigned int js;

	KBASE_TLSTREAM_TL_JS_SCHED_START(kbdev, 0);

	dev_dbg(kbdev->dev, "%s kbdev %pK mask 0x%x\n", __func__, (void *)kbdev,
		(unsigned int)js_mask);

	js_devdata = &kbdev->js_data;

	down(&js_devdata->schedule_sem);
	mutex_lock(&js_devdata->queue_mutex);

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		last_active_kctx[js] = kbdev->hwaccess.active_kctx[js];
		ctx_waiting[js] = false;
	}

	while (js_mask) {
		js = (unsigned int)ffs((int)js_mask) - 1;

		while (1) {
			struct kbase_context *kctx;
			unsigned long flags;
			bool context_idle = false;

			kctx = kbase_js_ctx_list_pop_head(kbdev, js);

			if (!kctx) {
				js_mask &= ~(1UL << js);
				dev_dbg(kbdev->dev, "No kctx on pullable list (s:%u)\n", js);
				break;
			}

			if (!kbase_ctx_flag(kctx, KCTX_ACTIVE)) {
				context_idle = true;

				dev_dbg(kbdev->dev, "kctx %pK is not active (s:%u)\n", (void *)kctx,
					js);

				if (kbase_pm_context_active_handle_suspend(
					    kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE)) {
					dev_dbg(kbdev->dev, "Suspend pending (s:%u)\n", js);

					mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
					if (kbase_js_ctx_list_add_pullable_head(kctx->kbdev, kctx,
										js))
						kbase_js_sync_timers(kbdev);
					mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
					mutex_unlock(&js_devdata->queue_mutex);
					up(&js_devdata->schedule_sem);
					KBASE_TLSTREAM_TL_JS_SCHED_END(kbdev, 0);
					return;
				}
				kbase_ctx_flag_set(kctx, KCTX_ACTIVE);
			}

			if (!kbase_js_use_ctx(kbdev, kctx, js)) {
				mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);

				dev_dbg(kbdev->dev, "kctx %pK cannot be used at this time\n", kctx);

				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
				if (kbase_js_ctx_pullable(kctx, js, false) ||
				    kbase_ctx_flag(kctx, KCTX_PRIVILEGED))
					timer_sync |= kbase_js_ctx_list_add_pullable_head_nolock(
						kctx->kbdev, kctx, js);
				else
					timer_sync |= kbase_js_ctx_list_add_unpullable_nolock(
						kctx->kbdev, kctx, js);
				spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
				mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
				if (context_idle) {
					WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
					kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
					kbase_pm_context_idle(kbdev);
				}

				js_mask &= ~(1UL << js);
				break;
			}
			mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			kbase_ctx_flag_clear(kctx, KCTX_PULLED);

			if (!kbase_jm_kick(kbdev, 1UL << js)) {
				dev_dbg(kbdev->dev, "No more jobs can be submitted (s:%u)\n", js);
				js_mask &= ~(1UL << js);
			}
			if (!kbase_ctx_flag(kctx, KCTX_PULLED)) {
				bool pullable;

				dev_dbg(kbdev->dev, "No atoms pulled from kctx %pK (s:%u)\n",
					(void *)kctx, js);

				pullable = kbase_js_ctx_pullable(kctx, js, true);

				if (pullable && kctx == last_active_kctx[js] &&
				    kbase_ctx_flag(kctx, (KCTX_PULLED_SINCE_ACTIVE_JS0 << js)))
					timer_sync |= kbase_js_ctx_list_add_pullable_nolock(
						kctx->kbdev, kctx, js);
				else if (pullable)
					timer_sync |= kbase_js_ctx_list_add_pullable_head_nolock(
						kctx->kbdev, kctx, js);
				else
					timer_sync |= kbase_js_ctx_list_add_unpullable_nolock(
						kctx->kbdev, kctx, js);

				if (last_active_kctx[js] && kctx != last_active_kctx[js] &&
				    kbase_js_ctx_pullable(last_active_kctx[js], js, true))
					ctx_waiting[js] = true;

				if (context_idle) {
					kbase_jm_idle_ctx(kbdev, kctx);
					spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
					WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
					kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
					kbase_pm_context_idle(kbdev);
				} else {
					spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
				}
				mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);

				js_mask &= ~(1UL << js);
				break;
			}

			dev_dbg(kbdev->dev, "Push kctx %pK to back of list\n", (void *)kctx);
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |= kbase_js_ctx_list_add_pullable_nolock(kctx->kbdev,
										    kctx, js);
			else
				timer_sync |= kbase_js_ctx_list_add_unpullable_nolock(kctx->kbdev,
										      kctx, js);

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
		}
	}

	if (timer_sync)
		kbase_js_sync_timers(kbdev);

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		if (kbdev->hwaccess.active_kctx[js] == last_active_kctx[js] && ctx_waiting[js]) {
			dev_dbg(kbdev->dev, "Marking kctx %pK as inactive (s:%u)\n",
				(void *)last_active_kctx[js], js);
			kbdev->hwaccess.active_kctx[js] = NULL;
		}
	}

	mutex_unlock(&js_devdata->queue_mutex);
	up(&js_devdata->schedule_sem);
	KBASE_TLSTREAM_TL_JS_SCHED_END(kbdev, 0);
}

void kbase_js_zap_context(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbase_ctx_flag_set(kctx, KCTX_DYING);

	dev_dbg(kbdev->dev, "Zap: Try Evict Ctx %pK", kctx);

	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		unsigned long flags;
		unsigned int js;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
				list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_NON_SCHEDULED, kctx, NULL, 0u,
				    kbase_ctx_flag(kctx, KCTX_SCHEDULED));

		dev_dbg(kbdev->dev, "Zap: Ctx %pK scheduled=0", kctx);

		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, false);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);
	} else {
		unsigned long flags;
		bool was_retained;
		CSTD_UNUSED(was_retained);

		KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u,
				    kbase_ctx_flag(kctx, KCTX_SCHEDULED));
		dev_dbg(kbdev->dev, "Zap: Ctx %pK is in RunPool", kctx);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		was_retained = kbase_ctx_sched_inc_refcount_nolock(kctx);

		KBASE_DEBUG_ASSERT(was_retained);

		dev_dbg(kbdev->dev, "Zap: Ctx %pK Kill Any Running jobs", kctx);

		kbase_backend_jm_kill_running_jobs_from_kctx(kctx);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);

		dev_dbg(kbdev->dev,
			"Zap: Ctx %pK Release (may or may not schedule out immediately)", kctx);

		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

	KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u);

}

static inline int trace_get_refcnt(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	return atomic_read(&kctx->refcount);
}

static void kbase_js_foreach_ctx_job(struct kbase_context *kctx, kbasep_js_ctx_job_cb *callback)
{
	struct kbase_device *kbdev;
	unsigned long flags;
	unsigned int js;

	kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_POLICY_FOREACH_CTX_JOBS, kctx, NULL, 0u,
				     trace_get_refcnt(kbdev, kctx));

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		jsctx_queue_foreach(kctx, js, callback);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

base_jd_prio kbase_js_priority_check(struct kbase_device *kbdev, base_jd_prio priority)
{
	struct priority_control_manager_device *pcm_device = kbdev->pcm_dev;
	int req_priority, out_priority;

	req_priority = kbasep_js_atom_prio_to_sched_prio(priority);
	out_priority = req_priority;

	if (pcm_device && (req_priority != KBASE_JS_ATOM_SCHED_PRIO_INVALID))
		out_priority = pcm_device->ops.pcm_scheduler_priority_check(pcm_device, current,
									    req_priority);
	return kbasep_js_sched_prio_to_atom_prio(kbdev, out_priority);
}
