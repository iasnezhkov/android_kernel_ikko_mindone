// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kernel.h>

#include "adapter.h"

const struct mindone_panel_step mindone_panel_init_steps[] = {
	PANEL_STEP_GPIO(0, 1),
	PANEL_STEP_SLEEP(20),
	PANEL_STEP_WRITE(0xf0, 0x51),
	PANEL_STEP_WRITE(0xb9, 0x11, 0x3f),
	PANEL_STEP_WRITE(0xb0, 0x01),
	PANEL_STEP_WRITE(0xaf, 0x02),
	PANEL_STEP_BRIGHTNESS,
	PANEL_STEP_WRITE(0x11, 0x00),
	PANEL_STEP_SLEEP(120),
};
const size_t mindone_panel_init_count = ARRAY_SIZE(mindone_panel_init_steps);

struct panel {
	int prepared;
	int enabled;
	int error;
	int has_gpio[3];
	int write_index;
	int fail_write_at;
	long fail_status;
	int backlight_status;
	int cmdq_available;
	int operation_depth;
	int command_depth;
};

static char trace[8192];
static int failures;

static void record(const char *format, ...)
{
	size_t used = strlen(trace);
	va_list args;

	va_start(args, format);
	vsnprintf(trace + used, sizeof(trace) - used, format, args);
	va_end(args);
}

static void expect(int condition, const char *name)
{
	if (!condition) {
		printf("FAIL %s\ntrace: %s\n", name, trace);
		failures++;
	}
}

static void reset(struct panel *panel)
{
	memset(panel, 0, sizeof(*panel));
	panel->has_gpio[0] = panel->has_gpio[1] = panel->has_gpio[2] = 1;
	panel->fail_write_at = -1;
	panel->cmdq_available = 1;
	trace[0] = 0;
}

void mindone_panel_operation_lock(void *context)
{
	((struct panel *)context)->operation_depth++;
	record("L");
}

void mindone_panel_operation_unlock(void *context)
{
	((struct panel *)context)->operation_depth--;
	record("U");
}

static int command_depth;

void mindone_panel_command_lock(void)
{
	command_depth++;
	record("l");
}

void mindone_panel_command_unlock(void)
{
	command_depth--;
	record("u");
}

int mindone_panel_state(void *context, u32 field)
{
	struct panel *panel = context;

	return field == PANEL_STATE_PREPARED ? panel->prepared :
	       field == PANEL_STATE_ENABLED ? panel->enabled : panel->error;
}

void mindone_panel_set_state(void *context, u32 field, int value)
{
	struct panel *panel = context;

	if (field == PANEL_STATE_PREPARED)
		panel->prepared = value != 0;
	else if (field == PANEL_STATE_ENABLED)
		panel->enabled = value != 0;
	else
		panel->error = value;
}

void mindone_panel_gpio(void *context, u32 pin, int value)
{
	record("g%u=%d ", pin, value);
}

u8 mindone_panel_has_gpio(void *context, u32 pin)
{
	return ((struct panel *)context)->has_gpio[pin];
}

void mindone_panel_sleep(void *context, u32 milliseconds)
{
	record("s%u ", milliseconds);
}

long mindone_panel_write(void *context, u8 generic, const u8 *data, size_t length)
{
	struct panel *panel = context;
	size_t i;

	record("w%c", generic ? 'G' : 'D');
	for (i = 0; i < length; i++)
		record("%02x", data[i]);
	record(" ");
	if (panel->write_index++ == panel->fail_write_at)
		return panel->fail_status;
	return (long)length;
}

u8 mindone_panel_level(void)
{
	return 0x7f;
}

int mindone_panel_backlight(void *context, u8 enabled)
{
	record("b%d ", enabled);
	return ((struct panel *)context)->backlight_status;
}

void mindone_panel_touch_reset(void *context)
{
	record("T ");
}

void mindone_panel_log_cleanup(void *context, int error)
{
	record("C%d ", error);
}

void mindone_panel_log_pm_state(void *context, u8 prepared, u8 enabled)
{
	record("P%d%d ", prepared, enabled);
}

static int queued_level = -1;

void mindone_panel_cmdq_set_level(u8 level)
{
	queued_level = level;
}

u8 mindone_panel_cmdq_available(void *context)
{
	return ((struct panel *)context)->cmdq_available;
}

void mindone_panel_cmdq_emit(void *context)
{
	record("E%d ", queued_level);
}

int mindone_panel_driver_register(void)
{
	return 0;
}

void mindone_panel_driver_unregister(void)
{
}

static const char *init_trace = "Lg0=1 s20 wGf051 wGb9113f wGb001 wDaf02 wD517f wD1100 s120 T U";

