/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HW_H_
#define _KBASE_HW_H_

#include "mali_kbase_defs.h"

#define kbase_hw_has_issue(kbdev, issue) test_bit(issue, &(kbdev)->hw_issues_mask[0])

#define kbase_hw_has_feature(kbdev, feature) test_bit(feature, &(kbdev)->hw_features_mask[0])

#define kbase_hw_has_l2_slice_hash_feature(kbdev) \
	test_bit(BASE_HW_FEATURE_L2_SLICE_HASH, &(kbdev)->hw_features_mask[0])

int kbase_hw_set_issues_mask(struct kbase_device *kbdev);

void kbase_hw_set_features_mask(struct kbase_device *kbdev);

#endif
