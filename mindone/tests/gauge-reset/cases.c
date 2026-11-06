// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct mtk_battery gm;
static struct mtk_gauge gauge;
static struct gauge_consumer plus;
static struct gauge_consumer minus;

static void setup(bool kernel, int origin)
{
	assert(held_count == 0);
	gm = (struct mtk_battery){
		.algo_lock = { .rank = 10 },
		.algo = {
			.active = kernel, .fg_c_d0_car = origin - 1000,
			.prev_car_bat0 = origin - 200,
			.zcv_learn_car = origin - 1300, .zcv_learn_valid = true,
			.sw_cycle_car = origin + 300, .sw_cycle_valid = true,
			.sw_cycle_ncar = 1700, .car = origin,
			.fg_c_d0_ocv = 40000, .quse_tb1 = 20000, .aging_factor = 9875,
		},
		.gauge = &gauge,
		.cs = { .init = true, .coulomb_lock = { .rank = 20 }, .hw_coulomb_lock = { .rank = 30 } },
		.log = { .car_diff = 100 },
		.bat_cycle_car = origin,
		.model_capacity_mah = 2000,
	};
	gauge = (struct mtk_gauge){
		.ops_lock = { .rank = 40 }, .gm = &gm, .attr = gauge_attrs,
		.charge_counter_valid = true, .charge_counter_car = origin,
		.charge_counter_uah = 1200000, .charge_counter_full_uah = 2000000,
		.coulomb_car = origin, .regmap = &gauge,
	};
	init_head(&gm.cs.coulomb_head_plus);
	init_head(&gm.cs.coulomb_head_minus);
	plus = (struct gauge_consumer){ .name = "plus", .start = origin, .end = origin + 100, .variable = 100 };
	minus = (struct gauge_consumer){ .name = "minus", .start = origin, .end = origin - 200, .variable = -200 };
	add_tail(&plus.list, &gm.cs.coulomb_head_plus);
	add_tail(&minus.list, &gm.cs.coulomb_head_minus);
	gauge_psy = (struct power_supply){ .data = &gauge };
	battery_psy = (struct power_supply){ .data = &gm };
	missing_gauge_psy = false;
	missing_battery_psy = false;
	hw_car = origin;
	car_reads = 0;
	read_error_at = 0;
	post_read_error = false;
	reset_error = 0;
	reset_increment = 0;
	hardware_writes = 0;
	reset_done = false;
	advance_read = 0;
	advance_delta = 0;
	daemon_cycle_calls = 0;
	wakes = 0;
	memset(interrupt_values, 0, sizeof(interrupt_values));
	interrupt_error = interrupt_calls = interrupt_error_at = 0;
	interrupt_clears_setup = false;
}

static void check_idle(void)
{
	assert(held_count == 0);
	assert(!gm.algo_lock.held && !gm.cs.coulomb_lock.held);
	assert(!gm.cs.hw_coulomb_lock.held && !gauge.ops_lock.held);
	assert(gauge_psy.refs == 0 && battery_psy.refs == 0);
	assert(gm.cs.coulomb_head_plus.next == &plus.list);
	assert(gm.cs.coulomb_head_minus.next == &minus.list);
	assert(plus.list.next == &gm.cs.coulomb_head_plus);
	assert(minus.list.next == &gm.cs.coulomb_head_minus);
}

static void check_kernel(void)
{
	const int origins[] = { 2000, -2000 };
	for (size_t i = 0; i < sizeof(origins) / sizeof(origins[0]); i++) {
		setup(true, origins[i]);
		fgr_update_c_dod(&gm);
		assert(gm.algo.fg_c_soc == 6500);
		assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
		fgr_update_c_dod(&gm);
		assert(gm.algo.fg_c_soc == 6500);
		assert(gm.algo.fg_c_d0_car == -1000 && gm.algo.prev_car_bat0 == -200);
		assert(gm.algo.zcv_learn_car == -1300 && gm.algo.sw_cycle_car == 0);
		assert(gm.algo.sw_cycle_ncar == 2000 && gm.bat_cycle == 0);
		assert(gm.algo.aging_factor == 9875 && gm.algo.quse_tb1 == 20000);
		assert(plus.end == 100 && minus.end == -200);
		assert(plus.start == 0 && minus.start == 0);
		assert(gauge.charge_counter_uah == 1200000 && daemon_cycle_calls == 0);
		assert(wakes == 1 && hardware_writes == 1);
		hw_car = -200;
		fgr_update_c_dod(&gm);
		assert(gm.algo.fg_c_soc == 6400);
		assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
		fgr_update_c_dod(&gm);
		assert(gm.algo.fg_c_soc == 6400 && gm.algo.sw_cycle_ncar == 2200);
		assert(gm.algo.fg_c_d0_car == -800 && gm.algo.prev_car_bat0 == 0);
		assert(gm.algo.zcv_learn_car == -1100 && gm.algo.sw_cycle_car == 0);
		assert(plus.end == 300 && minus.end == 0);
		check_idle();
	}
}

