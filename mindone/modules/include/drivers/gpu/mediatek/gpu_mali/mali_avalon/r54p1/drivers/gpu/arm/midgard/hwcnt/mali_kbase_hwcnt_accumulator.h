/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_ACCUMULATOR_H_
#define _KBASE_HWCNT_ACCUMULATOR_H_

#include <linux/types.h>

struct kbase_hwcnt_context;
struct kbase_hwcnt_accumulator;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

int kbase_hwcnt_accumulator_acquire(struct kbase_hwcnt_context *hctx,
				    struct kbase_hwcnt_accumulator **accum);

void kbase_hwcnt_accumulator_release(struct kbase_hwcnt_accumulator *accum);

int kbase_hwcnt_accumulator_set_counters(struct kbase_hwcnt_accumulator *accum,
					 const struct kbase_hwcnt_enable_map *new_map,
					 u64 *ts_start_ns, u64 *ts_end_ns,
					 struct kbase_hwcnt_dump_buffer *dump_buf);

int kbase_hwcnt_accumulator_dump(struct kbase_hwcnt_accumulator *accum, u64 *ts_start_ns,
				 u64 *ts_end_ns, struct kbase_hwcnt_dump_buffer *dump_buf);

u64 kbase_hwcnt_accumulator_timestamp_ns(struct kbase_hwcnt_accumulator *accum);

#endif
