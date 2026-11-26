// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
static void run_scheduled(struct mindone_chr_controller *c)
{
	assert(c->work.scheduled);
	c->work.scheduled = 0;
	c->work.work.func(&c->work.work);
}

static void case_irq_during_suspend(void)
{
	struct platform_device pdev = {};
	struct mindone_chr_controller *c;

	sim_reset();
	sim.pmic_0a88 = 0;
	c = mindone_chr_probe(&pdev);
	assert(!IS_ERR(c));
	run_scheduled(c);
	assert(c->last_attached_valid && !c->last_attached);

	assert(mindone_chr_suspend(c) == 0);
	assert(sim.cancel_calls >= 2);
	sim.pmic_0a88 = BIT(5);
	mutex_lock(&c->detector);
	mindone_chr_interrupt(c);
	mutex_unlock(&c->detector);
	assert(sim.bc12_online_calls == 0);
	assert(sim.settle_calls == 0);
	assert(c->pending_resume);

	c->pending_resume = false;
	mindone_chr_event(c, false);
	assert(sim.bc12_online_calls == 0);
	assert(c->pending_resume);

	assert(mindone_chr_resume(c) == 0);
	assert(c->work.scheduled && c->work.delay == 0);
	assert(sim.ws_holds == 1);
	run_scheduled(c);
	assert(sim.bc12_online_calls == 1);
	assert(sim.settle_calls == 1);
	assert(sim.bc12_changed_calls == 1);
	assert(c->last_attached);

	mindone_chr_remove(c);
	assert(sim.ws_registered == 1 && sim.ws_unregistered == 1);
}

static void case_enodata_retry_and_cap(void)
{
	struct platform_device pdev = {};
	struct mindone_chr_controller *c;

	sim_reset();
	sim.pmic_0a88 = BIT(5);
	sim.bc12_online_err = -ENODATA;
	c = mindone_chr_probe(&pdev);
	assert(!IS_ERR(c));
	run_scheduled(c);
	assert(c->work.scheduled && c->work.delay == MINDONE_CHR_RETRY_MS);
	assert(c->retry_count == 1);

	run_scheduled(c);
	assert(c->work.scheduled && c->retry_count == 2);

	sim.bc12_online_err = 0;
	run_scheduled(c);
	assert(!c->work.scheduled && c->retry_count == 0);
	assert(c->last_attached_valid && c->last_attached);

	sim.bc12_online_err = -ENODATA;
	mindone_chr_interrupt(c);
	for (int i = 0; i < MINDONE_CHR_RETRY_MAX; i++) {
		assert(c->work.scheduled);
		run_scheduled(c);
	}
	assert(!c->work.scheduled);
	assert(c->retry_count == MINDONE_CHR_RETRY_MAX);

	mindone_chr_remove(c);
}

static void case_silent_attach_in_sleep(void)
{
	struct platform_device pdev = {};
	struct mindone_chr_controller *c;

	sim_reset();
	sim.pmic_0a88 = 0;
	c = mindone_chr_probe(&pdev);
	assert(!IS_ERR(c));
	run_scheduled(c);
	assert(mindone_chr_suspend(c) == 0);

	sim.pmic_0a88 = BIT(5);
	assert(mindone_chr_resume(c) == 0);
	assert(c->work.scheduled && c->work.delay == 0);
	run_scheduled(c);
	assert(sim.bc12_changed_calls == 1 && c->last_attached);

	assert(mindone_chr_suspend(c) == 0);
	assert(mindone_chr_resume(c) == 0);
	assert(!c->work.scheduled);

	mindone_chr_remove(c);
}

static void case_eta_suspend_resume(void)
{
	struct i2c_client client = {};
	struct eta6965_device eta = {};
	struct eta6965_platform binding = {};
	struct power_supply psy = {};
	struct wakeup_source ws = {};
	union power_supply_propval val = {};

	sim_reset();
	eta6965_binding_lock.rank = 1;
	mutex_init(&eta.lock);
	eta.lock.rank = 2;
	eta.client = &client;
	eta.dev = &client.dev;
	eta.ws = &ws;
	client.dev.drvdata = &eta;
	binding.eta = &eta;
	binding.psy = &psy;
	binding.active = true;
	psy.drvdata = &binding;
	eta6965_otg_binding = &binding;

	sim.eta_reg08 = 0x00;
	assert(eta6965_suspend(&client.dev) == 0);
	assert(eta.suspended && eta.suspend_reg08_valid && eta.suspend_reg08 == 0x00);

	val.intval = 1;
	assert(eta6965_psy_set_property(&psy, POWER_SUPPLY_PROP_ONLINE, &val) == -ENODATA);
	assert(sim.psy_changed_calls == 0);
	assert(eta.notify_on_resume);

	val.intval = 500000;
	assert(eta6965_psy_set_property(&psy, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val) == 0);
	assert(eta.usb_input_current_limit == 500000);
	assert(sim.psy_changed_calls == 0);

	assert(eta6965_resume(&client.dev) == 0);
	assert(!eta.suspended);
	assert(sim.psy_changed_calls == 1);
	assert(sim.eta_ws_events == 1);

	assert(eta6965_suspend(&client.dev) == 0);
	assert(eta6965_psy_get_property(&psy, POWER_SUPPLY_PROP_USB_TYPE, &val) == -ENODATA);
	assert(eta.notify_on_resume);
	assert(eta6965_resume(&client.dev) == 0);
	assert(sim.psy_changed_calls == 2);

	assert(eta6965_suspend(&client.dev) == 0);
	sim.eta_reg08 = 0x24;
	assert(eta6965_resume(&client.dev) == 0);
	assert(sim.psy_changed_calls == 3);

	assert(eta6965_suspend(&client.dev) == 0);
	assert(eta6965_resume(&client.dev) == 0);
	assert(sim.psy_changed_calls == 3);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "irq-during-suspend"))
		case_irq_during_suspend();
	else if (!strcmp(argv[1], "enodata-retry-and-cap"))
		case_enodata_retry_and_cap();
	else if (!strcmp(argv[1], "silent-attach-in-sleep"))
		case_silent_attach_in_sleep();
	else if (!strcmp(argv[1], "eta-suspend-resume"))
		case_eta_suspend_resume();
	else
		return 2;
	printf("PASS %s\n", argv[1]);
	return 0;
}
