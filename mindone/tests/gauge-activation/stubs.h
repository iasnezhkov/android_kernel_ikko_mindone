/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define smp_load_acquire(pointer) (*(pointer))
#define smp_store_release(pointer, value) (*(pointer) = (value))
static int initialization_calls;
static int setup_calls;
static int publication_calls;
static int temperature_error;
static int sample_error;
static bool allocation_error;
static bool profile_error;
static bool profile_order_error;
static bool profile_missing;
typedef long ssize_t;
typedef unsigned int gfp_t;
struct device { int unused; };
struct device_attribute { int unused; };
#define GFP_KERNEL 0
#define WRITE_ONCE(value, new_value) ((value) = (new_value))
struct fuelgauge_profile_struct { int value; };
static struct fuelgauge_profile_struct profile[100];

static void *kmemdup(const void *source, size_t length, gfp_t flags)
{
	if (allocation_error)
		return NULL;
	void *copy = malloc(length);
	assert(copy);
	memcpy(copy, source, length);
	return copy;
}

static void kfree(void *pointer)
{
	free(pointer);
}

static int force_get_tbat_internal(struct mtk_battery *gm, bool update, int *value)
{
	if (temperature_error)
		return temperature_error;
	*value = -5;
	return 0;
}

static int gauge_read_ptim_sample(struct mtk_gauge *gauge,
	struct gauge_ptim_sample *sample)
{
	int ret = io_step(false);
	if (ret)
		return ret;
	if (sample_error)
		return sample_error;
	if (gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] <= 0)
		return -ENODATA;
	*sample = (struct gauge_ptim_sample) {
		.sequence = 1,
		.voltage_01mv = gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] * 10,
		.current_01ma = -1000,
	};
	return 0;
}

static struct fuelgauge_profile_struct *fg_get_profile(struct mtk_battery *gm, int table)
{
	return profile_missing ? NULL : profile;
}

static int fg_check_temperature_order(struct mtk_battery *gm, int *ascending, int *descending)
{
	return profile_order_error;
}

static void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	lock->held = true;
}

static void mutex_unlock(struct mutex *lock)
{
	assert(lock->held);
	lock->held = false;
}

static void fgr_construct_battery_profile(struct mtk_battery *gm, int table)
{
	initialization_calls++;
	for (int i = 0; i < 100; i++)
		profile[i].value = 777 + i;
	gm->algo.quse_tb1 = profile_error ? 0 : 20000;
}

static void fgr_construct_vboot_sample(struct mtk_battery *gm, int table,
	int temperature, int rac, int voltage, int current_sample)
{
}

static void fgr_dump_table(struct mtk_battery *gm, int table)
{
}

static void fgr_update_fg_bat_int1_threshold(struct mtk_battery *gm)
{
	setup_calls++;
	gm->algo.fg_bat_int1_gap = 100;
}

static void fgr_update_uisoc_threshold(struct mtk_battery *gm)
{
	gm->algo.ht_gap = 100;
	gm->algo.lt_gap = 100;
}

static int gauge_set_property(int property, int value)
{
	assert(property >= 0 && property < GAUGE_PROP_COUNT);
	int ret = io_step(true);
	if (ret)
		return ret;
	gauge_writes[property]++;
	gauge_values[property] = property == GAUGE_PROP_CON0_SOC ? value / 100 : value;
	if (background_error && property == GAUGE_PROP_IS_NVRAM_FAIL_MODE)
		active_gm->kernel_setup_error = background_error;
	return 0;
}

static int battery_kernel_setup_ready(struct mtk_battery *gm)
{
	return gm->kernel_setup_error ? -EUCLEAN : 0;
}

static struct mtk_battery *get_mtk_battery(void)
{
	return active_gm;
}

static int kstrtouint(const char *text, unsigned int radix, unsigned int *value)
{
	char *end;
	unsigned long parsed = strtoul(text, &end, radix);
	if (end == text || (*end && strcmp(end, "\n") != 0))
		return -EINVAL;
	*value = parsed;
	return 0;
}

static void battery_update(struct mtk_battery *gm)
{
	assert(gm->algo.active && gm->init_flag == 1 && !gm->algo_lock.held);
	publication_calls++;
}
