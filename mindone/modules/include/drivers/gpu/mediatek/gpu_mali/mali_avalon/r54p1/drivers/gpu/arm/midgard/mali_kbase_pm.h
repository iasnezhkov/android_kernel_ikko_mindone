/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_PM_H_
#define _KBASE_PM_H_

#include <linux/types.h>

struct kbase_device;

#define PM_ENABLE_IRQS 0x01
#define PM_HW_ISSUES_DETECT 0x02

#define PM_NO_RESET 0x04

int kbase_pm_init(struct kbase_device *kbdev);

int kbase_pm_powerup(struct kbase_device *kbdev, unsigned int flags);

void kbase_pm_halt(struct kbase_device *kbdev);

void kbase_pm_term(struct kbase_device *kbdev);

void kbase_pm_context_active(struct kbase_device *kbdev);

enum kbase_pm_suspend_handler {

	KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE,

	KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE,

	KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE,
#ifdef CONFIG_MALI_ARBITER_SUPPORT

	KBASE_PM_SUSPEND_HANDLER_VM_GPU_GRANTED,
#endif
};

int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev,
					   enum kbase_pm_suspend_handler suspend_handler);

int kbase_pm_context_active_handle_suspend_locked(struct kbase_device *kbdev,
						  enum kbase_pm_suspend_handler suspend_handler);

void kbase_pm_context_idle(struct kbase_device *kbdev);

void kbase_pm_context_idle_locked(struct kbase_device *kbdev);

int kbase_pm_suspend(struct kbase_device *kbdev);

void kbase_pm_resume(struct kbase_device *kbdev);

void kbase_pm_vsync_callback(int buffer_updated, void *data);

int kbase_pm_driver_suspend(struct kbase_device *kbdev);

void kbase_pm_driver_resume(struct kbase_device *kbdev, bool arb_gpu_start);

#ifdef CONFIG_MALI_ARBITER_SUPPORT

void kbase_pm_handle_gpu_lost(struct kbase_device *kbdev);
#endif

#endif
