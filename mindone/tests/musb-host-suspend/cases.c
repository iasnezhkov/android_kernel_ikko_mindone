/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static void reset_state(struct musb *musb, struct usb_phy *phy, struct usb_otg *otg,
			struct usb_bus *bus)
{
	memset(musb, 0, sizeof(*musb));
	memset(phy, 0, sizeof(*phy));
	memset(otg, 0, sizeof(*otg));
	memset(bus, 0, sizeof(*bus));
	otg->host = bus;
	phy->otg = otg;
	musb->xceiv = phy;
	musb->is_host = 1;
	jiffies = 1000;
	idle_work.pending = 0;
	dbg0_count = 0;
	dbg2_count = 0;
	timer_delete_calls = 0;
	timer_delete_sync_calls = 0;
	mod_timer_calls = 0;
	cancel_delayed_work_sync_calls = 0;
	udelay_total = 0;
	dpidle_last = 0;
	dpidle_calls = 0;
	power_read_calls = 0;
	power_clear_after = 0;
	mmio_power = 0;
	mmio_devctl = 0;
	mmio_resreg = 0;
	cd_clr_seen_high = false;
	cd_clr_seen_udelay = false;
}

#define CHECK_COND(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", (msg)); \
		return 1; \
	} \
} while (0)

static int case_a_suspend_no_idle_timer(void)
{
	struct musb musb;
	struct usb_phy phy;
	struct usb_otg otg;
	struct usb_bus bus;

	reset_state(&musb, &phy, &otg, &bus);
	otg.state = OTG_STATE_A_SUSPEND;
	musb.is_active = 0;
	musb.idle_timer.pending = 1;
	mt_usb_try_idle(&musb, 0);
	CHECK_COND(timer_delete_calls == 1, "idle_timer not deleted in A_SUSPEND");
	CHECK_COND(musb.idle_timer.pending == 0, "idle_timer still pending in A_SUSPEND");
	CHECK_COND(mod_timer_calls == 0, "idle_timer re-armed in A_SUSPEND");
	CHECK_COND(dbg0_count == 0, "mt_usb_try_idle emitted DBG(0) spam in A_SUSPEND");

	otg.state = OTG_STATE_A_HOST;
	jiffies = 1100;
	mt_usb_try_idle(&musb, 0);
	CHECK_COND(mod_timer_calls == 1, "idle_timer not armed in A_HOST");
	CHECK_COND(dbg0_count == 0, "mt_usb_try_idle emitted DBG(0) spam in A_HOST");
	printf("PASS a-suspend-no-idle-timer\n");
	return 0;
}

static int case_musb_suspend_drains_idle(void)
{
	struct musb musb;
	struct usb_phy phy;
	struct usb_otg otg;
	struct usb_bus bus;
	struct device dev = { .musb = &musb };

	reset_state(&musb, &phy, &otg, &bus);
	musb.idle_timer.pending = 1;
	idle_work.pending = 1;
	CHECK_COND(musb_suspend(&dev) == 0, "musb_suspend returned nonzero");
	CHECK_COND(timer_delete_sync_calls == 1, "musb_suspend did not call timer_delete_sync");
	CHECK_COND(musb.idle_timer.pending == 0, "idle_timer still pending after musb_suspend");
	CHECK_COND(cancel_delayed_work_sync_calls == 1, "musb_suspend did not drain idle_work");
	CHECK_COND(idle_work.pending == 0, "idle_work still pending after musb_suspend");
	printf("PASS musb-suspend-drains-idle\n");
	return 0;
}

static int case_uwk_v2_disable_clears_cd(void)
{
	struct musb musb;
	struct usb_phy phy;
	struct usb_otg otg;
	struct usb_bus bus;
	struct regmap peri = { 0 };
	struct regmap infra = { 0 };

	reset_state(&musb, &phy, &otg, &bus);
	pericfg = &peri;
	infracg = &infra;
	uwk_vers = MUSB_UWK_V2;
	musb.port1_status = USB_PORT_STAT_CONNECTION;
	peri.regs[USB_WK_CTRL / 4] = USB_CDEN | USB_CDDEBOUNCE(0xf) | BIT(1);
	infra.regs[MISC_CONFIG / 4] = BIT(2);
	mmio_resreg = HSTPWRDWN_OPT | BIT(3);

	mt_usb_wakeup(&musb, false);
	CHECK_COND(peri.regs[USB_WK_CTRL / 4] == BIT(1),
		   "MUSB_UWK_V2 disable did not clear USB_CDEN and full 4-bit USB_CDDEBOUNCE");
	CHECK_COND(cd_clr_seen_high, "MUSB_UWK_V2 disable did not assert USB_CD_CLR");
	CHECK_COND(cd_clr_seen_udelay && udelay_total >= 50,
		   "MUSB_UWK_V2 disable did not hold USB_CD_CLR for 50 us");
	CHECK_COND(infra.regs[MISC_CONFIG / 4] == BIT(2),
		   "MUSB_UWK_V2 disable left USB_CD_CLR stuck high or clobbered bits");
	CHECK_COND(mmio_resreg == BIT(3), "MUSB_UWK_V2 disable did not clear HSTPWRDWN_OPT");
	printf("PASS uwk-v2-disable-clears-cd\n");
	return 0;
}

