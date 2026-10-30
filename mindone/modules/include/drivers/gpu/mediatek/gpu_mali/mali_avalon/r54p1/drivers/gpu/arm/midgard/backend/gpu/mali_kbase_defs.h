/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_GPU_DEFS_H_
#define _KBASE_HWACCESS_GPU_DEFS_H_

#define SLOT_RB_SIZE 2
#define SLOT_RB_MASK (SLOT_RB_SIZE - 1)

struct rb_entry {
	struct kbase_jd_atom *katom;
};

#define SLOT_RB_TAG_PURGED ((u64)(1 << 1))
#define SLOT_RB_NULL_TAG_VAL ((u64)0)

#define SLOT_RB_TAG_KCTX(kctx) ((u64)(uintptr_t)(kctx))

struct slot_rb {
	struct rb_entry entries[SLOT_RB_SIZE];

	u64 last_kctx_tagged;

	u8 read_idx;
	u8 write_idx;

	u8 job_chain_flag;
};

struct kbase_backend_data {
#if !MALI_USE_CSF
	struct slot_rb slot_rb[BASE_JM_MAX_NR_SLOTS];
	struct hrtimer scheduling_timer;

	bool timer_running;
#endif
	bool suspend_timer;

	atomic_t reset_gpu;

#define KBASE_RESET_GPU_NOT_PENDING 0

#define KBASE_RESET_GPU_PREPARED 1

#define KBASE_RESET_GPU_COMMITTED 2

#define KBASE_RESET_GPU_HAPPENING 3

#define KBASE_RESET_GPU_SILENT 4
	struct workqueue_struct *reset_workq;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	struct hrtimer reset_timer;

	bool timeouts_updated;
};

#endif
