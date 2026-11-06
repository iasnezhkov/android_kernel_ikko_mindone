/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed at %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define HZ 100UL
#define time_before(a, b) ((long)((a) - (b)) < 0)
#define PTR_ERR_OR_ZERO(p) ((p) ? 0 : -ENODEV)
#define pr_notice(...) do { notices++; } while (0)
#define pr_notice_once(...) do { if (!notice_seen) { notice_seen = true; notices++; } } while (0)
#define pr_debug(...) do { } while (0)
#define pr_info(...) do { } while (0)
#define wait_event_interruptible(q, condition) do { CHECK(condition); } while (0)

struct mutex { int held; };
struct timer_list { int unused; };
struct wait_queue_head { int unused; };
struct iio_channel { int unused; };
struct device { int unused; };
enum LOW_BATTERY_LEVEL_TAG { LOW_BATTERY_LEVEL_0 };

@CONSTANTS@
@STRUCTURE@

static struct dlpt_priv dlpt;
static struct iio_channel channel;
static unsigned long jiffies, timer_at;
static int reads, notices, computations, callbacks, wakes, skips;
static int read_ret, read_value, gauge_present, charging, ui_soc;
static bool notice_seen;

static void mutex_lock(struct mutex *lock)
{
	CHECK(!lock->held);
	lock->held = 1;
}

static void mutex_unlock(struct mutex *lock)
{
	CHECK(lock->held);
	lock->held = 0;
}

static int iio_read_channel_raw(struct iio_channel *chan, int *value)
{
	CHECK(chan == &channel);
	CHECK(dlpt.notify_lock.held);
	CHECK(!dlpt.suspend_flag);
	reads++;
	*value = read_value;
	return read_ret;
}

static int dlpt_get_uisoc(void) { return ui_soc; }
static void __pm_stay_awake(struct wakeup_source *ws) { }
static void __pm_relax(struct wakeup_source *ws) { }
static int get_mtk_gauge_psy(void) { return gauge_present; }
static int dlpt_get_rgs_chrdet(void) { return charging; }
static int get_dlpt_imix(void)
{
	CHECK(dlpt.notify_lock.held);
	CHECK(dlpt.imix_r > 0);
	computations++;
	return 1234;
}
static int get_dlpt_imix_charging(void) { return get_dlpt_imix(); }
static void dlpt_update_imix(int value) { CHECK(value == 1234); }
static void exec_dlpt_callback(int value) { CHECK(value == 1234); callbacks++; }
static int dlpt_check_power_off(void) { skips++; return 0; }
static void dlpt_set_shutdown_condition(void) { CHECK(false); }
static void mod_timer(struct timer_list *timer, unsigned long at) { timer_at = at; }
static int kthread_should_stop(void) { return 1; }
static void wake_up_interruptible(struct wait_queue_head *queue) { wakes++; }

@FUNCTIONS@
