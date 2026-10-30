/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define smp_load_acquire(pointer) (*(pointer))
#define smp_store_release(pointer, value) (*(pointer) = (value))
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define bm_err(...) mock_log(__VA_ARGS__)
#define bm_debug(...) mock_log(__VA_ARGS__)
#define IRQ_HANDLED 1
#define FG_INTR_FG_ZCV 8192
#define EVT_INT_ZCV 7

typedef int irqreturn_t;

enum {
	GAUGE_PROP_COULOMB,
	GAUGE_PROP_ZCV_CURRENT,
	GAUGE_PROP_ZCV,
	GAUGE_PROP_ZCV_INTR_EN,
	GAUGE_PROP_COUNT,
};

struct mutex {
	bool held;
};

struct mtk_battery {
	bool disableGM30;
	struct { bool active; } algo;
	struct { int sleep_current_avg; } fg_cust_data;
	struct mutex algo_lock;
};

static int values[GAUGE_PROP_COUNT];
static int errors[GAUGE_PROP_COUNT];
static int reads[GAUGE_PROP_COUNT];
static int kernel_events;
static int daemon_events;
static int disable_writes;
static int disable_all_calls;
static int event_reports;
static int plug_checks;

static void mock_log(const char *format, ...)
{
}

static void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	lock->held = true;
}

static void mutex_unlock(struct mutex *lock)
{
	assert(lock->held);
	lock->held = false;
}

static int gauge_get_property(int property, int *value)
{
	assert(property >= 0 && property < GAUGE_PROP_COUNT);
	reads[property]++;
	if (errors[property])
		return errors[property];
	*value = values[property];
	return 0;
}

int gauge_get_int_property(int property)
{
	int value = 0;
	gauge_get_property(property, &value);
	return value;
}

static int gauge_set_property(int property, int value)
{
	assert(property == GAUGE_PROP_ZCV_INTR_EN && value == 0);
	disable_writes++;
	return 0;
}

static void disable_fg(struct mtk_battery *gm)
{
	disable_all_calls++;
}

static void do_fg_algo(struct mtk_battery *gm, unsigned int flow_state, int cmd, int para1)
{
	assert(gm->algo.active && gm->algo_lock.held);
	assert(flow_state == FG_INTR_FG_ZCV && cmd == 0 && para1 == 0);
	kernel_events++;
}

static int wakeup_fg_daemon(unsigned int flow_state, int cmd, int para1)
{
	assert(flow_state == FG_INTR_FG_ZCV && cmd == 0 && para1 == 0);
	daemon_events++;
	return 0;
}

static void fg_int_event(struct mtk_battery *gm, int event)
{
	assert(event == EVT_INT_ZCV && !gm->algo_lock.held);
	event_reports++;
}

static void sw_check_bat_plugout(struct mtk_battery *gm)
{
	assert(!gm->algo_lock.held);
	plug_checks++;
}
