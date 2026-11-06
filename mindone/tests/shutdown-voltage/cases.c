// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

@DEFINES@
#define CONFIG_BATTERY_SM5602 0
#define IS_ENABLED(x) (x)
#define IS_ERR(x) ((x) == NULL)
#define bm_debug(...) ((void)0)
#define bm_err(...) ((void)0)
typedef long long ktime_t;
struct timespec64 { long long tv_sec, tv_nsec; };
enum { SOC_ZERO_PERCENT, UISOC_ONE_PERCENT, DLPT_SHUTDOWN, LOW_BAT_VOLT, SHUTDOWN_FACTOR_MAX };
enum gauge_property { GAUGE_PROP_BATTERY_VOLTAGE, GAUGE_PROP_BATTERY_CURRENT, FG_INTR_SHUTDOWN };
struct shutdown_condition {
    bool is_soc_zero_percent, is_uisoc_one_percent, is_dlpt_shutdown, is_under_shutdown_voltage;
};
struct shutdown_controller {
    struct shutdown_condition shutdown_status;
    ktime_t pre_time[SHUTDOWN_FACTOR_MAX];
    int avgvbat, batdata[AVGVBAT_ARRAY_SIZE], batidx, vbat_lt, vbat_lt_lv1;
    bool lowbatteryshutdown;
};
struct mtk_battery { int ui_soc, soc; bool disableGM30; struct shutdown_controller sdc; };
struct mtk_gauge { void *chan_bat_voltage; };
struct mtk_gauge_sysfs_field_info { int unused; };
static ktime_t clock_now;
static int adc_ret, adc_value, adc_calls, powers, notifications, temperature = 25;
static bool adc_store;
static unsigned checks, failures;
static ktime_t ktime_get_boottime(void) { return clock_now; }
static ktime_t ktime_sub(ktime_t a, ktime_t b) { return a - b; }
static struct timespec64 ktime_to_timespec64(ktime_t a) { return (struct timespec64){ a, 0 }; }
static void kernel_power_off(void) { powers++; }
static void wakeup_fg_algo(struct mtk_battery *gm, int type) { notifications++; }
static int force_get_tbat(struct mtk_battery *gm, bool update) { return temperature; }
static int iio_read_channel_processed(void *channel, int *value)
{
    adc_calls++;
    if (adc_store) *value = adc_value;
    return adc_ret;
}

@PROVIDER@

static int gauge_get_property(int prop, int *val)
{
    if (prop == GAUGE_PROP_BATTERY_CURRENT) {
        *val = -100;
        return 0;
    }
    struct mtk_gauge gauge = { (void *)1 };
    return bat_vol_get(&gauge, NULL, val);
}

@FUNCTIONS@

static void check(bool result, const char *name)
{
    checks++;
    if (!result) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

static struct mtk_battery fresh(void)
{
    struct mtk_battery gm = { .ui_soc = 50, .soc = 50 };
    gm.sdc.shutdown_status.is_under_shutdown_voltage = true;
    gm.sdc.avgvbat = 4000;
    for (int i = 0; i < AVGVBAT_ARRAY_SIZE; i++) gm.sdc.batdata[i] = 4000;
    adc_ret = 0;
    adc_value = 4000;
    adc_store = true;
    clock_now = 100;
    shutdown_event_handler(&gm);
    powers = notifications = adc_calls = 0;
    return gm;
}

int main(void)
{
    const int errors[] = { -EIO, -ETIMEDOUT, -EOPNOTSUPP, -ENODEV };
    for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); e++) {
        for (unsigned store = 0; store < 2; store++) {
            struct mtk_battery gm = fresh();
            struct shutdown_controller before = gm.sdc;
            adc_ret = errors[e];
            adc_store = store;
            adc_value = 3100;
            for (int i = 0; i < 2 * AVGVBAT_ARRAY_SIZE; i++) {
                clock_now += 10;
                check(shutdown_event_handler(&gm) == 10, "fault retains polling");
            }
            check(memcmp(&before, &gm.sdc, sizeof(before)) == 0, "fault preserves voltage history and state");
            check(notifications == 0 && powers == 0, "fault never invents low voltage");
            check(adc_calls == 2 * AVGVBAT_ARRAY_SIZE, "fault retries fresh ADC");
            adc_ret = 0;
            adc_store = true;
            adc_value = 3970;
            check(shutdown_event_handler(&gm) == 10, "recovery polling");
            check(gm.sdc.avgvbat == 3999 && gm.sdc.batdata[before.batidx] == 3970,
                  "recovery inserts one real sample");
        }
    }
    for (int invalid = 0; invalid >= -1; invalid--) {
        struct mtk_battery gm = fresh();
        struct shutdown_controller before = gm.sdc;
        adc_value = invalid;
        check(shutdown_event_handler(&gm) == 10, "nonpositive keeps polling");
        check(memcmp(&before, &gm.sdc, sizeof(before)) == 0, "nonpositive not a voltage sample");
    }
    struct mtk_battery gm = fresh();
    gm.ui_soc = 0;
    adc_value = 3300;
    for (int i = 0; i < AVGVBAT_ARRAY_SIZE; i++) shutdown_event_handler(&gm);
    check(gm.sdc.lowbatteryshutdown && notifications == 1 && powers == 0,
          "real low voltage starts original shutdown grace");
    struct shutdown_controller before = gm.sdc;
    adc_ret = -EIO;
    adc_store = false;
    clock_now += SHUTDOWN_TIME;
    check(shutdown_event_handler(&gm) == 10 && powers == 0, "failed read cannot complete voltage shutdown");
    check(memcmp(&before, &gm.sdc, sizeof(before)) == 0, "failed read preserves pending low state");
    adc_ret = 0;
    adc_store = true;
    shutdown_event_handler(&gm);
    check(powers == 1, "fresh real low confirms original timed shutdown");
    gm = fresh();
    gm.ui_soc = 0;
    gm.sdc.shutdown_status.is_soc_zero_percent = true;
    gm.sdc.pre_time[SOC_ZERO_PERCENT] = clock_now - SHUTDOWN_TIME;
    adc_ret = -EIO;
    shutdown_event_handler(&gm);
    check(powers == 1 && adc_calls == 0, "independent SOC shutdown unchanged");
    gm = fresh();
    gm.disableGM30 = true;
    adc_ret = -EIO;
    shutdown_event_handler(&gm);
    check(adc_calls == 0 && gm.sdc.avgvbat == 4000, "disabled gauge behavior unchanged");
    gm = fresh();
    gm.sdc.shutdown_status.is_under_shutdown_voltage = false;
    check(shutdown_event_handler(&gm) == 0 && adc_calls == 0, "inactive monitor stays inactive");
    gm = fresh();
    temperature = LOW_TEMP_THRESHOLD - 1;
    adc_value = LOW_TMP_BAT_VOLTAGE_LOW_BOUND + 1;
    for (int i = 0; i < AVGVBAT_ARRAY_SIZE; i++) shutdown_event_handler(&gm);
    check(notifications == 0, "original cold threshold retained");
    adc_value = LOW_TMP_BAT_VOLTAGE_LOW_BOUND;
    for (int i = 0; i < AVGVBAT_ARRAY_SIZE; i++) shutdown_event_handler(&gm);
    check(notifications == 1, "original cold shutdown retained");
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