static void check_daemon(void)
{
	setup(false, 2000);
	struct mtk_battery_algo before;
	memcpy(&before, &gm.algo, sizeof(before));
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert(memcmp(&before, &gm.algo, sizeof(before)) == 0);
	assert(daemon_cycle_calls == 1 && plus.end == 100 && minus.end == -200);
	assert(gauge.charge_counter_uah == 1200000 && gm.bat_cycle_car == 0);
	check_idle();
}

static void check_origin(void)
{
	setup(true, 2000);
	advance_read = 2;
	advance_delta = -100;
	reset_increment = 2;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert((int)gm.cs.reset_coulomb == 1900);
	assert(gm.algo.fg_c_d0_car == -900 && gm.algo.prev_car_bat0 == -100);
	assert(gm.algo.zcv_learn_car == -1200 && gm.algo.sw_cycle_car == 0);
	assert(gm.algo.sw_cycle_ncar == 2100 && gm.log.car_diff == 2000);
	assert(plus.end == 200 && minus.end == -100);
	assert(gauge.charge_counter_uah == 1190200);
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6451);
	check_idle();
}

static void check_retry(void)
{
	setup(true, 2000);
	gm.algo.sw_cycle_ncar = 19750;
	reset_error = -EIO;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == -EIO);
	assert(gm.algo.sw_cycle_ncar == 50 && gm.bat_cycle == 1);
	assert(gm.algo.sw_cycle_car == 2000 && gm.algo.fg_c_d0_car == 1000);
	assert(gm.algo.prev_car_bat0 == 1800 && gm.algo.zcv_learn_car == 700);
	assert(plus.end == 2100 && minus.end == 1800 && !reset_done);
	assert(wakes == 1 && gm.log.car_diff == 100);
	reset_error = 0;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert(gm.algo.sw_cycle_ncar == 50 && gm.bat_cycle == 1);
	assert(gm.algo.sw_cycle_car == 0 && gm.algo.fg_c_d0_car == -1000);
	assert(wakes == 2 && hardware_writes == 2);
	check_idle();
}

static void check_read_errors(void)
{
	for (int failure = 1; failure <= 3; failure++) {
		setup(true, 2000);
		read_error_at = failure;
		assert(battery_set_property(BAT_PROP_FG_RESET, 0) == -EIO);
		assert(!reset_done && hardware_writes == 0);
		assert(gm.algo.fg_c_d0_car == 1000 && gm.algo.prev_car_bat0 == 1800);
		assert(gm.algo.zcv_learn_car == 700 && gm.log.car_diff == 100);
		assert(plus.end == 2100 && minus.end == 1800);
		if (failure == 3) {
			assert(gm.algo.sw_cycle_ncar == 2000 && wakes == 1);
			assert(interrupt_values[GAUGE_PROP_COULOMB_HT_INTERRUPT] == 300);
			assert(interrupt_values[GAUGE_PROP_COULOMB_LT_INTERRUPT] == 300);
		} else
			assert(gm.algo.sw_cycle_ncar == 1700 && wakes == 0);
		check_idle();
	}
}

static void check_write_error(void)
{
	setup(false, 2000);
	reset_error = -EACCES;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == -EACCES);
	assert(plus.end == 2100 && minus.end == 1800 && wakes == 1);
	assert(gm.log.car_diff == 100 && gm.bat_cycle_car == 2000);
	assert(hw_car == 2000 && !reset_done);
	check_idle();
}

static void check_post_read_error(void)
{
	setup(true, 2000);
	post_read_error = true;
	reset_increment = 2;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert(reset_done && gauge.coulomb_car == 0);
	assert(gm.algo.fg_c_d0_car == -1000 && gm.algo.sw_cycle_car == 0);
	assert(plus.end == 100 && minus.end == -200);
	assert(gauge.charge_counter_uah == 1200000);
	post_read_error = false;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6501);
	int car;
	assert(gauge_get_property(GAUGE_PROP_COULOMB, &car) == 0 && car == 2);
	charge_counter_update(&gauge, car);
	assert(gauge.charge_counter_uah == 1200200);
	check_idle();
}

static void check_no_cs(void)
{
	setup(true, -2000);
	gm.cs.init = false;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert(gm.algo.fg_c_d0_car == -1000 && gm.algo.sw_cycle_car == 0);
	assert(wakes == 0 && plus.end == -1900 && minus.end == -2200);
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6500);
	check_idle();
}

