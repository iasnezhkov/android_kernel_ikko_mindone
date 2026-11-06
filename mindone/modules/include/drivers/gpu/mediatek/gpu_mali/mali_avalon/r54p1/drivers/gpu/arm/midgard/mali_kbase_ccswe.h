/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CCSWE_H_
#define _KBASE_CCSWE_H_

#include <linux/types.h>
#include <linux/spinlock.h>

struct kbase_ccswe {
	spinlock_t access;
	u64 timestamp_ns;
	u64 cycles_elapsed;
	u32 gpu_freq;
	u32 prev_gpu_freq;
};

void kbase_ccswe_init(struct kbase_ccswe *self);

u64 kbase_ccswe_cycle_at(struct kbase_ccswe *self, u64 timestamp_ns);

void kbase_ccswe_freq_change(struct kbase_ccswe *self, u64 timestamp_ns, u32 gpu_freq);

void kbase_ccswe_reset(struct kbase_ccswe *self);

#endif
