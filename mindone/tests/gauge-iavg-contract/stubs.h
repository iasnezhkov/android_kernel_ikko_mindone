/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GAUGE_HW_V1000 1000
#define GAUGE_HW_V2000 2000
#define GAUGE_PROP_HW_VERSION 6
#define GAUGE_PROP_AVERAGE_CURRENT 29
#define FG_DAEMON_CMD_GET_FG_CURRENT_AVG 36
#define FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID 73
#define bm_debug(...) ((void)0)

enum gauge_property { GAUGE_UNUSED };
struct mtk_gauge { struct { int current_avg_valid; int current_avg; } fg_hw_info; };
struct mtk_battery { struct mtk_gauge *gauge; bool disableGM30; int sw_iavg; };
struct reply { unsigned char fgd_data[4]; int fgd_data_len; };
struct step { int error; int value; int valid; };

static struct mtk_gauge gauge;
static struct mtk_battery battery;
static struct step steps[4];
static int next_step;
static int version;

static int gauge_get_property(enum gauge_property property, int *value)
{
	if ((int)property == GAUGE_PROP_HW_VERSION) {
		*value = version;
		return 0;
	}
	assert((int)property == GAUGE_PROP_AVERAGE_CURRENT && next_step < 4);
	struct step step = steps[next_step++];
	if (step.error)
		return step.error;
	*value = step.value;
	gauge.fg_hw_info.current_avg = step.value;
	gauge.fg_hw_info.current_avg_valid = step.valid;
	return 0;
}
