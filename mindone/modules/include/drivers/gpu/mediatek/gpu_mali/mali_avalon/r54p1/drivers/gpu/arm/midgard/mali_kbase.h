/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_H_
#define _KBASE_H_

#include <mali_malisw.h>

#include <mali_kbase_debug.h>

#include <uapi/gpu/arm/midgard/mali_base_kernel.h>
#include <mali_kbase_linux.h>
#include <linux/version_compat_defs.h>

#include "mali_kbase_defs.h"

#include "debug/mali_kbase_debug_ktrace.h"
#include "context/mali_kbase_context.h"
#include "mali_kbase_mem_lowlevel.h"
#include "mali_kbase_mem.h"
#include "mmu/mali_kbase_mmu.h"
#include "mali_kbase_gpu_memory_debugfs.h"
#include "mali_kbase_mem_profile_debugfs.h"
#include "mali_kbase_gpuprops.h"
#include <uapi/gpu/arm/midgard/mali_kbase_ioctl.h>
#if !MALI_USE_CSF
#include "mali_kbase_debug_job_fault.h"
#include "mali_kbase_jd_debugfs.h"
#include "mali_kbase_jm.h"
#include "mali_kbase_js.h"
#else
#include "csf/mali_kbase_debug_csf_fault.h"
#endif

#include "ipa/mali_kbase_ipa.h"

#if MALI_USE_CSF
#include "csf/mali_kbase_csf.h"
#endif

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
#include <trace/events/gpu.h>
#endif

#include "mali_linux_trace.h"

#include <linux/atomic.h>
#include <linux/highmem.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#if (KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE)
#include <linux/sched/mm.h>
#endif
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

#define KBASE_DRV_NAME "mali"
#define KBASE_TIMELINE_NAME KBASE_DRV_NAME ".timeline"

#if MALI_USE_CSF

#define KBASE_MEM_GROUP_CSF_IO BASE_MEM_GROUP_DEFAULT

#define KBASE_MEM_GROUP_CSF_FW BASE_MEM_GROUP_DEFAULT
#endif

#define KBASE_MEM_GROUP_SINK BASE_MEM_GROUP_DEFAULT

struct kbase_device *kbase_device_alloc(void);

int kbase_device_misc_init(struct kbase_device *kbdev);
void kbase_device_misc_term(struct kbase_device *kbdev);
void kbase_device_free(struct kbase_device *kbdev);
int kbase_device_has_feature(struct kbase_device *kbdev, u32 feature);

struct kbase_device *kbase_find_device(int minor);
void kbase_release_device(struct kbase_device *kbdev);

unsigned long kbase_context_get_unmapped_area(struct kbase_context *kctx, const unsigned long addr,
					      const unsigned long len, const unsigned long pgoff,
					      const unsigned long flags);

int kbase_get_irqs(struct kbase_device *kbdev);

int kbase_sysfs_init(struct kbase_device *kbdev);
void kbase_sysfs_term(struct kbase_device *kbdev);

int kbase_protected_mode_init(struct kbase_device *kbdev);
void kbase_protected_mode_term(struct kbase_device *kbdev);

int kbase_device_backend_init(struct kbase_device *kbdev);

void kbase_device_backend_term(struct kbase_device *kbdev);

int power_control_init(struct kbase_device *kbdev);
void power_control_term(struct kbase_device *kbdev);

#if IS_ENABLED(CONFIG_DEBUG_FS)
void kbase_device_debugfs_term(struct kbase_device *kbdev);
int kbase_device_debugfs_init(struct kbase_device *kbdev);
#else
static inline int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	return 0;
}

static inline void kbase_device_debugfs_term(struct kbase_device *kbdev)
{
}
#endif

int registers_map(struct kbase_device *kbdev);
void registers_unmap(struct kbase_device *kbdev);

int kbase_device_coherency_init(struct kbase_device *kbdev);

#if !MALI_USE_CSF

int kbase_jd_init(struct kbase_context *kctx);
void kbase_jd_exit(struct kbase_context *kctx);

int kbase_jd_submit(struct kbase_context *kctx, void __user *user_addr, u32 nr_atoms, u32 stride,
		    bool uk6_atom);

void kbase_jd_done_worker(struct work_struct *data);

void kbase_jd_done(struct kbase_jd_atom *katom, unsigned int slot_nr, ktime_t *end_timestamp,
		   kbasep_js_atom_done_code done_code);
void kbase_jd_cancel(struct kbase_device *kbdev, struct kbase_jd_atom *katom);
void kbase_jd_zap_context(struct kbase_context *kctx);

bool kbase_jd_done_nolock(struct kbase_jd_atom *katom, bool post_immediately);

void kbase_jd_free_external_resources(struct kbase_jd_atom *katom);
void kbase_jd_dep_clear_locked(struct kbase_jd_atom *katom);

void kbase_job_done(struct kbase_device *kbdev, u32 done);

void kbase_job_slot_ctx_priority_check_locked(struct kbase_context *kctx,
					      struct kbase_jd_atom *katom);

void kbase_job_slot_softstop(struct kbase_device *kbdev, unsigned int js,
			     struct kbase_jd_atom *target_katom);

void kbase_job_slot_softstop_swflags(struct kbase_device *kbdev, unsigned int js,
				     struct kbase_jd_atom *target_katom, u32 sw_flags);

void kbase_job_check_enter_disjoint(struct kbase_device *kbdev, u32 action,
				    base_jd_core_req core_reqs, struct kbase_jd_atom *target_katom);

