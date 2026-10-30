// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/types.h>

#include "adapter.h"

static void panel_send(void *context, const u8 *data, size_t length)
{
	long status;

	if (mindone_panel_state(context, PANEL_STATE_ERROR) < 0)
		return;
	if (!length) {
		mindone_panel_set_state(context, PANEL_STATE_ERROR, -EINVAL);
		return;
	}
	status = mindone_panel_write(context, data[0] >= 0xb0, data, length);
	if (status < 0)
		mindone_panel_set_state(context, PANEL_STATE_ERROR, (int)status);
	else if ((size_t)status != length)
		mindone_panel_set_state(context, PANEL_STATE_ERROR, -EIO);
}

static void panel_initialize(void *context)
{
	size_t i;

	for (i = 0; i < mindone_panel_init_count; i++) {
		const struct mindone_panel_step *step = &mindone_panel_init_steps[i];
		u8 brightness[2];

		switch (step->kind) {
		case MINDONE_PANEL_STEP_GPIO:
			mindone_panel_gpio(context, step->argument, step->value);
			break;
		case MINDONE_PANEL_STEP_SLEEP:
			mindone_panel_sleep(context, step->argument);
			break;
		case MINDONE_PANEL_STEP_WRITE:
			panel_send(context, step->data, step->length);
			break;
		case MINDONE_PANEL_STEP_BRIGHTNESS:
			brightness[0] = 0x51;
			brightness[1] = mindone_panel_level();
			panel_send(context, brightness, sizeof(brightness));
			break;
		}
	}
}

static void panel_power_down(void *context)
{
	if (mindone_panel_has_gpio(context, 2))
		mindone_panel_gpio(context, 2, 0);
	if (mindone_panel_has_gpio(context, 1))
		mindone_panel_gpio(context, 1, 0);
}

static int panel_unprepare(void *context)
{
	static const u8 display_off[] = { 0x28 };
	static const u8 sleep_in[] = { 0x10 };
	static const u8 deep_standby[] = { 0x4f, 0x01 };
	int status;

	if (!mindone_panel_state(context, PANEL_STATE_PREPARED))
		return 0;
	panel_send(context, display_off, sizeof(display_off));
	mindone_panel_sleep(context, 20);
	panel_send(context, sleep_in, sizeof(sleep_in));
	mindone_panel_sleep(context, 120);
	panel_send(context, deep_standby, sizeof(deep_standby));
	status = mindone_panel_state(context, PANEL_STATE_ERROR);
	panel_power_down(context);
	mindone_panel_set_state(context, PANEL_STATE_ERROR, 0);
	mindone_panel_set_state(context, PANEL_STATE_PREPARED, 0);
	if (status < 0)
		mindone_panel_log_cleanup(context, status);
	return 0;
}

static int panel_prepare(void *context)
{
	int prepared = mindone_panel_state(context, PANEL_STATE_PREPARED);
	int status;

	if (prepared && mindone_panel_state(context, PANEL_STATE_ERROR) >= 0)
		return 0;
	if (!prepared)
		panel_initialize(context);
	status = mindone_panel_state(context, PANEL_STATE_ERROR);
	if (status < 0) {
		panel_power_down(context);
		mindone_panel_set_state(context, PANEL_STATE_ERROR, 0);
		mindone_panel_set_state(context, PANEL_STATE_PREPARED, 0);
		mindone_panel_set_state(context, PANEL_STATE_ENABLED, 0);
		return status;
	}
	mindone_panel_set_state(context, PANEL_STATE_PREPARED, 1);
	mindone_panel_touch_reset(context);
	return status;
}

static int panel_set_backlight(void *context, bool enable)
{
	int status;

	if (!!mindone_panel_state(context, PANEL_STATE_ENABLED) == enable)
		return 0;
	status = mindone_panel_backlight(context, enable);
	if (status)
		return status < 0 ? status : -EIO;
	mindone_panel_set_state(context, PANEL_STATE_ENABLED, enable);
	return 0;
}

static int panel_perform(void *context, u32 operation, int argument)
{
	int prepared, enabled;

	switch (operation) {
	case PANEL_OP_PREPARE:
		return panel_prepare(context);
	case PANEL_OP_UNPREPARE:
		return panel_unprepare(context);
	case PANEL_OP_ENABLE:
		return panel_set_backlight(context, true);
	case PANEL_OP_DISABLE:
		return panel_set_backlight(context, false);
	case PANEL_OP_RESET:
		mindone_panel_gpio(context, 0, argument);
		return 0;
	case PANEL_OP_ATA_CHECK:
		return -EOPNOTSUPP;
	case PANEL_OP_BOOT_STATE:
		mindone_panel_set_state(context, PANEL_STATE_PREPARED, 1);
		mindone_panel_set_state(context, PANEL_STATE_ENABLED, 1);
		return 0;
	case PANEL_OP_SUSPEND_OBSERVE:
		prepared = mindone_panel_state(context, PANEL_STATE_PREPARED);
		enabled = mindone_panel_state(context, PANEL_STATE_ENABLED);
		if (prepared || enabled)
			mindone_panel_log_pm_state(context, !!prepared, !!enabled);
		return 0;
	default:
		return -EINVAL;
	}
}

int mindone_panel_call(void *context, u32 operation, int argument)
{
	int status;

	if (!context)
		return -EINVAL;
	mindone_panel_operation_lock(context);
	status = panel_perform(context, operation, argument);
	mindone_panel_operation_unlock(context);
	return status;
}

int mindone_panel_brightness(void *context, u32 level)
{
	int status = 0;

	if (!context)
		return -EINVAL;
	mindone_panel_command_lock();
	mindone_panel_cmdq_set_level(min_t(u32, level, 255));
	if (mindone_panel_cmdq_available(context))
		mindone_panel_cmdq_emit(context);
	else
		status = -1;
	mindone_panel_command_unlock();
	return status;
}
