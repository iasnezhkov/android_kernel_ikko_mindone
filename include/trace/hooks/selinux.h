/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM selinux

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SELINUX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SELINUX_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct selinux_state;
DECLARE_RESTRICTED_HOOK(android_rvh_selinux_is_initialized,
	TP_PROTO(const struct selinux_state *state),
	TP_ARGS(state), 1);

/* MINDONE: MTK vendor-hook port (2026-08-20, android_vh cluster).
 * Unrestricted counterpart of
 * android_rvh_selinux_is_initialized, required by the out-of-tree mkp
 * module. DECLARE_HOOK ported from a MediaTek reference tree, which does
 * not itself export it; the export below is added mechanically, matching
 * its android_rvh_selinux_is_initialized sibling. */
DECLARE_HOOK(android_vh_selinux_is_initialized,
	TP_PROTO(const struct selinux_state *state),
	TP_ARGS(state));

#endif /* _TRACE_HOOK_SELINUX_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
