/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_INSTR_INTERNAL_H_
#define _KBASE_INSTR_INTERNAL_H_

void kbasep_cache_clean_worker(struct work_struct *data);

void kbase_instr_hwcnt_sample_done(struct kbase_device *kbdev);

#endif
