// SPDX-License-Identifier: GPL-2.0
/*
 * mindone_pmic_guard -- post-suspend verify/heal/evidence for the MT6366 PMIC
 * interrupt routing (F3381/F3382, battery/suspend track). s2idle thrash kills
 * the PSC sub-IRQ group (PWRKEY/CHRDET); the CON enable layer is verified
 * clean, so this guards the mask/misc layers nothing else rewrites, restoring
 * them every PM_POST_SUSPEND and logging deviations as evidence. Full design
 * notes: MINDONE-MODULES-NOTES-0901.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>

#define GUARD_TAG "MINDONE-PMIC-GUARD"

/* MT6366 uses the MT6358 register map (mt6366/core.h TOP_GEN → MT6358_*). */
#define PMIC_TOPSTATUS			0x28
#define PMIC_TOP_RST_MISC		0x14c
#define PMIC_TOP_INT_MASK_CON0		0x198
#define PMIC_TOP_INT_STATUS0		0x19e
#define PMIC_TOP_INT_RAW_STATUS0	0x1a0
#define PMIC_TOP_INT_CON0		0x1a2

#define PMIC_PSC_TOP_INT_CON0		0x910
#define PMIC_PSC_TOP_INT_CON0_SET	0x912
#define PMIC_PSC_TOP_INT_MASK_CON0	0x916
#define PMIC_PSC_TOP_INT_MASK_SET	0x918
#define PMIC_PSC_TOP_INT_MASK_CLR	0x91a
#define PMIC_PSC_TOP_INT_STATUS0	0x91c
#define PMIC_PSC_TOP_INT_RAW_STATUS0	0x91e
#define PMIC_PSC_TOP_INT_MISC_CON	0x920

/* Sub-IRQ bits inside the PSC group (hwirq - 48). */
#define PSC_BIT_PWRKEY			BIT(0)
#define PSC_BIT_KEYS_ALL		(BIT(0) | BIT(1) | BIT(2) | BIT(3))

enum guard_class {
	CL_RESTORE,	/* verify + write snapshot back, always log */
	CL_RESTORE_OR,	/* verify; re-set missing snapshot bits via SET reg */
	CL_MONITOR,	/* log-only with dedup, track last-seen value */
};

struct guard_reg {
	u16 addr;
	u16 set_reg;	/* for CL_RESTORE_OR */
	u8 class;
	u8 reports;	/* dedup counter for CL_MONITOR */
	u16 snap;	/* boot-time known-good (RESTORE*) / last seen (MONITOR) */
	u32 changes;
};

/* Per-group interrupt registers, MT6358/MT6366 map.
 * mask_con(j) = con0 + 6*num_int_regs + 6*j (verified against headers:
 * PSC 0x910+6=0x916, LDO 0x1a50+12=0x1a5c/0x1a62, BM 0xc32+12=0xc3e/0xc44).
 */
static const struct { u16 con0; u8 nregs; const char *name; } guard_groups[] = {
	{ 0x1318, 1, "BUCK" },
	{ 0x1a50, 2, "LDO"  },
	{ 0x0910, 1, "PSC"  },
	{ 0x052e, 1, "SCK"  },
	{ 0x0c32, 2, "BM"   },
	{ 0x0f92, 1, "HK"   },
	{ 0x2228, 1, "AUD"  },
	{ 0x0188, 1, "MISC" },
};

#define GUARD_MAX_REGS 128

static struct {
	struct regmap *regmap;
	struct device *pwrap_dev;
	struct mutex lock;
	struct guard_reg regs[GUARD_MAX_REGS];
	int nregs;
	u32 cycles;
	u32 heals;
	u32 monitor_hits;
	u32 raw_hidden_hits;
	u8 raw_hidden_reports;
	bool armed;
	struct dentry *dbg;
} g;

static char *guard_dev = "10026000.pwrap";
module_param(guard_dev, charp, 0444);
MODULE_PARM_DESC(guard_dev, "platform device owning the PMIC regmap");

