#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNIT_TRANS_10 10
#define UNIT_TRANS_100 100
#define READ_ONCE(value) (value)
#define bm_debug(...) mock_log(__VA_ARGS__)
#define bm_err(...) mock_log(__VA_ARGS__)
#define NSEC_PER_SEC INT64_C(1000000000)
#define lockdep_assert_held(lock) assert((lock)->held)

typedef int64_t ktime_t;

struct timespec64 {
	int64_t tv_sec;
	long tv_nsec;
};

struct alarm {
	ktime_t deadline;
	bool active;
	int starts;
	int cancels;
};

struct mutex {
	bool held;
};

struct shutdown_controller {
	struct alarm kthread_fgtimer;
};

struct mtk_battery_algo {
	int ui_soc;
	int low_tracking_enable;
	int fg_c_d0_ocv;
	int fg_c_d0_dod;
	int fg_c_d0_soc;
	int fg_c_d0_car;
	int fg_c_dod;
	int fg_c_soc;
	int car;
	int quse_tb1;
	int rtc_ui_soc;
	int ui_d0_soc;
	int soc;
	int sw_cycle_car;
	int sw_cycle_ncar;
	bool sw_cycle_valid;
	int zcv_learn_car;
	int zcv_learn_soc;
	bool zcv_learn_valid;
	int qmax_t_0ma_tb1;
	int aging_factor;
};

struct fuel_gauge_table_custom_data {
	int unused;
};

struct fuel_gauge_custom_data {
	int d0_sel;
	int shutdown_system_iboot;
	int discharge_tracking_time;
	int sleep_current_avg;
};

struct mtk_battery {
	struct mutex algo_lock;
	bool disableGM30;
	struct shutdown_controller sdc;
	int ui_soc;
	int imix;
	int bat_cycle;
	int model_capacity_mah;
	struct mtk_battery_algo algo;
	struct fuel_gauge_custom_data fg_cust_data;
	struct fuel_gauge_table_custom_data fg_table_cust_data;
	struct alarm tracking_timer;
};

enum {
	GAUGE_PROP_COULOMB,
	GAUGE_PROP_CON0_SOC,
	GAUGE_PROP_RTC_UI_SOC,
	GAUGE_PROP_PTIM_BATTERY_VOLTAGE,
	GAUGE_PROP_ZCV,
	GAUGE_PROP_ZCV_CURRENT,
	GAUGE_PROP_COUNT,
};

enum {
	BAT_PROP_FG_RESET,
	BAT_PROP_ENABLE_UISOC_HT_INT,
	BAT_PROP_ENABLE_UISOC_LT_INT,
	BAT_PROP_UISOC,
	BAT_PROP_COUNT,
};

static struct mtk_battery *active_gm;
static int gauge_values[GAUGE_PROP_COUNT];
static int gauge_errors[GAUGE_PROP_COUNT];
static int gauge_reads[GAUGE_PROP_COUNT];
static int battery_values[BAT_PROP_COUNT];
static int reset_calls;
static int reset_error;
static int read_error;
static bool charger_present;
static ktime_t clock_now;
static int shutdown_seconds;

static void mock_log(const char *format, ...)
{
}

static int shutdown_event_handler(struct mtk_battery *gm)
{
	return shutdown_seconds;
}

static ktime_t ktime_get_boottime(void)
{
	return clock_now;
}

struct timespec64 ktime_to_timespec64(ktime_t value)
{
	return (struct timespec64){ value / NSEC_PER_SEC, value % NSEC_PER_SEC };
}

static ktime_t ktime_set(int64_t seconds, long nanoseconds)
{
	assert(nanoseconds >= 0 && nanoseconds < NSEC_PER_SEC);
	return seconds * NSEC_PER_SEC + nanoseconds;
}

ktime_t ktime_add(ktime_t first, ktime_t second)
{
	return first + second;
}

static void alarm_start(struct alarm *alarm, ktime_t deadline)
{
	alarm->active = true;
	alarm->deadline = deadline;
	alarm->starts++;
}

static void alarm_cancel(struct alarm *alarm)
{
	alarm->active = false;
	alarm->cancels++;
}

int gauge_get_property(int property, int *value);

static int gauge_get_int_property(int property)
{
	int value = 0;
	gauge_get_property(property, &value);
	return value;
}

int gauge_get_property(int property, int *value)
{
	assert(property >= 0 && property < GAUGE_PROP_COUNT);
	gauge_reads[property]++;
	if (read_error)
		return read_error;
	if (gauge_errors[property])
		return gauge_errors[property];
	*value = gauge_values[property];
	return 0;
}

static int battery_set_property(int property, int value)
{
	assert(property >= 0 && property < BAT_PROP_COUNT);
	battery_values[property] = value;
	if (property == BAT_PROP_UISOC)
		active_gm->ui_soc = (value + 50) / 100;
	if (property == BAT_PROP_FG_RESET) {
		reset_calls++;
		if (reset_error)
			return reset_error;
		gauge_values[GAUGE_PROP_COULOMB] = 0;
	}
	return 0;
}

int battery_reset_charge_locked(struct mtk_battery *gm)
{
	lockdep_assert_held(&gm->algo_lock);
	assert(active_gm == gm);
	return battery_set_property(BAT_PROP_FG_RESET, 0);
}

int get_ptim_vbat(void)
{
	return gauge_get_int_property(GAUGE_PROP_PTIM_BATTERY_VOLTAGE) * 10;
}

static int OCV_to_SOC_c(struct mtk_battery *gm, int voltage)
{
	assert(voltage >= 34000 && voltage <= 44000);
	return voltage - 34000;
}

static int SOC_to_OCV_c(struct mtk_battery *gm, int soc)
{
	assert(soc >= 0 && soc <= 10000);
	return soc + 34000;
}

static int OCV_to_DOD_c(struct mtk_battery *gm, int voltage)
{
	return 10000 - OCV_to_SOC_c(gm, voltage);
}

static void fgr_update_quse(struct mtk_battery *gm, int table)
{
	assert(table == 1);
	assert(gm->algo.quse_tb1 > 0);
}

static int get_charger_exist(struct mtk_battery *gm)
{
	return charger_present;
}

static int mtk_battery_get_learned_q_max(struct mtk_battery *gm)
{
	return gm->model_capacity_mah;
}

int battery_reset_charge_init_locked(struct mtk_battery *gm)
{
	int car;
	int ret = gauge_get_property(GAUGE_PROP_COULOMB, &car);

	if (ret < 0)
		return ret;
	return battery_reset_charge_locked(gm);
}
