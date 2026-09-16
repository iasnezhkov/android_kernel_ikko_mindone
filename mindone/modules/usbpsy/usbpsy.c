// SPDX-License-Identifier: GPL-2.0
/*
 * usbpsy.ko -- register a power supply named `usb`, which Android expects.
 *
 * WHY (F2118, F2119). Android's battery service only considers power supplies
 * of type USB / Mains / Wireless. Our kernel-6 module set has none at all: the
 * charger is named `mtk-master-charger` and reports type `Unknown`, and no
 * `usb`/`ac` nodes exist. So Android thinks the device is running on battery
 * even though the kernel sees it charging (`mtk-master-charger/online = 1`,
 * confirmed by reading it). Effects already measured:
 *   - the system constantly goes into doze (strict power-saving mode for
 *     on-battery operation);
 *   - `svc power stayon true` has no effect -- it only keeps the screen on
 *     while power is connected;
 *   - `mIsPowered=false`, `mPlugType=0` in the power service.
 *
 * WHAT IT DOES. Registers a `usb` source of type POWER_SUPPLY_TYPE_USB and,
 * on property queries, relays whatever the real charger reports. Nothing is
 * faked or written to hardware -- only read and passed through.
 *
 * WHY NOT USE THE FACTORY MODULE. One exists (`eta6965-charger-sec.ko`), but
 * it's built for 5.10.233: `module_layout` 0x7c24b32d vs our 0xf4d8bdf7 -- the
 * kernel rejects it before the first line of code runs (F1040, RETRACTED).
 * Checked against the fact registry before writing our own (rule F1970).
 *
 * SAFETY RULE (F330). The stage-1 module loader runs in STRICT mode: the
 * first failure aborts loading of the entire remaining list. So init ALWAYS
 * returns success, even if the source supply isn't ready yet -- it keeps
 * being searched for on a timer.
 *
 * Parameters:
 *   src           -- name of the source power supply (default `mtk-master-charger`);
 *   force_online  -- -1 mirror the source (default), 0/1 report this value;
 *   registered    -- read-only: whether registration succeeded;
 *   queries       -- read-only: how many times Android asked.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/slab.h>

static char *src = "mtk-master-charger";
module_param(src, charp, 0444);
MODULE_PARM_DESC(src, "name of the source power supply to mirror");

static int force_online = -1;
module_param(force_online, int, 0644);
MODULE_PARM_DESC(force_online, "-1 = mirror source, 0/1 = report this value");

static int registered;
module_param(registered, int, 0444);

static unsigned long queries;
module_param(queries, ulong, 0444);

static unsigned long source_misses;
module_param(source_misses, ulong, 0444);

static struct power_supply *usb_psy;

static int source_get(enum power_supply_property prop, int *out)
{
	struct power_supply *d;
	union power_supply_propval v;
	int ret;

	d = power_supply_get_by_name(src);
	if (!d) {
		source_misses++;
		return -ENODEV;
	}
	ret = power_supply_get_property(d, prop, &v);
	power_supply_put(d);
	if (ret)
		return ret;
	*out = v.intval;
	return 0;
}

static int usbpsy_get_property(struct power_supply *psy,
			       enum power_supply_property prop,
			       union power_supply_propval *val)
{
	int v = 0;

	queries++;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (force_online >= 0) {
			val->intval = force_online ? 1 : 0;
			return 0;
		}
		if (source_get(POWER_SUPPLY_PROP_ONLINE, &v))
			return -ENODEV;
		val->intval = v ? 1 : 0;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		if (force_online >= 0) {
			val->intval = force_online ? 1 : 0;
			return 0;
		}
		if (source_get(POWER_SUPPLY_PROP_ONLINE, &v))
			return -ENODEV;
		val->intval = v ? 1 : 0;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (source_get(POWER_SUPPLY_PROP_VOLTAGE_NOW, &v))
			return -ENODEV;
		val->intval = v;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (source_get(POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &v))
			return -ENODEV;
		val->intval = v;
		return 0;
	default:
		return -EINVAL;
	}
}

static enum power_supply_property usbpsy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc usbpsy_desc = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= usbpsy_props,
	.num_properties	= ARRAY_SIZE(usbpsy_props),
	.get_property	= usbpsy_get_property,
};

/* NOTIFICATION KICK (F2137). The `extcon-mtk-usb` driver takes a power supply
 * by a DT reference, subscribes to notifications, and only ON NOTIFICATION
 * reads `online`/`type` and assigns the USB role. Our charger reports
 * `online=1` from boot and never changes after that, so no notification ever
 * fires: the log has zero `online=%d, type=%d` lines, with zero errors too.
 * Here we send one notification for the source supply -- a standard kernel
 * call, nothing faked. The `kick` parameter allows repeating it on a live
 * system. */
static int kick_delay_ms = 9000;
module_param(kick_delay_ms, int, 0644);
MODULE_PARM_DESC(kick_delay_ms, "delay before the one-shot power-supply notification, ms (0 = off)");

static unsigned long kicks;
module_param(kicks, ulong, 0444);

