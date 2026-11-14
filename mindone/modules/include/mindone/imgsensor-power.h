/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 The LineageOS Project */
#ifndef MINDONE_IMGSENSOR_POWER_H
#define MINDONE_IMGSENSOR_POWER_H

#include <linux/notifier.h>

enum imgsensor_power_action {
	IMGSENSOR_POWER_PIN_ON = 1,
	IMGSENSOR_POWER_ON,
	IMGSENSOR_POWER_OFF,
};

struct imgsensor_power_event {
	int sensor_idx;
	const char *pin;
};

int imgsensor_power_register_notifier(struct notifier_block *nb);
int imgsensor_power_unregister_notifier(struct notifier_block *nb);

#endif
