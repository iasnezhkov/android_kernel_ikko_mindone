/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JS_DEFS_H_
#define _KBASE_JS_DEFS_H_

struct kbase_device;
struct kbase_jd_atom;

typedef u32 kbase_context_flags;

typedef void kbasep_js_ctx_job_cb(struct kbase_device *kbdev, struct kbase_jd_atom *katom);

#define KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ 2

enum kbasep_js_ctx_attr {
	KBASEP_JS_CTX_ATTR_COMPUTE,
	KBASEP_JS_CTX_ATTR_NON_COMPUTE,
	KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES,
	KBASEP_JS_CTX_ATTR_COUNT
};

enum {

	KBASE_JS_ATOM_DONE_START_NEW_ATOMS = (1u << 0),

	KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT = (1u << 1)
};

typedef u32 kbasep_js_atom_done_code;

enum {

	KBASE_JS_SYSTEM_PRIORITY_MODE = 0,

	KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE,

	KBASE_JS_PRIORITY_MODE_COUNT,
};

enum {
	KBASE_JS_ATOM_SCHED_PRIO_FIRST = 0,
	KBASE_JS_ATOM_SCHED_PRIO_REALTIME = KBASE_JS_ATOM_SCHED_PRIO_FIRST,
	KBASE_JS_ATOM_SCHED_PRIO_HIGH,
	KBASE_JS_ATOM_SCHED_PRIO_MED,
	KBASE_JS_ATOM_SCHED_PRIO_LOW,
	KBASE_JS_ATOM_SCHED_PRIO_COUNT,
};

#define KBASE_JS_ATOM_SCHED_PRIO_INVALID -1

#define KBASE_JS_ATOM_SCHED_PRIO_DEFAULT KBASE_JS_ATOM_SCHED_PRIO_MED

typedef u8 kbase_js_prio_bitmap_t;

typedef u32 kbase_atom_ordering_flag_t;

#define KBASE_ATOM_ORDERING_FLAG_SEQNR (((kbase_atom_ordering_flag_t)1) << 0)

struct kbasep_js_device_data {
	struct runpool_irq {
		u16 submit_allowed;
		s8 ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT];
		u64 slot_affinities[BASE_JM_MAX_NR_SLOTS];
		s8 slot_affinity_refcount[BASE_JM_MAX_NR_SLOTS][64];
	} runpool_irq;
	struct semaphore schedule_sem;
	struct list_head ctx_list_pullable[BASE_JM_MAX_NR_SLOTS][KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	struct list_head ctx_list_unpullable[BASE_JM_MAX_NR_SLOTS][KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	s8 nr_user_contexts_running;
	s8 nr_all_contexts_running;
	base_jd_core_req js_reqs[BASE_JM_MAX_NR_SLOTS];

	u32 scheduling_period_ns;
	u32 soft_stop_ticks;
	u32 soft_stop_ticks_cl;
	u32 hard_stop_ticks_ss;
	u32 hard_stop_ticks_cl;
	u32 hard_stop_ticks_dumping;
	u32 gpu_reset_ticks_ss;
	u32 gpu_reset_ticks_cl;
	u32 gpu_reset_ticks_dumping;
	u32 ctx_timeslice_ns;

	struct list_head suspended_soft_jobs_list;

#ifdef CONFIG_MALI_DEBUG
	bool softstop_always;
#endif
	int init_status;
	u32 nr_contexts_pullable;
	atomic_t nr_contexts_runnable;
	atomic_t soft_job_timeout_ms;
	u32 js_free_wait_time_ms;

	struct mutex queue_mutex;

	struct mutex runpool_mutex;

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

	struct hrtimer gpu_metrics_timer;

	bool gpu_metrics_timer_needed;

	bool gpu_metrics_timer_running;
#endif
};

struct kbasep_js_kctx_info {
	struct kbase_jsctx {
		struct mutex jsctx_mutex;

		u32 nr_jobs;
		u32 ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT];
		wait_queue_head_t is_scheduled_wait;
		struct list_head ctx_list_entry[BASE_JM_MAX_NR_SLOTS];
	} ctx;
	int init_status;
};

struct kbasep_js_atom_retained_state {

	enum base_jd_event_code event_code;

	base_jd_core_req core_req;

	int sched_priority;

	u32 device_nr;
};

#define KBASEP_JS_RETRY_SUBMIT_SLOT_INVALID (-1)

#define KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID BASE_JD_REQ_DEP

#define KBASEP_JS_TICK_RESOLUTION_US 1

struct kbase_jsctx_slot_tracking {
	kbase_js_prio_bitmap_t blocked;
	atomic_t atoms_pulled;
	int atoms_pulled_pri[KBASE_JS_ATOM_SCHED_PRIO_COUNT];
};

#endif
