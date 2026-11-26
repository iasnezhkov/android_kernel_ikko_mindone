// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct i2c_adapter g_adap;
static struct i2c_client g_client;
static struct nfc_dev *g_nfc;
static struct file g_filp;
static struct inode g_inode;

static void setup_device(void)
{
	sim_reset();
	assert(nfc_pins_probe(&sim.pins_pdev) == 0);
	memset(&g_client, 0, sizeof(g_client));
	g_client.adapter = &g_adap;
	g_client.name = "nxp-nci";
	assert(nfc_i2c_probe(&g_client) == 0);
	g_nfc = i2c_get_clientdata(&g_client);
	assert(g_nfc != NULL);
	memset(&g_filp, 0, sizeof(g_filp));
	g_filp.private_data = &g_nfc->misc;
	assert(nfc_open(&g_inode, &g_filp) == 0);
}

static void hook_power_off(void)
{
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_OFF) == 0);
}

static void hook_set_release_read(void)
{
	WRITE_ONCE(g_nfc->release_read, true);
}

static void case_probe_no_autoen(void)
{
	setup_device();
	assert(sim.irq_requested == 1);
	assert((sim.irq_req_flags & IRQF_NO_AUTOEN) != 0);
	assert(sim.irq_req_had_drvdata);
	assert(sim.irq_req_had_wakeup);
	assert(!g_nfc->irq_enabled);
	assert(sim.irq_enable_calls == 0);
}

static void case_flush_before_enable(void)
{
	char buf[8];
	ssize_t r;

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	WRITE_ONCE(g_nfc->release_read, true);
	r = nfc_read(&g_filp, buf, sizeof(buf), NULL);
	assert(r == 0);
	assert(!g_nfc->irq_enabled);
	assert(sim.irq_enable_calls == 0);
	assert(sim.i2c_recv_calls == 0);

	WRITE_ONCE(g_nfc->release_read, false);
	sim.wait_hook = hook_set_release_read;
	r = nfc_read(&g_filp, buf, sizeof(buf), NULL);
	assert(r == 0);
	assert(!g_nfc->irq_enabled);
	assert(sim.i2c_recv_calls == 0);
}

static void case_flush_while_irq_high(void)
{
	char buf[8];
	ssize_t r;

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	sim.irq_level = 1;
	WRITE_ONCE(g_nfc->release_read, true);
	r = nfc_read(&g_filp, buf, sizeof(buf), NULL);
	assert(r == 0);
	assert(sim.i2c_recv_calls == 0);
}

static void case_power_off_wakes_reader(void)
{
	char buf[8];
	ssize_t r;

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	sim.wait_hook = hook_power_off;
	r = nfc_read(&g_filp, buf, sizeof(buf), NULL);
	assert(r == -EIO);
	assert(!g_nfc->irq_enabled);
	assert(!g_nfc->ven_high);
	assert(sim.wake_all_calls >= 1);
}

static void case_signal_disables_irq(void)
{
	char buf[8];
	ssize_t r;

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	sim.wait_return_rc = -ERESTARTSYS;
	r = nfc_read(&g_filp, buf, sizeof(buf), NULL);
	assert(r == -ERESTARTSYS);
	assert(!g_nfc->irq_enabled);
}

static void case_release_lowers_ven(void)
{
	int highs_before;

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	assert(g_nfc->ven_high);
	assert(nfc_release(&g_inode, &g_filp) == 0);
	assert(!g_nfc->ven_high);
	assert(!g_nfc->ven_on_by_nfc);

	highs_before = sim.ven_high_calls;
	g_filp.private_data = &g_nfc->misc;
	assert(nfc_open(&g_inode, &g_filp) == 0);
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	assert(g_nfc->ven_high);
	assert(sim.ven_high_calls == highs_before + 1);
}

static void case_suspend_wakeup_after_freeze(void)
{
	char buf[8];

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	sim.wait_return_rc = -ERESTARTSYS;
	assert(nfc_read(&g_filp, buf, sizeof(buf), NULL) == -ERESTARTSYS);
	assert(!g_nfc->irq_enabled);
	assert(g_nfc->ven_high);

	assert(nfc_i2c_suspend(&g_client.dev) == 0);
	assert(sim.irq_wake_enable_calls == 1);
	assert(g_nfc->irq_wake_up);
	assert(nfc_i2c_resume(&g_client.dev) == 0);
	assert(sim.irq_wake_disable_calls == 1);
	assert(!g_nfc->irq_wake_up);

	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_OFF) == 0);
	assert(nfc_i2c_suspend(&g_client.dev) == 0);
	assert(sim.irq_wake_enable_calls == 1);
}

static void case_write_nack_retry(void)
{
	char msg[4] = { 0x20, 0x00, 0x01, 0x00 };

	setup_device();
	assert(nfc_ioctl(&g_filp, NFC_SET_PWR, NFC_POWER_ON) == 0);
	sim.i2c_send_fail_first = 1;
	assert(nfc_write(&g_filp, msg, sizeof(msg), NULL) == 4);
	assert(sim.i2c_send_calls == 2);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "probe-no-autoen"))
		case_probe_no_autoen();
	else if (!strcmp(argv[1], "flush-before-enable"))
		case_flush_before_enable();
	else if (!strcmp(argv[1], "flush-while-irq-high"))
		case_flush_while_irq_high();
	else if (!strcmp(argv[1], "power-off-wakes-reader"))
		case_power_off_wakes_reader();
	else if (!strcmp(argv[1], "signal-disables-irq"))
		case_signal_disables_irq();
	else if (!strcmp(argv[1], "release-lowers-ven"))
		case_release_lowers_ven();
	else if (!strcmp(argv[1], "suspend-wakeup-after-freeze"))
		case_suspend_wakeup_after_freeze();
	else if (!strcmp(argv[1], "write-nack-retry"))
		case_write_nack_retry();
	else
		return 2;
	sim_reset();
	printf("PASS %s\n", argv[1]);
	return 0;
}
