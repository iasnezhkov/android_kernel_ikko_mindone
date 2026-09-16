// SPDX-License-Identifier: GPL-2.0
/*
 * roleshim.ko -- register the USB role switch on kernel 6 on behalf of mt_usb.
 * WHY (F1641-F1643): musb_hdrc is built with FPGA_EARLY_PORTING, so it never reads
 * "usb-role-switch" and registers no switch -> extcon finds no role -> USB dead.
 * MINDONE-ROLESHIM-GUARD (F2959, F3015, AUDIT-DMESG-61-0905): if musb DOES register its
 * own switch (packaging mistake), adopt it via usb_role_switch_get() and stop on -EEXIST
 * instead of retrying 240x with a kobject WARN storm (216 splats seen in expdb, F3713).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/usb/role.h>
#include <linux/property.h>   /* dev_fwnode */
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

/* MINDONE: resolve a symbol by NAME using a throwaway kprobe.
 * kallsyms_lookup_name() is not exported on 6.1, but kprobe.symbol_name resolves names,
 * including symbols exported by other modules. Same trick as fix_charger_selfres.c. */
static unsigned long roleshim_resolve(const char *name)
{
	struct kprobe kp;
	unsigned long addr = 0;

	memset(&kp, 0, sizeof(kp));
	kp.symbol_name = name;
	if (register_kprobe(&kp) == 0) {
		addr = (unsigned long)kp.addr;
		unregister_kprobe(&kp);
	}
	return addr;
}

/* A direct reference to mt_usb_connect DOES NOT WORK: the symbol is exported by
 * the musb_hdrc module, not the kernel, and the build trips on its absence
 * from the tree's Module.symvers. Pulling in a foreign Module.symvers is
 * risky -- symbol versions might not match the SHIPPED module, and loading
 * would fail. So the address is passed as a parameter and resolved at THIS
 * SAME boot (addresses are randomized). */
typedef void (*connect_fn)(void);
static unsigned long connect_addr;
module_param(connect_addr, ulong, 0);
MODULE_PARM_DESC(connect_addr, "address of mt_usb_connect from /proc/kallsyms");

/* MEASURED (F1647): mt_usb_connect() does NOTHING when the controller is
 * already up -- log shows "do_connection_work: do nothing, usb_on:1,
 * power:1" while the gadget is actually not attached. A RECONNECT call is
 * needed. Toggling soft_connect on kernel 6 is NOT ALLOWED -- it reboots the
 * device (F1648). */
static unsigned long reconnect_addr;
module_param(reconnect_addr, ulong, 0);
MODULE_PARM_DESC(reconnect_addr, "address of mt_usb_reconnect from /proc/kallsyms");

static struct usb_role_switch *sw;
/* MINDONE-ROLESHIM-GUARD: true only when `sw` was obtained by REGISTERING a
 * new switch (usb_role_switch_unregister() is the right teardown for it).
 * When `sw` was instead ADOPTED from an already-registered native switch
 * (see roleshim_try() below), teardown must be usb_role_switch_put() -- the
 * exit path is picked from this flag, never guessed from IS_ERR_OR_NULL. */
static bool sw_is_ours;
static struct device *mtusb;
static enum usb_role cur_role = USB_ROLE_NONE;

static unsigned long sets;
module_param(sets, ulong, 0444);
MODULE_PARM_DESC(sets, "how many times a role change was requested (check)");

static int shim_set(struct usb_role_switch *s, enum usb_role role)
{
	cur_role = role;
	sets++;
	pr_notice("roleshim: role %d requested\n", (int)role);
	if (role == USB_ROLE_DEVICE) {
		if (connect_addr)
			((connect_fn)connect_addr)();
		/* the reconnect is what actually brings the session up, see above */
		if (reconnect_addr)
			((connect_fn)reconnect_addr)();
	}
	return 0;
}

static enum usb_role shim_get(struct usb_role_switch *s)
{
	return cur_role;
}

/* A lever for MEASURING without extcon involved. The normal path is extcon
 * assigning the role itself, but its detector only wakes up on a power-supply
 * change notification, and sending that notification caused a reboot (F1651).
 * Here the role is set directly, isolating the question "does the reconnect
 * bring the bus up". Usage: echo 1 > /sys/module/roleshim/parameters/force_device */
static int force_device_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops force_device_ops = {
	.set = force_device_set,
	.get = param_get_int,
};
static int force_device;
module_param_cb(force_device, &force_device_ops, &force_device, 0644);
MODULE_PARM_DESC(force_device, "write 1 -- assign the device role directly");

static int match_by_name(struct device *dev, const void *data)
{
	return dev_name(dev) && !strcmp(dev_name(dev), (const char *)data);
}

static int force_device_set(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (ret)
		return ret;
	if (force_device) {
		pr_notice("roleshim: assigning device role manually\n");
		/* MINDONE-ROLESHIM-GUARD: `sw` may be a switch we ADOPTED (native
		 * musb registration, sw_is_ours == false) rather than one we
		 * registered ourselves. Calling shim_set() directly only touches
		 * our own bookkeeping and the connect_addr/reconnect_addr kprobe
		 * hack -- correct for a switch WE own, but a no-op on the real
		 * hardware path for an adopted one. Route through the real API in
		 * that case so the lever still does something on either switch. */
		if (sw_is_ours)
			shim_set(sw, USB_ROLE_DEVICE);
		else if (!IS_ERR_OR_NULL(sw))
			usb_role_switch_set_role(sw, USB_ROLE_DEVICE);
	}
	return 0;
}

