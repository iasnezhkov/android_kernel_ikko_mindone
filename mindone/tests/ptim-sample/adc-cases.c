// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
    memset(&adc, 0, sizeof(adc));
    memset(registers, 0, sizeof(registers));
    memset(trace, 0, sizeof(trace));
    memset(auxadc_chans, 0, sizeof(auxadc_chans));
    adc.info = &info;
    auxadc_chans[AUXADC_IMP_SAMPLE] = (struct auxadc_channels){ .r_ratio = {3, 1}, .res = 15 };
    auxadc_chans[AUXADC_IMP] = auxadc_chans[AUXADC_IMP_SAMPLE];
    auxadc_chans[3] = (struct auxadc_channels){ .r_ratio = {2, 1}, .res = 15, .regs = &adc, .cali_fn = calibration };
    io_count = fail_at = drop_at = corrupt_at = 0;
    corrupt_mask = 0;
    wake_count = lock_calls = delay_count = poll_count = starts = resets = 0;
    ready_after = general_error = cali_error = 0;
    next_voltage = 25000;
    next_current = 65400;
    active = stale_ready = false;
}

static int read_pair(int *voltage, int *current_value)
{
    int rc = mt635x_auxadc_read_raw(&iio, &pair_channel, voltage, current_value, IIO_CHAN_INFO_RAW);
    CHECK(!adc.lock.held);
    CHECK(wake_count == 0);
    CHECK(resets == 0);
    return rc;
}

static void unchanged_failure(int expected)
{
    int voltage = 123456, current_value = -234567;
    CHECK(read_pair(&voltage, &current_value) == expected);
    CHECK(voltage == 123456 && current_value == -234567);
    cases++;
}

