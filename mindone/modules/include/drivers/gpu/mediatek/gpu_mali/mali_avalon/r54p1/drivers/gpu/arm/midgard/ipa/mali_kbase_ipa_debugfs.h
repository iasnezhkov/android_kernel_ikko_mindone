/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_IPA_DEBUGFS_H_
#define _KBASE_IPA_DEBUGFS_H_

enum kbase_ipa_model_param_type {
	PARAM_TYPE_S32 = 1,
	PARAM_TYPE_STRING,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

void kbase_ipa_debugfs_init(struct kbase_device *kbdev);
int kbase_ipa_model_param_add(struct kbase_ipa_model *model, const char *name, void *addr,
			      size_t size, enum kbase_ipa_model_param_type type);
void kbase_ipa_model_param_free_all(struct kbase_ipa_model *model);

void kbase_ipa_model_param_set_s32(struct kbase_ipa_model *model, const char *name, s32 val);

#else

static inline int kbase_ipa_model_param_add(struct kbase_ipa_model *model, const char *name,
					    void *addr, size_t size,
					    enum kbase_ipa_model_param_type type)
{
	return 0;
}

static inline void kbase_ipa_model_param_free_all(struct kbase_ipa_model *model)
{
}

static inline void kbase_ipa_model_param_set_s32(struct kbase_ipa_model *model, const char *name,
						 s32 val)
{
}
#endif

#endif
