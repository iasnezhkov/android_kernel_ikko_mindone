/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MALI_KBASE_SYNC_H
#define MALI_KBASE_SYNC_H

#include <linux/fdtable.h>
#include <linux/syscalls.h>
#if IS_ENABLED(CONFIG_SYNC_FILE)
#include <linux/sync_file.h>
#endif

#include <linux/version_compat_defs.h>

#if !MALI_USE_CSF
struct kbase_jd_atom;
struct work_struct;
#endif

struct kbase_sync_fence_info {
	void *fence;
	char name[32];
	int status;
};

int kbase_sync_fence_stream_create(const char *name, int *const out_fd);

#if !MALI_USE_CSF

int kbase_sync_fence_out_create(struct kbase_jd_atom *katom, int stream_fd);

int kbase_sync_fence_in_from_fd(struct kbase_jd_atom *katom, int fd);
#endif

int kbase_sync_fence_validate(int fd);

#if !MALI_USE_CSF

enum base_jd_event_code kbase_sync_fence_out_trigger(struct kbase_jd_atom *katom, int result);

int kbase_sync_fence_in_wait(struct kbase_jd_atom *katom);

void kbase_sync_fence_in_cancel_wait(struct kbase_jd_atom *katom);

void kbase_sync_fence_in_remove(struct kbase_jd_atom *katom);

void kbase_sync_fence_out_remove(struct kbase_jd_atom *katom);
#endif

#if !MALI_USE_CSF

int kbase_sync_fence_in_info_get(struct kbase_jd_atom *katom, struct kbase_sync_fence_info *info);

int kbase_sync_fence_out_info_get(struct kbase_jd_atom *katom, struct kbase_sync_fence_info *info);
#endif

#if IS_ENABLED(CONFIG_SYNC_FILE)
void kbase_sync_fence_info_get(struct dma_fence *fence, struct kbase_sync_fence_info *info);
#endif

const char *kbase_sync_status_string(int status);

#if !MALI_USE_CSF

void kbase_sync_fence_wait_worker(struct work_struct *data);

#ifdef CONFIG_MALI_FENCE_DEBUG

void kbase_sync_fence_in_dump(struct kbase_jd_atom *katom);
#endif
#endif

#endif