static int case_port_suspend_no_spin(void)
{
	struct musb musb;
	struct usb_phy phy;
	struct usb_otg otg;
	struct usb_bus bus;

	reset_state(&musb, &phy, &otg, &bus);
	otg.state = OTG_STATE_A_HOST;
	bus.b_hnp_enable = 0;
	mmio_power = 0;
	CHECK_COND(musb_port_suspend(&musb, true) == 0, "musb_port_suspend failed");
	CHECK_COND(power_read_calls == 2, "musb_port_suspend spun on MUSB_POWER without HNP");
	CHECK_COND(otg.state == OTG_STATE_A_SUSPEND, "state did not enter A_SUSPEND");
	CHECK_COND((musb.port1_status & USB_PORT_STAT_SUSPEND) != 0, "port suspend bit missing");
	CHECK_COND(mod_timer_calls == 0, "port suspend armed idle_timer in A_SUSPEND");

	reset_state(&musb, &phy, &otg, &bus);
	otg.state = OTG_STATE_A_HOST;
	bus.b_hnp_enable = 1;
	mmio_power = 0;
	power_clear_after = 4;
	CHECK_COND(musb_port_suspend(&musb, true) == 0, "HNP musb_port_suspend failed");
	CHECK_COND(power_read_calls == 4, "HNP musb_port_suspend did not poll MUSB_POWER");
	printf("PASS port-suspend-no-spin\n");
	return 0;
}

static int case_bus_suspend_already_suspended(void)
{
	struct musb musb;
	struct usb_phy phy;
	struct usb_otg otg;
	struct usb_bus bus;
	struct usb_hcd hcd = { .musb = &musb };

	reset_state(&musb, &phy, &otg, &bus);
	otg.state = OTG_STATE_A_SUSPEND;
	musb.port1_status = USB_PORT_STAT_CONNECTION | USB_PORT_STAT_SUSPEND;
	mmio_power = MUSB_POWER_SUSPENDM;
	CHECK_COND(musb_bus_suspend(&hcd) == 0, "musb_bus_suspend failed when already suspended");
	CHECK_COND(power_read_calls == 0, "musb_bus_suspend re-entered musb_port_suspend");
	CHECK_COND(dbg0_count == 0, "musb_bus_suspend emitted bogus rh suspend log");
	CHECK_COND(dpidle_calls == 1 && dpidle_last == USB_DPIDLE_TIMER,
		   "musb_bus_suspend did not request USB_DPIDLE_TIMER");

	reset_state(&musb, &phy, &otg, &bus);
	otg.state = OTG_STATE_A_HOST;
	musb.port1_status = USB_PORT_STAT_CONNECTION;
	CHECK_COND(musb_bus_suspend(&hcd) == 0, "musb_bus_suspend failed from A_HOST");
	CHECK_COND(power_read_calls == 2, "musb_bus_suspend did not suspend active root port");
	CHECK_COND(otg.state == OTG_STATE_A_SUSPEND, "musb_bus_suspend did not reach A_SUSPEND");
	printf("PASS bus-suspend-already-suspended\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "a-suspend-no-idle-timer") == 0)
		return case_a_suspend_no_idle_timer();
	if (strcmp(argv[1], "musb-suspend-drains-idle") == 0)
		return case_musb_suspend_drains_idle();
	if (strcmp(argv[1], "uwk-v2-disable-clears-cd") == 0)
		return case_uwk_v2_disable_clears_cd();
	if (strcmp(argv[1], "port-suspend-no-spin") == 0)
		return case_port_suspend_no_spin();
	if (strcmp(argv[1], "bus-suspend-already-suspended") == 0)
		return case_bus_suspend_already_suspended();
	return 2;
}