static void guard_add(u16 addr, u8 class, u16 set_reg)
{
	if (g.nregs >= GUARD_MAX_REGS)
		return;
	g.regs[g.nregs].addr = addr;
	g.regs[g.nregs].class = class;
	g.regs[g.nregs].set_reg = set_reg;
	g.nregs++;
}

static int guard_snapshot(void)
{
	unsigned int val;
	int i, ret;

	for (i = 0; i < g.nregs; i++) {
		ret = regmap_read(g.regmap, g.regs[i].addr, &val);
		if (ret) {
			pr_err(GUARD_TAG ": snapshot read 0x%03x failed (%d)\n",
			       g.regs[i].addr, ret);
			return ret;
		}
		g.regs[i].snap = val;
	}
	return 0;
}

/* One verify/heal pass. Caller holds g.lock. Returns number of deviations. */
static int guard_pass(const char *why)
{
	unsigned int val, sta, raw;
	int i, ret, devs = 0;

	for (i = 0; i < g.nregs; i++) {
		struct guard_reg *r = &g.regs[i];

		ret = regmap_read(g.regmap, r->addr, &val);
		if (ret) {
			pr_err(GUARD_TAG ": read 0x%03x failed (%d)\n",
			       r->addr, ret);
			continue;
		}
		if ((u16)val == r->snap)
			continue;

		devs++;
		r->changes++;

		switch (r->class) {
		case CL_RESTORE:
			pr_err(GUARD_TAG ": [%s] reg 0x%03x DIED 0x%04x -> 0x%04x, restoring\n",
			       why, r->addr, r->snap, (u16)val);
			ret = regmap_write(g.regmap, r->addr, r->snap);
			if (!ret)
				ret = regmap_read(g.regmap, r->addr, &val);
			if (ret || (u16)val != r->snap)
				pr_err(GUARD_TAG ": restore 0x%03x FAILED (ret=%d, now 0x%04x)\n",
				       r->addr, ret, (u16)val);
			else
				g.heals++;
			break;
		case CL_RESTORE_OR:
			pr_err(GUARD_TAG ": [%s] reg 0x%03x lost bits 0x%04x (0x%04x -> 0x%04x), re-setting\n",
			       why, r->addr, r->snap & ~(u16)val, r->snap,
			       (u16)val);
			ret = regmap_write(g.regmap, r->set_reg,
					   r->snap & ~(u16)val);
			if (ret)
				pr_err(GUARD_TAG ": SET 0x%03x FAILED (%d)\n",
				       r->set_reg, ret);
			else
				g.heals++;
			break;
		case CL_MONITOR:
			g.monitor_hits++;
			if (r->reports < 4) {
				r->reports++;
				pr_err(GUARD_TAG ": [%s] monitor 0x%03x changed 0x%04x -> 0x%04x (hit %u)\n",
				       why, r->addr, r->snap, (u16)val,
				       r->changes);
			}
			r->snap = val;	/* track transitions */
			break;
		}
	}

	/* Latched-but-hidden detector: a key press latches in RAW_STATUS even
	 * if a mask layer above hides it from STATUS/dispatch. RAW bits with
	 * STATUS clear = direct proof of a masked path (or a dead dispatch).
	 */
	if (!regmap_read(g.regmap, PMIC_PSC_TOP_INT_STATUS0, &sta) &&
	    !regmap_read(g.regmap, PMIC_PSC_TOP_INT_RAW_STATUS0, &raw)) {
		if ((raw & PSC_BIT_KEYS_ALL) & ~sta) {
			g.raw_hidden_hits++;
			if (g.raw_hidden_reports < 8) {
				g.raw_hidden_reports++;
				pr_err(GUARD_TAG ": [%s] PSC RAW=0x%04x latched but STATUS=0x%04x — key event hidden by a mask layer\n",
				       why, raw, sta);
			}
		}
	}

	return devs;
}

