/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_IPA_SIMPLE_H_
#define _KBASE_IPA_SIMPLE_H_

#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

extern struct kbase_ipa_model_ops kbase_simple_ipa_model_ops;

#if MALI_UNIT_TEST

void kbase_simple_power_model_set_dummy_temp(int temp);
#endif

#endif

#endif
