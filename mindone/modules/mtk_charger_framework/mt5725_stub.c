// SPDX-License-Identifier: GPL-2.0
/*
 * Stub for the MT5725/MT5728 15W wireless-charging IC support that
 * mtk_charger.c / mtk_basic_charger.c call unconditionally (the vendor never
 * gated it behind a Kconfig symbol). This device has no MT5725/MT5728
 * wireless-charge chip, so the vendor driver was never built here (its own
 * internal calls, e.g. mt_vbus_revere_off(), have no definition anywhere in
 * the vendor tree either).
 *
 * 15.09: that unbuilt driver and its two Maxictech register headers were
 * DELETED outright. They were dead files -- no Makefile referenced them, no
 * source included them, and no wireless-charging module ships in the 290-module
 * set -- but they carried "Maxictech Proprietary and Confidential" notices,
 * which is a licensing risk in a tree meant to be published under GPL-2.0.
 * Eleven copies in five module directories; removing them leaves the build
 * bit-identical. These stubs report
 * "not present" exactly like the real driver would if it failed i2c
 * probe -- the vendor call sites already handle that outcome gracefully
 * (see mt5725_wireless_init() call site: return <0 is logged and
 * ignored).
 */
#include <linux/errno.h>
#include <linux/export.h>

struct charger_data;

int get_MT5725_status(void)
{
	return 1; /* non-zero == "not present", matches all `== 0` call sites */
}
EXPORT_SYMBOL(get_MT5725_status); /* also consumed cross-module by extcon_mtk_usb */

int get_wireless_charge_current(struct charger_data *pdata)
{
	return 0;
}

void En_Dis_add_current(int i)
{
}

int mt5725_wireless_init(void)
{
	return -ENODEV;
}
