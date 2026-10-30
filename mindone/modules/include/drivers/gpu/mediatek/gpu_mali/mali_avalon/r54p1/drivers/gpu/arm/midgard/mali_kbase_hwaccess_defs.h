/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_DEFS_H_
#define _KBASE_HWACCESS_DEFS_H_

#include <backend/gpu/mali_kbase_defs.h>

struct kbase_hwaccess_data {
#if !MALI_USE_CSF
	struct kbase_context *active_kctx[BASE_JM_MAX_NR_SLOTS];
#endif

	struct kbase_backend_data backend;
};

#endif
