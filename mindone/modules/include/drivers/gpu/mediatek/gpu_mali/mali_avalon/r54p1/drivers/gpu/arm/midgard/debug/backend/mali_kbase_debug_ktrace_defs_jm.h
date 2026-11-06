/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_KTRACE_DEFS_JM_H_
#define _KBASE_DEBUG_KTRACE_DEFS_JM_H_

#if KBASE_KTRACE_TARGET_RBUF

#define KBASE_KTRACE_VERSION_MAJOR 2
#define KBASE_KTRACE_VERSION_MINOR 2
#endif

#define KBASE_KTRACE_FLAG_JM_REFCOUNT (((kbase_ktrace_flag_t)1) << 0)

#define KBASE_KTRACE_FLAG_JM_JOBSLOT (((kbase_ktrace_flag_t)1) << 1)

#define KBASE_KTRACE_FLAG_JM_ATOM (((kbase_ktrace_flag_t)1) << 2)

#if KBASE_KTRACE_TARGET_RBUF

#define KBASE_KTRACE_FLAG_BACKEND_ALL \
	(KBASE_KTRACE_FLAG_JM_REFCOUNT | KBASE_KTRACE_FLAG_JM_JOBSLOT | KBASE_KTRACE_FLAG_JM_ATOM)

union kbase_ktrace_backend {
	struct {

		u64 atom_udata[2];

		u64 gpu_addr;
		unsigned int atom_number;

		kbase_ktrace_code_t code;
		kbase_ktrace_flag_t flags;
		u8 jobslot;
		u8 refcount;
	} gpu;
};
#endif

#endif
