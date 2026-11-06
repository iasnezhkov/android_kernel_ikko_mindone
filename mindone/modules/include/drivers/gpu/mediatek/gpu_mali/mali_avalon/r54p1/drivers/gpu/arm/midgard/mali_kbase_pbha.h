/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_PBHA_H
#define _KBASE_PBHA_H

#include <linux/types.h>

struct kbase_device;

bool kbasep_pbha_supported(struct kbase_device *kbdev);

int kbase_pbha_record_settings(struct kbase_device *kbdev, bool runtime, unsigned int id,
			       unsigned int read_setting, unsigned int write_setting);

void kbase_pbha_write_settings(struct kbase_device *kbdev);

int kbase_pbha_read_dtb(struct kbase_device *kbdev);

#endif