static int guard_pm_event(struct notifier_block *nb, unsigned long event,
			  void *unused)
{
	int devs;

	/* Heal on both edges: POST_SUSPEND repairs a kill that happened around
	 * resume; SUSPEND_PREPARE guarantees clean routing on every entry, so
	 * even a kill during a calm sleep (button cannot wake the system, so
	 * no resume ever comes) is repaired before the next sleep begins. */
	if ((event != PM_POST_SUSPEND && event != PM_SUSPEND_PREPARE) ||
	    !g.armed)
		return NOTIFY_DONE;

	mutex_lock(&g.lock);
	if (event == PM_POST_SUSPEND)
		g.cycles++;
	devs = guard_pass(event == PM_POST_SUSPEND ? "post-suspend"
						   : "pre-suspend");
	if (g.cycles <= 3 || (g.cycles & 511) == 0)
		pr_info(GUARD_TAG ": cycle %u devs=%d heals=%u monitor=%u raw-hidden=%u\n",
			g.cycles, devs, g.heals, g.monitor_hits,
			g.raw_hidden_hits);
	mutex_unlock(&g.lock);

	return NOTIFY_DONE;
}

static struct notifier_block guard_nb = {
	.notifier_call = guard_pm_event,
};

/* ---- debugfs ---- */

