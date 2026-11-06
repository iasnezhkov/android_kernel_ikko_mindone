/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GPUPROPS_H_
#define _KBASE_GPUPROPS_H_

#include <linux/types.h>

struct kbase_device;
struct max_config_props;
struct curr_config_props;
struct kbase_gpu_id_props;

#define KBASE_UBFX32(value, offset, size) \
	(((u32)(value) >> (u32)(offset)) & (u32)((1ULL << (u32)(size)) - 1))

#define KBASE_UBFX64(value, offset, size) \
	(((u64)(value) >> (u32)(offset)) & (u64)((1ULL << (u32)(size)) - 1))

void kbase_gpuprops_update_composite_ids(struct kbase_gpu_id_props *props);

void kbase_gpuprops_parse_gpu_id(struct kbase_gpu_id_props *props, u64 gpu_id);

int kbase_gpuprops_init(struct kbase_device *kbdev);

void kbase_gpuprops_term(struct kbase_device *kbdev);

int kbase_gpuprops_update_l2_features(struct kbase_device *kbdev);

int kbase_gpuprops_populate_user_buffer(struct kbase_device *kbdev);

void kbase_gpuprops_free_user_buffer(struct kbase_device *kbdev);

int kbase_device_populate_max_freq(struct kbase_device *kbdev);

void kbase_gpuprops_set_max_config(struct kbase_device *kbdev,
				   const struct max_config_props *max_config);

int kbase_gpuprops_get_curr_config_props(struct kbase_device *kbdev,
					 struct curr_config_props *const curr_config);

int kbase_gpuprops_req_curr_config_update(struct kbase_device *kbdev);

#endif
