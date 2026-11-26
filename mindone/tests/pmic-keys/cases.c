/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
int main(void)
{
	struct platform_driver *drv = pmic_keys_test_driver();
	struct regmap rmap = {};
	struct device pwrap = { .regmap = &rmap };
	struct device parent = { .parent = &pwrap };
	struct device_node pwr = { .keycode = 116, .wakeup = true };
	struct device_node home = { .keycode = 59, .wakeup = false };
	struct device_node root = {
		.lp_mode = 1,
		.lp_sec = 0,
		.num_children = 2,
		.children = { &pwr, &home },
	};
	struct platform_device pdev = {
		.dev = {
			.parent = &parent,
			.of_node = &root,
			.compatible = "mediatek,mt6366-keys",
		},
	};

	sim_reset();
	rmap.regs[MT6358_TOP_RST_MISC] = 0x0025;
	assert(drv->probe(&pdev) == 0);
	assert(rmap.regs[MT6358_TOP_RST_MISC] == 0x0225);
	assert(rmap.regs[MT6358_PSC_TOP_INT_CON0] == 0x0f);
	assert(sim.input.registered == 1 && sim.input.cap_count == 2);
	assert(pdev.dev.wakeup_capable == 1);

	assert(sim.irq_fn[1] != NULL && sim.irq_fn[3] != NULL);
	assert(sim.irq_fn[2] != NULL && sim.irq_fn[4] != NULL);
	rmap.regs[MT6358_TOPSTATUS] = 0x2;
	assert(sim.irq_fn[1](1, sim.irq_data[1]) == IRQ_HANDLED);
	assert(sim.input.last_code == 116 && sim.input.last_val == 1);
	assert(pdev.dev.stay_awake_count == 1);

	assert(sim.irq_fn[3](3, sim.irq_data[3]) == IRQ_HANDLED);
	assert(sim.input.last_code == 116 && sim.input.last_val == 0);
	assert(pdev.dev.stay_awake_count == 0);

	assert(drv->driver.pm->suspend(&pdev.dev) == 0);
	assert(sim.irq_wake[1] == 1 && sim.irq_wake[3] == 1);
	assert(sim.irq_wake[2] == 0 && sim.irq_wake[4] == 0);
	assert(drv->driver.pm->resume(&pdev.dev) == 0);
	assert(sim.irq_wake[1] == 0 && sim.irq_wake[3] == 0);

	sim_reset();
	root.lp_mode = 2;
	root.lp_sec = 2;
	rmap.regs[MT6358_TOP_RST_MISC] = 0x0025;
	assert(drv->probe(&pdev) == 0);
	assert(rmap.regs[MT6358_TOP_RST_MISC] == 0x2325);

	sim_reset();
	printf("PASS pmic-keys\n");
	return 0;
}
