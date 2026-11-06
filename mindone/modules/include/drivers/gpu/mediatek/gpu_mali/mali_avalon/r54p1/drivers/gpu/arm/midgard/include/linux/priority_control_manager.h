/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PRIORITY_CONTROL_MANAGER_H_
#define _PRIORITY_CONTROL_MANAGER_H_

#include <linux/mm.h>
#include <linux/of.h>
#include <linux/version.h>

struct priority_control_manager_device;

#define ADD_PRIORITIZED_PROCESS 0
#define REMOVE_PRIORITIZED_PROCESS 1

struct pcm_prioritized_process_notifier_data {
	uint32_t pid;
};

struct priority_control_manager_ops {
	int (*pcm_scheduler_priority_check)(struct priority_control_manager_device *pcm_dev,
					    struct task_struct *task, int requested_priority);

	int (*pcm_prioritized_process_notifier_register)(
		struct priority_control_manager_device *pcm_dev, struct notifier_block *nb);

	int (*pcm_prioritized_process_notifier_unregister)(
		struct priority_control_manager_device *pcm_dev, struct notifier_block *nb);
};

struct priority_control_manager_device {
	struct priority_control_manager_ops ops;
	void *data;
	struct module *owner;
};

#endif
