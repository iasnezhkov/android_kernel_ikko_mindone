/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CTX_SCHED_H_
#define _KBASE_CTX_SCHED_H_

#include <linux/types.h>

struct kbase_context;
struct kbase_device;

int kbase_ctx_sched_init(struct kbase_device *kbdev);

void kbase_ctx_sched_term(struct kbase_device *kbdev);

int kbase_ctx_sched_init_ctx(struct kbase_context *kctx);

int kbase_ctx_sched_retain_ctx(struct kbase_context *kctx);

void kbase_ctx_sched_retain_ctx_refcount(struct kbase_context *kctx);

void kbase_ctx_sched_release_ctx(struct kbase_context *kctx);

void kbase_ctx_sched_remove_ctx(struct kbase_context *kctx);

void kbase_ctx_sched_remove_ctx_nolock(struct kbase_context *kctx);

void kbase_ctx_sched_restore_all_as(struct kbase_device *kbdev);

struct kbase_context *kbase_ctx_sched_as_to_ctx_refcount(struct kbase_device *kbdev, size_t as_nr);

struct kbase_context *kbase_ctx_sched_as_to_ctx(struct kbase_device *kbdev, size_t as_nr);

struct kbase_context *kbase_ctx_sched_as_to_ctx_nolock(struct kbase_device *kbdev, size_t as_nr);

bool kbase_ctx_sched_inc_refcount_nolock(struct kbase_context *kctx);

bool kbase_ctx_sched_inc_refcount(struct kbase_context *kctx);

void kbase_ctx_sched_release_ctx_lock(struct kbase_context *kctx);

#if MALI_USE_CSF

bool kbase_ctx_sched_inc_refcount_if_as_valid(struct kbase_context *kctx);
#endif

#endif
