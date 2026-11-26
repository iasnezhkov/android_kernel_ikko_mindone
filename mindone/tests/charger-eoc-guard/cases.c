// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
static void case_term_window(void)
{
	struct charger_device dev = { .id = 1 };
	struct mtk_charger info = { .chg1_dev = &dev, .setting = { .cv = 4450000 } };

	sim_reset();
	sim.vbat_mV = 4395;
	sim.ibat_mA = 430;
	mtk_basic_update_termination(&info, false);
	mtk_basic_update_termination(&info, false);
	assert(!info.term_en && !sim.hw_term_en);

	sim.vbat_mV = 4420;
	sim.ibat_mA = 190;
	mtk_basic_update_termination(&info, false);
	assert(!info.term_en && info.term_ready_cnt == 1);
	mtk_basic_update_termination(&info, false);
	assert(info.term_en && sim.hw_term_en);

	sim.vbat_mV = 4400;
	sim.ibat_mA = 200;
	mtk_basic_update_termination(&info, false);
	assert(info.term_en && sim.hw_term_en);

	sim.vbat_mV = 4390;
	mtk_basic_update_termination(&info, false);
	assert(!info.term_en && !sim.hw_term_en);

	sim.vbat_mV = 4420;
	sim.ibat_mA = 150;
	mtk_basic_update_termination(&info, false);
	mtk_basic_update_termination(&info, false);
	assert(info.term_en && sim.hw_term_en);

	sim.ibat_mA = 350;
	mtk_basic_update_termination(&info, false);
	assert(!info.term_en && !sim.hw_term_en);
}

static void case_false_eoc_reject_and_cap(void)
{
	struct charger_device dev = { .id = 1 };
	struct mtk_charger info = { .chg1_dev = &dev, .setting = { .cv = 4450000 }, .term_en = true };

	sim_reset();
	sim.hw_term_en = true;
	sim.vbat_mV = 4338;
	assert(!mtk_basic_validate_chg_done(&info, true));
	assert(info.false_eoc_retry == 1);
	assert(!info.term_en && !sim.hw_term_en);
	assert(sim.disable_calls == 1 && sim.enable_calls == 1 && sim.hw_chg_en);

	assert(!mtk_basic_validate_chg_done(&info, true));
	assert(!mtk_basic_validate_chg_done(&info, true));
	assert(info.false_eoc_retry == 3);

	assert(mtk_basic_validate_chg_done(&info, true));
}

static void case_real_eoc_accept(void)
{
	struct charger_device dev = { .id = 1 };
	struct mtk_charger info = { .chg1_dev = &dev, .setting = { .cv = 4450000 }, .term_en = true };

	sim_reset();
	sim.hw_term_en = true;
	sim.vbat_mV = 4405;
	assert(mtk_basic_validate_chg_done(&info, true));
	assert(info.false_eoc_retry == 0);
	assert(sim.disable_calls == 0 && sim.enable_calls == 0);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "term-window"))
		case_term_window();
	else if (!strcmp(argv[1], "false-eoc-reject"))
		case_false_eoc_reject_and_cap();
	else if (!strcmp(argv[1], "real-eoc-accept"))
		case_real_eoc_accept();
	else
		return 2;
	printf("PASS %s\n", argv[1]);
	return 0;
}
