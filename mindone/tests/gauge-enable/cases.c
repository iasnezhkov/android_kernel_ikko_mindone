// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void fresh_activation(void)
{
	struct mtk_battery gm = {0};
	activate(&gm);
	assert(gm.algo.active && !gm.algo_lock.held);
	assert(initializations == 1 && publications == 1 && interrupts == 0);
	assert(gm.published_soc == 80);
	puts("PASS first activation, real publication and zero synthetic interrupts");
}

static void repeated_activation(void)
{
	struct mtk_battery gm = {0};
	initializations = 0;
	publications = 0;
	interrupts = 0;
	activate(&gm);
	gm.algo.aging_factor = 9875;
	gm.algo.ui_soc = 8343;
	gm.ui_soc = 83;
	gm.algo.zcv_learn_valid = true;
	gm.algo.zcv_learn_car = -4567;
	gm.algo.sw_cycle_valid = true;
	gm.algo.sw_cycle_ncar = 7890;
	gm.bat_cycle = 12;
	activate(&gm);
	assert(initializations == 1 && publications == 2);
	assert(gm.algo.aging_factor == 9875 && gm.algo.ui_soc == 8343);
	assert(gm.algo.zcv_learn_valid && gm.algo.zcv_learn_car == -4567);
	assert(gm.algo.sw_cycle_valid && gm.algo.sw_cycle_ncar == 7890);
	assert(gm.bat_cycle == 12 && gm.published_soc == 83);
	assert(!gm.algo_lock.held);
	puts("PASS repeated activation and learned-state preservation");
}

static void already_active(void)
{
	struct mtk_battery gm = { .algo = { .active = true, .aging_factor = 9555, .ui_soc = 7345 }, .ui_soc = 73 };
	initializations = 0;
	publications = 0;
	interrupts = 0;
	activate(&gm);
	assert(initializations == 0 && publications == 1 && interrupts == 0);
	assert(gm.algo.aging_factor == 9555 && gm.algo.ui_soc == 7345);
	assert(gm.published_soc == 73 && !gm.algo_lock.held);
	puts("PASS activation of an already active boot-time kernel algorithm");
}

int main(int argc, char **argv)
{
	if (argc == 1 || strcmp(argv[1], "fresh") == 0)
		fresh_activation();
	if (argc == 1 || strcmp(argv[1], "repeat") == 0)
		repeated_activation();
	if (argc == 1 || strcmp(argv[1], "already-active") == 0)
		already_active();
	return 0;
}
