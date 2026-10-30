// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct mtk_battery fixture(bool kernel)
{
	memset(values, 0, sizeof(values));
	memset(errors, 0, sizeof(errors));
	memset(reads, 0, sizeof(reads));
	kernel_events = daemon_events = disable_writes = disable_all_calls = event_reports = plug_checks = 0;
	values[GAUGE_PROP_ZCV] = 40000;
	return (struct mtk_battery){ .algo = { .active = kernel }, .fg_cust_data = { .sleep_current_avg = 100 } };
}

static void check_errors(void)
{
	const int errnos[] = { -EIO, -ETIMEDOUT, -EACCES };
	for (int kernel = 0; kernel <= 1; kernel++) {
		for (int property = GAUGE_PROP_COULOMB; property <= GAUGE_PROP_ZCV; property++) {
			for (unsigned int i = 0; i < sizeof(errnos) / sizeof(errnos[0]); i++) {
				struct mtk_battery gm = fixture(kernel);
				errors[property] = errnos[i];
				assert(zcv_irq(0, &gm) == IRQ_HANDLED);
				assert(kernel_events == 0 && daemon_events == 0 && disable_writes == 0);
				assert(event_reports == 0 && plug_checks == 0 && disable_all_calls == 0);
				assert(!gm.algo_lock.held);
				for (int index = GAUGE_PROP_COULOMB; index <= GAUGE_PROP_ZCV; index++)
					assert(reads[index] == (index <= property ? 1 : 0));
			}
		}
	}
}

static void check_success(bool resting)
{
	for (int kernel = 0; kernel <= 1; kernel++) {
		for (int current = -1; current <= 1; current++) {
			struct mtk_battery gm = fixture(kernel);
			values[GAUGE_PROP_ZCV_CURRENT] = current * (resting ? 99 : 100);
			values[GAUGE_PROP_COULOMB] = current * 12345;
			if (!resting && current == 0)
				values[GAUGE_PROP_ZCV_CURRENT] = 101;
			assert(zcv_irq(0, &gm) == IRQ_HANDLED);
			assert(kernel_events == (resting && kernel) && daemon_events == (resting && !kernel));
			assert(disable_writes == resting && disable_all_calls == 0);
			assert(event_reports == 1 && plug_checks == 1 && !gm.algo_lock.held);
			for (int property = GAUGE_PROP_COULOMB; property <= GAUGE_PROP_ZCV; property++)
				assert(reads[property] == 1);
		}
	}
}

static void check_disabled(void)
{
	struct mtk_battery gm = fixture(true);
	gm.disableGM30 = true;
	assert(zcv_irq(0, &gm) == IRQ_HANDLED);
	assert(disable_all_calls == 1 && disable_writes == 0 && kernel_events == 0 && daemon_events == 0);
	assert(event_reports == 0 && plug_checks == 0);
	for (int property = 0; property < GAUGE_PROP_COUNT; property++)
		assert(reads[property] == 0);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "errors") == 0)
		check_errors();
	else if (strcmp(argv[1], "rest") == 0)
		check_success(true);
	else if (strcmp(argv[1], "busy") == 0)
		check_success(false);
	else if (strcmp(argv[1], "disabled") == 0)
		check_disabled();
	else
		assert(false);
	printf("PASS %s\n", argv[1]);
	return 0;
}
