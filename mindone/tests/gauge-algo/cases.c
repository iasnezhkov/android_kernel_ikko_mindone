// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct mtk_battery fixture(void)
{
	memset(gauge_values, 0, sizeof(gauge_values));
	memset(gauge_errors, 0, sizeof(gauge_errors));
	memset(gauge_reads, 0, sizeof(gauge_reads));
	memset(battery_values, 0, sizeof(battery_values));
	reset_calls = 0;
	reset_error = 0;
	read_error = 0;
	charger_present = false;
	clock_now = 0;
	shutdown_seconds = 0;
	return (struct mtk_battery){
		.algo_lock = { .held = true },
		.model_capacity_mah = 2000,
		.algo = { .quse_tb1 = 20000, .qmax_t_0ma_tb1 = 20000, .aging_factor = 10000 },
		.fg_cust_data = { .shutdown_system_iboot = 5000, .discharge_tracking_time = 40, .sleep_current_avg = 100 },
	};
}

static void check_timer(void)
{
	struct mtk_battery gm = fixture();
	const ktime_t times[] = { 0, INT64_C(123456789), INT64_C(987654321012345), INT64_C(2147483647) * NSEC_PER_SEC + 999999999 };
	const int intervals[] = { 1, 40, 3600, 86400 };
	for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
		for (size_t j = 0; j < sizeof(intervals) / sizeof(intervals[0]); j++) {
			clock_now = times[i];
			set_fg_time(&gm, intervals[j]);
			assert(gm.tracking_timer.active);
			assert(gm.tracking_timer.deadline == times[i] + (ktime_t)intervals[j] * NSEC_PER_SEC);
		}
	}
	set_fg_time(&gm, 0);
	assert(!gm.tracking_timer.active);
	set_fg_time(&gm, -1);
	assert(!gm.tracking_timer.active);
	assert(gm.tracking_timer.starts == 16 && gm.tracking_timer.cancels == 2);
}

static void check_dlpt(void)
{
	struct mtk_battery gm = fixture();
	active_gm = &gm;
	gm.ui_soc = 5;
	gm.algo.ui_soc = 500;
	gm.algo.low_tracking_enable = 1;
	gm.tracking_timer.active = true;
	fgr_dlpt_sd_handler(&gm);
	assert(gm.algo.ui_soc == 0);
	assert(gm.ui_soc == 0 && battery_values[BAT_PROP_UISOC] == 0);
	assert(gm.algo.low_tracking_enable == 0 && !gm.tracking_timer.active);
	assert(battery_values[BAT_PROP_ENABLE_UISOC_HT_INT] == 0);
	assert(battery_values[BAT_PROP_ENABLE_UISOC_LT_INT] == 0);
	assert(reset_calls == 0);
	gm.imix = 100;
	fgr_dlpt_sd_handler(&gm);
	assert(reset_calls == 1);
	gm.imix = 500;
	fgr_dlpt_sd_handler(&gm);
	assert(reset_calls == 1);
}

static void check_boot(void)
{
	struct mtk_battery gm = fixture();
	active_gm = &gm;
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 3900;
	gauge_values[GAUGE_PROP_COULOMB] = 7654;
	fgr_dod_init(&gm);
	assert(reset_calls == 1 && gm.algo.fg_c_d0_car == 0);
	assert(gm.algo.rtc_ui_soc == 5000 && gm.algo.ui_soc == 5000);
	assert(gm.algo.fg_c_soc == 5000 && gm.algo.soc == 5000);
	gm = fixture();
	active_gm = &gm;
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 4300;
	gauge_values[GAUGE_PROP_CON0_SOC] = 61;
	gauge_values[GAUGE_PROP_RTC_UI_SOC] = 63;
	gauge_values[GAUGE_PROP_COULOMB] = -4321;
	fgr_dod_init(&gm);
	assert(reset_calls == 1 && gm.algo.fg_c_d0_car == 0);
	assert(gm.algo.ui_soc == 6300 && gm.algo.soc == 6100);
	assert(gm.algo.fg_c_d0_ocv == 40100);
	gm = fixture();
	active_gm = &gm;
	reset_error = -5;
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 3900;
	gauge_values[GAUGE_PROP_COULOMB] = 7654;
	struct mtk_battery_algo before = gm.algo;
	assert(fgr_dod_init(&gm) == -EIO);
	assert(reset_calls == 1);
	assert(memcmp(&gm.algo, &before, sizeof(before)) == 0);
}

