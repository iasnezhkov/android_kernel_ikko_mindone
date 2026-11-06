// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void basic_and_faults(void)
{
    reset();
    CHECK(run_rac() == 165);
    CHECK(samples_count == 2 && io_count == 7 && on_writes == 4 && off_writes == 2 && reads == 1);
    CHECK(sleep_ms == 50 && !adc.load_cleanup_pending && !adc.load_cleanup_error);
    CHECK(registers[MT6358_ISINK_EN_CTRL_SMPL] == 0);
    for (int fault = 1; fault <= 7; fault++) {
        for (int applied = 0; applied < 2; applied++) {
            reset(); failures[fault] = -ENXIO; apply_failed[fault] = applied;
            CHECK(run_rac() == -ENXIO);
            CHECK(off_writes == 2 && reads == 1);
            CHECK(samples_count == (fault <= 4 ? 1 : 2));
            CHECK(sleep_ms == (fault <= 4 ? 0 : 50));
            CHECK(locks == unlocks && !adc.lock.held);
        }
    }
    const int sample_errors[] = {-ETIMEDOUT, -EPERM, -EIO, -ENODATA};
    for (unsigned int error = 0; error < ARRAY_SIZE(sample_errors); error++) {
        for (int which = 0; which < 2; which++) {
            reset(); samples[which].ret = sample_errors[error];
            CHECK(run_rac() == sample_errors[error]);
            CHECK(samples_count == which + 1);
            CHECK(off_writes == (which ? 2 : 0));
            CHECK(on_writes == (which ? 4 : 0));
        }
    }
    for (int which = 0; which < 2; which++) {
        reset(); samples[which].ret = 5;
        CHECK(run_rac() == -EIO);
        CHECK(off_writes == (which ? 2 : 0));
    }
    reset(); samples[1].ret = -ETIMEDOUT; failures[5] = -ENXIO;
    CHECK(run_rac() == -ETIMEDOUT);
    CHECK(last_primary == -ETIMEDOUT && last_cleanup == -ENXIO && cleanup_logs == 1);
    CHECK(adc.load_cleanup_pending && adc.load_cleanup_error == -EIO);
    reset(); info.imp_conv = other_imp_conv; failures[1] = -ENXIO;
    CHECK(run_rac() == 165 && io_count == 6 && reads == 0);
}

static void cleanup_states(void)
{
    const unsigned bits[] = {1, 2, 0x100, 0x200};
    for (size_t i = 0; i < ARRAY_SIZE(bits); i++) {
        reset(); read_bits[7] = bits[i];
        CHECK(run_rac() == -EIO);
        CHECK(adc.load_cleanup_pending && adc.load_cleanup_error == -EIO);
        CHECK(off_writes == 2 && reads == 1);
    }
    for (int fault = 5; fault <= 6; fault++) {
        reset(); drops[fault] = 1;
        CHECK(run_rac() == -EIO);
        CHECK(adc.load_cleanup_pending);
        CHECK(registers[MT6358_ISINK_EN_CTRL_SMPL] != 0);
    }
    reset(); read_bits[7] = 0x4000;
    CHECK(run_rac() == 165 && !adc.load_cleanup_pending);
    reset(); failures[5] = -ENXIO; apply_failed[5] = 1;
    CHECK(run_rac() == -ENXIO && !adc.load_cleanup_pending && adc.load_cleanup_error == 0);
    reset(); failures[5] = -ENXIO; failures[6] = -EAGAIN; failures[7] = -ENODEV;
    CHECK(run_rac() == -ENXIO);
    CHECK(adc.load_cleanup_pending && adc.load_cleanup_error == -ENODEV);
    CHECK(off_writes == 2 && reads == 1);
    reset(); pending(); failures[1] = -ENXIO;
    CHECK(run_rac() == -ENXIO && samples_count == 0 && io_count == 3);
    CHECK(adc.load_cleanup_pending);
    memset(failures, 0, sizeof(failures)); io_count = off_writes = reads = 0;
    CHECK(run_rac() == 165);
    CHECK(io_count == 10 && off_writes == 4 && reads == 2 && !adc.load_cleanup_pending);
}

