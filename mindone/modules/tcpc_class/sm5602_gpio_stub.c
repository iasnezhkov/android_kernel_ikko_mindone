// SPDX-License-Identifier: GPL-2.0
/*
 * set_otg_en_t()/test_gpio_t() are board-specific debug GPIO toggles
 * defined in drivers/power/supply/sm5602_fg.c (SM5602 fuel-gauge driver,
 * not part of this build/manifest target and not confirmed present on
 * this device). tcpci_typec.c calls them unconditionally from the
 * typec attach/detach path for what looks like SM5602-reference-board
 * wiring (spare GPIOs toggled on C-to-C OTG/charge transitions). Stubbed
 * as no-ops here -- both are void, pure side effect, no return value
 * any caller depends on.
 */

void set_otg_en_t(int en)
{
}

void test_gpio_t(int en)
{
}
