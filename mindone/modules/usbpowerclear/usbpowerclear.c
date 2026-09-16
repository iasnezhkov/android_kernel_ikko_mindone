// SPDX-License-Identifier: GPL-2.0
/*
 * usbpowerclear.ko — let the MUSB driver take its own enable path.
 *
 * WHY (F2286). do_connection_work() picks a branch from two flags:
 *
 *     if (!power &&  usb_on) -> musb_start() + set_usb_phy_mode(DEVICE)
 *     if ( power && !usb_on) -> musb_stop()
 *     else                   -> "do nothing, usb_on:%d, power:%d"
 *
 * `power` is already 1 from mt_usb_enable() at probe time (1.71 s), long before any
 * cable event. By the time a real connect arrives (9.97 s) both flags are 1, no branch
 * matches, and musb_start() is never called. musb_start() is the ONLY function that
 * programs MUSB_INTRUSBE, so interrupt enables are never written and GICv3 160 never
 * fires.
 *
 * WHY NOT CALL musb_start() DIRECTLY (F2287). That was tried (musbkick.ko) and it took
 * the machine down. musb_start() immediately calls musb_platform_reset() and
 * musb_generic_disable(), which are raw MMIO with no clock or runtime-PM guarantees of
 * their own — those are the CALLER's job. The USB power domain is runtime suspended
 * here (measured: 764 s suspended vs 0.7 s active), and raw MMIO against an unclocked
 * block hangs the bus.
 *
 * WHAT THIS DOES INSTEAD. Clear the `power` flag with a plain memory write — no MMIO at
 * all — and then call the exported mt_usb_connect(), which only allocates and queues
 * work. do_connection_work() then runs in its normal worker context, where it does
 * usb_prepare_clock(true) FIRST and takes mtk_musb->lock, and calls musb_start() itself
 * with both guarantees in place.
 *
 * SAFETY. The offset of `power` inside struct musb was recovered from disassembly, not
 * from a header. If the byte there is not 1, the offset is wrong and this module
 * refuses to touch anything.
 *
 * Markers are Latin (F1973).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irqflags.h>

extern void *mtk_musb;
extern void mt_usb_connect(void);

static int power_off = 0x2210;
module_param(power_off, int, 0444);
MODULE_PARM_DESC(power_off, "byte offset of musb->power inside struct musb");

static int force;
module_param(force, int, 0444);
MODULE_PARM_DESC(force, "1 = act even if the byte does not read as 1");

static int seen = -1;
module_param(seen, int, 0444);
MODULE_PARM_DESC(seen, "value of the power byte before the change");

static int __init upc_init(void)
{
	volatile unsigned char *pb;
	unsigned long flags;

	if (!mtk_musb) {
		pr_notice("MINDONE-UPC: mtk_musb is NULL, nothing to do\n");
		return 0;
	}

	pb = (volatile unsigned char *)mtk_musb + power_off;
	seen = *pb;
	pr_notice("MINDONE-UPC: musb=%px power_off=0x%x byte=%d\n",
		  mtk_musb, power_off, seen);

	if (seen != 1 && !force) {
		pr_notice("MINDONE-UPC: byte is not 1 - offset looks wrong, refusing to act\n");
		return 0;
	}

	local_irq_save(flags);
	*pb = 0;
	local_irq_restore(flags);
	mb();
	pr_notice("MINDONE-UPC: power flag cleared, byte now %d, issuing connect\n", *pb);

	mt_usb_connect();
	pr_notice("MINDONE-UPC: mt_usb_connect() issued\n");
	return 0;
}

static void __exit upc_exit(void)
{
	pr_notice("MINDONE-UPC: unloaded\n");
}

module_init(upc_init);
module_exit(upc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clear the stuck musb power flag so the driver runs its own enable path");
