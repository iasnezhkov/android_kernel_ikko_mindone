// SPDX-License-Identifier: GPL-2.0
/*
 * MINDONE-DPTMO (F2134/F2111/F2106): probe of `13000000.mali` is deferred 21x
 * and only succeeds when driver_deferred_probe_timeout (default 10s, reset on
 * every successful bind) finally expires at ~24s -- not because its gpufreq
 * supplier ever appears; meanwhile the compositor fails 3x with "no suitable
 * EGLConfig found". Shorten the exported timeout early instead of fixing the
 * missing supplier. See HACKS for the open task and risk analysis.
 */
#include <linux/module.h>
#include <linux/kernel.h>

extern int driver_deferred_probe_timeout;

static int timeout = 2;
module_param(timeout, int, 0644);
MODULE_PARM_DESC(timeout, "new value for driver_deferred_probe_timeout, seconds");

static int was;
module_param(was, int, 0444);
MODULE_PARM_DESC(was, "value observed before the change");

static int __init dptmo_init(void)
{
	was = driver_deferred_probe_timeout;
	if (timeout > 0)
		driver_deferred_probe_timeout = timeout;
	pr_notice("MINDONE-DPTMO: driver_deferred_probe_timeout %d -> %d\n",
		  was, driver_deferred_probe_timeout);
	return 0;
}

static void __exit dptmo_exit(void)
{
	pr_notice("MINDONE-DPTMO: unloaded (value stays %d)\n",
		  driver_deferred_probe_timeout);
}

module_init(dptmo_init);
module_exit(dptmo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shorten the global deferred-probe timeout");
