// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
    cases++;
    memset(registers, 0, sizeof(registers));
    memset(trace, 0, sizeof(trace));
    gauge = (struct mtk_gauge){ .regmap = &registers, .gm = &gm,
        .hw_status.r_fg_value = 50, .fg_hw_info.current_2 = 789012 };
    gm = (struct mtk_battery){ .gauge = &gauge, .fg_cust_data.car_tune_value = 1000 };
    operations = fault_at = fault_at2 = unapplied_at = corrupt_at = change_calibration_at = 0;
    operation_errno = -ENXIO;
    secondary_errno = -EACCES;
    ambiguous_error = start_stuck = clear_stuck = false;
    corrupt_mask = 0;
    dwa_writes = high_reads = low_reads = sample_reads = latch_reads = cali_calls = nafg_calls = 0;
    registers[RG_FGADC_CUR_CON3] = 20;
    registers[RG_FGADC_IAVG_CON1] = FG_IAVG_VLD_MASK << FG_IAVG_VLD_SHIFT;
    registers[RG_FGADC_IAVG_CON0] = 100;
    registers[RG_FGADC_ANA_TEST_CON0] = BIT(13) | BIT(12) | BIT(7);
    registers[RG_FGADC_ANA_CON0] = BIT(5) | BIT(4);
}

static void set_average(unsigned int raw, bool valid)
{
    CHECK(raw <= GENMASK(27, 0));
    registers[RG_FGADC_IAVG_CON1] = ((raw >> 16) & FG_IAVG_27_16_MASK) |
        (valid ? FG_IAVG_VLD_MASK << FG_IAVG_VLD_SHIFT : 0);
    registers[RG_FGADC_IAVG_CON0] = raw & FG_IAVG_15_00_MASK;
}

static int read_sample(struct gauge_dwa_sample *sample)
{
    mutex_lock(&gauge.ops_lock);
    int ret = read_iavg_dwa_sample(&gauge, sample);
    mutex_unlock(&gauge.ops_lock);
    return ret;
}

static int run_event(enum gauge_event event)
{
    mutex_lock(&gauge.ops_lock);
    int ret = event_set(&gauge, NULL, event);
    mutex_unlock(&gauge.ops_lock);
    return ret;
}

static int reference_current(unsigned int raw, int resistance, int tune, int *value)
{
    if (raw > UINT16_MAX || resistance <= 0 || tune <= 0)
        return -ERANGE;
    __int128 number = raw > INT16_MAX ? 65535 - raw : raw;
    number = number * 381470 / 100000;
    number = number * 100 / resistance;
    if (raw > INT16_MAX)
        number = -number;
    number = number * tune / 1000;
    if (number < INT_MIN || number > INT_MAX)
        return -ERANGE;
    *value = number;
    return 0;
}

static int reference_average(unsigned int raw, int resistance, int tune, int *value)
{
    __int128 number = raw & BIT(27) ? BIT(28) - raw : raw;
    number *= 190735;
    number *= tune;
    if (number > INT64_MAX)
        return -ERANGE;
    number /= 1000000;
    number /= resistance;
    if (raw & BIT(27))
        number = -number;
    if (number < INT_MIN || number > INT_MAX)
        return -ERANGE;
    *value = number;
    return 0;
}

