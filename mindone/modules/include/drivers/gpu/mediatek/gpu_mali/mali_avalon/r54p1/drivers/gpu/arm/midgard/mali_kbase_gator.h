/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GATOR_H_
#define _KBASE_GATOR_H_

#include <linux/types.h>

#define GATOR_JOB_SLOT_START 1
#define GATOR_JOB_SLOT_STOP 2
#define GATOR_JOB_SLOT_SOFT_STOPPED 3

#ifdef CONFIG_MALI_GATOR_SUPPORT

#define GATOR_MAKE_EVENT(type, number) (((type) << 24) | ((number) << 16))

struct kbase_context;

void kbase_trace_mali_job_slots_event(u32 dev_id, u32 event, const struct kbase_context *kctx,
				      u8 atom_id);
void kbase_trace_mali_pm_status(u32 dev_id, u32 event, u64 value);
void kbase_trace_mali_page_fault_insert_pages(u32 dev_id, int event, u32 value);
void kbase_trace_mali_total_alloc_pages_change(u32 dev_id, long long event);

#endif

#endif
