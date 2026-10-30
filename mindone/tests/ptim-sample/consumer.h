/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
struct mtk_gauge { int unused; };
struct mtk_battery_algo { int last_temp, fg_bat_tmp_c_gap; };
struct fuel_gauge_table_custom_data { int temperature_tb1; };
struct fuel_gauge_custom_data { int qmax_sel; };
struct mtk_battery {
    struct fuel_gauge_custom_data fg_cust_data;
    struct mutex algo_lock;
    struct mtk_gauge *gauge;
    struct mtk_battery_algo algo;
    struct fuel_gauge_table_custom_data fg_table_cust_data;
};
enum { GAUGE_PROP_PTIM_RESIST };
static int temperature_error, resistance_error, pair_error, resistance_value;
static int temperature_calls, resistance_calls, pair_calls, model_calls, end_calls;
static int last_voltage, last_current, last_temperature, last_rac;
static int force_get_tbat_internal(struct mtk_battery *gm, bool update, int *value)
{
    CHECK(gm->algo_lock.held && update);
    temperature_calls++;
    if (temperature_error)
        return temperature_error;
    *value = 23;
    return 0;
}
static int gauge_get_property(int property, int *value)
{
    CHECK(property == GAUGE_PROP_PTIM_RESIST);
    resistance_calls++;
    if (resistance_error)
        return resistance_error;
    *value = resistance_value;
    return 0;
}
static int gauge_read_ptim_sample(struct mtk_gauge *gauge, struct gauge_ptim_sample *sample)
{
    pair_calls++;
    if (pair_error)
        return pair_error;
    *sample = (struct gauge_ptim_sample){ .voltage_01mv = 41190, .current_01ma = -123 };
    return 0;
}
static void fgr_construct_battery_profile(struct mtk_battery *gm, int table) { CHECK(gm->algo.last_temp == 23); model_calls++; }
static void fgr_construct_vboot_sample(struct mtk_battery *gm, int table, int temperature, int resistance, int voltage, int current_value)
{
    model_calls++;
    last_temperature = temperature;
    last_rac = resistance;
    last_voltage = voltage;
    last_current = current_value;
}
static void fgr_update_c_dod(struct mtk_battery *gm) { model_calls++; }
static void fgr_set_soc_by_vc_mode(struct mtk_battery *gm) { model_calls++; }
static void set_fg_bat_tmp_c_gap(int gap) { CHECK(gap == 1); model_calls++; }
static void fgr_int_end_flow(struct mtk_battery *gm, int interrupt) { end_calls++; }
static int unsupported(void) { CHECK(false); return 0; }
