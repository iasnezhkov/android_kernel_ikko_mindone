// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct mtk_gauge fixture_gauge;

static struct mtk_battery fixture(void)
{
	struct mtk_battery gm = { 0 };
	memset(&fixture_gauge, 0, sizeof(fixture_gauge));
	gm.gauge = &fixture_gauge;
	gm.soc = 61;
	gm.fg_cust_data.vbat2_det_voltage1 = 34000;
	gm.fg_cust_data.qmax_sel = 1;
	io_calls = io_fail_at = io_errno = background_error = 0;
	mismatch_property = -1;
	memset(gauge_writes, 0, sizeof(gauge_writes));
	memset(gauge_values, 0, sizeof(gauge_values));
	memset(gauge_errors, 0, sizeof(gauge_errors));
	memset(gauge_reads, 0, sizeof(gauge_reads));
	memset(battery_values, 0, sizeof(battery_values));
	memset(profile, 0, sizeof(profile));
	for (int i = 0; i < 100; i++)
		profile[i].value = 321 + i;
	gm.algo.quse_tb1 = 19800;
	gm.algo.aging_factor = 9150;
	gm.algo.fg_c_d0_car = 3333;
	gm.algo.fg_c_soc = 6100;
	gm.algo.ui_soc = 6300;
	gm.algo.zcv_learn_car = 123;
	gm.algo.zcv_learn_valid = true;
	gm.algo.sw_cycle_valid = true;
	gm.algo.sw_cycle_ncar = 100;
	gm.ui_soc = 63;
	gm.tracking_timer.deadline = 777;
	gm.tracking_timer.active = true;
	gauge_values[GAUGE_PROP_BATTERY_EXIST] = 1;
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 4050;
	gauge_values[GAUGE_PROP_PTIM_RESIST] = 100;
	gauge_values[GAUGE_PROP_CON0_SOC] = 61;
	gauge_values[GAUGE_PROP_RTC_UI_SOC] = 63;
	gauge_values[GAUGE_PROP_COULOMB] = 2000;
	reset_calls = 0;
	reset_error = 0;
	read_error = 0;
	initialization_calls = 0;
	setup_calls = 0;
	publication_calls = 0;
	temperature_error = 0;
	sample_error = 0;
	allocation_error = false;
	profile_error = false;
	profile_order_error = false;
	profile_missing = false;
	return gm;
}

static ssize_t activate(void)
{
	return BAT_EC_store(NULL, NULL, "805 0\n", 6);
}

static void verify_failure(struct mtk_battery *gm, int expected)
{
	struct mtk_battery_algo before = gm->algo;
	struct alarm timer_before = gm->tracking_timer;
	int prior_flag = gm->init_flag;
	active_gm = gm;
	assert(activate() == expected);
	assert(!gm->algo.active && !gm->algo_lock.held && gm->init_flag == prior_flag);
	assert(memcmp(&gm->algo, &before, sizeof(before)) == 0);
	assert(memcmp(&gm->tracking_timer, &timer_before, sizeof(timer_before)) == 0);
	for (int i = 0; i < 100; i++)
		assert(profile[i].value == 321 + i);
	assert(gm->ui_soc == 63);
	assert(setup_calls == 0 && publication_calls == 0);
	assert(gauge_values[GAUGE_PROP_COULOMB] == 2000);
}

