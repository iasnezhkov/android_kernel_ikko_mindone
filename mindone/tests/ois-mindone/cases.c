// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
static struct ois_mindone *g_drop_ois;

static void drop_power_cb(void)
{
	if (g_drop_ois) {
		atomic_inc(&g_drop_ois->seq);
		atomic_set(&g_drop_ois->powered, 0);
	}
}

static void reset_params(void)
{
	default_mode = OIS_MODE_OIS;
	boot_delay_ms = 350;
	reg26_off = -1;
	reg26_center = -1;
	reg26_ois = -1;
}

static struct ois_mindone *setup_client(struct i2c_adapter *adap, struct i2c_client *client)
{
	sim_reset();
	reset_params();
	memset(adap, 0, sizeof(*adap));
	memset(client, 0, sizeof(*client));
	adap->nr = 4;
	client->adapter = adap;
	client->addr = 0x69;
	assert(ois_mindone_probe(client) == 0);
	return i2c_get_clientdata(client);
}

static void case_probe_remove(void)
{
	struct i2c_adapter adap = { .nr = 4 };
	struct i2c_client client = { .adapter = &adap, .addr = 0x69 };
	struct ois_mindone *ois;

	sim_reset();
	reset_params();
	sim.notifier_fail_rc = -ENOMEM;
	assert(ois_mindone_probe(&client) == -ENOMEM);
	assert(sim.slow_calls == 2);
	assert(sim.slow_hz == 0);
	stub_free_all();

	ois = setup_client(&adap, &client);
	assert(sim.slow_nr == 4 && sim.slow_addr == 0x69 && sim.slow_hz == 100000);
	assert(sim.registered_nb == &ois->nb);

	struct imgsensor_power_event ev = { .sensor_idx = 0 };
	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev) == NOTIFY_OK);
	stub_now_ms += 350;
	ois_boot_work(&ois->boot_work.work);
	assert(ois->ready && ois->active_mode == OIS_MODE_OIS && sim.regs[0x0a] == 0x0001);

	ois_mindone_remove(&client);
	assert(sim.unregister_calls == 1);
	assert(sim.registered_nb == NULL);
	assert(sim.regs[0x0a] == 0x0000);
	assert(!ois->ready && ois->active_mode == OIS_MODE_OFF);
	assert(sim.slow_hz == 0);
	stub_free_all();
}

static void case_write_allowlist(void)
{
	struct i2c_adapter adap;
	struct i2c_client client;
	struct ois_mindone *ois = setup_client(&adap, &client);

	for (int r = 0; r < 128; r++) {
		int before = sim.log_count;
		int rc = ois_write_reg(ois, (u8)r, 0x1234);
		if (r == OIS_REG_ENABLE || r == OIS_REG_MODE) {
			assert(rc == 0);
			assert(sim.log_count == before + 1);
			assert(sim.log[before].is_write == 1);
			assert(sim.log[before].reg == (u8)r);
			assert(sim.log[before].val == 0x1234);
		} else {
			assert(rc == -EACCES);
			assert(sim.log_count == before);
		}
	}
	stub_free_all();
}

