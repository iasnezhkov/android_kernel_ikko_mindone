// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase.h"
#include "hwcnt/mali_kbase_hwcnt_watchdog_if.h"
#include "hwcnt/mali_kbase_hwcnt_watchdog_if_timer.h"

#include <linux/workqueue.h>
#include <linux/slab.h>

struct kbase_hwcnt_watchdog_if_timer_info {
	struct workqueue_struct *workq;
	struct delayed_work dwork;
	bool timer_enabled;
	kbase_hwcnt_watchdog_callback_fn *callback;
	void *user_data;
};

static void kbasep_hwcnt_watchdog_callback(struct work_struct *const work)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const info =
		container_of(work, struct kbase_hwcnt_watchdog_if_timer_info, dwork.work);

	if (info->callback)
		info->callback(info->user_data);
}

static int kbasep_hwcnt_watchdog_if_timer_enable(
	const struct kbase_hwcnt_watchdog_info *const timer, u32 const period_ms,
	kbase_hwcnt_watchdog_callback_fn *const callback, void *const user_data)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info = (void *)timer;

	if (WARN_ON(!timer) || WARN_ON(!callback) || WARN_ON(timer_info->timer_enabled))
		return -EINVAL;

	timer_info->callback = callback;
	timer_info->user_data = user_data;

	queue_delayed_work(timer_info->workq, &timer_info->dwork, msecs_to_jiffies(period_ms));
	timer_info->timer_enabled = true;

	return 0;
}

static void
kbasep_hwcnt_watchdog_if_timer_disable(const struct kbase_hwcnt_watchdog_info *const timer)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info = (void *)timer;

	if (WARN_ON(!timer))
		return;

	if (!timer_info->timer_enabled)
		return;

	cancel_delayed_work_sync(&timer_info->dwork);
	timer_info->timer_enabled = false;
}

static void
kbasep_hwcnt_watchdog_if_timer_modify(const struct kbase_hwcnt_watchdog_info *const timer,
				      u32 const delay_ms)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info = (void *)timer;

	if (WARN_ON(!timer) || WARN_ON(!timer_info->timer_enabled))
		return;

	mod_delayed_work(timer_info->workq, &timer_info->dwork, msecs_to_jiffies(delay_ms));
}

void kbase_hwcnt_watchdog_if_timer_destroy(struct kbase_hwcnt_watchdog_interface *const watchdog_if)
{
	struct kbase_hwcnt_watchdog_if_timer_info *timer_info;

	if (WARN_ON(!watchdog_if))
		return;

	timer_info = (void *)watchdog_if->timer;

	if (WARN_ON(!timer_info))
		return;

	destroy_workqueue(timer_info->workq);
	kfree(timer_info);

	*watchdog_if = (struct kbase_hwcnt_watchdog_interface){
		.timer = NULL, .enable = NULL, .disable = NULL, .modify = NULL
	};
}

int kbase_hwcnt_watchdog_if_timer_create(struct kbase_hwcnt_watchdog_interface *const watchdog_if)
{
	struct kbase_hwcnt_watchdog_if_timer_info *timer_info;

	if (WARN_ON(!watchdog_if))
		return -EINVAL;

	timer_info = kmalloc(sizeof(*timer_info), GFP_KERNEL);
	if (!timer_info)
		return -ENOMEM;

	*timer_info = (struct kbase_hwcnt_watchdog_if_timer_info){ .timer_enabled = false };

	INIT_DELAYED_WORK(&timer_info->dwork, kbasep_hwcnt_watchdog_callback);

	*watchdog_if = (struct kbase_hwcnt_watchdog_interface){
		.timer = (void *)timer_info,
		.enable = kbasep_hwcnt_watchdog_if_timer_enable,
		.disable = kbasep_hwcnt_watchdog_if_timer_disable,
		.modify = kbasep_hwcnt_watchdog_if_timer_modify,
	};

	timer_info->workq = alloc_workqueue("mali_hwc_watchdog_wq", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (timer_info->workq)
		return 0;

	kfree(timer_info);
	return -ENOMEM;
}
