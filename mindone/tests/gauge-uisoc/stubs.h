#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define GAUGE_PROP_COULOMB 1
#define bm_debug(...) mock_log(__VA_ARGS__)
struct mtk_battery_algo { int car, quse_tb1, soc, ui_soc, ht_gap, lt_gap; };
struct mtk_battery {
    struct mtk_battery_algo algo;
    struct { int diff_soc_setting; } fg_cust_data;
};
static int gauge_get_int_property(int property) { return 74; }
static void fgr_update_quse(struct mtk_battery *gm, int table) {}
static void mock_log(const char *format, ...) {}
