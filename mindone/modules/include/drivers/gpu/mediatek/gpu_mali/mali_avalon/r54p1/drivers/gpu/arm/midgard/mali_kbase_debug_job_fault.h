/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUG_JOB_FAULT_H
#define _KBASE_DEBUG_JOB_FAULT_H

#include <linux/types.h>

struct kbase_context;
struct kbase_device;

#define REGISTER_DUMP_TERMINATION_FLAG 0xFFFFFFFF

int kbase_debug_job_fault_dev_init(struct kbase_device *kbdev);

void kbase_debug_job_fault_debugfs_init(struct kbase_device *kbdev);

void kbase_debug_job_fault_dev_term(struct kbase_device *kbdev);

int kbase_debug_job_fault_context_init(struct kbase_context *kctx);

void kbase_debug_job_fault_context_term(struct kbase_context *kctx);

void kbase_debug_job_fault_kctx_unblock(struct kbase_context *kctx);

bool kbase_debug_job_fault_process(struct kbase_jd_atom *katom, u32 completion_code);

bool kbase_debug_job_fault_reg_snapshot_init(struct kbase_context *kctx, int reg_range);

bool kbase_job_fault_get_reg_snapshot(struct kbase_context *kctx);

#endif
