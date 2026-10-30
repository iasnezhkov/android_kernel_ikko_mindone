/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JM_DEFS_H_
#define _KBASE_JM_DEFS_H_

#include "mali_kbase_js_defs.h"

#include <linux/version_compat_defs.h>

#define KBASE_KTRACE_DUMP_ON_JOB_SLOT_ERROR 1

#define ZAP_TIMEOUT 1000

#define KBASE_DISABLE_SCHEDULING_SOFT_STOPS 0

#define KBASE_DISABLE_SCHEDULING_HARD_STOPS 0

#define KBASE_KATOM_FLAG_BEEN_SOFT_STOPPED (1U << 1)

#define KBASE_KATOM_FLAGS_RERUN (1U << 2)

#define KBASE_KATOM_FLAGS_JOBCHAIN (1U << 3)

#define KBASE_KATOM_FLAG_BEEN_HARD_STOPPED (1U << 4)

#define KBASE_KATOM_FLAG_IN_DISJOINT (1U << 5)

#define KBASE_KATOM_FLAG_X_DEP_BLOCKED (1U << 7)

#define KBASE_KATOM_FLAG_FAIL_BLOCKER (1U << 8)

#define KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST (1U << 9)

#define KBASE_KATOM_FLAG_PROTECTED (1U << 11)

#define KBASE_KATOM_FLAG_JSCTX_IN_TREE (1U << 12)

#define KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT (1U << 13)

#define JS_COMMAND_SW_CAUSES_DISJOINT 0x100

#define JS_COMMAND_SW_BITS (JS_COMMAND_SW_CAUSES_DISJOINT)

#if (JS_COMMAND_SW_BITS & JS_COMMAND_MASK)
#error "JS_COMMAND_SW_BITS not masked off by JS_COMMAND_MASK." \
	"Must update JS_COMMAND_SW_<..> bitmasks"
#endif

#define JS_COMMAND_SOFT_STOP_WITH_SW_DISJOINT (JS_COMMAND_SW_CAUSES_DISJOINT | JS_COMMAND_SOFT_STOP)

#define KBASEP_ATOM_ID_INVALID BASE_JD_ATOM_COUNT

#define KBASE_SERIALIZE_INTRA_SLOT (1 << 0)

#define KBASE_SERIALIZE_INTER_SLOT (1 << 1)

#define KBASE_SERIALIZE_RESET (1 << 2)

enum kbase_timeout_selector {
	MMU_AS_INACTIVE_WAIT_TIMEOUT,
	JM_DEFAULT_JS_FREE_TIMEOUT,
	KBASE_PRFCNT_ACTIVE_TIMEOUT,
	KBASE_CLEAN_CACHE_TIMEOUT,
	KBASE_AS_INACTIVE_TIMEOUT,

	KBASE_TIMEOUT_SELECTOR_COUNT,
	KBASE_DEFAULT_TIMEOUT = JM_DEFAULT_JS_FREE_TIMEOUT
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

struct base_job_fault_event {
	u32 event_code;
	struct kbase_jd_atom *katom;
	struct work_struct job_fault_work;
	struct list_head head;
	int reg_offset;
};
#endif

struct kbase_jd_atom_dependency {
	struct kbase_jd_atom *atom;
	u8 dep_type;
};

static inline const struct kbase_jd_atom *
kbase_jd_katom_dep_atom(const struct kbase_jd_atom_dependency *dep)
{
	return (const struct kbase_jd_atom *)(dep->atom);
}

static inline u8 kbase_jd_katom_dep_type(const struct kbase_jd_atom_dependency *dep)
{
	return dep->dep_type;
}

static inline void kbase_jd_katom_dep_set(const struct kbase_jd_atom_dependency *const_dep,
					  struct kbase_jd_atom *a, u8 type)
{
	struct kbase_jd_atom_dependency *dep;

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = a;
	dep->dep_type = type;
}

static inline void kbase_jd_katom_dep_clear(const struct kbase_jd_atom_dependency *const_dep)
{
	struct kbase_jd_atom_dependency *dep;

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = NULL;
	dep->dep_type = BASE_JD_DEP_TYPE_INVALID;
}

enum kbase_atom_gpu_rb_state {
	KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB,
	KBASE_ATOM_GPU_RB_WAITING_BLOCKED,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION,
	KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE,
	KBASE_ATOM_GPU_RB_READY,
	KBASE_ATOM_GPU_RB_SUBMITTED,
	KBASE_ATOM_GPU_RB_RETURN_TO_JS = -1
};

enum kbase_atom_enter_protected_state {

