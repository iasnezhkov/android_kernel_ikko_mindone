// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>

#ifdef __aarch64__
#include <asm/pointer_auth.h>
#else
#include <asm/unwind.h>
#endif

#include "hang_unwind.h"

#ifdef __aarch64__
/*
 * MINDONE-HANG-STACKFRAME: arch/arm64's struct stackframe (and the raw
 * fp-chain walk around it) is internal to the arm64 unwinder and was
 * reworked for 6.1 -- moved to asm/stacktrace/common.h, losing the
 * out-parameter this code relied on. Re-deriving that layout is the "don't
 * rely on kernel internals" trap module-fix rules warn about -- use the
 * kernel's own stack_trace_save_tsk() (EXPORT_SYMBOL_GPL) instead.
 */
unsigned int hang_kernel_trace(struct task_struct *tsk,
					unsigned long *store, unsigned int size)
{
#ifdef CONFIG_STACKTRACE
	return stack_trace_save_tsk(tsk, store, size, 0);
#else
	return 0;
#endif
}
EXPORT_SYMBOL(hang_kernel_trace);

const char *hang_arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}
#else /* __aarch64__ */
unsigned int hang_kernel_trace(struct task_struct *tsk,
					unsigned long *store, unsigned int size)
{
#if IS_ENABLED(CONFIG_ARM_UNWIND)
	struct stackframe frame;
	unsigned int store_len = 1;

	if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.lr = (unsigned long)__builtin_return_address(0);
		frame.pc = (unsigned long)unwind_backtrace;
	} else {
		/* task blocked in __switch_to */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		/*
		 * The function calling __switch_to cannot be a leaf function
		 * so LR is recovered from the stack.
		 */
		frame.lr = 0;
		frame.pc = thread_saved_pc(tsk);
	}
	*store = frame.pc;
	while (store_len < size) {
		int urc;

		urc = unwind_frame(&frame);
		if (urc < 0)
			break;
		*(++store) = frame.pc;
		store_len += 1;
	}
	return store_len;
#else
	return 0;
#endif
}

#ifdef MODULE
const char *hang_arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}
#else
const char *hang_arch_vma_name(struct vm_area_struct *vma)
{
	return arch_vma_name(vma);
}
#endif

#endif /* __aarch64__ */

EXPORT_SYMBOL(hang_arch_vma_name);

