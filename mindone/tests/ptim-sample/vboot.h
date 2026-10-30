/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
struct mtk_battery_algo {
    int vboot, vboot_c, T_table, T_table_c;
    int qmax_t_0ma_h, qmax_t_0ma_h_tb1, qmax_t_aging, aging_factor;
};
struct fuel_gauge_custom_data { int qmax_sel, iboot_sel, shutdown_system_iboot; };
struct fuel_gauge_table_custom_data {
    int temperature_tb0, temperature_tb1;
    struct { int pon_iboot, pmic_min_vol; } fg_profile[1];
};
struct mtk_gauge { int unused; };
struct mtk_battery {
    struct mutex algo_lock;
    struct mtk_gauge *gauge;
    struct mtk_battery_algo algo;
    struct fuel_gauge_custom_data fg_cust_data;
    struct fuel_gauge_table_custom_data fg_table_cust_data;
};
enum { GAUGE_PROP_PTIM_RESIST };
static int resistance_error = -ENODATA;
static int force_get_tbat_internal(struct mtk_battery *gm, bool update, int *value)
{
    *value = -5;
    return 0;
}
static int gauge_get_property(int property, int *value)
{
    CHECK(property == GAUGE_PROP_PTIM_RESIST);
    if (resistance_error)
        return resistance_error;
    *value = 117;
    return 0;
}
static int gauge_read_ptim_sample(struct mtk_gauge *gauge, struct gauge_ptim_sample *sample)
{
    *sample = (struct gauge_ptim_sample){.voltage_01mv = 41190, .current_01ma = -123};
    return 0;
}
static int seeds[2], seed_count, compensation_count, qmax_count, notice_count;
static int notice_temperature, notice_vboot, notice_iboot;
static void fg_construct_battery_profile_by_qmax(struct mtk_battery *gm, int qmax, int table)
{
    qmax_count++;
}
static void fg_construct_battery_profile_by_vboot(struct mtk_battery *gm, int vboot, int table)
{
    CHECK(seed_count < 2);
    seeds[seed_count++] = vboot;
}
static int fg_compensate_battery_voltage_from_low(struct mtk_battery *gm, int voltage, int current_value, int table)
{
    CHECK(seed_count == 1 && voltage == 34000 && current_value == -5000);
    compensation_count++;
    return table == 255 ? 34592 : 34600;
}
static void notice(const char *format, const char *function_name, int temperature, int vboot, int iboot)
{
    CHECK(strstr(format, "source=profile") && strcmp(function_name, "fgr_construct_vboot_sample") == 0);
    notice_count++;
    notice_temperature = temperature;
    notice_vboot = vboot;
    notice_iboot = iboot;
}
#define pr_notice_once(...) do { static bool printed; if (!printed) { printed = true; notice(__VA_ARGS__); } } while (0)
