// SPDX-License-Identifier: GPL-2.0
/*
 * ktrace.ko -- call tracing via kprobes, WITHOUT rebuilding the kernel.
 *
 * WHY. Kernel 6 dies ~0.9s after the GPU binds, and ALL FOUR software watchdogs stay
 * silent (F845) -- the CPU stops executing instructions. The only thing visible is
 * whatever was logged BEFOREHAND. The kernel's own tracer is built for exactly this, but
 * it is NOT ENABLED in our image (F1890).
 *
 * WHY NOT REBUILD THE KERNEL WITH TRACING. `CONFIG_FUNCTION_TRACER` adds fields to
 * `struct module`, which changes the `module_layout` checksum. All 174 vendor modules are
 * binary-only, there's nothing to rebuild them with, and with a changed checksum they get
 * REJECTED -- including the very GPU driver this whole experiment is about. So a rebuild
 * would invalidate the experiment. kprobes have none of that problem: the ABI is
 * unchanged, the module set stays valid, no reflash needed at all.
 *
 * WHAT IT DOES. Places a kprobe on each function in the `funcs` list (comma-separated
 * names) and prints the name and return address on every entry. Printed at level 3,
 * because at `printk=4` normal messages never reach the log (verified in this project).
 *
 * KEEP THE FILTER NARROW. A wide list floods the log, and the last entries -- the ones
 * the experiment is for -- get lost. Only trace the GPU, display, and graphics-domain
 * power.
 *
 * MANDATORY CONTROL. The `control` parameter hooks a function that is definitely called.
 * If it never fires, the log channel is dead, and silence on everything else means
 * nothing.
 *
 * Parameters: funcs=name1,name2,... · control=name · limit=how many to print per function
 * Counters: placed= (how many kprobes landed) · hits= (total firings)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MAXF 96

static char *funcs = "";
module_param(funcs, charp, 0444);
MODULE_PARM_DESC(funcs, "comma-separated function names");

/* RETURN-SIDE HOOKS. Entry into a function only tells you it was called; to learn WHAT
 * IT RETURNED, a return-side hook is needed. For checking bus idle this is decisive: if
 * it reports "idle" while the GPU block is still active, the following clock switch and
 * gating causes a bus stall (variant F1912). */
static char *rets = "";
module_param(rets, charp, 0444);
MODULE_PARM_DESC(rets, "comma-separated functions whose RETURN value to print");

static char *control = "do_sys_openat2";
module_param(control, charp, 0444);
MODULE_PARM_DESC(control, "a function known to be called, to verify the log channel");

static int limit = 8;
module_param(limit, int, 0644);
MODULE_PARM_DESC(limit, "how many firings to print per function");

static int placed;
module_param(placed, int, 0444);
static int failed;
module_param(failed, int, 0444);
static unsigned long hits;
module_param(hits, ulong, 0444);
static unsigned long ctl_hits;
module_param(ctl_hits, ulong, 0444);

struct ent {
	struct kprobe kp;
	char name[64];
	unsigned long n;
	int is_ctl;
};
static struct ent *tab;
static int ntab;

struct rent {
	struct kretprobe rp;
	char name[64];
	unsigned long n;
};
static struct rent *rtab;
static int nrtab;
static int rplaced;
module_param(rplaced, int, 0444);

static int pre(struct kprobe *p, struct pt_regs *regs)
{
	struct ent *e = container_of(p, struct ent, kp);

	e->n++;
	if (e->is_ctl) {
		ctl_hits++;
		return 0;
	}
	hits++;
	if (e->n <= limit)
		printk(KERN_ERR "MINDONE-KT: %s called (%lu), return to %pS\n",
		       e->name, e->n, (void *)regs->regs[30]);
	return 0;
}

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rent *e = container_of(get_kretprobe(ri), struct rent, rp);
	long v = regs_return_value(regs);

	e->n++;
	if (e->n <= limit)
		printk(KERN_ERR "MINDONE-KTR: %s RETURNED %ld (0x%lx), call %lu\n",
		       e->name, v, (unsigned long)v, e->n);
	return 0;
}

static void addret(const char *name)
{
	struct rent *e;

	if (nrtab >= MAXF)
		return;
	e = &rtab[nrtab];
	strscpy(e->name, name, sizeof(e->name));
	e->rp.kp.symbol_name = e->name;
	e->rp.handler = ret_handler;
	e->rp.maxactive = 32;
	if (register_kretprobe(&e->rp) == 0) {
		rplaced++;
		nrtab++;
	} else {
		printk(KERN_ERR "MINDONE-KTR: failed to place return-hook on \"%s\"\n", name);
	}
}

static void add(const char *name, int is_ctl)
{
	struct ent *e;

	if (ntab >= MAXF)
		return;
	e = &tab[ntab];
	strscpy(e->name, name, sizeof(e->name));
	e->is_ctl = is_ctl;
	e->kp.symbol_name = e->name;
	e->kp.pre_handler = pre;
	if (register_kprobe(&e->kp) == 0) {
		placed++;
		ntab++;
	} else {
		failed++;
		printk(KERN_ERR "MINDONE-KT: failed to place hook on \"%s\"\n", name);
	}
}

static int __init kt_init(void)
{
	char *copy, *p, *tok;

	tab = kcalloc(MAXF, sizeof(*tab), GFP_KERNEL);
	if (!tab)
		return -ENOMEM;
	rtab = kcalloc(MAXF, sizeof(*rtab), GFP_KERNEL);
	if (!rtab) {
		kfree(tab);
		return -ENOMEM;
	}

	/* The control is placed FIRST -- so its firings are visible from the very start. */
	if (control && *control)
		add(control, 1);

	copy = kstrdup(funcs, GFP_KERNEL);
	if (copy) {
		p = copy;
		while ((tok = strsep(&p, ",")) != NULL) {
			while (*tok == ' ')
				tok++;
			if (*tok)
				add(tok, 0);
		}
		kfree(copy);
	}
	copy = kstrdup(rets, GFP_KERNEL);
	if (copy) {
		p = copy;
		while ((tok = strsep(&p, ",")) != NULL) {
			while (*tok == ' ')
				tok++;
			if (*tok)
				addret(tok);
		}
		kfree(copy);
	}
	printk(KERN_ERR "MINDONE-KT: entry %d (failed %d), return %d, control \"%s\"\n",
	       placed, failed, rplaced, control);
	return 0;
}

static void __exit kt_exit(void)
{
	int i;

	for (i = 0; i < ntab; i++)
		unregister_kprobe(&tab[i].kp);
	for (i = 0; i < nrtab; i++)
		unregister_kretprobe(&rtab[i].rp);
	kfree(rtab);
	printk(KERN_ERR "MINDONE-KT: removed - hits %lu - control %lu\n", hits, ctl_hits);
	kfree(tab);
}

module_init(kt_init);
module_exit(kt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kprobe-based call tracing without kernel rebuild");
