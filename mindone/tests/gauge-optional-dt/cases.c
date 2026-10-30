// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(void)
{
    struct platform_device dev = {0};
    const int missing[] = {-EINVAL, -ENODATA, -EOVERFLOW};
    for (int error = 0; error < 3; error++) {
        for (int mask = 0; mask < 32; mask++) {
            struct mtk_battery gm = {0};
            memset(reads, 0, sizeof(reads));
            for (int i = 0; i < 5; i++) {
                errors[i] = mask & (1 << i) ? 0 : missing[error];
                values[i] = (unsigned int)(i == 1 ? -315 : 401 + i);
            }
            fg_custom_init_from_dts(&dev, &gm);
            assert(gm.ptim_lk_v == (errors[0] ? 0 : 401));
            assert(gm.ptim_lk_i == (errors[1] ? 0 : -315));
            assert(gm.pl_shutdown_time == (errors[2] ? 0 : 403));
            assert(observed_multi == (errors[3] ? MULTI_BATTERY : 404));
            assert(observed_active == (errors[4] ? ACTIVE_TABLE : 405));
            for (int i = 0; i < 5; i++) assert(reads[i] == 1);
            gm.ptim_lk_v = 39000;
            gm.ptim_lk_i = -550;
            gm.pl_shutdown_time = 77;
            memset(reads, 0, sizeof(reads));
            fg_custom_init_from_dts(&dev, &gm);
            assert(gm.ptim_lk_v == 39000 && gm.ptim_lk_i == -550 && gm.pl_shutdown_time == 77);
            for (int i = 0; i < 3; i++) assert(reads[i] == 0);
        }
    }
    memset(errors, 0, sizeof(errors));
    memset(values, 0, sizeof(values));
    struct mtk_battery gm = {0};
    fg_custom_init_from_dts(&dev, &gm);
    assert(gm.ptim_lk_v == 0 && gm.ptim_lk_i == 0 && gm.pl_shutdown_time == 0);
    assert(observed_multi == 0 && observed_active == 0);
    const int seed_values[] = {43731, -11, 1800};
    const int healthy[][5] = {
        {43988, -3, 0, 0, 6},
        {42000, 0, 10, 1, 4},
        {41000, 15, 7200, 0, 0},
    };
    for (size_t scenario = 0; scenario < 3; scenario++) {
        for (size_t error = 0; error < 3; error++) {
            for (unsigned mask = 0; mask < 32; mask++) {
                for (unsigned seeds = 0; seeds < 8; seeds++) {
                    struct mtk_battery mixed = {0};
                    int *fields[] = {&mixed.ptim_lk_v, &mixed.ptim_lk_i, &mixed.pl_shutdown_time};
                    memset(reads, 0, sizeof(reads));
                    for (size_t i = 0; i < 5; i++) {
                        values[i] = (unsigned int)healthy[scenario][i];
                        errors[i] = mask & (1u << i) ? missing[error] : 0;
                    }
                    for (size_t i = 0; i < 3; i++)
                        *fields[i] = seeds & (1u << i) ? seed_values[i] : 0;
                    fg_custom_init_from_dts(&dev, &mixed);
                    for (size_t i = 0; i < 3; i++) {
                        const int expected = seeds & (1u << i) ? seed_values[i] :
                                             errors[i] ? 0 : healthy[scenario][i];
                        assert(*fields[i] == expected);
                        assert(reads[i] == !(seeds & (1u << i)));
                    }
                    assert(observed_multi == (errors[3] ? MULTI_BATTERY : healthy[scenario][3]));
                    assert(observed_active == (errors[4] ? ACTIVE_TABLE : healthy[scenario][4]));
                }
            }
        }
    }
    puts("PASS 96 optional-property combinations and 2304 mixed-seed cases with 18432 independent assertions");
    return 0;
}
