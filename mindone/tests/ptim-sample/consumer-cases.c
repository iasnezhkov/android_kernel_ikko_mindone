// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
    temperature_error = resistance_error = pair_error = 0;
    temperature_calls = resistance_calls = pair_calls = model_calls = end_calls = 0;
    last_voltage = last_current = last_temperature = last_rac = 0;
    resistance_value = 90;
}

int main(void)
{
    const unsigned int events[] = {FG_INTR_BAT_TMP_C_HT, FG_INTR_BAT_TMP_C_LT};
    const int errors[] = {-EPERM, -EIO, -ENODEV, -ETIMEDOUT, -EOPNOTSUPP};
    for (unsigned int event = 0; event < ARRAY_SIZE(events); event++) {
        for (int selection = 0; selection <= 2; selection++) {
            for (int failure = 0; failure < 19; failure++) {
                struct mtk_battery gm = { .algo_lock = {.held = true}, .algo = { .last_temp = 17, .fg_bat_tmp_c_gap = 4 }, .fg_cust_data = { .qmax_sel = selection } };
                reset();
                if (failure >= 1 && failure <= 5) temperature_error = errors[failure - 1];
                if (failure >= 6 && failure <= 10) resistance_error = errors[failure - 6];
                if (failure >= 11 && failure <= 15) pair_error = errors[failure - 11];
                if (failure == 16) resistance_error = -ENODATA;
                if (failure == 17) resistance_value = 0;
                if (failure == 18) resistance_value = -90;
                bool fallback = failure >= 16 && selection == 1;
                do_fg_algo(&gm, events[event], 0, 0);
                CHECK(temperature_calls == 1);
                if (failure && !fallback) {
                    CHECK(model_calls == 0 && end_calls == 0);
                    CHECK(gm.algo.last_temp == 17 && gm.algo.fg_bat_tmp_c_gap == 4);
                    if (failure <= 10 || failure >= 16) CHECK(pair_calls == 0);
                } else {
                    CHECK(model_calls == 5 && end_calls == 1);
                    CHECK(resistance_calls == 1 && pair_calls == 1);
                    CHECK(last_temperature == 23 && last_rac == (fallback ? 0 : 90));
                    CHECK(last_voltage == 41190 && last_current == -123);
                    CHECK(gm.algo.last_temp == 23 && gm.algo.fg_bat_tmp_c_gap == 1);
                    if (fallback) {
                        resistance_error = 0;
                        resistance_value = 117;
                        do_fg_algo(&gm, events[event], 0, 0);
                        CHECK(last_rac == 117 && resistance_calls == 2 && pair_calls == 2);
                        CHECK(model_calls == 10 && end_calls == 2);
                    }
                }
                cases++;
            }
        }
    }
    const int errors_with_no_rac[] = {-EPERM, -EIO, -ENODATA, -ETIMEDOUT};
    for (unsigned int i = 0; i < ARRAY_SIZE(errors_with_no_rac); i++) {
        struct mtk_battery gm = { .algo_lock = {.held = true}, .fg_cust_data = {.qmax_sel = 1} };
        struct fgr_vboot_sample sample = {.temperature = 99, .rac = 123};
        struct fgr_vboot_sample before = sample;
        reset();
        resistance_error = -ENODATA;
        pair_error = errors_with_no_rac[i];
        CHECK(fgr_read_vboot_sample(&gm, &sample) == pair_error);
        CHECK(memcmp(&sample, &before, sizeof(sample)) == 0 && pair_calls == 1);
        reset();
        temperature_error = errors_with_no_rac[i];
        CHECK(fgr_read_vboot_sample(&gm, &sample) == temperature_error);
        CHECK(memcmp(&sample, &before, sizeof(sample)) == 0);
        CHECK(resistance_calls == 0 && pair_calls == 0);
        cases++;
    }
    finish();
    return 0;
}
