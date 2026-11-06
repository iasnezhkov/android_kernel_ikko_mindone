/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_KTRACE_DEFS_H_
#define _KBASE_DEBUG_KTRACE_DEFS_H_

#if defined(CONFIG_MALI_MIDGARD_ENABLE_TRACE) || defined(CONFIG_MALI_SYSTEM_TRACE)
#define KBASE_KTRACE_ENABLE 1
#endif

#ifndef KBASE_KTRACE_ENABLE
#ifdef CONFIG_MALI_DEBUG
#define KBASE_KTRACE_ENABLE 1
#else
#define KBASE_KTRACE_ENABLE 0
#endif
#endif

#if KBASE_KTRACE_ENABLE

#ifdef CONFIG_MALI_SYSTEM_TRACE
#define KBASE_KTRACE_TARGET_FTRACE 1
#else
#define KBASE_KTRACE_TARGET_FTRACE 0
#endif

#ifdef CONFIG_MALI_MIDGARD_ENABLE_TRACE
#define KBASE_KTRACE_TARGET_RBUF 1
#else
#define KBASE_KTRACE_TARGET_RBUF 0
#endif

#else
#define KBASE_KTRACE_TARGET_FTRACE 0
#define KBASE_KTRACE_TARGET_RBUF 0
#endif

typedef u8 kbase_ktrace_flag_t;

#if KBASE_KTRACE_TARGET_RBUF
typedef u8 kbase_ktrace_code_t;

union kbase_ktrace_backend;

#endif

#if MALI_USE_CSF
#include "debug/backend/mali_kbase_debug_ktrace_defs_csf.h"
#else
#include "debug/backend/mali_kbase_debug_ktrace_defs_jm.h"
#endif

#if KBASE_KTRACE_TARGET_RBUF

#define KBASE_KTRACE_FLAG_BACKEND (((kbase_ktrace_flag_t)1) << 7)

#define KBASE_KTRACE_FLAG_COMMON_ALL (KBASE_KTRACE_FLAG_BACKEND)

#define KBASE_KTRACE_FLAG_ALL (KBASE_KTRACE_FLAG_COMMON_ALL | KBASE_KTRACE_FLAG_BACKEND_ALL)

#define KBASE_KTRACE_SHIFT (9)
#define KBASE_KTRACE_SIZE (1 << KBASE_KTRACE_SHIFT)
#define KBASE_KTRACE_MASK ((1 << KBASE_KTRACE_SHIFT) - 1)

#define KBASE_KTRACE_CODE(X) KBASE_KTRACE_CODE_##X

enum kbase_ktrace_code {

#define KBASE_KTRACE_CODE_MAKE_CODE(X) KBASE_KTRACE_CODE(X)
#include <debug/mali_kbase_debug_ktrace_codes.h>
#undef KBASE_KTRACE_CODE_MAKE_CODE

	,

	KBASE_KTRACE_CODE_COUNT
};

struct kbase_ktrace_msg {
	struct timespec64 timestamp;
	u32 thread_id;
	u32 cpu;
	pid_t kctx_tgid;
	u32 kctx_id;
	u64 info_val;
	union kbase_ktrace_backend backend;
};

struct kbase_ktrace {
	spinlock_t lock;
	u16 first_out;
	u16 next_in;
	struct kbase_ktrace_msg *rbuf;
};

static inline void kbase_ktrace_compiletime_asserts(void)
{

	compiletime_assert(sizeof(kbase_ktrace_code_t) == sizeof(unsigned long long) ||
				   KBASE_KTRACE_CODE_COUNT <=
					   (1ull << (sizeof(kbase_ktrace_code_t) * BITS_PER_BYTE)),
			   "kbase_ktrace_code_t not wide enough for KBASE_KTRACE_CODE_COUNT");
	compiletime_assert((KBASE_KTRACE_FLAG_BACKEND_ALL & KBASE_KTRACE_FLAG_COMMON_ALL) == 0,
			   "KTrace backend flags intersect with KTrace common flags");
}

#endif
#endif
