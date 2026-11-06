/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 The LineageOS Project */
#ifndef MINDONE_PANEL_CONTROL_ADAPTER_H
#define MINDONE_PANEL_CONTROL_ADAPTER_H

#include <linux/types.h>

#define PANEL_OP_PREPARE 1
#define PANEL_OP_UNPREPARE 2
#define PANEL_OP_ENABLE 3
#define PANEL_OP_DISABLE 4
#define PANEL_OP_RESET 5
#define PANEL_OP_ATA_CHECK 6
#define PANEL_OP_BOOT_STATE 7
#define PANEL_OP_SUSPEND_OBSERVE 8

#define PANEL_STATE_PREPARED 0
#define PANEL_STATE_ENABLED 1
#define PANEL_STATE_ERROR 2

enum mindone_panel_step_kind {
	MINDONE_PANEL_STEP_GPIO,
	MINDONE_PANEL_STEP_SLEEP,
	MINDONE_PANEL_STEP_WRITE,
	MINDONE_PANEL_STEP_BRIGHTNESS,
};

struct mindone_panel_step {
	enum mindone_panel_step_kind kind;
	u32 argument;
	int value;
	const u8 *data;
	size_t length;
};

#define PANEL_STEP_GPIO(pin, level) \
	{ .kind = MINDONE_PANEL_STEP_GPIO, .argument = (pin), .value = (level) }
#define PANEL_STEP_SLEEP(milliseconds) \
	{ .kind = MINDONE_PANEL_STEP_SLEEP, .argument = (milliseconds) }
#define PANEL_STEP_WRITE(...) \
	{ .kind = MINDONE_PANEL_STEP_WRITE, .data = (const u8[]){ __VA_ARGS__ }, \
	  .length = sizeof((const u8[]){ __VA_ARGS__ }) }
#define PANEL_STEP_BRIGHTNESS { .kind = MINDONE_PANEL_STEP_BRIGHTNESS }

extern const struct mindone_panel_step mindone_panel_init_steps[];
extern const size_t mindone_panel_init_count;

void mindone_panel_operation_lock(void *context);
void mindone_panel_operation_unlock(void *context);
void mindone_panel_command_lock(void);
void mindone_panel_command_unlock(void);
int mindone_panel_call(void *context, u32 operation, int argument);
int mindone_panel_brightness(void *context, u32 level);
int mindone_panel_state(void *context, u32 field);
void mindone_panel_set_state(void *context, u32 field, int value);
void mindone_panel_gpio(void *context, u32 pin, int value);
u8 mindone_panel_has_gpio(void *context, u32 pin);
void mindone_panel_sleep(void *context, u32 milliseconds);
long mindone_panel_write(void *context, u8 generic, const u8 *data, size_t length);
u8 mindone_panel_level(void);
int mindone_panel_backlight(void *context, u8 enabled);
void mindone_panel_touch_reset(void *context);
void mindone_panel_log_cleanup(void *context, int error);
void mindone_panel_log_pm_state(void *context, u8 prepared, u8 enabled);
void mindone_panel_cmdq_set_level(u8 level);
u8 mindone_panel_cmdq_available(void *context);
void mindone_panel_cmdq_emit(void *context);
int mindone_panel_driver_register(void);
void mindone_panel_driver_unregister(void);

#endif
