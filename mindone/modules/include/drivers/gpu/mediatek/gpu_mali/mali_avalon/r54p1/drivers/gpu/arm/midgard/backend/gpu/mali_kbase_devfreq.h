/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BASE_DEVFREQ_H_
#define _BASE_DEVFREQ_H_

int kbase_devfreq_init(struct kbase_device *kbdev);

void kbase_devfreq_term(struct kbase_device *kbdev);

void kbase_devfreq_force_freq(struct kbase_device *kbdev, unsigned long freq);

void kbase_devfreq_enqueue_work(struct kbase_device *kbdev, enum kbase_devfreq_work_type work_type);

void kbase_devfreq_opp_translate(struct kbase_device *kbdev, unsigned long freq, u64 *core_mask,
				 unsigned long *freqs, unsigned long *volts);
#endif