static void verify_recovery(struct mtk_battery *gm)
{
	memset(gauge_errors, 0, sizeof(gauge_errors));
	reset_error = 0;
	temperature_error = 0;
	sample_error = 0;
	allocation_error = false;
	profile_error = false;
	profile_order_error = false;
	profile_missing = false;
	active_gm = gm;
	assert(activate() == 6);
	assert(gm->algo.active && !gm->algo_lock.held && gm->init_flag == 1);
	assert(gm->algo.fg_c_d0_car == 0 && gauge_values[GAUGE_PROP_COULOMB] == 0);
	assert(gm->algo.fg_c_soc == 6100 && gm->algo.ui_soc == 6300);
	assert(gm->algo.last_temp == -5 && setup_calls == 1 && publication_calls == 1);
	mutex_lock(&gm->algo_lock);
	fgr_update_c_dod(gm);
	mutex_unlock(&gm->algo_lock);
	assert(gm->algo.fg_c_soc == 6100);
	gm->algo.aging_factor = 9321;
	gm->algo.zcv_learn_car = 77;
	struct mtk_battery_algo before = gm->algo;
	int resets_before = reset_calls;
	int init_before = initialization_calls;
	assert(activate() == 6);
	assert(memcmp(&gm->algo, &before, sizeof(before)) == 0);
	assert(reset_calls == resets_before && initialization_calls == init_before);
	assert(setup_calls == 1 && publication_calls == 2);
}