static void check_disabled(void)
{
	setup(true, 2000);
	gm.disableGM30 = true;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == 0);
	assert(hardware_writes == 0 && car_reads == 0 && wakes == 0);
	assert(gm.algo.fg_c_d0_car == 1000);
	check_idle();
}

static void check_locked(void)
{
	setup(true, 2000);
	mutex_lock(&gm.algo_lock);
	assert(fg_adc_reset(&gm) == 0);
	mutex_unlock(&gm.algo_lock);
	assert(gm.algo.fg_c_d0_car == -1000 && gm.algo.sw_cycle_ncar == 2000);
	setup(true, 2000);
	reset_error = -EIO;
	mutex_lock(&gm.algo_lock);
	assert(fg_adc_reset(&gm) == -EIO);
	mutex_unlock(&gm.algo_lock);
	assert(gm.algo.fg_c_d0_car == 1000);
	check_idle();
}

static void check_raw_sysfs(void)
{
	setup(true, 2000);
	struct device dev = { .data = &gauge_psy };
	assert(gauge_sysfs_store(&dev, &gauge_attrs[GAUGE_PROP_RESET].attr, "1\n", 2) == 2);
	assert(gm.algo.fg_c_d0_car == -1000 && plus.end == 100);
	assert(gm.algo.sw_cycle_ncar == 2000);
	setup(false, 2000);
	assert(gauge_sysfs_store(&dev, &gauge_attrs[GAUGE_PROP_RESET].attr, "1\n", 2) == 2);
	assert(gm.algo.fg_c_d0_car == 1000 && plus.end == 100);
	assert(daemon_cycle_calls == 1);
	setup(true, 2000);
	reset_error = -EIO;
	assert(gauge_sysfs_store(&dev, &gauge_attrs[GAUGE_PROP_RESET].attr, "1\n", 2) == -EIO);
	assert(gm.algo.fg_c_d0_car == 1000 && plus.end == 2100);
	assert(gauge_sysfs_store(&dev, &gauge_attrs[GAUGE_PROP_RESET].attr, "bad", 3) == -EINVAL);
	check_idle();
}

static void check_wrappers(void)
{
	setup(false, 2000);
	reset_error = -EIO;
	assert(gauge_set_property(GAUGE_PROP_RESET, 0) == -EIO);
	assert(gauge_set_property(GAUGE_PROP_READONLY, 0) == -ENOTSUPP);
	assert(battery_set_property(BAT_PROP_READONLY, 0) == -ENOTSUPP);
	missing_gauge_psy = true;
	assert(gauge_set_property(GAUGE_PROP_RESET, 0) == -ENODEV);
	missing_gauge_psy = false;
	struct mtk_gauge_sysfs_field_info *saved = gauge.attr;
	gauge.attr = NULL;
	assert(gauge_set_property(GAUGE_PROP_RESET, 0) == -ENODEV);
	gauge.attr = saved;
	missing_battery_psy = true;
	assert(battery_set_property(BAT_PROP_FG_RESET, 0) == -ENODEV);
	missing_battery_psy = false;
	check_idle();
}

static void check_initializing(void)
{
	setup(false, 2000);
	struct mtk_battery_algo before = gm.algo;
	mutex_lock(&gm.algo_lock);
	assert(battery_reset_charge_init_locked(&gm) == 0);
	mutex_unlock(&gm.algo_lock);
	assert(memcmp(&gm.algo, &before, sizeof(before)) == 0);
	assert(daemon_cycle_calls == 0 && reset_done && hw_car == 0);
	assert(plus.end == 100 && minus.end == -200);
	setup(false, 2000);
	before = gm.algo;
	reset_error = -EIO;
	mutex_lock(&gm.algo_lock);
	assert(battery_reset_charge_init_locked(&gm) == -EIO);
	mutex_unlock(&gm.algo_lock);
	assert(memcmp(&gm.algo, &before, sizeof(before)) == 0);
	assert(daemon_cycle_calls == 0 && !reset_done && hw_car == 2000);
	assert(plus.end == 2100 && minus.end == 1800);
	check_idle();
}

static void check_setup_tracking(void)
{
	setup(false, 2000);
	gm.kernel_setup_in_progress = true;
	interrupt_error = -EIO;
	assert(gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 50) == -EIO);
	assert(gm.kernel_setup_mutated && gm.kernel_setup_error == -EIO);
	interrupt_error = -EACCES;
	assert(gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT, 40) == -EACCES);
	assert(gm.kernel_setup_error == -EIO);
	gm.kernel_setup_in_progress = false;
	gm.kernel_setup_mutated = false;
	gm.kernel_setup_error = 0;
	assert(gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 60) == -EACCES);
	assert(!gm.kernel_setup_mutated && gm.kernel_setup_error == 0);
	gm.kernel_setup_in_progress = true;
	interrupt_clears_setup = true;
	assert(gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 70) == -EACCES);
	assert(!gm.kernel_setup_in_progress && gm.kernel_setup_mutated && gm.kernel_setup_error == -EACCES);
	setup(false, 2000);
	gm.kernel_setup_in_progress = true;
	interrupt_error = -ETIMEDOUT;
	interrupt_error_at = 1;
	mutex_lock(&gm.algo_lock);
	assert(battery_reset_charge_init_locked(&gm) == -ETIMEDOUT);
	mutex_unlock(&gm.algo_lock);
	assert(gm.kernel_setup_mutated && gm.kernel_setup_error == -ETIMEDOUT);
	assert(hardware_writes == 0 && !reset_done && wakes == 0 && hw_car == 2000 && interrupt_calls == 1);
	check_idle();
}

