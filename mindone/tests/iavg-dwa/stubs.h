/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t s64;
#define current get_current()
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define READ_ONCE(x) (x)
#define check_mul_overflow(a, b, out) __builtin_mul_overflow(a, b, out)
#define bm_err(...) ((void)0)
#define bm_debug(...) ((void)0)
#define do_div(a, b) ((a) /= (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static unsigned int checks, cases;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
struct mutex { bool held; };
static void mutex_lock(struct mutex *lock) { CHECK(!lock->held); lock->held = true; }
static void mutex_unlock(struct mutex *lock) { CHECK(lock->held); lock->held = false; }
#define lockdep_assert_held(lock) CHECK((lock)->held)
static s64 div_s64(s64 value, int denominator) { CHECK(denominator > 0); return value / denominator; }
struct mtk_battery;
struct mtk_gauge {
    void *regmap;
    struct mtk_battery *gm;
    struct mutex ops_lock;
    struct { int r_fg_value, meta_current, tmp_car_tune; } hw_status;
    struct { int current_2; } fg_hw_info;
};
struct mtk_battery {
    struct mtk_gauge *gauge;
    struct { int car_tune_value; } fg_cust_data;
};
struct mtk_gauge_sysfs_field_info { int unused; };
enum gauge_event { EVT_INT_IAVG, EVT_INT_BAT_INT2_HT, EVT_INT_BAT_INT2_LT, EVT_INT_NAFG_CHECK, EVT_OTHER };
struct operation { char kind; unsigned int address, mask, value; };
static struct operation trace[8192];
static unsigned int registers[0x2000];
static struct mtk_gauge gauge;
static struct mtk_battery gm;
static int operations, fault_at, fault_at2, operation_errno, secondary_errno;
static int unapplied_at, corrupt_at, change_calibration_at;
static unsigned int corrupt_mask;
static bool ambiguous_error, start_stuck, clear_stuck;
static int dwa_writes, high_reads, low_reads, sample_reads, latch_reads;
static int cali_calls, nafg_calls;

static int record(char kind, unsigned int address, unsigned int mask, unsigned int value)
{
    CHECK(gauge.ops_lock.held);
    CHECK(address < ARRAY_SIZE(registers));
    CHECK(operations < (int)ARRAY_SIZE(trace));
    trace[operations++] = (struct operation){kind, address, mask, value};
    if (operations == change_calibration_at) {
        gauge.hw_status.r_fg_value = -44;
        gm.fg_cust_data.car_tune_value = INT_MAX;
    }
    if (operations == fault_at)
        return operation_errno;
    return operations == fault_at2 ? secondary_errno : 0;
}

static int regmap_read(void *map, unsigned int address, unsigned int *value)
{
    int ret = record('r', address, 0, 0);
    CHECK(map == &registers);
    if (address == RG_FGADC_CON1)
        latch_reads++;
    else if (address == RG_FGADC_IAVG_CON1)
        high_reads++;
    else if (address == RG_FGADC_IAVG_CON0)
        low_reads++;
    if (address == RG_FGADC_CUR_CON3 || address == RG_FGADC_OFFSET_CON0 ||
        address == RG_FGADC_IAVG_CON1 || address == RG_FGADC_IAVG_CON0) {
        sample_reads++;
        CHECK(registers[RG_FGADC_CON1] & FG_LATCHDATA_ST_MASK);
    }
    if (ret)
        return ret;
    *value = registers[address];
    if (operations == corrupt_at) {
        *value ^= corrupt_mask;
        registers[address] = *value;
    }
    return 0;
}

static int regmap_write(void *map, unsigned int address, unsigned int value)
{
    int ret = record('w', address, 0, value);
    CHECK(map == &registers && address == RG_FGADC_CON1 && value == 1);
    if ((ret && !ambiguous_error) || operations == unapplied_at)
        return ret;
    registers[address] = value | (start_stuck ? 0 : FG_LATCHDATA_ST_MASK);
    return ret;
}

static int regmap_update_bits(void *map, unsigned int address, unsigned int mask, unsigned int value)
{
    int ret = record('u', address, mask, value);
    CHECK(map == &registers);
    if (address == RG_FGADC_CON1) {
        CHECK(mask == (FG_SW_CLEAR_MASK | FG_SW_READ_PRE_MASK));
        CHECK(value == FG_SW_CLEAR_MASK || value == 0);
    } else {
        CHECK(address == RG_FGADC_ANA_TEST_CON0 || address == RG_FGADC_ANA_CON0);
        dwa_writes++;
    }
    if ((ret && !ambiguous_error) || operations == unapplied_at)
        return ret;
    registers[address] = (registers[address] & ~mask) | (value & mask);
    if (address == RG_FGADC_CON1 && value == FG_SW_CLEAR_MASK && !clear_stuck)
        registers[address] &= ~FG_LATCHDATA_ST_MASK;
    return ret;
}

static int calculate_car_tune(struct mtk_gauge *target)
{
    CHECK(target == &gauge && target->ops_lock.held);
    CHECK(target->hw_status.meta_current == 123);
    cali_calls++;
    target->hw_status.tmp_car_tune = 1050;
    return 0;
}

static int nafg_check_corner(struct mtk_gauge *target)
{
    CHECK(target == &gauge && target->ops_lock.held);
    nafg_calls++;
    return 0;
}
