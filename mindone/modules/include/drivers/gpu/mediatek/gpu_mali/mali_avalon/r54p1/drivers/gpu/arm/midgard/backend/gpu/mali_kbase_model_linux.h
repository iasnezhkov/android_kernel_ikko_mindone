/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MODEL_LINUX_H_
#define _KBASE_MODEL_LINUX_H_

#include <backend/gpu/mali_kbase_model_dummy.h>

int kbase_gpu_device_create(struct kbase_device *kbdev);

void kbase_gpu_device_destroy(struct kbase_device *kbdev);

void *midgard_model_create(struct kbase_device *kbdev);

void midgard_model_destroy(void *h);

void midgard_model_write_reg(void *h, u32 addr, u32 value);

void midgard_model_read_reg(void *h, u32 addr, u32 *const value);

void gpu_device_raise_irq(void *model, u32 irq);

void gpu_device_set_data(void *model, void *data);

void *gpu_device_get_data(void *model);

#endif