static void check_car(void)
{
	struct mtk_battery gm = fixture();
	gm.algo.fg_c_d0_ocv = 40000;
	gm.algo.fg_c_d0_car = 1200;
	gauge_values[GAUGE_PROP_COULOMB] = 1200;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6000);
	gauge_values[GAUGE_PROP_COULOMB] = 200;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 5500);
	gauge_values[GAUGE_PROP_COULOMB] = 2200;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6500);
	gm.algo.fg_c_d0_car -= 2200;
	gauge_values[GAUGE_PROP_COULOMB] = 0;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 6500);
}

static void check_cycles(void)
{
	struct mtk_battery gm = fixture();
	gauge_values[GAUGE_PROP_COULOMB] = 1000;
	fgr_sw_cycle_accu(&gm);
	assert(gm.algo.sw_cycle_valid && gm.algo.sw_cycle_ncar == 0);
	gauge_values[GAUGE_PROP_COULOMB] = 3000;
	fgr_sw_cycle_accu(&gm);
	assert(gm.algo.sw_cycle_ncar == 0);
	gauge_values[GAUGE_PROP_COULOMB] = -1000;
	fgr_sw_cycle_accu(&gm);
	assert(gm.algo.sw_cycle_ncar == 4000 && gm.bat_cycle == 0);
	gauge_values[GAUGE_PROP_COULOMB] = 1000;
	fgr_sw_cycle_accu(&gm);
	gauge_values[GAUGE_PROP_COULOMB] = -38000;
	fgr_sw_cycle_accu(&gm);
	assert(gm.algo.sw_cycle_ncar == 3000 && gm.bat_cycle == 2);
	gm.model_capacity_mah = 0;
	gauge_values[GAUGE_PROP_COULOMB] = -38001;
	fgr_sw_cycle_accu(&gm);
	assert(gm.algo.sw_cycle_ncar == 3001 && gm.bat_cycle == 2);
}

static void check_aging(void)
{
	struct mtk_battery gm = fixture();
	fgr_learn_aging_factor(&gm, 0, 8000);
	assert(gm.algo.zcv_learn_valid && gm.algo.aging_factor == 10000);
	fgr_learn_aging_factor(&gm, -4000, 6000);
	assert(gm.algo.aging_factor == 10000);
	fgr_learn_aging_factor(&gm, -7600, 4000);
	assert(gm.algo.aging_factor == 9875);
	fgr_learn_aging_factor(&gm, -7700, 3900);
	assert(gm.algo.aging_factor == 9875);
	fgr_learn_aging_factor(&gm, -8200, 1900);
	assert(gm.algo.aging_factor == 9875);
	fgr_learn_aging_factor(&gm, -16200, 4000);
	assert(gm.algo.aging_factor == 9875);
	assert(gm.algo.zcv_learn_car == -16200 && gm.algo.zcv_learn_soc == 4000);
}

static void check_zcv(void)
{
	struct mtk_battery gm = fixture();
	gm.algo.fg_c_d0_ocv = 40000;
	gm.algo.fg_c_d0_car = 1200;
	gauge_values[GAUGE_PROP_COULOMB] = -2800;
	gauge_values[GAUGE_PROP_ZCV] = 39000;
	gauge_values[GAUGE_PROP_ZCV_CURRENT] = 100;
	fgr_zcv_recal_handler(&gm);
	assert(gm.algo.fg_c_d0_car == 1200 && !gm.algo.zcv_learn_valid);
	gauge_values[GAUGE_PROP_ZCV_CURRENT] = -100;
	fgr_zcv_recal_handler(&gm);
	assert(gm.algo.fg_c_d0_car == 1200);
	gauge_values[GAUGE_PROP_ZCV_CURRENT] = -99;
	fgr_zcv_recal_handler(&gm);
	assert(gm.algo.fg_c_d0_car == -2800 && gm.algo.fg_c_d0_soc == 5000);
	assert(gm.algo.soc == 5000 && gm.algo.aging_factor == 10000);
	gauge_values[GAUGE_PROP_COULOMB] = -3000;
	fgr_update_c_dod(&gm);
	assert(gm.algo.fg_c_soc == 4900);
	gauge_values[GAUGE_PROP_ZCV] = 0;
	fgr_zcv_recal_handler(&gm);
	assert(gm.algo.fg_c_d0_car == -2800);
}

