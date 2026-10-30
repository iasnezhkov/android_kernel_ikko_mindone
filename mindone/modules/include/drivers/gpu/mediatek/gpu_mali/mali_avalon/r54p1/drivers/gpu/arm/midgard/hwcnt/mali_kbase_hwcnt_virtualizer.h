/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_VIRTUALIZER_H_
#define _KBASE_HWCNT_VIRTUALIZER_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct kbase_hwcnt_context;
struct kbase_hwcnt_virtualizer;
struct kbase_hwcnt_virtualizer_client;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

int kbase_hwcnt_virtualizer_init(struct kbase_hwcnt_context *hctx, u64 dump_threshold_ns,
				 struct kbase_hwcnt_virtualizer **out_hvirt);

void kbase_hwcnt_virtualizer_term(struct kbase_hwcnt_virtualizer *hvirt);

const struct kbase_hwcnt_metadata *
kbase_hwcnt_virtualizer_metadata(struct kbase_hwcnt_virtualizer *hvirt);

int kbase_hwcnt_virtualizer_client_create(struct kbase_hwcnt_virtualizer *hvirt,
					  const struct kbase_hwcnt_enable_map *enable_map,
					  struct kbase_hwcnt_virtualizer_client **out_hvcli);

void kbase_hwcnt_virtualizer_client_destroy(struct kbase_hwcnt_virtualizer_client *hvcli);

int kbase_hwcnt_virtualizer_client_set_counters(struct kbase_hwcnt_virtualizer_client *hvcli,
						const struct kbase_hwcnt_enable_map *enable_map,
						u64 *ts_start_ns, u64 *ts_end_ns,
						struct kbase_hwcnt_dump_buffer *dump_buf);

int kbase_hwcnt_virtualizer_client_dump(struct kbase_hwcnt_virtualizer_client *hvcli,
					u64 *ts_start_ns, u64 *ts_end_ns,
					struct kbase_hwcnt_dump_buffer *dump_buf);

bool kbase_hwcnt_virtualizer_queue_work(struct kbase_hwcnt_virtualizer *hvirt,
					struct work_struct *work);

#endif
