// SPDX-License-Identifier: GPL-2.0
/* MINDONE-USBLOCK-SHIM (F3339): workaround, see the fact log F3336/F3337 and
 * the hacks registry. On cable unplug, musb's own release path for the "USB
 * suspend lock" wakeup source is unreachable (F3211/F3303/F3333), so the lock
 * stays held forever and suspend never triggers. The proper fix needs musb
 * reconstructed first (MINDONE-USBLOCK, F3337). Until then,
 * this shim polls every check_ms and releases the wakeup source when no
 * cable is present; musb re-takes it normally once the cable returns. */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/string.h>

/* 30 s, not 10 s (mind_one, 15.09).
 *
 * The stale "USB suspend lock" this shim releases is a musb bug on cable removal, and what it
 * costs while stuck is suspend being blocked - so the shim must run, but it does not have to run
 * often. Every ten seconds it walks the global wakeup_sources list and calls
 * power_supply_get_by_name(), three times more wake-ups than the problem needs: leaving the
 * stale lock in place another twenty seconds changes nothing the user can perceive, while the
 * polling itself is exactly the kind of self-inflicted wake this project already had to hunt
 * down once (F4446, the connsys storm).
 *
 * Still tunable at runtime for diagnosis:
 *     echo 10000 > /sys/module/mindone_usblock_shim/parameters/check_ms
 */
static unsigned int check_ms = 30000;
module_param(check_ms, uint, 0644);
MODULE_PARM_DESC(check_ms, "poll period, ms");

static unsigned long relax_count;
module_param(relax_count, ulong, 0444);
MODULE_PARM_DESC(relax_count, "times the stale lock was released");

static char lock_name[64] = "USB suspend lock";
module_param_string(lock_name, lock_name, sizeof(lock_name), 0644);
MODULE_PARM_DESC(lock_name, "wakeup source name to police");

static char psy_name[32] = "usb";
module_param_string(psy_name, psy_name, sizeof(psy_name), 0644);
MODULE_PARM_DESC(psy_name, "power supply whose ONLINE means cable present");

static struct delayed_work shim_work;

static bool cable_present(void)
{
	struct power_supply *psy;
	union power_supply_propval val;
	bool present = true; /* fail safe: assume present -> touch nothing */

	psy = power_supply_get_by_name(psy_name);
	if (!psy)
		return true;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val))
		present = !!val.intval;
	power_supply_put(psy);
	return present;
}

static void shim_check(struct work_struct *w)
{
	struct wakeup_source *ws;
	int idx;

	if (!cable_present()) {
		idx = wakeup_sources_read_lock();
		for (ws = wakeup_sources_walk_start(); ws;
		     ws = wakeup_sources_walk_next(ws)) {
			if (ws->name && !strcmp(ws->name, lock_name)) {
				if (ws->active) {
					__pm_relax(ws);
					relax_count++;
					pr_notice("MINDONE-USBLOCK-SHIM: relaxed stale \"%s\" (n=%lu)\n",
						  lock_name, relax_count);
				}
				break;
			}
		}
		wakeup_sources_read_unlock(idx);
	}
	schedule_delayed_work(&shim_work, msecs_to_jiffies(check_ms));
}

static int __init shim_init(void)
{
	INIT_DELAYED_WORK(&shim_work, shim_check);
	schedule_delayed_work(&shim_work, msecs_to_jiffies(check_ms));
	pr_info("MINDONE-USBLOCK-SHIM: armed, period %u ms, lock \"%s\", psy \"%s\"\n",
		check_ms, lock_name, psy_name);
	return 0;
}

static void __exit shim_exit(void)
{
	cancel_delayed_work_sync(&shim_work);
	pr_info("MINDONE-USBLOCK-SHIM: disarmed (relaxed %lu times)\n", relax_count);
}

module_init(shim_init);
module_exit(shim_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mind_one: release stale musb USB suspend lock when no cable is present");
