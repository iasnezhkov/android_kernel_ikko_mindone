// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include "debug/mali_kbase_debug_ktrace_internal.h"
#include "debug/backend/mali_kbase_debug_ktrace_jm.h"

#if KBASE_KTRACE_TARGET_RBUF

void kbasep_ktrace_backend_format_header(char *buffer, int sz, s32 *written)
{
	*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0),
				  "katom,gpu_addr,jobslot,refcount"),
			0);
}

void kbasep_ktrace_backend_format_msg(struct kbase_ktrace_msg *trace_msg, char *buffer, int sz,
				      s32 *written)
{

	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_ATOM)
		*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0),
					  "atom %u (ud: 0x%llx 0x%llx)",
					  trace_msg->backend.gpu.atom_number,
					  trace_msg->backend.gpu.atom_udata[0],
					  trace_msg->backend.gpu.atom_udata[1]),
				0);

	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_BACKEND)
		*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0),
					  ",%.8llx,", trace_msg->backend.gpu.gpu_addr),
				0);
	else
		*written +=
			MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0), ",,"), 0);

	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_JOBSLOT)
		*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0), "%d",
					  trace_msg->backend.gpu.jobslot),
				0);

	*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0), ","), 0);

	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_REFCOUNT)
		*written += MAX(scnprintf(buffer + *written, (size_t)MAX(sz - *written, 0), "%d",
					  trace_msg->backend.gpu.refcount),
				0);
}

void kbasep_ktrace_add_jm(struct kbase_device *kbdev, enum kbase_ktrace_code code,
			  struct kbase_context *kctx, const struct kbase_jd_atom *katom,
			  u64 gpu_addr, kbase_ktrace_flag_t flags, int refcount,
			  unsigned int jobslot, u64 info_val)
{
	unsigned long irqflags;
	struct kbase_ktrace_msg *trace_msg;

	if (unlikely(!kbasep_ktrace_initialized(&kbdev->ktrace)))
		return;

	spin_lock_irqsave(&kbdev->ktrace.lock, irqflags);

	trace_msg = kbasep_ktrace_reserve(&kbdev->ktrace);

	kbasep_ktrace_msg_init(&kbdev->ktrace, trace_msg, code, kctx, flags, info_val);

	trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_BACKEND;

	if (katom) {
		trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_JM_ATOM;

		trace_msg->backend.gpu.atom_number = kbase_jd_atom_id(katom->kctx, katom);
		trace_msg->backend.gpu.atom_udata[0] = katom->udata.blob[0];
		trace_msg->backend.gpu.atom_udata[1] = katom->udata.blob[1];
	}

	trace_msg->backend.gpu.gpu_addr = gpu_addr;
	trace_msg->backend.gpu.jobslot = jobslot;

	trace_msg->backend.gpu.refcount = MIN((unsigned int)refcount, 0xFF);

	WARN_ON((trace_msg->backend.gpu.flags & ~KBASE_KTRACE_FLAG_ALL));

	spin_unlock_irqrestore(&kbdev->ktrace.lock, irqflags);
}

#endif
