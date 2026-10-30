/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GPU_METRICS_H_
#define _KBASE_GPU_METRICS_H_

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

#include <linux/types.h>

struct kbase_device;
struct kbase_context;
struct kbase_gpu_metrics_ctx;

unsigned long kbase_gpu_metrics_get_tp_emit_interval(void);

void kbase_gpu_metrics_ctx_put(struct kbase_device *kbdev,
			       struct kbase_gpu_metrics_ctx *gpu_metrics_ctx);

struct kbase_gpu_metrics_ctx *kbase_gpu_metrics_ctx_get(struct kbase_device *kbdev, u32 aid);

void kbase_gpu_metrics_ctx_init(struct kbase_device *kbdev,
				struct kbase_gpu_metrics_ctx *gpu_metrics_ctx, u32 aid);

void kbase_gpu_metrics_ctx_start_activity(struct kbase_context *kctx, u64 timestamp_ns);

void kbase_gpu_metrics_ctx_end_activity(struct kbase_context *kctx, u64 timestamp_ns);

void kbase_gpu_metrics_emit_tracepoint(struct kbase_device *kbdev, u64 ts);

int kbase_gpu_metrics_init(struct kbase_device *kbdev);

void kbase_gpu_metrics_term(struct kbase_device *kbdev);

#endif
#endif
