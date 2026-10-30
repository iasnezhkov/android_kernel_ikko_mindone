/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t s64;
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define AUXADC_DEBUG 1
#define current get_current()
static atomic_int checks;
static int cases;
static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_t lock_owner;
static bool racing;
static int contenders;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "CHECK failed line=%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define bm_debug(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define dev_notice(...) ((void)0)
static s64 div_s64(s64 value, int divisor) { return value / divisor; }
struct mutex { bool held; };
#define lockdep_assert_held(p) CHECK((p)->held && pthread_equal(lock_owner, pthread_self()))
struct regmap { int unused; };
struct device { void *data; };
struct auxadc_regs { int unused; };
struct iio_chan_spec { int channel; };
struct mt635x_auxadc_device;
struct auxadc_channels {
    unsigned char r_ratio[2];
    unsigned char res;
    const struct auxadc_regs *regs;
    int (*cali_fn)(struct mt635x_auxadc_device *, int, int);
};
@CONSTANTS@
@STRUCTURES@
static struct mt635x_auxadc_device adc;
static struct device device = { .data = &adc };
struct iio_dev { struct mt635x_auxadc_device *data; };
static struct iio_dev iio = { .data = &adc };
#define iio_priv(p) ((p)->data)
static struct auxadc_channels auxadc_chans[AUXADC_IMP_SAMPLE + 1];
@ARRAYS@
static unsigned int registers[0x2000];
static int io_count, failures[128], drops[128], apply_failed[128];
static unsigned int read_bits[128];
static int off_writes, on_writes, reads, samples_count, checked_samples;
static int sleep_count, sleep_ms, locks, unlocks, awake, relaxes;
static int cleanup_logs, calibration_logs, last_primary, last_cleanup;
static int imp_cleanup_calls, imp_cleanup_error;
static bool gauge_enabled;
static int ui_soc, next_soc = 200;
struct sample { int ret, raw, ibat; };
static struct sample samples[16];
static int mt6358_imp_conv(struct mt635x_auxadc_device *a, int *v, int *i)
{
    CHECK(a->lock.held);
    CHECK(samples_count < (int)ARRAY_SIZE(samples));
    if (samples_count & 1)
        CHECK((registers[MT6358_ISINK_EN_CTRL_SMPL] & 0x303) == 0x303);
    else
        CHECK((registers[MT6358_ISINK_EN_CTRL_SMPL] & 0x303) == 0);
    struct sample next = samples[samples_count++];
    *v = next.raw;
    *i = next.ibat;
    a->imp_vbat = next.raw;
    return next.ret;
}
static int other_imp_conv(struct mt635x_auxadc_device *a, int *v, int *i)
{
    return mt6358_imp_conv(a, v, i);
}
static struct auxadc_info info = {
    .en_isink_setting = mt6358_en_isink_setting,
    .num_en_isink_setting = ARRAY_SIZE(mt6358_en_isink_setting),
    .dis_isink_setting = mt6358_dis_isink_setting,
    .num_dis_isink_setting = ARRAY_SIZE(mt6358_dis_isink_setting),
    .imp_conv = mt6358_imp_conv,
};
static void mutex_lock(struct mutex *lock)
{
    if (racing) {
        assert(pthread_mutex_lock(&start_lock) == 0);
        contenders++;
        assert(pthread_cond_signal(&start_cond) == 0);
        assert(pthread_mutex_unlock(&start_lock) == 0);
    }
    assert(pthread_mutex_lock(&host_lock) == 0);
    CHECK(!lock->held);
    lock_owner = pthread_self();
    lock->held = true;
    locks++;
}
static void mutex_unlock(struct mutex *lock)
{
    lockdep_assert_held(lock);
    lock->held = false;
    unlocks++;
    assert(pthread_mutex_unlock(&host_lock) == 0);
}
static void *dev_get_drvdata(struct device *dev) { return dev->data; }
static void pm_stay_awake(struct device *dev) { CHECK(adc.lock.held); CHECK(!awake); awake++; }
static void pm_relax(struct device *dev) { CHECK(adc.lock.held); CHECK(awake == 1); awake--; relaxes++; }
static int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int value)
{
    CHECK(adc.lock.held);
    CHECK(reg < ARRAY_SIZE(registers));
    CHECK(io_count + 1 < (int)ARRAY_SIZE(failures));
    ++io_count;
    if (value) {
        on_writes++;
        if (info.imp_conv == mt6358_imp_conv)
            CHECK(adc.load_cleanup_pending);
    } else {
        CHECK(reg == MT6358_ISINK_EN_CTRL_SMPL);
        off_writes++;
    }
    if ((!failures[io_count] || apply_failed[io_count]) && !drops[io_count])
        registers[reg] = (registers[reg] & ~mask) | (value & mask);
    return failures[io_count];
}
static int regmap_read(struct regmap *map, unsigned int reg, unsigned int *value)
{
    CHECK(adc.lock.held);
    CHECK(reg == MT6358_ISINK_EN_CTRL_SMPL);
    CHECK(io_count + 1 < (int)ARRAY_SIZE(failures));
    ++io_count;
    reads++;
    if (failures[io_count])
        return failures[io_count];
    *value = registers[reg] | read_bits[io_count];
    return 0;
}
static void msleep(unsigned int ms) { CHECK(adc.lock.held); CHECK(ms == 50); sleep_count++; sleep_ms += ms; }
static void dev_err(struct device *dev, const char *format, int cleanup, int primary)
{
    CHECK(adc.lock.held); cleanup_logs++; last_primary = primary; last_cleanup = cleanup;
}
static void dev_warn(struct device *dev, const char *format, int status)
{
    CHECK(adc.lock.held); CHECK(status != 0); calibration_logs++;
}
static int mt6358_imp_cleanup(struct mt635x_auxadc_device *a)
{
    CHECK(a->lock.held); imp_cleanup_calls++;
    if (!imp_cleanup_error) a->imp_cleanup_pending = false;
    return imp_cleanup_error;
}
static int mt6358_imp_sample(struct mt635x_auxadc_device *a, int *v, int *i)
{
    CHECK(a->lock.held);
    CHECK((registers[MT6358_ISINK_EN_CTRL_SMPL] & 0x303) == 0);
    checked_samples++; *v = 26000; *i = 123;
    return 0;
}
static int get_auxadc_out(struct mt635x_auxadc_device *a, const struct auxadc_channels *c, int *out)
{
    CHECK(a->lock.held); *out = 17; return 0;
}
static int mt635x_bat_temp_cali(struct mt635x_auxadc_device *a, int x, int y)
{
    CHECK(adc.lock.held); CHECK(!a && x == -1 && y == -1); return 0;
}
static void *get_mtk_gauge_psy(void) { return gauge_enabled ? &adc : NULL; }
static int auxadc_get_uisoc(void) { return ui_soc; }

