static struct mtk_battery setup(bool active, int cached)
{
	error = publications = poweroffs = daemon_events = kernel_events = 0;
	reports = disabled_irqs = reads = 0;
	irq_allowed = true;
	present = 1;
	return (struct mtk_battery) {
		.bs_data = { .psy = &supply, .bat_present = cached, .bat_status = 2 },
		.is_probe_done = true, .disable_plug_int = true,
		.algo = { .active = active }, .ui_soc = 65, .plug_miss_count = 2
	};
}

static void errors_preserve_state(void)
{
	int faults[] = { -EIO, -ETIMEDOUT, -EACCES };
	for (int active = 0; active <= 1; active++)
		for (int cached = 0; cached <= 1; cached++)
			for (unsigned int i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
				struct mtk_battery gm = setup(active, cached);
				error = faults[i];
				struct mtk_battery saved = gm;
				sw_check_bat_plugout(&gm);
				assert(memcmp(&gm, &saved, sizeof(gm)) == 0);
				assert(bat_plugout_irq(0, &gm) == IRQ_HANDLED);
				assert(memcmp(&gm, &saved, sizeof(gm)) == 0);
				assert(!poweroffs && !reports && !disabled_irqs && !publications);
				assert(!daemon_events && !kernel_events);
				battery_update(&gm);
				assert(gm.bs_data.bat_present == cached && publications == 1);
				struct message reply = { 0 };
				presence_reply(&gm, &reply);
				int value;
				memcpy(&value, reply.fgd_data, sizeof(value));
				assert(value == cached && reply.fgd_data_len == sizeof(value));
			}
	puts("PASS presence errors preserve both modes, shutdown and publication");
}

static void valid_absence(void)
{
	for (int active = 0; active <= 1; active++) {
		struct mtk_battery gm = setup(active, 1);
		present = 0;
		sw_check_bat_plugout(&gm);
		assert(poweroffs == 1 && publications == 1 && !reports && !disabled_irqs);
		assert(daemon_events == !active && kernel_events == active);
		assert(gm.bs_data.bat_status == POWER_SUPPLY_STATUS_UNKNOWN);
		assert(gm.bs_data.bat_present == 0);
		gm = setup(active, 1);
		present = 0;
		assert(bat_plugout_irq(0, &gm) == IRQ_HANDLED);
		assert(poweroffs == 1 && publications == 1 && reports == 1 && !disabled_irqs);
		assert(daemon_events == !active && kernel_events == active);
	}
	puts("PASS verified absence retains shutdown behavior");
}

static void valid_presence_and_disable(void)
{
	struct mtk_battery gm = setup(false, 0);
	sw_check_bat_plugout(&gm);
	assert(!poweroffs && !publications && !reports);
	assert(bat_plugout_irq(0, &gm) == IRQ_HANDLED);
	assert(gm.plug_miss_count == 3 && gm.disable_plug_int && disabled_irqs == 1);
	assert(reports == 1 && !poweroffs && !publications);
	battery_update(&gm);
	assert(gm.bs_data.bat_present == 1 && publications == 1);
	struct message reply = { 0 };
	presence_reply(&gm, &reply);
	int value;
	memcpy(&value, reply.fgd_data, sizeof(value));
	assert(value == 1);
	gm = setup(false, 1);
	irq_allowed = false;
	present = 0;
	assert(bat_plugout_irq(0, &gm) == IRQ_HANDLED);
	assert(!poweroffs && !publications && !reports && !disabled_irqs);
	gm.disableGM30 = true;
	sw_check_bat_plugout(&gm);
	assert(!poweroffs);
	puts("PASS valid presence, mismatch threshold and disabled guards");
}

int main(void)
{
	errors_preserve_state();
	valid_absence();
	valid_presence_and_disable();
	return 0;
}
