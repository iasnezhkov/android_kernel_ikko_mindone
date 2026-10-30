/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_KTRACE_JM_H_
#define _KBASE_DEBUG_KTRACE_JM_H_

#if KBASE_KTRACE_TARGET_RBUF

void kbasep_ktrace_add_jm(struct kbase_device *kbdev, enum kbase_ktrace_code code,
			  struct kbase_context *kctx, const struct kbase_jd_atom *katom,
			  u64 gpu_addr, kbase_ktrace_flag_t flags, int refcount,
			  unsigned int jobslot, u64 info_val);

#define KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, gpu_addr, flags, refcount, jobslot, \
				 info_val)                                                     \
	kbasep_ktrace_add_jm(kbdev, KBASE_KTRACE_CODE(code), kctx, katom, gpu_addr, flags,     \
			     refcount, jobslot, info_val)

#else

#define KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, gpu_addr, flags, refcount, jobslot, \
				 info_val)                                                     \
	do {                                                                                   \
		CSTD_UNUSED(kbdev);                                                            \
		CSTD_NOP(code);                                                                \
		CSTD_UNUSED(kctx);                                                             \
		CSTD_UNUSED(katom);                                                            \
		CSTD_UNUSED(gpu_addr);                                                         \
		CSTD_UNUSED(flags);                                                            \
		CSTD_UNUSED(refcount);                                                         \
		CSTD_UNUSED(jobslot);                                                          \
		CSTD_UNUSED(info_val);                                                         \
		CSTD_NOP(0);                                                                   \
	} while (0)
#endif

#if KBASE_KTRACE_TARGET_FTRACE
#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, jobslot) \
	trace_mali_##code(kctx, jobslot, 0)

#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, gpu_addr, jobslot, \
					     info_val)                                    \
	trace_mali_##code(kctx, jobslot, info_val)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, gpu_addr, refcount) \
	trace_mali_##code(kctx, refcount, 0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, gpu_addr, refcount, \
						 info_val)                                     \
	trace_mali_##code(kctx, refcount, info_val)

#define KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, info_val) \
	trace_mali_##code(kctx, gpu_addr, info_val)
#else
#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, jobslot) \
	do {                                                                         \
		CSTD_UNUSED(kbdev);                                                  \
		CSTD_NOP(code);                                                      \
		CSTD_UNUSED(kctx);                                                   \
		CSTD_UNUSED(katom);                                                  \
		CSTD_UNUSED(gpu_addr);                                               \
		CSTD_UNUSED(jobslot);                                                \
		CSTD_NOP(0);                                                         \
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, gpu_addr, jobslot, \
					     info_val)                                    \
	do {                                                                              \
		CSTD_UNUSED(kbdev);                                                       \
		CSTD_NOP(code);                                                           \
		CSTD_UNUSED(kctx);                                                        \
		CSTD_UNUSED(katom);                                                       \
		CSTD_UNUSED(gpu_addr);                                                    \
		CSTD_UNUSED(jobslot);                                                     \
		CSTD_UNUSED(info_val);                                                    \
		CSTD_NOP(0);                                                              \
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, gpu_addr, refcount) \
	do {                                                                              \
		CSTD_UNUSED(kbdev);                                                       \
		CSTD_NOP(code);                                                           \
		CSTD_UNUSED(kctx);                                                        \
		CSTD_UNUSED(katom);                                                       \
		CSTD_UNUSED(gpu_addr);                                                    \
		CSTD_UNUSED(refcount);                                                    \
		CSTD_NOP(0);                                                              \
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, gpu_addr, refcount, \
						 info_val)                                     \
	do {                                                                                   \
		CSTD_UNUSED(kbdev);                                                            \
		CSTD_NOP(code);                                                                \
		CSTD_UNUSED(kctx);                                                             \
		CSTD_UNUSED(katom);                                                            \
		CSTD_UNUSED(gpu_addr);                                                         \
		CSTD_UNUSED(info_val);                                                         \
		CSTD_NOP(0);                                                                   \
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, info_val) \
	do {                                                                     \
		CSTD_UNUSED(kbdev);                                              \
		CSTD_NOP(code);                                                  \
		CSTD_UNUSED(kctx);                                               \
		CSTD_UNUSED(katom);                                              \
		CSTD_UNUSED(gpu_addr);                                           \
		CSTD_UNUSED(info_val);                                           \
		CSTD_NOP(0);                                                     \
	} while (0)
#endif

#define KBASE_KTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, jobslot)                     \
	do {                                                                                      \
		                  \
		u64 __gpu_addr = gpu_addr;                                                        \
		unsigned int __jobslot = jobslot;                                                 \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr,                    \
					 KBASE_KTRACE_FLAG_JM_JOBSLOT, 0, __jobslot, 0);          \
		KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, __gpu_addr, __jobslot); \
	} while (0)

#define KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, gpu_addr, jobslot, info_val)      \
	do {                                                                                      \
		                  \
		u64 __gpu_addr = gpu_addr;                                                        \
		unsigned int __jobslot = jobslot;                                                 \
		u64 __info_val = info_val;                                                        \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr,                    \
					 KBASE_KTRACE_FLAG_JM_JOBSLOT, 0, __jobslot, __info_val); \
		KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, __gpu_addr,        \
						     __jobslot, __info_val);                      \
	} while (0)

#define KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, gpu_addr, refcount)          \
	do {                                                                                \
		            \
		u64 __gpu_addr = gpu_addr;                                                  \
		int __refcount = refcount;                                                  \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr,              \
					 KBASE_KTRACE_FLAG_JM_REFCOUNT, __refcount, 0, 0u); \
		KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, __gpu_addr,   \
						    (unsigned int)__refcount);              \
	} while (0)

#define KBASE_KTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, gpu_addr, refcount, info_val) \
	do {                                                                                      \
		                  \
		u64 __gpu_addr = gpu_addr;                                                        \
		int __refcount = refcount;                                                        \
		u64 __info_val = info_val;                                                        \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr,                    \
					 KBASE_KTRACE_FLAG_JM_REFCOUNT, __refcount, 0,            \
					 __info_val);                                             \
		KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, __gpu_addr,         \
						    (unsigned int)__refcount, __info_val);        \
	} while (0)

#define KBASE_KTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, info_val)                     \
	do {                                                                                  \
		              \
		u64 __gpu_addr = gpu_addr;                                                    \
		u64 __info_val = info_val;                                                    \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, 0u, 0, 0,      \
					 __info_val);                                         \
		KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, __info_val); \
	} while (0)

#endif
