#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define bm_debug(...) mock_log(__VA_ARGS__)
#define bm_err(...) mock_log(__VA_ARGS__)
#define do_div(value, divisor) ((value) /= (divisor))

struct mtk_battery {
	struct {
		int r_fg_value;
		int car_tune_value;
	} fg_cust_data;
};

struct mtk_gauge {
	void *regmap;
	struct mtk_battery *gm;
};

struct mtk_gauge_sysfs_field_info {
	int unused;
};

static unsigned int register_value;
static int register_error;
static int register_calls;
static unsigned int expected_register;

static void mock_log(const char *format, ...)
{
}

static int regmap_read(void *regmap, unsigned int reg, void *value)
{
	assert(reg == expected_register);
	register_calls++;
	if (register_error)
		return register_error;
	memcpy(value, &register_value, sizeof(register_value));
	return 0;
}
