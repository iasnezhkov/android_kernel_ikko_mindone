/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM creds

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CREDS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CREDS_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct cred;
struct task_struct;
DECLARE_RESTRICTED_HOOK(android_rvh_commit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_exit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *cred),
	TP_ARGS(task, cred), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_override_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_revert_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *old),
	TP_ARGS(task, old), 1);

/* MINDONE: MTK vendor-hook port (2026-08-20, android_vh cluster).
 * Unrestricted (multi-listener) counterparts
 * of the android_rvh_* hooks above, required by the out-of-tree mkp
 * module. Ported from a MediaTek MT8781 reference tree. */
DECLARE_HOOK(android_vh_commit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new));

DECLARE_HOOK(android_vh_exit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *cred),
	TP_ARGS(task, cred));

DECLARE_HOOK(android_vh_override_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new));

DECLARE_HOOK(android_vh_revert_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *old),
	TP_ARGS(task, old));

#endif /* _TRACE_HOOK_CREDS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