int main(void)
{
    struct operation successful[256];
    int voltage, current_value, count, current_index, last_clock_clear = -1;
    reset();
    CHECK(read_pair(&voltage, &current_value) == IIO_VAL_INT_MULTIPLE);
    CHECK(voltage == 25000 && current_value == 65400);
    CHECK(!adc.imp_cleanup_pending && starts == 1 && poll_count == 1);
    count = io_count;
    memcpy(successful, trace, sizeof(successful));
    current_index = count - 1;
    CHECK(successful[current_index].kind == 'r' && successful[current_index].reg == 0xd88);
    for (int i = 0; i < count; i++)
        if (successful[i].kind == 'w' && successful[i].reg == MT6358_AUXADC_DCM_CON && !successful[i].value)
            last_clock_clear = i;
    CHECK(last_clock_clear >= 0 && current_index > last_clock_clear + 3);
    cases++;

    for (int i = 1; i <= count; i++) {
        reset();
        fail_at = i;
        unchanged_failure(-EIO);
        fail_at = 0;
        io_count = 0;
        poll_count = 0;
        CHECK(read_pair(&voltage, &current_value) == IIO_VAL_INT_MULTIPLE);
        CHECK(voltage == 25000 && current_value == 65400 && !adc.imp_cleanup_pending);
        cases++;
    }

    for (int i = 0; i < count; i++) {
        struct operation op = successful[i];
        if (op.kind == 'r' && i < current_index &&
            (op.reg == MT6358_AUXADC_DCM_CON || op.reg == MT6358_AUXADC_IMP1 ||
             (op.reg == MT6358_AUXADC_IMP0 && i > last_clock_clear))) {
            reset();
            corrupt_at = i + 1;
            corrupt_mask = op.reg == MT6358_AUXADC_DCM_CON ? MT6358_IMP_CK_SW_MASK :
                op.reg == MT6358_AUXADC_IMP1 ? MT6358_IMP_AUTORPT_EN_MASK : MT6358_IMP_CLR_MASK;
            unchanged_failure(-EIO);
            CHECK(adc.imp_cleanup_pending);
        }
        if (op.kind == 'w' && !op.value && i >= last_clock_clear - 2 && i <= last_clock_clear) {
            reset();
            drop_at = i + 1;
            unchanged_failure(-EIO);
            CHECK(adc.imp_cleanup_pending);
        }
        if (op.kind == 'w' && op.value &&
            (op.reg == MT6358_AUXADC_DCM_CON || op.reg == MT6358_AUXADC_IMP1)) {
            reset();
            drop_at = i + 1;
            unchanged_failure(-ETIMEDOUT);
            CHECK(delay_count == 33 && !adc.imp_cleanup_pending);
        }
    }

    reset();
    ready_after = -1;
    unchanged_failure(-ETIMEDOUT);
    CHECK(poll_count == 34 && delay_count == 33 && !adc.imp_cleanup_pending);
    reset();
    ready_after = 32;
    CHECK(read_pair(&voltage, &current_value) == IIO_VAL_INT_MULTIPLE);
    CHECK(poll_count == 33 && delay_count == 32);
    cases++;
    reset();
    stale_ready = true;
    registers[MT6358_AUXADC_IMP0] = MT6358_IMP_IRQ_RDY_BIT;
    unchanged_failure(-EIO);
    CHECK(starts == 0);

    reset();
    adc.imp_cleanup_pending = true;
    fail_at = 1;
    adc.lock.held = true;
    voltage = 1; current_value = 2;
    CHECK(mt6358_imp_conv(&adc, &voltage, &current_value) == -EIO);
    CHECK(starts == 0 && voltage == 1 && current_value == 2 && adc.imp_cleanup_pending);
    adc.lock.held = false;
    cases++;

    reset();
    adc.imp_cleanup_pending = true;
    registers[MT6358_AUXADC_IMP1] = MT6358_IMP_AUTORPT_EN_MASK;
    registers[MT6358_AUXADC_DCM_CON] = MT6358_IMP_CK_SW_MASK;
    fail_at = 1;
    unchanged_failure(-EIO);
    CHECK(adc.imp_cleanup_pending && starts == 0);
    io_count = 0;
    unchanged_failure(-EIO);
    CHECK(adc.imp_cleanup_pending && starts == 0);
    io_count = fail_at = 0;
    CHECK(read_pair(&voltage, &current_value) == IIO_VAL_INT_MULTIPLE);
    CHECK(!adc.imp_cleanup_pending && starts == 1);
    cases++;

    reset();
    CHECK(mt635x_auxadc_read_raw(&iio, &pair_channel, &voltage, &current_value, IIO_CHAN_INFO_SCALE) == IIO_VAL_FRACTIONAL);
    CHECK(voltage == 5400 && current_value == 32768 && io_count == 0 && lock_calls == 0);
    auxadc_chans[AUXADC_IMP_SAMPLE].r_ratio[1] = 0;
    CHECK(mt635x_auxadc_read_raw(&iio, &pair_channel, &voltage, &current_value, IIO_CHAN_INFO_SCALE) == -EINVAL);
    CHECK(mt635x_auxadc_read_raw(&iio, &pair_channel, &voltage, &current_value, IIO_CHAN_INFO_PROCESSED) == -EINVAL);
    CHECK(io_count == 0 && lock_calls == 0);
    cases++;

    for (int failure = 0; failure < 3; failure++) {
        struct iio_chan_spec temp = { .channel = 3 };
        reset();
        general_error = failure == 1 ? -EIO : 0;
        cali_error = failure == 2 ? -ERANGE : 0;
        voltage = 123456;
        int rc = mt635x_auxadc_read_raw(&iio, &temp, &voltage, &current_value, IIO_CHAN_INFO_PROCESSED);
        CHECK(rc == (failure == 1 ? -EIO : failure == 2 ? -ERANGE : IIO_VAL_INT));
        CHECK(!adc.lock.held && wake_count == 0);
        if (failure)
            CHECK(voltage == 123456);
        cases++;
    }
    reset();
    struct auxadc_info unsupported = {0};
    adc.info = &unsupported;
    unchanged_failure(-EOPNOTSUPP);
    CHECK(io_count == 0);
    finish();
    return 0;
}
