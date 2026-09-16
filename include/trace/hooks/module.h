/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM module

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MODULE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MODULE_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct module;
DECLARE_RESTRICTED_HOOK(android_rvh_set_module_permit_before_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_module_permit_after_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_module_core_rw_nx,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_module_init_rw_nx,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod), 1);

/* MINDONE: MTK vendor-hook port (2026-08-20, android_vh cluster).
 * Unrestricted (multi-listener) counterparts
 * of the android_rvh_set_module_permit_* hooks above, required by the
 * out-of-tree mkp module. Ported from a MediaTek MT8781 reference tree. */
DECLARE_HOOK(android_vh_set_module_permit_before_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod));

DECLARE_HOOK(android_vh_set_module_permit_after_init,
	TP_PROTO(const struct module *mod),
	TP_ARGS(mod));

#endif /* _TRACE_HOOK_MODULE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
