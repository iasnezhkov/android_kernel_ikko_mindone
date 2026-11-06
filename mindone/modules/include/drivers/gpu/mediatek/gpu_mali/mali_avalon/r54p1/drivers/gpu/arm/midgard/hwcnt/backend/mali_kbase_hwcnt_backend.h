/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_BACKEND_H_
#define _KBASE_HWCNT_BACKEND_H_

#include <linux/types.h>

struct kbase_hwcnt_metadata;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

struct kbase_hwcnt_backend_info;

struct kbase_hwcnt_backend;

typedef const struct kbase_hwcnt_metadata *
kbase_hwcnt_backend_metadata_fn(const struct kbase_hwcnt_backend_info *info);

typedef int kbase_hwcnt_backend_init_fn(const struct kbase_hwcnt_backend_info *info,
					struct kbase_hwcnt_backend **out_backend);

typedef void kbase_hwcnt_backend_term_fn(struct kbase_hwcnt_backend *backend);

typedef void kbase_hwcnt_backend_acquire_fn(const struct kbase_hwcnt_backend *backend);

typedef void kbase_hwcnt_backend_release_fn(const struct kbase_hwcnt_backend *backend);

typedef u64 kbase_hwcnt_backend_timestamp_ns_fn(struct kbase_hwcnt_backend *backend);

typedef int kbase_hwcnt_backend_dump_enable_fn(struct kbase_hwcnt_backend *backend,
					       const struct kbase_hwcnt_enable_map *enable_map);

typedef int
kbase_hwcnt_backend_dump_enable_nolock_fn(struct kbase_hwcnt_backend *backend,
					  const struct kbase_hwcnt_enable_map *enable_map);

typedef void kbase_hwcnt_backend_dump_disable_fn(struct kbase_hwcnt_backend *backend,
						 struct kbase_hwcnt_dump_buffer *dump_buffer,
						 const struct kbase_hwcnt_enable_map *enable_map);

typedef int kbase_hwcnt_backend_dump_clear_fn(struct kbase_hwcnt_backend *backend);

typedef int kbase_hwcnt_backend_dump_request_fn(struct kbase_hwcnt_backend *backend);

typedef int kbase_hwcnt_backend_dump_wait_fn(struct kbase_hwcnt_backend *backend);

typedef int kbase_hwcnt_backend_dump_get_fn(struct kbase_hwcnt_backend *backend,
					    struct kbase_hwcnt_dump_buffer *dump_buffer,
					    const struct kbase_hwcnt_enable_map *enable_map,
					    bool accumulate, u64 *dump_time_ns);

struct kbase_hwcnt_backend_interface {
	const struct kbase_hwcnt_backend_info *info;
	kbase_hwcnt_backend_metadata_fn *metadata;
	kbase_hwcnt_backend_init_fn *init;
	kbase_hwcnt_backend_term_fn *term;
	kbase_hwcnt_backend_acquire_fn *acquire;
	kbase_hwcnt_backend_release_fn *release;
	kbase_hwcnt_backend_timestamp_ns_fn *timestamp_ns;
	kbase_hwcnt_backend_dump_enable_fn *dump_enable;
	kbase_hwcnt_backend_dump_enable_nolock_fn *dump_enable_nolock;
	kbase_hwcnt_backend_dump_disable_fn *dump_disable;
	kbase_hwcnt_backend_dump_clear_fn *dump_clear;
	kbase_hwcnt_backend_dump_request_fn *dump_request;
	kbase_hwcnt_backend_dump_wait_fn *dump_wait;
	kbase_hwcnt_backend_dump_get_fn *dump_get;
};

#endif