static void channel_guards(void)
{
    const int channels[] = {AUXADC_IMP, AUXADC_IMP_SAMPLE};
    for (size_t c = 0; c < ARRAY_SIZE(channels); c++) {
        for (int fault = 1; fault <= 3; fault++) {
            reset(); pending(); failures[fault] = -ENXIO;
            struct iio_chan_spec spec = { .channel = channels[c] };
            int voltage = 123456, ibat = 789012;
            CHECK(mt635x_auxadc_read_raw(&iio, &spec, &voltage, &ibat, IIO_CHAN_INFO_RAW) == -ENXIO);
            CHECK(voltage == 123456 && ibat == 789012);
            CHECK(samples_count == 0 && checked_samples == 0 && io_count == 3);
            CHECK(locks == 1 && unlocks == 1 && awake == 0 && relaxes == 1);
        }
        reset(); pending();
        struct iio_chan_spec spec = { .channel = channels[c] };
        int voltage = 123456, ibat = 789012;
        CHECK(mt635x_auxadc_read_raw(&iio, &spec, &voltage, &ibat, IIO_CHAN_INFO_RAW) == IIO_VAL_INT_MULTIPLE);
        CHECK(voltage == 26000 && !adc.load_cleanup_pending && io_count == 3);
        CHECK(samples_count == (c == 0) && checked_samples == (c == 1));
        CHECK(locks == unlocks && awake == 0 && relaxes == 1);
    }
    reset(); pending();
    struct iio_chan_spec spec = { .channel = AUXADC_IMP_SAMPLE };
    int n = 0, den = 0;
    CHECK(mt635x_auxadc_read_raw(&iio, &spec, &n, &den, IIO_CHAN_INFO_SCALE) == IIO_VAL_FRACTIONAL);
    CHECK(n == 5400 && den == 32768 && io_count == 0 && adc.load_cleanup_pending);
}

static void calibration_states(void)
{
    for (int fault = 1; fault <= 14; fault++) {
        reset(); failures[fault] = -ENXIO;
        CHECK(run_cali() == -ENXIO && adc.imix_r == 137);
        memset(failures, 0, sizeof(failures));
        io_count = samples_count = off_writes = reads = sleep_ms = 0;
        CHECK(run_cali() == 0 && adc.imix_r == 165 && samples_count == 4);
        int prior = samples_count;
        CHECK(run_cali() == 0 && samples_count == prior);
    }
    for (int which = 0; which < 4; which++) {
        reset(); samples[which].ret = -ENODATA;
        CHECK(run_cali() == -ENODATA && adc.imix_r == 137);
        samples[which].ret = 0; samples_count = io_count = 0;
        CHECK(run_cali() == 0 && adc.imix_r == 165 && samples_count == 4);
    }
    reset(); CHECK(run_cali() == 0);
    pending(); int previous = samples_count; io_count = 0;
    CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    CHECK(io_count == 3 && samples_count == previous && !adc.load_cleanup_pending);
}

static void suspend_states(void)
{
    reset(); gauge_enabled = false; pending();
    CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    CHECK(io_count == 3 && !adc.load_cleanup_pending && samples_count == 0);
    reset(); ui_soc = -EINVAL; pending();
    CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    CHECK(io_count == 3 && !adc.load_cleanup_pending && samples_count == 0);
    reset(); gauge_enabled = false; pending(); failures[3] = -ENODEV;
    CHECK(mt635x_auxadc_suspend_late(&device) == -ENODEV);
    CHECK(adc.load_cleanup_pending && samples_count == 0 && io_count == 3);
    reset(); pending(); failures[1] = -EAGAIN; apply_failed[1] = 1;
    CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    CHECK(!adc.load_cleanup_pending && samples_count == 0 && calibration_logs == 1);
    reset(); samples[0].ret = -ETIMEDOUT;
    CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    CHECK(adc.imix_r == 137 && !adc.load_cleanup_pending && calibration_logs == 1);
    reset(); failures[1] = -EAGAIN; read_bits[4] = 1;
    CHECK(mt635x_auxadc_suspend_late(&device) == -EIO);
    CHECK(adc.load_cleanup_pending && adc.imix_r == 137 && last_primary == -EAGAIN);
    reset(); pending(); adc.imp_cleanup_pending = true;
    failures[3] = -ENODEV; imp_cleanup_error = -ENXIO;
    CHECK(mt635x_auxadc_suspend_late(&device) == -ENXIO);
    CHECK(io_count == 3 && imp_cleanup_calls == 1 && samples_count == 0);
    CHECK(locks == unlocks && !adc.lock.held);
}

