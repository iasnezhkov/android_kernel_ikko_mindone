/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_INSTR_DEFS_H_
#define _KBASE_INSTR_DEFS_H_

#include <hwcnt/mali_kbase_hwcnt_gpu.h>

enum kbase_instr_state {

	KBASE_INSTR_STATE_DISABLED = 0,

	KBASE_INSTR_STATE_IDLE,

	KBASE_INSTR_STATE_DUMPING,

	KBASE_INSTR_STATE_FAULT,

	KBASE_INSTR_STATE_UNRECOVERABLE_ERROR,
};

struct kbase_instr_backend {
	wait_queue_head_t wait;
	int triggered;
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
	enum kbase_hwcnt_physical_set override_counter_set;
#endif

	enum kbase_instr_state state;
};

#endif