	KBASE_ATOM_ENTER_PROTECTED_CHECK = 0,
	KBASE_ATOM_ENTER_PROTECTED_HWCNT,
	KBASE_ATOM_ENTER_PROTECTED_IDLE_L2,
	KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY,
	KBASE_ATOM_ENTER_PROTECTED_FINISHED,
};

enum kbase_atom_exit_protected_state {

	KBASE_ATOM_EXIT_PROTECTED_CHECK = 0,
	KBASE_ATOM_EXIT_PROTECTED_IDLE_L2,
	KBASE_ATOM_EXIT_PROTECTED_RESET,
	KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT,
};

struct kbase_jd_atom {
	struct work_struct work;
	ktime_t start_timestamp;

	struct base_jd_udata udata;
	struct kbase_context *kctx;

	struct list_head dep_head[2];
	struct list_head dep_item[2];
	const struct kbase_jd_atom_dependency dep[2];
	struct list_head jd_item;
	bool in_jd_list;

#if MALI_JIT_PRESSURE_LIMIT_BASE
	u8 jit_ids[2];
#endif

	u16 nr_extres;
	struct kbase_va_region **extres;

	u32 device_nr;
	u64 jc;
	void *softjob_data;
#if IS_ENABLED(CONFIG_SYNC_FILE)
	struct {

		struct dma_fence *fence_in;

		struct dma_fence *fence;

		struct dma_fence_cb fence_cb;
		bool fence_cb_added;

		unsigned int context;
		atomic_t seqno;
	} dma_fence;
#endif

	enum base_jd_event_code event_code;
	base_jd_core_req core_req;
	u8 jobslot;

	u32 ticks;
	int sched_priority;

	wait_queue_head_t completed;
	enum kbase_jd_atom_state status;
#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS) || IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
	int work_id;
#endif
	unsigned int slot_nr;

	u32 atom_flags;

	enum kbase_atom_gpu_rb_state gpu_rb_state;

	bool need_cache_flush_cores_retained;

	atomic_t blocked;

	u64 seq_nr;

	struct kbase_jd_atom *pre_dep;
	struct kbase_jd_atom *post_dep;

	struct kbase_jd_atom *x_pre_dep;
	struct kbase_jd_atom *x_post_dep;

	u32 flush_id;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct base_job_fault_event fault_event;
#endif
	struct list_head queue;

	struct list_head jit_node;
	bool jit_blocked;

	enum base_jd_event_code will_fail_event_code;

	union {
		enum kbase_atom_enter_protected_state enter;
		enum kbase_atom_exit_protected_state exit;
	} protected_state;

	struct rb_node runnable_tree_node;

	u32 age;
#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)

	u32 frame_nr;
#endif
};

static inline bool kbase_jd_katom_is_protected(const struct kbase_jd_atom *katom)
{
	return (bool)(katom->atom_flags & KBASE_KATOM_FLAG_PROTECTED);
}

static inline bool kbase_jd_atom_is_younger(const struct kbase_jd_atom *katom_a,
					    const struct kbase_jd_atom *katom_b)
{
	return ((s32)(katom_a->age - katom_b->age) < 0);
}

static inline bool kbase_jd_atom_is_earlier(const struct kbase_jd_atom *katom_a,
					    const struct kbase_jd_atom *katom_b)
{

	if (!katom_a->seq_nr || !katom_b->seq_nr)
		return false;

	return (s64)(katom_a->seq_nr - katom_b->seq_nr) < 0;
}

#define KBASE_JD_DEP_QUEUE_SIZE 256

struct kbase_jd_context {
	struct mutex lock;
	struct kbasep_js_kctx_info sched_info;
	struct kbase_jd_atom atoms[BASE_JD_ATOM_COUNT];
	struct workqueue_struct *job_done_wq;

	wait_queue_head_t zero_jobs_wait;
	spinlock_t tb_lock;
	u32 *tb;
	u32 job_nr;
	size_t tb_wrap_offset;

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS) || IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
	atomic_t work_id;
#endif

	struct list_head jit_atoms_head;
	struct list_head jit_pending_alloc;
	int max_priority;
};

struct jsctx_queue {
	struct rb_root runnable_tree;
	struct list_head x_dep_head;
};

struct kbase_as {
	unsigned int number;
	struct workqueue_struct *pf_wq;
	struct work_struct work_pagefault;
	struct work_struct work_busfault;
	struct kbase_fault pf_data;
	struct kbase_fault bf_data;
	struct kbase_mmu_setup current_setup;
};

#endif