static struct timer_list roleshim_timer;
static unsigned long roleshim_tries;
static int roleshim_done;
module_param(roleshim_done, int, 0444);
MODULE_PARM_DESC(roleshim_done, "1 = role switch registered");
/* MINDONE-ROLESHIM-GUARD: 1 = roleshim_try() saw -EEXIST and stopped instead
 * of retrying -- readable check that the shim gave up cleanly rather than
 * spinning; distinct from roleshim_done because in that case NO switch was
 * adopted by us (someone else's registration lost the name race). */
static int roleshim_conflict;
module_param(roleshim_conflict, int, 0444);
MODULE_PARM_DESC(roleshim_conflict, "1 = a role switch with our name already existed, gave up");

static int roleshim_try(void)
{
	struct usb_role_switch_desc d = { 0 };
	struct usb_role_switch *existing;

	/* bus_find_device_by_name is NOT exported on 6.1 -- search via
	 * bus_find_device with our own name comparison. */
	if (roleshim_done || roleshim_conflict)
		return 0;

	/* MINDONE: resolve helper addresses by name if not given as parameters */
	if (!connect_addr)
		connect_addr = roleshim_resolve("mt_usb_connect");
	if (!reconnect_addr)
		reconnect_addr = roleshim_resolve("mt_usb_reconnect");

	mtusb = bus_find_device(&platform_bus_type, NULL, "mt_usb", match_by_name);
	if (!mtusb) {
		if (roleshim_tries <= 3)
			pr_notice("MINDONE-ROLESHIM: mt_usb device not present yet (try %lu)\n",
				  roleshim_tries);
		return -ENODEV;
	}

	/* MINDONE-ROLESHIM-GUARD (F2959, F3015): if `mt_usb` already carries a
	 * role switch -- musb_hdrc registering its own, native one -- adopt it
	 * instead of racing usb_role_switch_register() into a guaranteed
	 * -EEXIST. This is the common case whenever a build ships musb_hdrc
	 * with its role-switch registration intact; roleshim only needs to do
	 * anything when that registration was compiled out (see file header). */
	existing = usb_role_switch_get(mtusb);
	if (!IS_ERR_OR_NULL(existing)) {
		sw = existing;
		sw_is_ours = false;
		roleshim_done = 1;
		pr_notice("MINDONE-ROLESHIM: %s already has a native role switch, adopting it (try %lu)\n",
			  dev_name(mtusb), roleshim_tries);
		return 0;
	}

	d.set = shim_set;
	d.get = shim_get;
	d.allow_userspace_control = true;
	d.fwnode = dev_fwnode(mtusb);

	sw = usb_role_switch_register(mtusb, &d);
	if (IS_ERR(sw)) {
		long err = PTR_ERR(sw);

		if (err == -EEXIST) {
			/* MINDONE-ROLESHIM-GUARD: lost a name race against something
			 * else that registered between our get() above and this
			 * register() call. Retrying will not change the outcome --
			 * the name is taken -- so stop now instead of hammering
			 * kobject_add_internal() (and its WARN + full stack dump)
			 * every 500ms for up to 240 tries (F2959, F3015, F3713). */
			roleshim_conflict = 1;
			pr_err("roleshim: a role switch named %s-role-switch already exists, giving up (not retrying -EEXIST)\n",
			       dev_name(mtusb));
			put_device(mtusb);
			return 0;
		}
		pr_err("roleshim: registration failed: %ld\n", err);
		put_device(mtusb);
		return err;
	}
	sw_is_ours = true;
	roleshim_done = 1;
	pr_notice("MINDONE-ROLESHIM: role switch registered on %s (connect=%lx reconnect=%lx, try %lu)\n",
		  dev_name(mtusb), connect_addr, reconnect_addr, roleshim_tries);
	return 0;
}

/* MINDONE-ROLESHIM-BACKOFF: while genuinely waiting for `mt_usb` to appear
 * (-ENODEV -- a normal, expected wait, not an error condition) keep the
 * original fast 500ms cadence for the first few seconds since the device
 * usually shows up quickly, then back off to reduce timer/softirq churn and
 * log volume for the (now rare, since -EEXIST no longer loops) remainder of
 * the retry window. Total window is longer than the old fixed-cadence one
 * (over 4 minutes vs ~2) despite firing far fewer times. */
static unsigned long roleshim_delay_ms(unsigned long tries)
{
	if (tries < 20)		/* first ~10s: fast, mt_usb usually appears here */
		return 500;
	if (tries < 40)		/* next ~40s: slow down */
		return 2000;
	return 5000;		/* long tail: rare, cheap to keep waiting */
}

static void roleshim_retry(struct timer_list *t)
{
	roleshim_tries++;
	if (roleshim_try() == 0)
		return;
	if (roleshim_tries < 60)
		mod_timer(&roleshim_timer, jiffies + msecs_to_jiffies(roleshim_delay_ms(roleshim_tries)));
	else
		pr_notice("MINDONE-ROLESHIM: giving up after %lu tries\n", roleshim_tries);
}

static int __init roleshim_init(void)
{
	roleshim_tries = 1;
	if (roleshim_try() != 0) {
		/* MINDONE: ALWAYS return success - a failure here would abort loading of the
		 * whole remaining module list in the first stage (F330). Retry by timer. */
		timer_setup(&roleshim_timer, roleshim_retry, 0);
		mod_timer(&roleshim_timer, jiffies + msecs_to_jiffies(500));
	}
	return 0;
}

static void __exit roleshim_exit(void)
{
	del_timer_sync(&roleshim_timer);
	if (!IS_ERR_OR_NULL(sw)) {
		if (sw_is_ours)
			usb_role_switch_unregister(sw);
		else
			usb_role_switch_put(sw);
	}
	if (mtusb)
		put_device(mtusb);
	pr_notice("roleshim: removed, %lu role changes total\n", sets);
}
module_init(roleshim_init);
module_exit(roleshim_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Register USB role switch for mt_usb on kernel 6");