static void check_eoc(void)
{
	struct mtk_battery gm = fixture();
	gm.algo.ui_soc = 9800;
	gm.algo.soc = 9500;
	fgr_learn_aging_factor(&gm, 0, 8000);
	gauge_values[GAUGE_PROP_COULOMB] = 4000;
	fgr_chr_full_int_handler(&gm);
	assert(gm.algo.zcv_learn_soc == 10000 && gm.algo.zcv_learn_car == 4000);
	assert(gm.algo.aging_factor == 10000);
	assert(gm.algo.ui_soc == 9800 && gm.algo.soc == 9500);
	assert(gm.algo.quse_tb1 == 20000 && gm.algo.qmax_t_0ma_tb1 == 20000);
}

static void check_tracking(void)
{
	struct mtk_battery gm = fixture();
	gm.algo.low_tracking_enable = 1;
	gm.algo.ui_soc = 200;
	clock_now = INT64_C(123456789);
	fgr_time_handler(&gm);
	assert(gm.algo.ui_soc == 100 && gm.tracking_timer.active);
	assert(gm.tracking_timer.deadline == clock_now + 40 * NSEC_PER_SEC);
	charger_present = true;
	fgr_time_handler(&gm);
	assert(gm.algo.ui_soc == 100);
	charger_present = false;
	fgr_time_handler(&gm);
	assert(gm.algo.ui_soc == 0 && gm.algo.low_tracking_enable == 0);
	assert(!gm.tracking_timer.active);
}

static struct mtk_battery learning_fixture(void)
{
	struct mtk_battery gm = fixture();
	gm.algo.aging_factor = 9875;
	gm.algo.fg_c_d0_ocv = 40500;
	gm.algo.fg_c_d0_soc = 6500;
	gm.algo.fg_c_d0_dod = 3500;
	gm.algo.fg_c_d0_car = 4500;
	gm.algo.zcv_learn_valid = true;
	gm.algo.zcv_learn_car = 4500;
	gm.algo.zcv_learn_soc = 6500;
	gm.algo.sw_cycle_valid = true;
	gm.algo.sw_cycle_ncar = 3000;
	gm.algo.ui_soc = 6700;
	gm.algo.soc = 6600;
	gm.ui_soc = 67;
	gm.bat_cycle = 12;
	gauge_values[GAUGE_PROP_ZCV] = 39000;
	gauge_values[GAUGE_PROP_ZCV_CURRENT] = 0;
	gauge_values[GAUGE_PROP_COULOMB] = -4000;
	return gm;
}

static void check_zcv_error_property(int property)
{
	const int properties[] = { GAUGE_PROP_ZCV, GAUGE_PROP_ZCV_CURRENT, GAUGE_PROP_COULOMB };
	const int errors[] = { -EIO, -ETIMEDOUT, -EACCES };
	unsigned int failure_position = 0;
	while (properties[failure_position] != property)
		failure_position++;
	for (unsigned int j = 0; j < sizeof(errors) / sizeof(errors[0]); j++) {
		struct mtk_battery gm = learning_fixture();
		struct mtk_battery before = gm;
		gauge_errors[property] = errors[j];
		fgr_zcv_recal_handler(&gm);
		assert(memcmp(&gm, &before, sizeof(gm)) == 0);
		assert(reset_calls == 0 && battery_values[BAT_PROP_UISOC] == 0);
		for (unsigned int k = 0; k < sizeof(properties) / sizeof(properties[0]); k++)
			assert(gauge_reads[properties[k]] == (k <= failure_position ? 1 : 0));
	}
}

static void check_zcv_errors(void)
{
	check_zcv_error_property(GAUGE_PROP_ZCV);
	check_zcv_error_property(GAUGE_PROP_ZCV_CURRENT);
	check_zcv_error_property(GAUGE_PROP_COULOMB);
}

