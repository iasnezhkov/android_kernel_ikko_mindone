/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_IPA_COUNTER_COMMON_JM_H_
#define _KBASE_IPA_COUNTER_COMMON_JM_H_

#include "mali_kbase.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "hwcnt/mali_kbase_hwcnt_types.h"

#define KBASE_IPA_MAX_GROUP_DEF_NUM 16

#define KBASE_IPA_NR_BYTES_PER_CNT (sizeof(u64))

#define KBASE_IPA_NR_CNT_PER_BLOCK 64

#define KBASE_IPA_NR_BYTES_PER_BLOCK (KBASE_IPA_NR_CNT_PER_BLOCK * KBASE_IPA_NR_BYTES_PER_CNT)

struct kbase_ipa_model_vinstr_data;

typedef u32 kbase_ipa_get_active_cycles_callback(struct kbase_ipa_model_vinstr_data *);

struct kbase_ipa_model_vinstr_data {
	struct kbase_device *kbdev;
	s32 group_values[KBASE_IPA_MAX_GROUP_DEF_NUM];
	const struct kbase_ipa_group *groups_def;
	size_t groups_def_num;
	kbase_ipa_get_active_cycles_callback *get_active_cycles;
	struct kbase_hwcnt_virtualizer_client *hvirt_cli;
	struct kbase_hwcnt_dump_buffer dump_buf;
	s32 reference_voltage;
	s32 scaling_factor;
	s32 min_sample_cycles;
	u64 total_active_cycles;
};

struct kbase_ipa_group {
	const char *name;
	s32 default_value;
	s64 (*op)(struct kbase_ipa_model_vinstr_data *model_data, s32 coeff,
		  u32 counter_block_offset);
	u32 counter_block_offset;
};

s64 kbase_ipa_sum_all_shader_cores(struct kbase_ipa_model_vinstr_data *model_data, s32 coeff,
				   u32 counter);

s64 kbase_ipa_sum_all_memsys_blocks(struct kbase_ipa_model_vinstr_data *model_data, s32 coeff,
				    u32 counter);

s64 kbase_ipa_single_counter(struct kbase_ipa_model_vinstr_data *model_data, s32 coeff,
			     u32 counter);

int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp);

void kbase_ipa_vinstr_reset_data(struct kbase_ipa_model *model);

int kbase_ipa_vinstr_common_model_init(struct kbase_ipa_model *model,
				       const struct kbase_ipa_group *ipa_groups_def,
				       size_t ipa_group_size,
				       kbase_ipa_get_active_cycles_callback *get_active_cycles,
				       s32 reference_voltage);

void kbase_ipa_vinstr_common_model_term(struct kbase_ipa_model *model);

#endif