static void arithmetic_and_retry(void)
{
    reset();
    int value = -123;
    CHECK(mt6358_rac_voltage(26000, &value) == 0 && value == 42846);
    for (int ratio = 1; ratio <= 255; ratio += 127) {
        for (int divisor = 1; divisor <= 255; divisor += 127) {
            for (int res = 1; res <= 30; res += 29) {
                auxadc_chans[AUXADC_IMP].r_ratio[0] = ratio;
                auxadc_chans[AUXADC_IMP].r_ratio[1] = divisor;
                auxadc_chans[AUXADC_IMP].res = res;
                int raw = (1U << res) - 1;
                CHECK(mt6358_rac_voltage(raw, &value) == 0);
                CHECK(value == ((((s64)raw * ratio * 18000) / divisor) >> res));
            }
        }
    }
    reset(); auxadc_chans[AUXADC_IMP].res = 31;
    CHECK(mt6358_rac_voltage(1, &value) == -ERANGE);
    reset(); auxadc_chans[AUXADC_IMP].r_ratio[1] = 0;
    CHECK(mt6358_rac_voltage(1, &value) == -ERANGE);
    reset(); auxadc_chans[AUXADC_IMP].r_ratio[0] = 0; value = 2468;
    CHECK(mt6358_rac_voltage(1, &value) == -ERANGE && value == 2468);
    reset(); CHECK(mt6358_rac_voltage(-1, &value) == -ERANGE);
    CHECK(mt6358_rac_voltage(32768, &value) == -ERANGE);
    reset(); samples[0].raw = -1;
    CHECK(run_rac() == -ERANGE && on_writes == 0);
    reset(); samples[1].raw = -1;
    CHECK(run_rac() == -ERANGE && off_writes == 2 && !adc.load_cleanup_pending);
    for (int reverse = 0; reverse < 2; reverse++) {
        reset();
        for (size_t i = 0; i < ARRAY_SIZE(samples); i++)
            samples[i].ibat = ((i & 1) ^ reverse) ? INT_MAX : INT_MIN;
        CHECK(run_rac() == -ENODATA && samples_count == 6 && sleep_ms == 150);
        CHECK(io_count == 21 && !adc.load_cleanup_pending);
    }
    reset();
    for (size_t i = 1; i < ARRAY_SIZE(samples); i += 2)
        samples[i].raw = 25999;
    CHECK(run_rac() == -ENODATA && samples_count == 6 && sleep_ms == 150 && off_writes == 6);
}

static void threshold_edges(void)
{
    const struct { int dv, di, expected; } edges[] = {
        {100, 699, -ENODATA}, {100, 700, 142}, {100, 1200, 83}, {100, 1201, -ENODATA},
        {39, 700, -ENODATA}, {40, 700, 57}, {2000, 1000, 2000}, {2001, 1000, -ENODATA},
        {40, 1000, -ENODATA}, {49, 1000, -ENODATA}, {50, 1000, 50},
    };
    for (size_t edge = 0; edge < ARRAY_SIZE(edges); edge++) {
        reset(); auxadc_chans[AUXADC_IMP].r_ratio[0] = 1;
        int loaded = 26000;
        while (((26000LL * 18000) >> 15) - (((s64)loaded * 18000) >> 15) < edges[edge].dv)
            loaded--;
        CHECK(((26000LL * 18000) >> 15) - (((s64)loaded * 18000) >> 15) == edges[edge].dv);
        for (size_t i = 0; i < ARRAY_SIZE(samples); i++) {
            samples[i].raw = i & 1 ? loaded : 26000;
            samples[i].ibat = i & 1 ? edges[edge].di : 0;
        }
        CHECK(run_rac() == edges[edge].expected);
        CHECK(samples_count == (edges[edge].expected < 0 ? 6 : 2));
        CHECK(!adc.load_cleanup_pending);
    }
}

static int read_rac(int *value)
{
    struct iio_chan_spec spec = { .channel = AUXADC_IMIX_R };
    int second = 2468;
    int ret = mt635x_auxadc_read_raw(&iio, &spec, value, &second, IIO_CHAN_INFO_RAW);
    CHECK(second == 2468);
    return ret;
}

