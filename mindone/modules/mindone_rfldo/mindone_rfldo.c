// SPDX-License-Identifier: GPL-2.0
/* MINDONE (F3190): live experiment -- force-enable the modem RF-LDOs (vrf12/vrf18/vfe28),
 * which are disabled/uses=0 on K6, and see whether MD survives past L1 on the next restart. */
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static char *names = "vrf12,vrf18,vfe28";
module_param(names, charp, 0444);
static struct regulator *regs[8];
static int nregs;

static int __init rfldo_init(void)
{
	char *buf, *p, *tok; int ret;
	buf = kstrdup(names, GFP_KERNEL); if (!buf) return -ENOMEM; p = buf;
	while ((tok = strsep(&p, ",")) && nregs < 8) {
		struct regulator *r = regulator_get_optional(NULL, tok);
		if (IS_ERR(r)) { pr_notice("MINDONE-RFLDO: %s: get failed %ld\n", tok, PTR_ERR(r)); continue; }
		ret = regulator_enable(r);
		pr_notice("MINDONE-RFLDO: %s: enable ret=%d, now %s, %d uV\n", tok, ret,
			  regulator_is_enabled(r) ? "ON" : "off", regulator_get_voltage(r));
		if (ret) { regulator_put(r); continue; }
		regs[nregs++] = r;
	}
	kfree(buf);
	return nregs ? 0 : -ENODEV;
}
static void __exit rfldo_exit(void)
{
	int i; for (i = 0; i < nregs; i++) { regulator_disable(regs[i]); regulator_put(regs[i]); }
	pr_notice("MINDONE-RFLDO: released %d\n", nregs);
}
module_init(rfldo_init); module_exit(rfldo_exit);
MODULE_LICENSE("GPL");