/* MUST KICK THE SUPPLY THE DT ACTUALLY REFERENCES (F2137, F2142).
 * `extcon-mtk-usb` takes its power supply via the `charger` reference of the
 * `/soc/extcon_usb` node; that reference points to `/eta6965_chg`, and a
 * supply of the same name exists on this device. The first attempt kicked
 * `mtk-master-charger` -- the notification arrived, but the driver dropped it
 * because it compares the supply against its own. The right supply already
 * has all the correct data:
 *   POWER_SUPPLY_TYPE=USB . ONLINE=1 . USB_TYPE=Unknown [SDP] DCP CDP
 * i.e. the cable is recognized as a host connection -- the driver just had
 * no one to tell. */
static char *kick_src = "eta6965_chg";
module_param(kick_src, charp, 0644);
MODULE_PARM_DESC(kick_src, "power supply to notify (the one extcon watches)");

static void kick_one(const char *name)
{
	struct power_supply *d = power_supply_get_by_name(name);

	if (!d) {
		pr_notice("MINDONE-USBPSY: kick skipped, supply '%s' absent\n", name);
		return;
	}
	power_supply_changed(d);
	power_supply_put(d);
	kicks++;
	pr_notice("MINDONE-USBPSY: power_supply_changed('%s') sent (kick %lu)\n", name, kicks);
}

static void do_kick(void)
{
	/* kick both the one extcon listens to and the source -- both calls are
	 * standard and harmless */
	kick_one(kick_src);
	if (strcmp(kick_src, src) != 0)
		kick_one(src);
}

static int kick_set(const char *val, const struct kernel_param *kp)
{
	do_kick();
	return 0;
}
static const struct kernel_param_ops kick_ops = { .set = kick_set };
module_param_cb(kick, &kick_ops, NULL, 0644);
MODULE_PARM_DESC(kick, "write anything to send a power-supply notification now");

static struct timer_list kick_timer;
static void kick_fn(struct timer_list *t)
{
	do_kick();
}

static struct timer_list retry_timer;
static unsigned long tries;

/* THE PARENT IS TAKEN FROM THE SOURCE, AND THIS MATTERS (F2122).
 * Registering with a NULL parent puts the node under
 * /sys/devices/virtual/power_supply/ and gives it the `sysfs_power_supply`
 * security label. Android's health service only has access to
 * `sysfs_batteryinfo`, so our node is INVISIBLE to it: the log shows a direct
 * denial `avc: denied { search } ... name="power_supply"`. A manual `chcon`
 * relabel fixes this but only until reboot. Registering under the SAME parent
 * as the real charger instead puts the node into an already-labeled subtree,
 * and it picks up the correct label on its own. */
static void try_register(void)
{
	struct power_supply_config cfg = { };
	struct power_supply *p, *d;
	struct device *parent = NULL;

	if (registered)
		return;

	tries++;

	d = power_supply_get_by_name(src);
	if (!d) {
		/* the source isn't there yet -- wait, we won't register with NULL:
		 * the label would come out wrong, and that's the very defect we're fixing */
		if (tries <= 3)
			pr_info("MINDONE-USBPSY: source '%s' not ready yet (try %lu)\n",
				src, tries);
		return;
	}
	parent = d->dev.parent;
	power_supply_put(d);

	if (!parent) {
		if (tries <= 3)
			pr_info("MINDONE-USBPSY: source '%s' has no parent device (try %lu)\n",
				src, tries);
		return;
	}

	p = power_supply_register(parent, &usbpsy_desc, &cfg);
	if (IS_ERR(p)) {
		pr_info("MINDONE-USBPSY: register failed, ret=%ld (try %lu)\n",
			PTR_ERR(p), tries);
		return;
	}
	usb_psy = p;
	registered = 1;
	pr_info("MINDONE-USBPSY: power supply 'usb' registered under '%s', source='%s' (try %lu)\n",
		dev_name(parent), src, tries);

	/* one delayed kick -- gives extcon a reason to read the state (see above) */
	if (kick_delay_ms > 0) {
		timer_setup(&kick_timer, kick_fn, 0);
		mod_timer(&kick_timer, jiffies + msecs_to_jiffies(kick_delay_ms));
	}
}

static void retry_fn(struct timer_list *t)
{
	try_register();
	if (!registered && tries < 120)
		mod_timer(&retry_timer, jiffies + msecs_to_jiffies(1000));
}

static int __init usbpsy_init(void)
{
	try_register();
	if (!registered) {
		/* Return SUCCESS: otherwise the strict stage-1 loader aborts the
		 * entire remaining module list (F330). Retry on a timer. */
		timer_setup(&retry_timer, retry_fn, 0);
		mod_timer(&retry_timer, jiffies + msecs_to_jiffies(1000));
		pr_info("MINDONE-USBPSY: not registered yet, retrying every second\n");
	}
	return 0;
}

static void __exit usbpsy_exit(void)
{
	del_timer_sync(&retry_timer);
	if (usb_psy)
		power_supply_unregister(usb_psy);
	pr_info("MINDONE-USBPSY: unregistered (queries=%lu, source_misses=%lu)\n",
		queries, source_misses);
}

module_init(usbpsy_init);
module_exit(usbpsy_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Expose a 'usb' power supply mirroring the MediaTek master charger");