int main(void)
{
	struct panel panel;
	int status;

	reset(&panel);
	status = mindone_panel_call(&panel, PANEL_OP_PREPARE, 0);
	expect(status == 0 && panel.prepared && !panel.error, "prepare succeeds");
	expect(!strcmp(trace, init_trace), "prepare follows the init table and generic flag");
	expect(panel.operation_depth == 0, "prepare releases the operation lock");

	trace[0] = 0;
	status = mindone_panel_call(&panel, PANEL_OP_PREPARE, 0);
	expect(status == 0 && !strcmp(trace, "LU"), "prepared panel is not reinitialized");

	reset(&panel);
	panel.fail_write_at = 1;
	panel.fail_status = 2;
	status = mindone_panel_call(&panel, PANEL_OP_PREPARE, 0);
	expect(status == -EIO, "short write fails prepare with EIO");
	expect(!panel.prepared && !panel.enabled && !panel.error, "failed prepare clears state");
	expect(strstr(trace, "wD517f") == NULL, "writes stop after the first error");
	expect(strstr(trace, "g2=0 g1=0 U") != NULL, "failed prepare powers rails down");
	expect(strstr(trace, "T") == NULL, "failed prepare skips touch reset");

	reset(&panel);
	panel.fail_write_at = 0;
	panel.fail_status = -ETIMEDOUT;
	status = mindone_panel_call(&panel, PANEL_OP_PREPARE, 0);
	expect(status == -ETIMEDOUT, "write errno is returned from prepare");

	reset(&panel);
	panel.has_gpio[2] = 0;
	panel.fail_write_at = 0;
	panel.fail_status = -EIO;
	mindone_panel_call(&panel, PANEL_OP_PREPARE, 0);
	expect(strstr(trace, "g2=0") == NULL && strstr(trace, "g1=0") != NULL, "power down skips absent GPIOs");

	reset(&panel);
	status = mindone_panel_call(&panel, PANEL_OP_UNPREPARE, 0);
	expect(status == 0 && !strcmp(trace, "LU"), "unprepare of an unprepared panel is a no-op");

	reset(&panel);
	panel.prepared = 1;
	status = mindone_panel_call(&panel, PANEL_OP_UNPREPARE, 0);
	expect(status == 0 && !strcmp(trace, "LwD28 s20 wD10 s120 wD4f01 g2=0 g1=0 U"), "unprepare sequence");
	expect(!panel.prepared && !panel.error, "unprepare clears state");

	reset(&panel);
	panel.prepared = 1;
	panel.fail_write_at = 1;
	panel.fail_status = -EIO;
	status = mindone_panel_call(&panel, PANEL_OP_UNPREPARE, 0);
	expect(status == 0 && strstr(trace, "C-5 ") != NULL && !panel.error, "unprepare logs and clears a write error");
	expect(strstr(trace, "wD4f01") == NULL, "unprepare stops writing after an error");

	reset(&panel);
	status = mindone_panel_call(&panel, PANEL_OP_ENABLE, 0);
	expect(status == 0 && panel.enabled && !strcmp(trace, "Lb1 U"), "enable turns the backlight on");
	trace[0] = 0;
	status = mindone_panel_call(&panel, PANEL_OP_ENABLE, 0);
	expect(status == 0 && !strcmp(trace, "LU"), "enable of an enabled panel is a no-op");

	reset(&panel);
	panel.backlight_status = 3;
	status = mindone_panel_call(&panel, PANEL_OP_ENABLE, 0);
	expect(status == -EIO && !panel.enabled, "positive backlight status maps to EIO");
	panel.backlight_status = -ENODEV;
	status = mindone_panel_call(&panel, PANEL_OP_ENABLE, 0);
	expect(status == -ENODEV && !panel.enabled, "backlight errno is returned");

	reset(&panel);
	panel.enabled = 1;
	panel.backlight_status = -EBUSY;
	status = mindone_panel_call(&panel, PANEL_OP_DISABLE, 0);
	expect(status == -EBUSY && panel.enabled, "failed disable keeps enabled state");
	panel.backlight_status = 0;
	status = mindone_panel_call(&panel, PANEL_OP_DISABLE, 0);
	expect(status == 0 && !panel.enabled, "disable turns the backlight off");

	reset(&panel);
	expect(mindone_panel_call(&panel, PANEL_OP_RESET, 0) == 0 && !strcmp(trace, "Lg0=0 U"), "reset drives GPIO 0");
	expect(mindone_panel_call(&panel, PANEL_OP_ATA_CHECK, 0) == -EOPNOTSUPP, "ATA check is unsupported");
	expect(mindone_panel_call(&panel, 99, 0) == -EINVAL, "unknown operation is rejected");
	expect(mindone_panel_call(NULL, PANEL_OP_PREPARE, 0) == -EINVAL, "NULL context is rejected");

	reset(&panel);
	mindone_panel_call(&panel, PANEL_OP_SUSPEND_OBSERVE, 0);
	expect(!strcmp(trace, "LU"), "idle panel is not logged on suspend");
	mindone_panel_call(&panel, PANEL_OP_BOOT_STATE, 0);
	expect(panel.prepared && panel.enabled, "boot state marks the panel live");
	trace[0] = 0;
	mindone_panel_call(&panel, PANEL_OP_SUSPEND_OBSERVE, 0);
	expect(!strcmp(trace, "LP11 U"), "live panel is logged on suspend");

	reset(&panel);
	expect(mindone_panel_brightness(&panel, 300) == 0 && !strcmp(trace, "lE255 u"), "brightness clamps and emits");
	trace[0] = 0;
	panel.cmdq_available = 0;
	expect(mindone_panel_brightness(&panel, 10) == -1 && !strcmp(trace, "lu"), "unavailable queue does not emit");
	expect(mindone_panel_brightness(NULL, 10) == -EINVAL, "NULL brightness context is rejected");
	expect(command_depth == 0, "command lock is balanced");

	printf("%s %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
