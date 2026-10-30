// SPDX-License-Identifier: GPL-2.0
#include <linux/workqueue.h>
#include "mali_kbase.h"
#include "mali_kbase_sync.h"

#if !MALI_USE_CSF
void kbase_sync_fence_wait_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom;

	katom = container_of(data, struct kbase_jd_atom, work);
	kbase_soft_event_wait_callback(katom);
}
#endif

const char *kbase_sync_status_string(int status)
{
	if (status == 0)
		return "active";
	else if (status > 0)
		return "signaled";
	else
		return "error";
}