static void nominal(void)
{
    reset();
    CHECK(RG_FGADC_CON1 == 0xd0a && RG_FGADC_IAVG_CON1 == 0xd32 && RG_FGADC_IAVG_CON0 == 0xd30);
    CHECK(FG_LATCHDATA_ST_MASK == 0x8000 && FG_SW_CLEAR_MASK == 0x8 && FG_SW_READ_PRE_MASK == 0x1);
    CHECK(FG_IAVG_27_16_MASK == 0xfff && FG_IAVG_15_00_MASK == 0xffff && FG_IAVG_VLD_SHIFT == 15);
    CHECK(run_event(EVT_INT_IAVG) == 0);
    CHECK(operations == 17 && sample_reads == 4 && high_reads == 1 && low_reads == 1);
    CHECK(dwa_writes == 3 && gauge.fg_hw_info.current_2 == 152);
    CHECK(trace[0].kind == 'u' && trace[0].value == FG_SW_CLEAR_MASK);
    CHECK(trace[4].kind == 'w' && trace[4].value == 1);
    CHECK(trace[13].kind == 'r' && trace[13].address == RG_FGADC_CON1);
    CHECK(trace[14].address == RG_FGADC_ANA_TEST_CON0 && trace[14].mask == BIT(13));
    CHECK(trace[15].address == RG_FGADC_ANA_TEST_CON0 && trace[15].mask == BIT(12));
    CHECK(trace[16].address == RG_FGADC_ANA_CON0 && trace[16].mask == BIT(5));
    CHECK(registers[RG_FGADC_ANA_TEST_CON0] == BIT(7));
    CHECK(registers[RG_FGADC_ANA_CON0] == BIT(4));
    reset();
    set_average(0, true);
    CHECK(run_event(EVT_INT_BAT_INT2_HT) == 0 && dwa_writes == 3);
    CHECK(trace[14].value == BIT(13) && trace[15].value == BIT(12) && trace[16].value == BIT(5));
    reset();
    CHECK(run_event(EVT_INT_NAFG_CHECK) == 0 && nafg_calls == 1 && dwa_writes == 0);
    reset();
    CHECK(run_event(EVT_OTHER) == 0 && sample_reads == 4 && dwa_writes == 0);
    reset();
    registers[RG_FGADC_IAVG_CON1] |= BIT(12) | BIT(13) | BIT(14);
    struct gauge_dwa_sample sample;
    CHECK(read_sample(&sample) == 0 && sample.iavg == 381 && sample.iavg_valid);
}

static void operation_failures(void)
{
    for (int ambiguous = 0; ambiguous < 2; ambiguous++) {
        for (int failure = 1; failure <= 17; failure++) {
            reset();
            fault_at = failure;
            ambiguous_error = ambiguous;
            CHECK(run_event(EVT_INT_IAVG) == -ENXIO);
            CHECK(!gauge.ops_lock.held);
            if (failure <= 14) {
                CHECK(gauge.fg_hw_info.current_2 == 789012 && dwa_writes == 0);
            } else {
                CHECK(gauge.fg_hw_info.current_2 == 152 && dwa_writes == failure - 14);
                CHECK(operations == failure);
                CHECK((registers[RG_FGADC_ANA_TEST_CON0] & BIT(13)) ==
                    (failure == 15 && !ambiguous ? BIT(13) : 0));
                CHECK((registers[RG_FGADC_ANA_TEST_CON0] & BIT(12)) ==
                    (failure < 16 || (failure == 16 && !ambiguous) ? BIT(12) : 0));
                CHECK((registers[RG_FGADC_ANA_CON0] & BIT(5)) ==
                    (failure < 17 || (failure == 17 && !ambiguous) ? BIT(5) : 0));
            }
            fault_at = 0;
            CHECK(run_event(EVT_INT_IAVG) == 0);
            CHECK(gauge.fg_hw_info.current_2 == 152);
        }
    }
    for (int failure = 1; failure <= 14; failure++) {
        reset();
        struct gauge_dwa_sample sample, previous;
        memset(&sample, 0xa5, sizeof(sample));
        previous = sample;
        fault_at = failure;
        CHECK(read_sample(&sample) == -ENXIO);
        CHECK(memcmp(&sample, &previous, sizeof(sample)) == 0);
    }
    reset();
    fault_at = 7;
    fault_at2 = 8;
    CHECK(run_event(EVT_INT_IAVG) == -ENXIO && dwa_writes == 0);
}

