/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define AUXADC_DEBUG 1
struct mt635x_auxadc_device;
struct auxadc_info {
    int (*imp_conv)(struct mt635x_auxadc_device *, int *, int *);
    void (*imp_stop)(struct mt635x_auxadc_device *);
};
struct mt635x_auxadc_device {
    void *regmap;
    void *dev;
    struct mutex lock;
    const struct auxadc_info *info;
    int imp_vbat;
    int imix_r;
    bool imp_cleanup_pending;
};
struct auxadc_channels {
    unsigned char r_ratio[2];
    unsigned char res;
    const void *regs;
    int (*cali_fn)(struct mt635x_auxadc_device *, int, int);
};
struct iio_dev { struct mt635x_auxadc_device *adc; };
struct iio_chan_spec { int channel; };
#define iio_priv(device) ((device)->adc)
static struct auxadc_channels auxadc_chans[AUXADC_IMP_SAMPLE + 1];
static struct mt635x_auxadc_device adc;
static struct iio_dev iio = { .adc = &adc };
static struct iio_chan_spec pair_channel = { .channel = AUXADC_IMP_SAMPLE };
static unsigned int registers[0x2000];
struct operation { char kind; unsigned int reg, mask, value; };
static struct operation trace[256];
static int io_count, fail_at, drop_at, corrupt_at;
static unsigned int corrupt_mask;
static int wake_count, lock_calls, delay_count, poll_count, starts, resets;
static int ready_after, general_error, cali_error;
static unsigned int next_voltage, next_current;
static bool active, stale_ready;

static int record(char kind, unsigned int reg, unsigned int mask, unsigned int value)
{
    CHECK(adc.lock.held);
    CHECK(reg < ARRAY_SIZE(registers));
    CHECK(io_count < (int)ARRAY_SIZE(trace));
    trace[io_count++] = (struct operation){kind, reg, mask, value};
    return io_count == fail_at ? -EIO : 0;
}

static int regmap_update_bits(void *map, unsigned int reg, unsigned int mask, unsigned int value)
{
    int rc = record('w', reg, mask, value);
    if (rc || io_count == drop_at)
        return rc;
    registers[reg] = (registers[reg] & ~mask) | (value & mask);
    if (reg == MT6358_AUXADC_IMP0 && (value & MT6358_IMP_CLR_MASK) && !stale_ready)
        registers[reg] &= ~MT6358_IMP_IRQ_RDY_BIT;
    if (reg == MT6358_AUXADC_IMP1) {
        active = (registers[reg] & MT6358_IMP_AUTORPT_EN_MASK) != 0;
        if (active)
            starts++;
    }
    return 0;
}

static int regmap_read(void *map, unsigned int reg, void *output)
{
    unsigned int *value = output;
    int rc = record('r', reg, 0, 0);
    if (rc)
        return rc;
    if (reg == MT6358_AUXADC_IMP0 && active) {
        poll_count++;
        if (ready_after >= 0 && poll_count > ready_after &&
            (registers[MT6358_AUXADC_DCM_CON] & MT6358_IMP_CK_SW_MASK) == MT6358_IMP_CK_SW_MASK) {
            registers[reg] |= MT6358_IMP_IRQ_RDY_BIT;
            registers[MT6358_AUXADC_ADC28] = next_voltage;
            registers[MT6358_IMP_FGADC_R_CON0] = next_current;
        }
    }
    *value = registers[reg];
    if (io_count == corrupt_at)
        *value |= corrupt_mask;
    return 0;
}

static void udelay(unsigned int delay) { CHECK(delay == IMP_POLL_DELAY_US); delay_count++; }
static void pm_stay_awake(void *device) { CHECK(adc.lock.held); CHECK(wake_count == 0); wake_count++; lock_calls++; }
static void pm_relax(void *device) { CHECK(adc.lock.held); CHECK(wake_count == 1); wake_count--; }
static void imp_timeout_handler(struct mt635x_auxadc_device *device, bool timeout) { if (timeout) resets++; }
static int auxadc_bootstrap_imix_r(struct mt635x_auxadc_device *device)
{
    CHECK(device->lock.held);
    return -ENODATA;
}
static int mt6358_recover_dummy_load(struct mt635x_auxadc_device *device)
{
    CHECK(device->lock.held);
    return 0;
}
static int auxadc_get_imp_ibat(struct mt635x_auxadc_device *device) { return 17; }
static int get_auxadc_out(struct mt635x_auxadc_device *device, const struct auxadc_channels *channel, int *value)
{
    CHECK(device->lock.held);
    if (general_error)
        return general_error;
    *value = 1000;
    return 0;
}
static int calibration(struct mt635x_auxadc_device *device, int value, int precision)
{
    CHECK(device->lock.held);
    CHECK(wake_count == 1);
    CHECK(precision == 10);
    return cali_error ? cali_error : value + 7;
}
static int mt6358_imp_conv(struct mt635x_auxadc_device *, int *, int *);
static void mt6358_imp_stop(struct mt635x_auxadc_device *);
static struct auxadc_info info = { .imp_conv = mt6358_imp_conv, .imp_stop = mt6358_imp_stop };