static void bootstrap_states(void)
{
    reset(); adc.imix_r = 0; ui_soc = -ENODATA;
    int value = -123;
    CHECK(read_rac(&value) == IIO_VAL_INT && value == 165);
    CHECK(samples_count == 4 && !adc.load_cleanup_pending);
    int count = io_count;
    CHECK(read_rac(&value) == IIO_VAL_INT && io_count == count);
    for (int fault = 1; fault <= 14; fault++) {
        reset(); adc.imix_r = 0; ui_soc = -ENODATA; failures[fault] = -ENXIO;
        value = -123;
        CHECK(read_rac(&value) == -ENXIO && value == -123 && adc.imix_r == 0);
        CHECK(locks == unlocks && awake == 0);
        memset(failures, 0, sizeof(failures)); io_count = samples_count = 0;
        CHECK(read_rac(&value) == IIO_VAL_INT && value == 165);
    }
    for (int which = 0; which < 4; which++) {
        reset(); adc.imix_r = 0; samples[which].ret = -ETIMEDOUT;
        value = -123;
        CHECK(read_rac(&value) == -ETIMEDOUT && value == -123 && adc.imix_r == 0);
        CHECK(!adc.load_cleanup_pending && locks == unlocks && awake == 0);
        samples[which].ret = 0; samples_count = io_count = 0;
        CHECK(read_rac(&value) == IIO_VAL_INT && value == 165);
    }
    for (int delta = 99; delta <= 100; delta++) {
        reset(); adc.imix_r = 0;
        auxadc_chans[AUXADC_IMP].r_ratio[0] = 1;
        int loaded = 26000;
        while (((26000LL * 18000) >> 15) - (((s64)loaded * 18000) >> 15) < delta)
            loaded--;
        for (size_t i = 1; i < ARRAY_SIZE(samples); i += 2)
            samples[i].raw = loaded;
        value = -123;
        CHECK(read_rac(&value) == -ENODATA && value == -123 && adc.imix_r == 0);
        CHECK(!adc.load_cleanup_pending);
    }
    reset(); adc.imix_r = 0;
    samples[3].raw = 25800;
    CHECK(read_rac(&value) == IIO_VAL_INT && value == (165 + 330) / 2);
    CHECK(adc.imix_r == value && samples_count == 4);
    reset(); adc.imix_r = 0; info.imp_conv = other_imp_conv;
    CHECK(read_rac(&value) == IIO_VAL_INT && value == 0 && io_count == 0 && samples_count == 0);
    reset(); adc.imix_r = 90;
    CHECK(read_rac(&value) == IIO_VAL_INT && value == 90 && io_count == 0);
}

static void *race_call(void *suspend)
{
    int value = -123;
    if (suspend)
        CHECK(mt635x_auxadc_suspend_late(&device) == 0);
    else
        CHECK(read_rac(&value) == IIO_VAL_INT && value == 165);
    return NULL;
}

static void bootstrap_suspend_race(void)
{
    reset(); adc.imix_r = 0;
    pthread_t reader, suspender;
    struct timespec deadline;
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_sec += 5;
    contenders = 0; racing = true;
    assert(pthread_mutex_lock(&host_lock) == 0);
    assert(pthread_create(&reader, NULL, race_call, NULL) == 0);
    assert(pthread_create(&suspender, NULL, race_call, &device) == 0);
    assert(pthread_mutex_lock(&start_lock) == 0);
    while (contenders < 2)
        assert(pthread_cond_timedwait(&start_cond, &start_lock, &deadline) == 0);
    assert(pthread_mutex_unlock(&start_lock) == 0);
    assert(pthread_mutex_unlock(&host_lock) == 0);
    assert(pthread_join(reader, NULL) == 0);
    assert(pthread_join(suspender, NULL) == 0);
    racing = false;
    CHECK(adc.imix_r == 165 && (samples_count == 4 || samples_count == 8));
    CHECK(locks == unlocks && !adc.lock.held && !adc.load_cleanup_pending && awake == 0);
    int value, count = io_count;
    CHECK(read_rac(&value) == IIO_VAL_INT && io_count == count);
}

int main(void)
{
    basic_and_faults();
    cleanup_states();
    channel_guards();
    calibration_states();
    suspend_states();
    arithmetic_and_retry();
    threshold_edges();
    bootstrap_states();
    bootstrap_suspend_race();
    CHECK(!adc.lock.held && awake == 0);
    printf("PASS cases=%d checks=%d\n", cases, checks);
    return 0;
}
