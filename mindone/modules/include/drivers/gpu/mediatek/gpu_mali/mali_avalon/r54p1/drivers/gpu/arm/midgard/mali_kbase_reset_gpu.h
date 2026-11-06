/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_RESET_GPU_H_
#define _KBASE_RESET_GPU_H_

struct kbase_device;

int kbase_reset_gpu_prevent_and_wait(struct kbase_device *kbdev);

int kbase_reset_gpu_try_prevent(struct kbase_device *kbdev);

void kbase_reset_gpu_allow(struct kbase_device *kbdev);

void kbase_reset_gpu_assert_prevented(struct kbase_device *kbdev);

void kbase_reset_gpu_assert_failed_or_prevented(struct kbase_device *kbdev);

#define RESET_FLAGS_NONE (0U)

#define RESET_FLAGS_HWC_UNRECOVERABLE_ERROR ((unsigned int)(1 << 0))

bool kbase_prepare_to_reset_gpu_locked(struct kbase_device *kbdev, unsigned int flags);

bool kbase_prepare_to_reset_gpu(struct kbase_device *kbdev, unsigned int flags);

void kbase_reset_gpu(struct kbase_device *kbdev);

void kbase_reset_gpu_locked(struct kbase_device *kbdev);

int kbase_reset_gpu_silent(struct kbase_device *kbdev);

bool kbase_reset_gpu_is_active(struct kbase_device *kbdev);

bool kbase_reset_gpu_is_not_pending(struct kbase_device *kbdev);

int kbase_reset_gpu_wait(struct kbase_device *kbdev);

int kbase_reset_gpu_init(struct kbase_device *kbdev);

void kbase_reset_gpu_term(struct kbase_device *kbdev);

#endif
