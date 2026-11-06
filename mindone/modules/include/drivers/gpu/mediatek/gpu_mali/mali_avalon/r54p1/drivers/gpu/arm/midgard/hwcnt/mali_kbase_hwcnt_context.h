/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_CONTEXT_H_
#define _KBASE_HWCNT_CONTEXT_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct kbase_hwcnt_backend_interface;
struct kbase_hwcnt_context;

int kbase_hwcnt_context_init(const struct kbase_hwcnt_backend_interface *iface,
			     struct kbase_hwcnt_context **out_hctx);

void kbase_hwcnt_context_term(struct kbase_hwcnt_context *hctx);

const struct kbase_hwcnt_metadata *kbase_hwcnt_context_metadata(struct kbase_hwcnt_context *hctx);

void kbase_hwcnt_context_disable(struct kbase_hwcnt_context *hctx);

bool kbase_hwcnt_context_disable_atomic(struct kbase_hwcnt_context *hctx);

void kbase_hwcnt_context_enable(struct kbase_hwcnt_context *hctx);

bool kbase_hwcnt_context_queue_work(struct kbase_hwcnt_context *hctx, struct work_struct *work);

#endif
