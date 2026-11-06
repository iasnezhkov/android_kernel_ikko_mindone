/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BIT(bit) (1U << (bit))
#define GENMASK(high, low) (((1U << ((high) + 1)) - 1) & ~((1U << (low)) - 1))
#define do_div(number, divisor) ((number) /= (divisor))
#define bm_debug(...) mock_debug(__VA_ARGS__)
#define bm_err(...) mock_debug(__VA_ARGS__)

struct mtk_battery;
struct mtk_gauge_sysfs_field_info { int unused; };
struct mtk_gauge {
	void *regmap;
	struct mtk_battery *gm;
	struct { int r_fg_value; } hw_status;
	struct { int current_avg; int current_avg_sign; int current_avg_valid; int current_1; } fg_hw_info;
};
struct mtk_battery { struct { int car_tune_value; } fg_cust_data; };

enum operation { PRE_WRITE, PRE_READ, VALID_READ, UPPER_READ, LOWER_READ, INSTANT_READ, CLEAR_WRITE, CLEAR_READ, FINAL_CLEAR, OPERATION_COUNT };
static unsigned int counts[OPERATION_COUNT];
static enum operation fault;
static int fault_number;
static int fault_errno;
static bool pre_timeout;
static bool post_timeout;
static bool clearing;
static bool latched;
static int average_reads;
static unsigned int hardware_valid;
static unsigned int raw_average;
static unsigned int raw_instant;

static void mock_debug(const char *format, ...)
{
	(void)format;
}

static int operation(enum operation op)
{
	counts[op]++;
	return op == fault && (int)counts[op] == fault_number ? fault_errno : 0;
}

static int regmap_write(void *map, int address, int value)
{
	int ret;

	(void)map;
	assert(address == RG_FGADC_CON1 && value == 1);
	ret = operation(PRE_WRITE);
	if (!ret) {
		latched = true;
		clearing = false;
	}
	return ret;
}

static int regmap_update_bits(void *map, int address, int mask, int value)
{
	int ret;

	(void)map;
	assert(address == RG_FGADC_CON1);
	assert(mask == (FG_SW_CLEAR_MASK | FG_SW_READ_PRE_MASK));
	assert(value == 0 || value == FG_SW_CLEAR_MASK);
	ret = operation(value ? CLEAR_WRITE : FINAL_CLEAR);
	if (!ret) {
		clearing = value != 0;
		if (!value)
			latched = false;
	}
	return ret;
}

static int regmap_read(void *map, int address, void *output)
{
	unsigned int value;
	int ret;
	enum operation op;

	(void)map;
	if (address == RG_FGADC_CON1) {
		op = clearing ? CLEAR_READ : PRE_READ;
		value = clearing ? (post_timeout ? FG_LATCHDATA_ST_MASK : 0) : (!pre_timeout && latched ? FG_LATCHDATA_ST_MASK : 0);
	} else if (address == RG_FGADC_IAVG_CON1) {
		op = average_reads++ ? UPPER_READ : VALID_READ;
		value = ((raw_average >> 16) & FG_IAVG_27_16_MASK) | (hardware_valid << FG_IAVG_VLD_SHIFT);
	} else if (address == RG_FGADC_IAVG_CON0) {
		op = LOWER_READ;
		value = raw_average & 0xffff;
	} else {
		assert(address == RG_FGADC_CUR_CON0);
		op = INSTANT_READ;
		value = raw_instant;
	}
	ret = operation(op);
	if (!ret)
		memcpy(output, &value, sizeof(value));
	return ret;
}