static void latch_controls(void)
{
    reset();
    registers[RG_FGADC_CON1] = FG_LATCHDATA_ST_MASK | FG_SW_READ_PRE_MASK;
    CHECK(run_event(EVT_INT_IAVG) == 0 && operations == 17);
    const int unapplied[] = {3, 5, 11, 13};
    for (unsigned int i = 0; i < ARRAY_SIZE(unapplied); i++) {
        reset();
        unapplied_at = unapplied[i];
        int ret = run_event(EVT_INT_IAVG);
        CHECK(ret == (unapplied[i] == 5 || unapplied[i] == 11 ? -ETIMEDOUT : -EIO));
        CHECK(gauge.fg_hw_info.current_2 == 789012 && dwa_writes == 0);
        unapplied_at = 0;
        CHECK(run_event(EVT_INT_IAVG) == 0);
    }
    reset();
    registers[RG_FGADC_CON1] = FG_LATCHDATA_ST_MASK;
    unapplied_at = 1;
    CHECK(run_event(EVT_INT_IAVG) == -ETIMEDOUT && latch_reads == 1001 && sample_reads == 0);
    reset();
    start_stuck = true;
    CHECK(run_event(EVT_INT_IAVG) == -ETIMEDOUT);
    CHECK(latch_reads == 1004 && operations == 1009 && sample_reads == 0 && dwa_writes == 0);
    reset();
    clear_stuck = true;
    CHECK(run_event(EVT_INT_IAVG) == -ETIMEDOUT && sample_reads == 4 && dwa_writes == 0);
    CHECK(latch_reads == 1004 && operations == 1013);
    clear_stuck = false;
    CHECK(run_event(EVT_INT_IAVG) == 0);
    const unsigned int masks[] = {FG_LATCHDATA_ST_MASK, FG_SW_READ_PRE_MASK, FG_SW_CLEAR_MASK};
    for (unsigned int i = 0; i < ARRAY_SIZE(masks); i++) {
        for (int stage = 0; stage < 2; stage++) {
            reset();
            corrupt_at = stage ? 14 : 4;
            corrupt_mask = masks[i];
            CHECK(run_event(EVT_INT_IAVG) == -EIO && dwa_writes == 0);
            CHECK(gauge.fg_hw_info.current_2 == 789012);
        }
    }
    reset();
    unapplied_at = 15;
    CHECK(run_event(EVT_INT_IAVG) == 0);
    CHECK(registers[RG_FGADC_ANA_TEST_CON0] & BIT(13));
}

static void numeric_controls(void)
{
    const unsigned int raw[] = {0, 1, 32767, 32768, 65534, 65535, 65536, BIT(27) - 1, BIT(27), GENMASK(27, 0)};
    const int resistance[] = {1, 17, 50, 100, 103, INT_MAX};
    const int tuning[] = {1, 999, 1000, 1001, 5000, INT_MAX};
    for (unsigned int i = 0; i < ARRAY_SIZE(raw); i++) {
        for (unsigned int j = 0; j < ARRAY_SIZE(resistance); j++) {
            for (unsigned int k = 0; k < ARRAY_SIZE(tuning); k++) {
                reset();
                int expected = 11011, actual = expected;
                int ref = reference_current(raw[i], resistance[j], tuning[k], &expected);
                CHECK(dwa_convert_current(raw[i], resistance[j], tuning[k], &actual) == ref);
                CHECK(actual == expected);
                gauge.hw_status.r_fg_value = resistance[j];
                gm.fg_cust_data.car_tune_value = tuning[k];
                registers[RG_FGADC_CUR_CON3] = 0;
                set_average(raw[i], true);
                struct gauge_dwa_sample sample, previous;
                memset(&sample, 0x91, sizeof(sample));
                previous = sample;
                ref = reference_average(raw[i], resistance[j], tuning[k], &expected);
                CHECK(read_sample(&sample) == ref);
                if (ref) {
                    CHECK(memcmp(&sample, &previous, sizeof(sample)) == 0);
                } else {
                    CHECK(sample.iavg_valid && sample.iavg == expected);
                    CHECK(sample.cic2 == 0 && sample.offset == 0);
                    CHECK(sample.r_fg_value == resistance[j] && sample.car_tune_value == tuning[k]);
                }
            }
        }
    }
    for (int invalid = -1; invalid <= 0; invalid++) {
        for (int field = 0; field < 2; field++) {
            reset();
            if (field)
                gm.fg_cust_data.car_tune_value = invalid;
            else
                gauge.hw_status.r_fg_value = invalid;
            CHECK(run_event(EVT_INT_IAVG) == -ERANGE && operations == 0 && dwa_writes == 0);
        }
    }
    reset();
    change_calibration_at = 7;
    struct gauge_dwa_sample sample;
    CHECK(read_sample(&sample) == 0 && sample.r_fg_value == 50 && sample.car_tune_value == 1000);
    CHECK(sample.iavg == 381 && sample.cic2 == 152);
    reset();
    gauge.hw_status.r_fg_value = 1;
    registers[RG_FGADC_OFFSET_CON0] = 32767;
    set_average(11200000, true);
    CHECK(run_event(EVT_INT_IAVG) == 0 && dwa_writes == 0);
    for (int channel = 0; channel < 2; channel++) {
        for (int invalid_raw = 0; invalid_raw < 2; invalid_raw++) {
            reset();
            gauge.hw_status.r_fg_value = 1;
            gm.fg_cust_data.car_tune_value = INT_MAX;
            registers[RG_FGADC_CUR_CON3] = 0;
            registers[channel ? RG_FGADC_OFFSET_CON0 : RG_FGADC_CUR_CON3] = invalid_raw ? 65536 : 32767;
            CHECK(run_event(EVT_INT_BAT_INT2_HT) == -ERANGE && operations == 14 && dwa_writes == 0);
            CHECK(gauge.fg_hw_info.current_2 == 789012);
            CHECK(registers[RG_FGADC_CON1] == 0);
        }
    }
}