static void check_threshold_setup(void)
{
	for (int sign = -1; sign <= 1; sign += 2) {
		setup(false, 2000);
		struct gauge_consumer *consumer = sign > 0 ? &plus : &minus;
		struct gauge_consumer before = *consumer;
		read_error_at = 1;
		assert(gauge_coulomb_start(&gm, consumer, sign * 80) == -EIO);
		assert(memcmp(consumer, &before, sizeof(before)) == 0);
		assert(held_count == 0 && wakes == 0);
		read_error_at = 0;
		interrupt_error = -ETIMEDOUT;
		assert(gauge_coulomb_start(&gm, consumer, sign * 80) == -ETIMEDOUT);
		assert(consumer->start == 2000 && consumer->end == 2000 + sign * 80);
		assert(held_count == 0 && wakes == 0);
		interrupt_error = 0;
		assert(gauge_coulomb_start(&gm, consumer, sign * 90) == 0);
		assert(consumer->end == 2000 + sign * 90);
		gm.cs.init = false;
		assert(gauge_coulomb_start(&gm, consumer, sign * 100) == -EAGAIN);
		assert(consumer->end == 2000 + sign * 90);
		assert(gauge_coulomb_start(&gm, consumer, 0) == 0);
	}
	for (int callback = 0; callback < 3; callback++) {
		setup(false, 2000);
		init_head(&gm.coulomb_plus.list);
		init_head(&gm.coulomb_minus.list);
		init_head(&gm.uisoc_plus.list);
		init_head(&gm.uisoc_minus.list);
		interrupt_error = -EACCES;
		interrupt_error_at = 1;
		int ret = callback == 0 ? coulomb_int_gap_set(&gm, NULL, 75) :
			callback == 1 ? uisoc_ht_int_gap_set(&gm, NULL, 75) :
			uisoc_lt_int_gap_set(&gm, NULL, 75);
		assert(ret == -EACCES && held_count == 0);
		assert(gm.coulomb_int_gap == 0 && gm.uisoc_int_ht_gap == 0 && gm.uisoc_int_lt_gap == 0);
		interrupt_error = 0;
		ret = callback == 0 ? coulomb_int_gap_set(&gm, NULL, 75) :
			callback == 1 ? uisoc_ht_int_gap_set(&gm, NULL, 75) :
			uisoc_lt_int_gap_set(&gm, NULL, 75);
		assert(ret == 0 && held_count == 0);
		if (callback == 0) {
			assert(gm.coulomb_int_gap == 75 && gm.coulomb_int_ht == 2075 && gm.coulomb_int_lt == 1925);
		} else if (callback == 1) assert(gm.uisoc_int_ht_gap == 75);
		else assert(gm.uisoc_int_lt_gap == 75);
	}
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "kernel") == 0)
		check_kernel();
	else if (strcmp(argv[1], "daemon") == 0)
		check_daemon();
	else if (strcmp(argv[1], "origin") == 0)
		check_origin();
	else if (strcmp(argv[1], "retry") == 0)
		check_retry();
	else if (strcmp(argv[1], "read-errors") == 0)
		check_read_errors();
	else if (strcmp(argv[1], "write-error") == 0)
		check_write_error();
	else if (strcmp(argv[1], "post-read-error") == 0)
		check_post_read_error();
	else if (strcmp(argv[1], "no-cs") == 0)
		check_no_cs();
	else if (strcmp(argv[1], "disabled") == 0)
		check_disabled();
	else if (strcmp(argv[1], "locked") == 0)
		check_locked();
	else if (strcmp(argv[1], "raw-sysfs") == 0)
		check_raw_sysfs();
    else if (strcmp(argv[1], "wrappers") == 0)
        check_wrappers();
    else if (strcmp(argv[1], "initializing") == 0)
        check_initializing();
	else if (strcmp(argv[1], "threshold-setup") == 0)
		check_threshold_setup();
	else if (strcmp(argv[1], "setup-tracking") == 0)
		check_setup_tracking();
	else
		abort();
	printf("PASS %s\n", argv[1]);
	return 0;
}
