/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define bm_debug(...) ((void)0)
#define bm_err(...) ((void)0)
struct device_node { int unused; };
struct platform_device { struct { struct device_node *of_node; } dev; };
struct fuel_gauge_custom_data { bool disable_nafg; };
struct fuel_gauge_table_custom_data { int unused; };
struct mtk_battery {
    int battery_id, ptim_lk_v, ptim_lk_i, pl_shutdown_time;
    struct fuel_gauge_custom_data fg_cust_data;
    struct fuel_gauge_table_custom_data fg_table_cust_data;
};
static const char *names[] = {"fg_swocv_v", "fg_swocv_i", "shutdown_time", "MULTI_BATTERY", "ACTIVE_TABLE"};
static int errors[5], reads[5], observed_multi, observed_active;
static unsigned int values[5];
static int fgauge_get_profile_id(void) { return 0; }
static bool of_property_read_bool(const struct device_node *np, const char *name) { return false; }
static int of_property_read_u32(const struct device_node *np, const char *name, unsigned int *value)
{
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, names[i])) continue;
        reads[i]++;
        if (errors[i]) return errors[i];
        *value = values[i];
        return 0;
    }
    assert(false);
    return -EINVAL;
}
