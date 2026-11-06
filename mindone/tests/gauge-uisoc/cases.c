// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(int argc, char **argv)
{
    assert(argc == 7);
    int ui = atoi(argv[1]);
    struct mtk_battery gm = {
        .algo = { .quse_tb1 = atoi(argv[2]), .soc = atoi(argv[3]), .ui_soc = ui },
        .fg_cust_data = { .diff_soc_setting = atoi(argv[4]) }
    };
    fgr_update_uisoc_threshold(&gm);
    assert(gm.algo.car == 74 && gm.algo.ht_gap >= CAR_MIN_GAP);
    assert(gm.algo.lt_gap >= CAR_MIN_GAP);
    assert(gm.algo.ht_gap == atoi(argv[5]));
    assert(gm.algo.lt_gap == atoi(argv[6]));
    return 0;
}