@FUNCTIONS@

static void reset(void)
{
    CHECK(!adc.lock.held);
    memset(&adc, 0, sizeof(adc));
    memset(registers, 0, sizeof(registers));
    memset(failures, 0, sizeof(failures));
    memset(drops, 0, sizeof(drops));
    memset(apply_failed, 0, sizeof(apply_failed));
    memset(read_bits, 0, sizeof(read_bits));
    memset(auxadc_chans, 0, sizeof(auxadc_chans));
    adc.info = &info; adc.dev = &device; adc.imix_r = 137;
    info.imp_conv = mt6358_imp_conv;
    for (size_t i = 0; i < ARRAY_SIZE(samples); i++)
        samples[i] = (struct sample){0, i & 1 ? 25900 : 26000, i & 1 ? 1000 : 0};
    auxadc_chans[AUXADC_IMP].r_ratio[0] = 3;
    auxadc_chans[AUXADC_IMP].r_ratio[1] = 1;
    auxadc_chans[AUXADC_IMP].res = 15;
    auxadc_chans[AUXADC_IMP_SAMPLE] = auxadc_chans[AUXADC_IMP];
    io_count = off_writes = on_writes = reads = samples_count = checked_samples = 0;
    sleep_count = sleep_ms = locks = unlocks = awake = relaxes = 0;
    cleanup_logs = calibration_logs = last_primary = last_cleanup = 0;
    imp_cleanup_calls = imp_cleanup_error = 0;
    gauge_enabled = true;
    ui_soc = next_soc++;
    cases++;
}
static int run_rac(void)
{
    mutex_lock(&adc.lock); int ret = auxadc_get_rac(&adc); mutex_unlock(&adc.lock); return ret;
}
static int run_cali(void)
{
    mutex_lock(&adc.lock); int ret = auxadc_cali_imix_r(&adc); mutex_unlock(&adc.lock); return ret;
}
static void pending(void)
{
    adc.load_cleanup_pending = true; adc.load_cleanup_error = -EIO;
    registers[MT6358_ISINK_EN_CTRL_SMPL] = 0x303;
}
