/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_KTRACE_INTERNAL_H_
#define _KBASE_DEBUG_KTRACE_INTERNAL_H_

#if KBASE_KTRACE_TARGET_RBUF

#define KTRACE_DUMP_MESSAGE_SIZE 256

void kbasep_ktrace_backend_format_header(char *buffer, int sz, s32 *written);

void kbasep_ktrace_backend_format_msg(struct kbase_ktrace_msg *trace_msg, char *buffer, int sz,
				      s32 *written);

struct kbase_ktrace_msg *kbasep_ktrace_reserve(struct kbase_ktrace *ktrace);

void kbasep_ktrace_msg_init(struct kbase_ktrace *ktrace, struct kbase_ktrace_msg *trace_msg,
			    enum kbase_ktrace_code code, struct kbase_context *kctx,
			    kbase_ktrace_flag_t flags, u64 info_val);

#endif

#endif
