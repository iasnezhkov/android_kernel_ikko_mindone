/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_FENCE_H_
#define _KBASE_FENCE_H_

#if IS_ENABLED(CONFIG_SYNC_FILE)

#include "mali_kbase.h"

#include <linux/list.h>
#include <linux/version_compat_defs.h>

#if MALI_USE_CSF

#define MAX_DIGITS_FOR_UNSIGNED_TYPE(t) ((((sizeof(t) * BITS_PER_BYTE) * 1233) >> 12) + 1)

#define MAX_DIGITS_FOR_SIGNED_TYPE(t) (MAX_DIGITS_FOR_UNSIGNED_TYPE(t) + 1)

#define MAX_KBDEV_ID_LEN MAX_DIGITS_FOR_UNSIGNED_TYPE(u32)

#define MAX_KCTX_TGID_LEN MAX_DIGITS_FOR_SIGNED_TYPE(pid_t)

#define MAX_KCTX_ID_LEN MAX_DIGITS_FOR_UNSIGNED_TYPE(u32)

#define MAX_KCTX_QUEUE_FENCE_CTX_LEN MAX_DIGITS_FOR_UNSIGNED_TYPE(u64)

#define FIXED_FORMAT_LEN (9)

#define MAX_TIMELINE_NAME                                                                        \
	(MAX_KBDEV_ID_LEN + MAX_KCTX_TGID_LEN + MAX_KCTX_ID_LEN + MAX_KCTX_QUEUE_FENCE_CTX_LEN + \
	 FIXED_FORMAT_LEN)

struct kbase_kcpu_dma_fence_meta {
	kbase_refcount_t refcount;
	struct kbase_device *kbdev;
	u32 kctx_id;
	char timeline_name[MAX_TIMELINE_NAME];
};

struct kbase_kcpu_dma_fence {
	struct dma_fence base;
	struct kbase_kcpu_dma_fence_meta *metadata;
};
#endif

extern const struct dma_fence_ops kbase_fence_ops;

struct dma_fence *kbase_fence_out_new(struct kbase_jd_atom *katom);

#if IS_ENABLED(CONFIG_SYNC_FILE)

#define kbase_fence_fence_in_set(katom, fence)        \
	do {                                          \
		WARN_ON((katom)->dma_fence.fence_in); \
		(katom)->dma_fence.fence_in = fence;  \
	} while (0)
#endif

#if !MALI_USE_CSF

static inline void kbase_fence_out_remove(struct kbase_jd_atom *katom)
{
	if (katom->dma_fence.fence) {
		dma_fence_put(katom->dma_fence.fence);
		katom->dma_fence.fence = NULL;
	}
}

#if IS_ENABLED(CONFIG_SYNC_FILE)

static inline void kbase_fence_in_remove(struct kbase_jd_atom *katom)
{
	if (katom->dma_fence.fence_in) {
		dma_fence_put(katom->dma_fence.fence_in);
		katom->dma_fence.fence_in = NULL;
	}
}
#endif

static inline bool kbase_fence_out_is_ours(struct kbase_jd_atom *katom)
{
	return katom->dma_fence.fence && katom->dma_fence.fence->ops == &kbase_fence_ops;
}

static inline int kbase_fence_out_signal(struct kbase_jd_atom *katom, int status)
{
	if (status)
		dma_fence_set_error_helper(katom->dma_fence.fence, status);
	return dma_fence_signal(katom->dma_fence.fence);
}

#if IS_ENABLED(CONFIG_SYNC_FILE)

#define kbase_fence_in_get(katom) dma_fence_get((katom)->dma_fence.fence_in)
#endif

#define kbase_fence_out_get(katom) dma_fence_get((katom)->dma_fence.fence)

#endif

#define kbase_fence_get(fence_info) dma_fence_get((fence_info)->fence)

#if MALI_USE_CSF
static inline struct kbase_kcpu_dma_fence *kbase_kcpu_dma_fence_get(struct dma_fence *fence)
{
	if (fence->ops == &kbase_fence_ops)
		return (struct kbase_kcpu_dma_fence *)fence;

	return NULL;
}

static inline void kbase_kcpu_dma_fence_meta_put(struct kbase_kcpu_dma_fence_meta *metadata)
{
	if (kbase_refcount_dec_and_test(&metadata->refcount)) {
		atomic_dec(&metadata->kbdev->live_fence_metadata);
		kfree(metadata);
	}
}

static inline void kbase_kcpu_dma_fence_put(struct dma_fence *fence)
{
	struct kbase_kcpu_dma_fence *kcpu_fence = kbase_kcpu_dma_fence_get(fence);

	if (kcpu_fence)
		kbase_kcpu_dma_fence_meta_put(kcpu_fence->metadata);
}
#endif

static inline void kbase_fence_put(struct dma_fence *fence)
{
	dma_fence_put(fence);
}

#endif

#endif