static void validity_and_thresholds(void)
{
    const unsigned int fallback[] = {0, 1, 32767, 32768, 65534, 65535};
    for (unsigned int i = 0; i < ARRAY_SIZE(fallback); i++) {
        reset();
        set_average(GENMASK(27, 0), false);
        registers[RG_FGADC_CUR_CON3] = fallback[i];
        struct gauge_dwa_sample sample;
        int expected;
        CHECK(reference_current(fallback[i], 50, 1000, &expected) == 0);
        CHECK(read_sample(&sample) == 0 && !sample.iavg_valid);
        CHECK(sample.iavg == expected && sample.cic2 == expected && high_reads == 1 && low_reads == 0);
    }
    reset();
    set_average(100, false);
    CHECK(run_event(EVT_INT_BAT_INT2_LT) == 0 && operations == 16 && dwa_writes == 3);
    CHECK(trace[13].value == BIT(13));
    reset();
    set_average(100, false);
    fault_at = 7;
    CHECK(run_event(EVT_INT_BAT_INT2_LT) == -ENXIO && dwa_writes == 0);
    CHECK(gauge.fg_hw_info.current_2 == 789012);
    for (int negative = 0; negative < 2; negative++) {
        for (int edge = 0; edge < 2; edge++) {
            reset();
            gauge.hw_status.r_fg_value = 1;
            gm.fg_cust_data.car_tune_value = edge ? 1573 : 1572;
            registers[RG_FGADC_CUR_CON3] = 0;
            set_average(negative ? GENMASK(27, 0) : 1, true);
            CHECK(run_event(EVT_INT_IAVG) == 0);
            CHECK(dwa_writes == (edge ? 3 : 0));
            reset();
            gauge.hw_status.r_fg_value = 1;
            gm.fg_cust_data.car_tune_value = edge ? 5000 : 4999;
            registers[RG_FGADC_CUR_CON3] = 0;
            registers[RG_FGADC_OFFSET_CON0] = negative ? 65534 : 1;
            set_average(0, true);
            CHECK(run_event(EVT_INT_IAVG) == 0);
            CHECK(dwa_writes == (edge ? 0 : 3));
        }
    }
}

static void factory_lock(void)
{
    reset();
    CHECK(battery_meter_meta_tool_cali_car_tune(&gm, 123) == 1050);
    CHECK(cali_calls == 1 && !gauge.ops_lock.held);
    CHECK(gauge.hw_status.meta_current == 123);
    CHECK(battery_meter_meta_tool_cali_car_tune(&gm, 0) == 10000);
    CHECK(cali_calls == 1 && !gauge.ops_lock.held);
}

int main(void)
{
    nominal();
    operation_failures();
    latch_controls();
    numeric_controls();
    validity_and_thresholds();
    factory_lock();
    printf("PASS cases=%u assertions=%u\n", cases, checks);
    return 0;
}
