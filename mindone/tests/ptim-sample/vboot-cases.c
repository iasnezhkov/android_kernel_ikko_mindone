// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(int argc, char **argv)
{
    int table = argc > 1 ? atoi(argv[1]) : 255;
    struct mtk_battery gm = {
        .algo_lock = {.held = true},
        .algo = {.vboot = -1, .vboot_c = -2},
        .fg_cust_data = {.qmax_sel = 1},
        .fg_table_cust_data = {
            .temperature_tb0 = 255, .temperature_tb1 = 254,
            .fg_profile = {{.pon_iboot = 5000, .pmic_min_vol = 34000}},
        },
    };
    struct fgr_vboot_sample sample;
    CHECK(fgr_read_vboot_sample(&gm, &sample) == 0 && sample.rac == 0);
    fgr_construct_vboot_sample(&gm, table, sample.temperature, sample.rac,
        sample.ptim.voltage_01mv, sample.ptim.current_01ma);
    int final_vboot = table == 255 ? 34592 : 34600;
    CHECK(seed_count == 2 && seeds[0] == 34000 && seeds[1] == final_vboot);
    CHECK(compensation_count == 1 && notice_count == 1);
    CHECK(notice_temperature == -5 && notice_vboot == final_vboot && notice_iboot == 5000);
    seed_count = 0;
    resistance_error = 0;
    CHECK(fgr_read_vboot_sample(&gm, &sample) == 0 && sample.rac == 117);
    fgr_construct_vboot_sample(&gm, table, sample.temperature, sample.rac,
        sample.ptim.voltage_01mv, sample.ptim.current_01ma);
    CHECK(seed_count == 2 && seeds[0] == 34058 && seeds[1] == final_vboot);
    CHECK(compensation_count == 2 && notice_count == 1);
    seed_count = 0;
    fgr_construct_vboot_sample(&gm, table, 24, 0, 41190, -123);
    CHECK(seeds[0] == 34000 && notice_count == 1);
    gm.fg_cust_data.qmax_sel = 0;
    fgr_construct_vboot_sample(&gm, table, 24, 117, 41190, -123);
    CHECK(gm.algo.vboot == 34058 && qmax_count == 1);
    cases++;
    finish();
    return 0;
}