static void check_eoc_error(void)
{
	struct mtk_battery gm = learning_fixture();
	struct mtk_battery before = gm;
	gauge_errors[GAUGE_PROP_COULOMB] = -EIO;
	fgr_chr_full_int_handler(&gm);
	assert(memcmp(&gm, &before, sizeof(gm)) == 0);
	assert(gauge_reads[GAUGE_PROP_COULOMB] == 1);
	assert(reset_calls == 0 && battery_values[BAT_PROP_UISOC] == 0);
}

static void check_learning_zero(void)
{
	struct mtk_battery gm = learning_fixture();
	gauge_values[GAUGE_PROP_COULOMB] = 0;
	fgr_zcv_recal_handler(&gm);
	assert(gm.algo.fg_c_d0_car == 0 && gm.algo.zcv_learn_car == 0);
	assert(gm.algo.fg_c_d0_soc == 5000 && gm.algo.soc == 5000);
	assert(gm.algo.zcv_learn_valid && gm.algo.zcv_learn_soc == 5000);
	assert(gm.algo.aging_factor == 9875);
	assert(gauge_reads[GAUGE_PROP_ZCV] == 1 && gauge_reads[GAUGE_PROP_ZCV_CURRENT] == 1);
	assert(gauge_reads[GAUGE_PROP_COULOMB] == 1);
	gm = learning_fixture();
	gm.algo.zcv_learn_car = -4000;
	gm.algo.zcv_learn_soc = 8000;
	gauge_values[GAUGE_PROP_COULOMB] = 0;
	fgr_chr_full_int_handler(&gm);
	assert(gm.algo.zcv_learn_car == 0 && gm.algo.zcv_learn_soc == 10000);
	assert(gm.algo.aging_factor == 9890 && gm.algo.ui_soc == 6700 && gm.algo.soc == 6600);
	assert(gauge_reads[GAUGE_PROP_COULOMB] == 1);
}

static void check_shutdown_timer(void)
{
	struct mtk_battery gm = fixture();
	clock_now = INT64_C(2147483647) * NSEC_PER_SEC + 123456789;
	shutdown_seconds = 10;
	power_misc_handler(&gm);
	assert(gm.sdc.kthread_fgtimer.active);
	assert(gm.sdc.kthread_fgtimer.deadline == clock_now + 10 * NSEC_PER_SEC);
	assert(gm.sdc.kthread_fgtimer.starts == 1);
	shutdown_seconds = 0;
	power_misc_handler(&gm);
	assert(gm.sdc.kthread_fgtimer.starts == 1);
	shutdown_seconds = -1;
	power_misc_handler(&gm);
	assert(gm.sdc.kthread_fgtimer.starts == 1);
	shutdown_seconds = 10;
	gm.disableGM30 = true;
	power_misc_handler(&gm);
	assert(gm.sdc.kthread_fgtimer.starts == 1);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "timer") == 0)
		check_timer();
	else if (strcmp(argv[1], "dlpt") == 0)
		check_dlpt();
	else if (strcmp(argv[1], "boot") == 0)
		check_boot();
	else if (strcmp(argv[1], "car") == 0)
		check_car();
	else if (strcmp(argv[1], "cycles") == 0)
		check_cycles();
	else if (strcmp(argv[1], "aging") == 0)
		check_aging();
	else if (strcmp(argv[1], "zcv") == 0)
		check_zcv();
	else if (strcmp(argv[1], "eoc") == 0)
		check_eoc();
	else if (strcmp(argv[1], "tracking") == 0)
		check_tracking();
	else if (strcmp(argv[1], "shutdown-timer") == 0)
		check_shutdown_timer();
	else if (strcmp(argv[1], "zcv-errors") == 0)
		check_zcv_errors();
	else if (strcmp(argv[1], "zcv-current-error") == 0)
		check_zcv_error_property(GAUGE_PROP_ZCV_CURRENT);
	else if (strcmp(argv[1], "zcv-car-error") == 0)
		check_zcv_error_property(GAUGE_PROP_COULOMB);
	else if (strcmp(argv[1], "eoc-error") == 0)
		check_eoc_error();
	else if (strcmp(argv[1], "learning-zero") == 0)
		check_learning_zero();
	else
		abort();
	printf("PASS %s\n", argv[1]);
	return 0;
}