static void case_boot_and_park(void)
{
	struct i2c_adapter adap;
	struct i2c_client client;
	struct ois_mindone *ois = setup_client(&adap, &client);
	struct imgsensor_power_event ev0 = { .sensor_idx = 0 };
	struct imgsensor_power_event ev1 = { .sensor_idx = 1 };
	char buf[512];

	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev0) == NOTIFY_OK);
	assert(ois->boot_work.scheduled == 1 && ois->boot_work.delay == 350);
	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_OFF, &ev0) == NOTIFY_OK);
	assert(ois->boot_work.scheduled == 0);
	assert(sim.log_count == 0);

	sim.nack_reads_left = 3;
	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev1) == NOTIFY_OK);
	stub_now_ms += 350;
	ois_boot_work(&ois->boot_work.work);
	assert(ois->ready);
	assert(ois->chip_id == 0x5a30);
	assert(ois->active_mode == OIS_MODE_OIS);
	assert(ois->boot_ms == 350 + 3 * 30);
	assert(sim.regs[0x0a] == 0x0001);
	assert(sim.regs[0x26] == 0x0002);
	for (int i = 0; i < sim.log_count; i++)
		assert(!(sim.log[i].is_write && sim.log[i].reg == OIS_REG_MODE));

	assert(status_show(&client.dev, NULL, buf) > 0);
	assert(strstr(buf, "powered=1 ready=1 sensor_idx=1 chip_id=0x5a30 target=ois active=ois reg0a=0x0001 reg26=0x0002") != NULL);
	assert(telemetry_show(&client.dev, NULL, buf) > 0);
	assert(strstr(buf, "id=0x5a30") != NULL && strstr(buf, "x=16 y=-25") != NULL);

	assert(mode_store(&client.dev, NULL, "center\n", 7) == 7);
	assert(ois->active_mode == OIS_MODE_CENTER && sim.regs[0x0a] == 0x0000);
	assert(mode_store(&client.dev, NULL, "ois", 3) == 3);
	assert(ois->active_mode == OIS_MODE_OIS && sim.regs[0x0a] == 0x0001);
	assert(mode_store(&client.dev, NULL, "bad", 3) == -EINVAL);

	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_OFF, &ev1) == NOTIFY_OK);
	assert(!ois->ready && ois->active_mode == OIS_MODE_OFF);
	assert(ois->target_mode == OIS_MODE_OIS);
	assert(sim.regs[0x0a] == 0x0000);
	assert(telemetry_show(&client.dev, NULL, buf) > 0);
	assert(strcmp(buf, "offline\n") == 0);
	stub_free_all();
}

static void case_chip_id_and_cancel(void)
{
	struct i2c_adapter adap;
	struct i2c_client client;
	struct ois_mindone *ois = setup_client(&adap, &client);
	struct imgsensor_power_event ev = { .sensor_idx = 0 };

	sim.regs[0x01] = 0x5a31;
	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev) == NOTIFY_OK);
	ois_boot_work(&ois->boot_work.work);
	assert(!ois->ready);
	assert(ois->last_err == -ENODEV);
	assert(ois->active_mode == OIS_MODE_OFF);
	for (int i = 0; i < sim.log_count; i++)
		assert(!sim.log[i].is_write);

	sim.regs[0x01] = 0x5a30;
	sim.nack_reads_left = 1;
	g_drop_ois = ois;
	sim.on_msleep = drop_power_cb;
	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev) == NOTIFY_OK);
	int before = sim.log_count;
	ois_boot_work(&ois->boot_work.work);
	g_drop_ois = NULL;
	sim.on_msleep = NULL;
	assert(!ois->ready);
	assert(sim.log_count == before);
	stub_free_all();
}

static void case_reg26_params(void)
{
	struct i2c_adapter adap;
	struct i2c_client client;
	struct ois_mindone *ois = setup_client(&adap, &client);
	struct imgsensor_power_event ev = { .sensor_idx = 0 };
	struct kernel_param kp = { .arg = &reg26_ois };

	assert(ois_reg26_set("-2", &kp) == -EINVAL);
	assert(ois_reg26_set("16", &kp) == -EINVAL);
	assert(ois_reg26_set("-1", &kp) == 0 && reg26_ois == -1);
	assert(ois_reg26_set("4", &kp) == 0 && reg26_ois == 4);

	assert(ois_power_event(&ois->nb, IMGSENSOR_POWER_ON, &ev) == NOTIFY_OK);
	ois_boot_work(&ois->boot_work.work);
	assert(ois->ready);
	assert(sim.regs[0x26] == 4);
	assert(sim.regs[0x0a] == 1);
	assert(ois->reg26 == 4);
	stub_free_all();
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "probe-remove"))
		case_probe_remove();
	else if (!strcmp(argv[1], "write-allowlist"))
		case_write_allowlist();
	else if (!strcmp(argv[1], "boot-and-park"))
		case_boot_and_park();
	else if (!strcmp(argv[1], "chip-id-and-cancel"))
		case_chip_id_and_cancel();
	else if (!strcmp(argv[1], "reg26-params"))
		case_reg26_params();
	else
		return 2;
	printf("PASS %s\n", argv[1]);
	return 0;
}
