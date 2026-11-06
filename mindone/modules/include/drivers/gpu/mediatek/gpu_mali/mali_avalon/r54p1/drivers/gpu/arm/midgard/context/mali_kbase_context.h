/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CONTEXT_H_
#define _KBASE_CONTEXT_H_

#include <linux/atomic.h>

void kbase_context_debugfs_init(struct kbase_context *const kctx);

void kbase_context_debugfs_term(struct kbase_context *const kctx);

struct kbase_context *kbase_create_context(struct kbase_device *kbdev, bool is_compat,
					   base_context_create_flags const flags,
					   unsigned long api_version, struct file *filp);

void kbase_destroy_context(struct kbase_context *kctx);

static inline bool kbase_ctx_flag(struct kbase_context *kctx, enum kbase_context_flags flag)
{
	return atomic_read(&kctx->flags) & (int)flag;
}

bool kbase_ctx_compat_mode(struct kbase_context *kctx);

static inline void kbase_ctx_flag_clear(struct kbase_context *kctx, enum kbase_context_flags flag)
{
	atomic_andnot(flag, &kctx->flags);
}

static inline void kbase_ctx_flag_set(struct kbase_context *kctx, enum kbase_context_flags flag)
{
	atomic_or(flag, &kctx->flags);
}
#endif
