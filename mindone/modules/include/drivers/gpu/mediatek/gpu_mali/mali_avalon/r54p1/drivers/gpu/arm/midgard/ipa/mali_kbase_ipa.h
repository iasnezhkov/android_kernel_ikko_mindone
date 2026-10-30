/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_IPA_H_
#define _KBASE_IPA_H_

#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

struct devfreq;

enum kbase_ipa_block_type {
	KBASE_IPA_BLOCK_TYPE_TOP_LEVEL,
	KBASE_IPA_BLOCK_TYPE_SHADER_CORES,
	KBASE_IPA_BLOCK_TYPE_NUM
};

struct kbase_ipa_model {
	struct kbase_device *kbdev;
	void *model_data;
	const struct kbase_ipa_model_ops *ops;
	struct list_head params;
	bool missing_dt_node_warning;
};

int kbase_ipa_model_add_param_s32(struct kbase_ipa_model *model, const char *name, s32 *addr,
				  size_t num_elems, bool dt_required);

int kbase_ipa_model_add_param_string(struct kbase_ipa_model *model, const char *name, char *addr,
				     size_t size, bool dt_required);

struct kbase_ipa_model_ops {
	char *name;

	int (*init)(struct kbase_ipa_model *model);

	int (*recalculate)(struct kbase_ipa_model *model);
	void (*term)(struct kbase_ipa_model *model);

	int (*get_dynamic_coeff)(struct kbase_ipa_model *model, u32 *coeffp);

	int (*get_static_coeff)(struct kbase_ipa_model *model, u32 *coeffp);

	void (*reset_counter_data)(struct kbase_ipa_model *model);
};

int kbase_ipa_init(struct kbase_device *kbdev);

void kbase_ipa_term(struct kbase_device *kbdev);

int kbase_ipa_model_recalculate(struct kbase_ipa_model *model);

const struct kbase_ipa_model_ops *kbase_ipa_model_ops_find(struct kbase_device *kbdev,
							   const char *name);

const struct kbase_ipa_model_ops *kbase_ipa_counter_model_ops_find(struct kbase_device *kbdev,
								   const char *name);

const char *kbase_ipa_model_name_from_id(struct kbase_gpu_id_props *gpu_id);

const char *kbase_ipa_counter_model_name_from_id(struct kbase_gpu_id_props *gpu_id);

struct kbase_ipa_model *kbase_ipa_init_model(struct kbase_device *kbdev,
					     const struct kbase_ipa_model_ops *ops);

void kbase_ipa_term_model(struct kbase_ipa_model *model);

void kbase_ipa_protection_mode_switch_event(struct kbase_device *kbdev);

int kbase_get_real_power(struct devfreq *df, u32 *power, unsigned long freq, unsigned long voltage);

int kbase_get_real_power_locked(struct kbase_device *kbdev, u32 *power, unsigned long freq,
				unsigned long voltage);

extern struct devfreq_cooling_power kbase_ipa_power_model_ops;

void kbase_ipa_reset_data(struct kbase_device *kbdev);

#else

static inline void kbase_ipa_protection_mode_switch_event(struct kbase_device *kbdev)
{
}

#endif

#endif