void kbase_job_check_leave_disjoint(struct kbase_device *kbdev, struct kbase_jd_atom *target_katom);

#endif

void kbase_event_post(struct kbase_context *kctx, struct kbase_jd_atom *event);
#if !MALI_USE_CSF
int kbase_event_dequeue(struct kbase_context *kctx, struct base_jd_event_v2 *uevent);
#endif
int kbase_event_pending(struct kbase_context *kctx);
int kbase_event_init(struct kbase_context *kctx);
void kbase_event_close(struct kbase_context *kctx);
void kbase_event_cleanup(struct kbase_context *kctx);
void kbase_event_wakeup(struct kbase_context *kctx);

int kbasep_jit_alloc_validate(struct kbase_context *kctx, struct base_jit_alloc_info *info);

void kbase_jit_retry_pending_alloc(struct kbase_context *kctx);

static inline void kbase_free_user_buffer(struct kbase_debug_copy_buffer *buffer)
{
	struct page **pages = buffer->extres_pages;
	uint nr_pages = buffer->nr_extres_pages;

	if (pages) {
		uint i;

		for (i = 0; i < nr_pages; i++) {
			struct page *pg = pages[i];

			if (pg)
				put_page(pg);
		}
		kfree(pages);
	}
}

#if !MALI_USE_CSF
int kbase_process_soft_job(struct kbase_jd_atom *katom);
int kbase_prepare_soft_job(struct kbase_jd_atom *katom);
void kbase_finish_soft_job(struct kbase_jd_atom *katom);
void kbase_cancel_soft_job(struct kbase_jd_atom *katom);
void kbase_resume_suspended_soft_jobs(struct kbase_device *kbdev);
void kbasep_remove_waiting_soft_job(struct kbase_jd_atom *katom);
#if IS_ENABLED(CONFIG_SYNC_FILE)
void kbase_soft_event_wait_callback(struct kbase_jd_atom *katom);
#endif
int kbase_soft_event_update(struct kbase_context *kctx, u64 event, unsigned char new_status);

void kbasep_soft_job_timeout_worker(struct timer_list *timer);
void kbasep_complete_triggered_soft_events(struct kbase_context *kctx, u64 evt);
#endif

void kbasep_as_do_poke(struct work_struct *work);

static inline bool kbase_pm_is_suspending(struct kbase_device *kbdev)
{
	return kbdev->pm.suspending;
}

static inline bool kbase_pm_is_resuming(struct kbase_device *kbdev)
{
	return kbdev->pm.resuming;
}

#ifdef CONFIG_MALI_ARBITER_SUPPORT

static inline bool kbase_pm_is_gpu_lost(struct kbase_device *kbdev)
{
	return (atomic_read(&kbdev->pm.gpu_lost) == 0 ? false : true);
}

static inline void kbase_pm_set_gpu_lost(struct kbase_device *kbdev, bool gpu_lost)
{
	const int new_val = (gpu_lost ? 1 : 0);
	const int cur_val = atomic_xchg(&kbdev->pm.gpu_lost, new_val);

	if (new_val != cur_val)
		KBASE_KTRACE_ADD(kbdev, ARB_GPU_LOST, NULL, (u64)new_val);
}
#endif

static inline bool kbase_pm_is_active(struct kbase_device *kbdev)
{
	return kbdev->pm.active_count > 0;
}

int kbase_pm_gpu_freq_init(struct kbase_device *kbdev);

void kbase_pm_metrics_start(struct kbase_device *kbdev);

void kbase_pm_metrics_stop(struct kbase_device *kbdev);

#if IS_ENABLED(CONFIG_MALI_MTK_POWER_TRANSITION_TIMEOUT_DEBUG)
void kbase_pm_debug_status(struct kbase_device *kbdev);
#endif

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)

int kbase_pm_handle_runtime_suspend(struct kbase_device *kbdev);

int kbase_pm_force_mcu_wakeup_after_sleep(struct kbase_device *kbdev);
#endif

#if !MALI_USE_CSF

static inline unsigned int kbase_jd_atom_id(struct kbase_context *kctx,
					    const struct kbase_jd_atom *katom)
{
	unsigned int result;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->kctx == kctx);

	result = katom - &kctx->jctx.atoms[0];
	KBASE_DEBUG_ASSERT(result <= BASE_JD_ATOM_COUNT);
	return result;
}

static inline struct kbase_jd_atom *kbase_jd_atom_from_id(struct kbase_context *kctx, int id)
{
	return &kctx->jctx.atoms[id];
}
#endif

void kbase_disjoint_init(struct kbase_device *kbdev);

void kbase_disjoint_event(struct kbase_device *kbdev);

void kbase_disjoint_event_potential(struct kbase_device *kbdev);

u32 kbase_disjoint_event_get(struct kbase_device *kbdev);

void kbase_disjoint_state_up(struct kbase_device *kbdev);

void kbase_disjoint_state_down(struct kbase_device *kbdev);

int kbase_device_pcm_dev_init(struct kbase_device *const kbdev);

void kbase_device_pcm_dev_term(struct kbase_device *const kbdev);

#if MALI_USE_CSF

bool kbasep_adjust_prioritized_process(struct kbase_device *kbdev, bool add, uint32_t tgid);

#endif

#define KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD 2

#if !defined(UINT64_MAX)
#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#endif

#if !defined(UINT32_MAX)
#define UINT32_MAX ((uint32_t)0xFFFFFFFFU)
#endif

#endif
