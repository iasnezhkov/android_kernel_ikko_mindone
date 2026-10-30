// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
    battery.fg_cust_data.car_tune_value = 1000;
    gauge = (struct mtk_gauge){ .gm = &battery, .chan_ptim_sample = &channel, .hw_status = { .r_fg_value = DEFAULT_R_FG } };
    scale_rc = IIO_VAL_FRACTIONAL;
    raw_rc = IIO_VAL_INT_MULTIPLE;
    scale_n = 5400;
    scale_d = 32768;
    sample_v = 25000;
    sample_i = 1000;
    scale_calls = raw_calls = time_calls = 0;
    first_time = 100;
    last_time = 200;
}

static void failure(int expected)
{
    struct gauge_ptim_sample sample, before;
    memset(&sample, 0xa5, sizeof(sample));
    before = sample;
    u64 sequence = gauge.ptim_sequence;
    CHECK(gauge_read_ptim_sample(&gauge, &sample) == expected);
    CHECK(memcmp(&sample, &before, sizeof(sample)) == 0);
    CHECK(gauge.ptim_sequence == sequence && !gauge.ops_lock.held);
    cases++;
}

static int expected_current(unsigned int raw, int resistance, int tune)
{
    s64 signed_count = raw > 32767 ? (s64)raw - 65535 : raw;
    s64 current_value = signed_count * UNIT_FGCURRENT / 100000;
    current_value = current_value * DEFAULT_R_FG / resistance;
    return -(current_value * tune / 1000);
}

int main(void)
{
    struct gauge_ptim_sample sample;
    const unsigned int raw_values[] = {0, 1, 1000, 32767, 32768, 65534, 65535};
    const int resistances[] = {50, DEFAULT_R_FG, 200};
    const int tunes[] = {501, 1000, 1499};
    for (unsigned int i = 0; i < ARRAY_SIZE(raw_values); i++) {
        for (unsigned int j = 0; j < ARRAY_SIZE(resistances); j++) {
            for (unsigned int k = 0; k < ARRAY_SIZE(tunes); k++) {
                reset();
                sample_i = raw_values[i];
                gauge.hw_status.r_fg_value = resistances[j];
                battery.fg_cust_data.car_tune_value = tunes[k];
                CHECK(gauge_read_ptim_sample(&gauge, &sample) == 0);
                CHECK(sample.current_01ma == expected_current(sample_i, resistances[j], tunes[k]));
                CHECK(sample.voltage_01mv == 41190 && sample.sequence == 1);
                CHECK(sample.r_fg_value == resistances[j] && sample.car_tune_value == tunes[k]);
                CHECK(sample.started_ns == 100 && sample.acquired_ns == 200);
                CHECK(raw_calls == 1 && scale_calls == 1 && !gauge.ops_lock.held);
                cases++;
            }
        }
    }
    reset(); raw_rc = -EIO; failure(-EIO);
    raw_rc = IIO_VAL_INT_MULTIPLE;
    scale_calls = raw_calls = time_calls = 0;
    CHECK(gauge_read_ptim_sample(&gauge, &sample) == 0 && sample.sequence == 1);
    scale_calls = raw_calls = time_calls = 0;
    sample_i = 0;
    CHECK(gauge_read_ptim_sample(&gauge, &sample) == 0 && sample.sequence == 2 && sample.current_01ma == 0);
    cases++;
    reset(); scale_rc = -ENODEV; failure(-ENODEV); CHECK(raw_calls == 0);
    reset(); scale_rc = IIO_VAL_INT; failure(-EINVAL); CHECK(raw_calls == 0);
    reset(); scale_n = 0; failure(-EINVAL); CHECK(raw_calls == 0);
    reset(); scale_d = 0; failure(-EINVAL); CHECK(raw_calls == 0);
    reset(); raw_rc = IIO_VAL_INT; failure(-EINVAL);
    reset(); sample_v = 0; failure(-ENODATA);
    reset(); sample_v = -1; failure(-ENODATA);
    reset(); sample_v = 32768; failure(-ENODATA);
    reset(); sample_i = -1; failure(-ENODATA);
    reset(); sample_i = 65536; failure(-ENODATA);
    reset(); last_time = first_time - 1; failure(-ENODATA);
    reset(); scale_n = 1; sample_v = 1; failure(-ERANGE);
    reset(); scale_n = INT_MAX; scale_d = 1; failure(-ERANGE);
    reset(); gauge.hw_status.r_fg_value = 0; failure(-ERANGE); CHECK(scale_calls == 0);
    reset(); battery.fg_cust_data.car_tune_value = -1; failure(-ERANGE); CHECK(scale_calls == 0);
    reset(); gauge.ptim_sequence = U64_MAX; failure(-ERANGE); CHECK(scale_calls == 0);
    reset(); gauge.hw_status.r_fg_value = 1; battery.fg_cust_data.car_tune_value = INT_MAX; sample_i = 32767; failure(-ERANGE);
    reset(); gauge.chan_ptim_sample = NULL; failure(-EOPNOTSUPP);
    reset(); gauge.chan_ptim_sample = ERR_PTR(-EPROBE_DEFER); failure(-EPROBE_DEFER);
    reset(); CHECK(gauge_read_ptim_sample(NULL, &sample) == -EINVAL); CHECK(gauge_read_ptim_sample(&gauge, NULL) == -EINVAL); cases++;
    finish();
    return 0;
}