static int guard_status_show(struct seq_file *s, void *unused)
{
	static const char * const cls[] = { "RESTORE", "RESTORE|", "monitor" };
	unsigned int val;
	int i;

	mutex_lock(&g.lock);
	seq_printf(s, "cycles=%u heals=%u monitor=%u raw_hidden=%u nregs=%d\n",
		   g.cycles, g.heals, g.monitor_hits, g.raw_hidden_hits,
		   g.nregs);
	for (i = 0; i < g.nregs; i++) {
		struct guard_reg *r = &g.regs[i];

		if (regmap_read(g.regmap, r->addr, &val))
			val = 0xdead;
		if (r->class != CL_MONITOR || (u16)val != r->snap ||
		    r->changes)
			seq_printf(s, "0x%03x %s snap=0x%04x cur=0x%04x changes=%u\n",
				   r->addr, cls[r->class], r->snap, (u16)val,
				   r->changes);
	}
	mutex_unlock(&g.lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(guard_status);

static ssize_t guard_heal_write(struct file *f, const char __user *buf,
				size_t len, loff_t *off)
{
	int devs;

	mutex_lock(&g.lock);
	devs = guard_pass("manual");
	pr_info(GUARD_TAG ": manual pass devs=%d heals=%u\n", devs, g.heals);
	mutex_unlock(&g.lock);
	return len;
}

static const struct file_operations guard_heal_fops = {
	.owner = THIS_MODULE,
	.write = guard_heal_write,
};

/* echo 1 — mask the PWRKEY sub-IRQ inside the PSC group (simulate the dead
 * state under test); echo 0 — unmask. Test aid only. */
static ssize_t guard_sim_write(struct file *f, const char __user *buf,
			       size_t len, loff_t *off)
{
	char c = 0;

	if (copy_from_user(&c, buf, 1))
		return -EFAULT;
	mutex_lock(&g.lock);
	if (c == '1') {
		regmap_write(g.regmap, PMIC_PSC_TOP_INT_MASK_SET,
			     PSC_BIT_PWRKEY);
		pr_err(GUARD_TAG ": SIMULATE pwrkey masked (0x916 |= 1)\n");
	} else {
		regmap_write(g.regmap, PMIC_PSC_TOP_INT_MASK_CLR,
			     PSC_BIT_PWRKEY);
		pr_err(GUARD_TAG ": SIMULATE cleared\n");
	}
	mutex_unlock(&g.lock);
	return len;
}

static const struct file_operations guard_sim_fops = {
	.owner = THIS_MODULE,
	.write = guard_sim_write,
};

/* ---- init ---- */

static void guard_build_table(void)
{
	int i, j;
	u16 a;

	/* Restore class: pure interrupt routing / static key-reset config. */
	guard_add(PMIC_TOP_RST_MISC, CL_RESTORE, 0);
	guard_add(PMIC_TOP_INT_MASK_CON0, CL_RESTORE, 0);
	guard_add(PMIC_TOP_INT_CON0, CL_RESTORE, 0);
	guard_add(PMIC_PSC_TOP_INT_MISC_CON, CL_RESTORE, 0);
	/* PSC enable: OR-restore via the SET register so a legitimate runtime
	 * change of other bits is never clobbered. */
	guard_add(PMIC_PSC_TOP_INT_CON0, CL_RESTORE_OR,
		  PMIC_PSC_TOP_INT_CON0_SET);

	/* Every group's mask register(s): nothing in the kernel writes these,
	 * boot leaves them 0 — restore unconditionally. */
	for (i = 0; i < ARRAY_SIZE(guard_groups); i++)
		for (j = 0; j < guard_groups[i].nregs; j++)
			guard_add(guard_groups[i].con0 +
				  6 * (guard_groups[i].nregs + j),
				  CL_RESTORE, 0);

	/* Monitor class: the other groups' CON registers (dynamic owners such
	 * as the gauge legitimately flip BM/HK bits — log-only), TOPSTATUS,
	 * and the TOP clock/reset window 0x100..0x160 where a PMIC low-power
	 * excursion would show up. */
	for (i = 0; i < ARRAY_SIZE(guard_groups); i++) {
		if (guard_groups[i].con0 == PMIC_PSC_TOP_INT_CON0)
			continue;
		for (j = 0; j < guard_groups[i].nregs; j++)
			guard_add(guard_groups[i].con0 + 6 * j, CL_MONITOR, 0);
	}
	guard_add(PMIC_TOPSTATUS, CL_MONITOR, 0);
	for (a = 0x100; a <= 0x160; a += 2) {
		if (a == PMIC_TOP_RST_MISC)
			continue;
		guard_add(a, CL_MONITOR, 0);
	}
}

static int __init guard_init(void)
{
	struct device *dev;
	unsigned int val;
	int ret;

	dev = bus_find_device_by_name(&platform_bus_type, NULL, guard_dev);
	if (!dev) {
		pr_err(GUARD_TAG ": device %s not found\n", guard_dev);
		return -ENODEV;
	}
	g.pwrap_dev = dev;

	g.regmap = dev_get_regmap(dev, NULL);
	if (!g.regmap) {
		pr_err(GUARD_TAG ": no regmap on %s\n", guard_dev);
		put_device(dev);
		return -ENODEV;
	}

	/* Sanity: the PSC enable register must show the 4 key IRQs the keys
	 * driver enabled — guards against snapshotting a wrong/broken state. */
	ret = regmap_read(g.regmap, PMIC_PSC_TOP_INT_CON0, &val);
	if (ret) {
		put_device(dev);
		return ret;
	}
	if ((val & PSC_BIT_KEYS_ALL) != PSC_BIT_KEYS_ALL)
		pr_err(GUARD_TAG ": warning: PSC CON0=0x%04x, key bits not all set — snapshotting anyway\n",
		       val);

	mutex_init(&g.lock);
	guard_build_table();
	ret = guard_snapshot();
	if (ret) {
		put_device(dev);
		return ret;
	}

	g.dbg = debugfs_create_dir("mindone_pmic_guard", NULL);
	debugfs_create_file("status", 0444, g.dbg, NULL, &guard_status_fops);
	debugfs_create_file("heal", 0200, g.dbg, NULL, &guard_heal_fops);
	debugfs_create_file("simulate", 0200, g.dbg, NULL, &guard_sim_fops);

	g.armed = true;
	register_pm_notifier(&guard_nb);

	pr_info(GUARD_TAG ": armed on %s, %d regs (PSC CON0=0x%04x MASK=snap TOP_MASK=snap), heal-on-resume active\n",
		guard_dev, g.nregs, val);
	return 0;
}

static void __exit guard_exit(void)
{
	unregister_pm_notifier(&guard_nb);
	g.armed = false;
	debugfs_remove_recursive(g.dbg);
	put_device(g.pwrap_dev);
	pr_info(GUARD_TAG ": disarmed\n");
}

module_init(guard_init);
module_exit(guard_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mind_one: MT6366 PMIC interrupt-routing guard (F3381/F3382)");
MODULE_AUTHOR("mind_one kernel 6.1 bring-up");
