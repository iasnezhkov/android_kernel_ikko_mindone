/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_KTRACE_H_
#define _KBASE_DEBUG_KTRACE_H_

#if KBASE_KTRACE_TARGET_FTRACE
#include "mali_linux_trace.h"
#endif

#if MALI_USE_CSF
#include "debug/backend/mali_kbase_debug_ktrace_csf.h"
#else
#include "debug/backend/mali_kbase_debug_ktrace_jm.h"
#endif

int kbase_ktrace_init(struct kbase_device *kbdev);

void kbase_ktrace_term(struct kbase_device *kbdev);

void kbase_ktrace_hook_wrapper(void *param);

#if IS_ENABLED(CONFIG_DEBUG_FS)

void kbase_ktrace_debugfs_init(struct kbase_device *kbdev);
#endif

#if KBASE_KTRACE_TARGET_RBUF

static inline bool kbasep_ktrace_initialized(struct kbase_ktrace *ktrace)
{
	return ktrace->rbuf != NULL;
}

void kbasep_ktrace_add(struct kbase_device *kbdev, enum kbase_ktrace_code code,
		       struct kbase_context *kctx, kbase_ktrace_flag_t flags, u64 info_val);

void kbasep_ktrace_clear(struct kbase_device *kbdev);

void kbasep_ktrace_dump(struct kbase_device *kbdev);

#define KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, info_val) \
	kbasep_ktrace_add(kbdev, KBASE_KTRACE_CODE(code), kctx, 0, info_val)

#define KBASE_KTRACE_RBUF_CLEAR(kbdev) kbasep_ktrace_clear(kbdev)

#define KBASE_KTRACE_RBUF_DUMP(kbdev) kbasep_ktrace_dump(kbdev)

#else

#define KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, info_val) \
	do {                                               \
		CSTD_UNUSED(kbdev);                        \
		CSTD_NOP(code);                            \
		CSTD_UNUSED(kctx);                         \
		CSTD_UNUSED(info_val);                     \
		CSTD_NOP(0);                               \
	} while (0)

#define KBASE_KTRACE_RBUF_CLEAR(kbdev) \
	do {                           \
		CSTD_UNUSED(kbdev);    \
		CSTD_NOP(0);           \
	} while (0)
#define KBASE_KTRACE_RBUF_DUMP(kbdev) \
	do {                          \
		CSTD_UNUSED(kbdev);   \
		CSTD_NOP(0);          \
	} while (0)
#endif

#if KBASE_KTRACE_TARGET_FTRACE

#define KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, info_val) trace_mali_##code(kctx, info_val)

#else
#define KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, info_val) \
	do {                                                 \
		CSTD_UNUSED(kbdev);                          \
		CSTD_NOP(code);                              \
		CSTD_UNUSED(kctx);                           \
		CSTD_UNUSED(info_val);                       \
		CSTD_NOP(0);                                 \
	} while (0)
#endif

#define KBASE_KTRACE_FTRACE_CLEAR(kbdev) \
	do {                             \
		CSTD_UNUSED(kbdev);      \
		CSTD_NOP(0);             \
	} while (0)

#define KBASE_KTRACE_FTRACE_DUMP(kbdev) \
	do {                            \
		CSTD_UNUSED(kbdev);     \
		CSTD_NOP(0);            \
	} while (0)

#define KBASE_KTRACE_ADD(kbdev, code, kctx, info_val)                             \
	do {                                                                      \
		  \
		u64 __info_val = info_val;                                        \
		KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, __info_val);             \
		KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, __info_val);           \
	} while (0)

#define KBASE_KTRACE_CLEAR(kbdev)                 \
	do {                                      \
		KBASE_KTRACE_RBUF_CLEAR(kbdev);   \
		KBASE_KTRACE_FTRACE_CLEAR(kbdev); \
	} while (0)

#define KBASE_KTRACE_DUMP(kbdev)                 \
	do {                                     \
		KBASE_KTRACE_RBUF_DUMP(kbdev);   \
		KBASE_KTRACE_FTRACE_DUMP(kbdev); \
	} while (0)

#endif