int main(void)
{
	const int required[] = { GAUGE_PROP_BATTERY_EXIST, GAUGE_PROP_CON0_SOC,
		GAUGE_PROP_RTC_UI_SOC, GAUGE_PROP_PTIM_RESIST, GAUGE_PROP_COULOMB };
	const int errors[] = { -EIO, -ETIMEDOUT, -EACCES, -EPERM };
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		for (size_t j = 0; j < sizeof(errors) / sizeof(errors[0]); j++) {
			struct mtk_battery gm = fixture();
			gauge_errors[required[i]] = errors[j];
			verify_failure(&gm, errors[j]);
			assert(reset_calls == 0);
			verify_recovery(&gm);
		}
	}
	puts("PASS checked read errors, preserved state, retry, exact sysfs errno and no fabricated CAR movement");
	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		struct mtk_battery gm = fixture();
		gm.init_flag = 1;
		reset_error = errors[i];
		verify_failure(&gm, errors[i]);
		assert(reset_calls == 1 && gm.kernel_setup_error == errors[i]);
		reset_error = 0;
		assert(activate() == -EUCLEAN && reset_calls == 1 && !gm.algo.active);
		gm = fixture();
		verify_recovery(&gm);
		gm = fixture();
		sample_error = errors[i];
		verify_failure(&gm, errors[i]);
		verify_recovery(&gm);
		gm = fixture();
		temperature_error = errors[i];
		verify_failure(&gm, errors[i]);
		verify_recovery(&gm);
	}
	puts("PASS reset failure requires reboot while pre-write temperature failures allow retry");
	struct mtk_battery gm = fixture();
	allocation_error = true;
	verify_failure(&gm, -ENOMEM);
	verify_recovery(&gm);
	gm = fixture();
	profile_error = true;
	verify_failure(&gm, -ENODATA);
	verify_recovery(&gm);
	gm = fixture();
	profile_order_error = true;
	verify_failure(&gm, -EINVAL);
	verify_recovery(&gm);
	gm = fixture();
	profile_missing = true;
	verify_failure(&gm, -EINVAL);
	verify_recovery(&gm);
	puts("PASS allocation/profile preparation failures do not arm consumers or timers");
	gm = fixture();
	gauge_values[GAUGE_PROP_BATTERY_EXIST] = 0;
	verify_failure(&gm, -ENODEV);
	gauge_values[GAUGE_PROP_BATTERY_EXIST] = 1;
	verify_recovery(&gm);
	gm = fixture();
	gauge_values[GAUGE_PROP_RTC_UI_SOC] = 127;
	verify_failure(&gm, -ERANGE);
	gauge_values[GAUGE_PROP_RTC_UI_SOC] = 63;
	verify_recovery(&gm);
	gm = fixture();
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 0;
	verify_failure(&gm, -ENODATA);
	gauge_values[GAUGE_PROP_PTIM_BATTERY_VOLTAGE] = 4050;
	verify_recovery(&gm);
	puts("PASS absent battery, invalid stored percentage and unavailable voltage");
	for (int selection = 0; selection <= 2; selection++) {
		for (int unavailable = 0; unavailable < 3; unavailable++) {
			gm = fixture();
			gm.fg_cust_data.qmax_sel = selection;
			gauge_values[GAUGE_PROP_PTIM_RESIST] = unavailable == 2 ? -1 : 0;
			gauge_errors[GAUGE_PROP_PTIM_RESIST] = unavailable == 0 ? -ENODATA : 0;
			if (selection == 1) {
				active_gm = &gm;
				assert(activate() == 6 && gm.algo.active);
			} else
				verify_failure(&gm, -ENODATA);
		}
	}
	for (int source = 0; source < 2; source++) {
		gm = fixture();
		gauge_errors[GAUGE_PROP_PTIM_RESIST] = -ENODATA;
		if (source == 0)
			temperature_error = -ENODATA;
		else
			sample_error = -ENODATA;
		verify_failure(&gm, -ENODATA);
		verify_recovery(&gm);
	}
	puts("PASS unavailable RAC permits profile activation only for qmax_sel=1; temperature and PTIM remain required");
	gm = fixture();
	gauge_values[GAUGE_PROP_COULOMB] = 0;
	active_gm = &gm;
	assert(activate() == 6);
	assert(gm.algo.active && gm.algo.fg_c_soc == 6100 && reset_calls == 1);
	puts("PASS legal zero CAR, positive IIO success and signed negative temperature");
	gm = fixture();
	active_gm = &gm;
	assert(activate() == 6);
	int successful_io = io_calls;
	for (int failure = 1; failure <= successful_io; failure++) {
		for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
			gm = fixture();
			active_gm = &gm;
			struct mtk_battery_algo before = gm.algo;
			io_fail_at = failure;
			io_errno = errors[i];
			assert(activate() == errors[i]);
			assert(io_calls == failure);
			assert(!gm.algo.active && !gm.algo_lock.held && !gm.gauge->ops_lock.held);
			assert(!gm.kernel_setup_in_progress && publication_calls == 0);
			assert(gm.soc == 61 && gm.ui_soc == 63);
			assert(memcmp(&gm.algo, &before, sizeof(before)) == 0);
			for (int j = 0; j < 100; j++) assert(profile[j].value == 321 + j);
			io_fail_at = 0;
			if (gm.kernel_setup_mutated) {
				assert(gm.kernel_setup_error == errors[i]);
				assert(activate() == -EUCLEAN && io_calls == failure);
				gm = fixture();
			} else {
				assert(gm.kernel_setup_error == 0 && gauge_values[GAUGE_PROP_COULOMB] == 2000);
			}
			verify_recovery(&gm);
		}
	}
	printf("PASS every activation IO fault (%d positions), exact errno, immediate stop, software restore and retry/reboot boundary\n", successful_io);
	const int mismatches[] = {GAUGE_PROP_RTC_UI_SOC, GAUGE_PROP_CON0_SOC, GAUGE_PROP_IS_NVRAM_FAIL_MODE};
	for (size_t i = 0; i < sizeof(mismatches) / sizeof(mismatches[0]); i++) {
		gm = fixture();
		active_gm = &gm;
		mismatch_property = mismatches[i];
		assert(activate() == -EIO);
		assert(!gm.algo.active && gm.kernel_setup_error == -EIO && publication_calls == 0);
		assert(gauge_values[GAUGE_PROP_COULOMB] == 0);
		assert(activate() == -EUCLEAN);
	}
	gm = fixture();
	active_gm = &gm;
	background_error = -EACCES;
	assert(activate() == -EACCES);
	assert(!gm.algo.active && gm.kernel_setup_error == -EACCES && publication_calls == 0);
	assert(activate() == -EUCLEAN);
	puts("PASS RTC/CON0/NVRAM readback and concurrent setup writer error prevent active publication after CAR reset");
	return 0;
}
