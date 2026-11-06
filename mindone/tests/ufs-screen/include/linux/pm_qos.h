/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef UFS_TEST_PM_QOS_H
#define UFS_TEST_PM_QOS_H
#include <linux/device.h>
enum dev_pm_qos_req_type {
	DEV_PM_QOS_RESUME_LATENCY = 1,
	DEV_PM_QOS_LATENCY_TOLERANCE,
	DEV_PM_QOS_MIN_FREQUENCY,
	DEV_PM_QOS_MAX_FREQUENCY,
	DEV_PM_QOS_FLAGS,
};
struct dev_pm_qos_request {
	struct device *dev;
	int value;
};
int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req, enum dev_pm_qos_req_type type, int value);
int dev_pm_qos_remove_request(struct dev_pm_qos_request *req);
#endif
