// SPDX-License-Identifier: GPL-2.0
/*
 * musb_glue_mt6789.c -- the platform glue that MediaTek did not ship to us.
 *
 * WHY THIS FILE EXISTS (F2213, F2214). Our musb_hdrc was built with
 * CONFIG_FPGA_EARLY_PORTING=1, which makes usb20.h define FPGA_PLATFORM and cuts eight regions
 * out of musb_core.c -- among them ALL clock acquisition (sys_clk, ref_clk, src_clk, dma_clk,
 * phy_clk, mcu_clk), the controller regulator and extcon lookup. The USB block therefore ran
 * with its clocks never enabled, which explains the whole symptom set: the interrupt is
 * registered but never fires, nothing appears on the bus, and writing soft_connect kills the
 * device (a register write into an unclocked block aborts).
 *
 * Dropping the flag compiles cleanly, but leaves eleven undefined symbols. They live in the
 * MediaTek platform files usb20.c and usb20_phy.c, and MediaTek's published sources do not contain
 * those two files at all -- the same gap already recorded in F2143. This file supplies them.
 *
 * HONEST SPLIT -- what is real and what is not:
 *   REAL: mt_usb_clock_prepare / mt_usb_clock_unprepare. usb_prepare_clock() is present in
 *         musb_core.c, so these are thin wrappers with the vendor semantics, not stubs.
 *   STUB: everything else. Each stub below says what is given up.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>

#include <musb_core.h>
#include "usb20.h"

/* ---- REAL: clock prepare/unprepare -------------------------------------
 * The vendor puts these in usb20.c as wrappers over usb_prepare_clock(), which we DO have
 * (musb_core.c). Called from the suspend/resume paths (musb_core.c:2799/2814/2843/2857).
 */
void mt_usb_clock_prepare(void)
{
	usb_prepare_clock(true);
}

void mt_usb_clock_unprepare(void)
{
	usb_prepare_clock(false);
}

/* ---- STUB: PHY context across deep sleep --------------------------------
 * Vendor saves/restores PHY tuning registers around suspend. Giving this up means PHY tuning
 * may be lost after a deep sleep cycle; it does NOT affect a cold boot, which is what we need
 * first. Revisit once enumeration works.
 */
void usb_phy_context_save(void) { }
void usb_phy_context_restore(void) { }

/* ---- STUB: UART mode on the USB pins ------------------------------------
 * Some MediaTek boards multiplex a debug UART onto the USB D+/D- pins. This device does not use
 * that, so reporting "not in UART mode" is the correct answer here, not a compromise.
 */
bool in_uart_mode;
EXPORT_SYMBOL(in_uart_mode);

bool usb_phy_check_in_uart_mode(void)
{
	return false;
}

/* ---- STUB: Apple-charger workaround flag --------------------------------
 * Used once in the interrupt handler (musb_core.c:970) to swallow a spurious SUSPEND when an
 * Apple charger is attached. Default false = workaround off, which is the plain behaviour.
 */
bool apple;
EXPORT_SYMBOL(apple);

/* ---- STUB: VBUS polling thread -----------------------------------------
 * Vendor spawns a kthread (musb_core.c:2513) that polls the VBUS ADC on boards without a
 * comparator interrupt. Ours reports VBUS through the charger and extcon, so an immediately
 * returning thread costs nothing.
 */
int polling_vbus_value(void *data)
{
	return 0;
}

/* ---- STUB: audio SRAM sharing ------------------------------------------
 * USB audio can borrow the audio SRAM for its queue descriptors. Refusing makes the queue fall
 * back to ordinary DRAM -- slower for USB audio, harmless for everything else.
 */
int mtk_audio_request_sram(dma_addr_t *phys_addr, unsigned char **virt_addr,
			   unsigned int length, void *user)
{
	return -ENOMEM;
}

void mtk_audio_free_sram(void *user) { }

/* ---- STUB: legacy dual-role notification --------------------------------
 * The vendor file mtk_dual_role.c calls devm_dual_role_instance_register(), an interface that
 * no longer exists in kernel 6. Its modern replacement -- the USB role switch -- is already
 * provided by our roleshim.ko, so returning success here is the correct mapping, not a loss.
 */
int mt_usb_dual_role_changed(struct musb *musb)
{
	return 0;
}
