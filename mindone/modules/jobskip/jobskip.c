// SPDX-License-Identifier: GPL-2.0
/*
 * jobskip.ko -- prevent the GPU from EXECUTING jobs, leaving everything else as is.
 *
 * WHY. F2029 proved: the kernel driver by itself is harmless -- with a swapped-in
 * factory GL library the device survives 800+ seconds. It's userspace work through
 * the driver that kills it. What's left is to split two cases: does the device die
 * from the access PATH ITSELF (memory allocation, mapping, configuration), or
 * specifically from the GPU EXECUTING jobs.
 *
 * Same technique already proven on USB and charging: hook the entry point by NAME,
 * fake the return (x0 = 0, jump via the return-address register). `kbase_api_job_submit`
 * returns int, where zero means success (declaration checked BEFORE hooking, F1913), so
 * faking it is harmless to the caller: it believes the job was accepted.
 *
 * WHAT THIS DOES NOT PROVE. The compositor will wait for jobs that never happened and
 * hang. That is EXPECTED and says nothing about the cause. The only signal that matters:
 * does the device stay alive.
 *
 * Liveness check: `placed` must become 1, and `hits` must grow. Zero hits means the
 * hook never landed, and silence means nothing.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

static int armed = 1;
module_param(armed, int, 0644);
MODULE_PARM_DESC(armed, "1 -- skip the call, 0 -- count only");

/* The target is a parameter so the module doesn't need rebuilding for every check.
 * Only works for functions returning void or int, where zero is harmless.
 * Check the target's declaration BEFORE hooking it (F1913). */
static char *target = "kbase_jd_submit";
module_param(target, charp, 0444);
MODULE_PARM_DESC(target, "name of the function to hook");

static unsigned long hits;
module_param(hits, ulong, 0444);
static unsigned long skips;
module_param(skips, ulong, 0444);
static int placed;
module_param(placed, int, 0444);

static int pre(struct kprobe *p, struct pt_regs *regs)
{
	hits++;
	if (!armed)
		return 0;
	skips++;
	if (skips <= 3)
		printk(KERN_ERR "MINDONE-JOBSKIP: skipped submit %lu, caller %pS\n",
		       skips, (void *)regs->regs[30]);
	regs->regs[0] = 0;			/* 0 = success */
	instruction_pointer_set(regs, regs->regs[30]);
	return 1;
}

static struct kprobe kp = {
	.pre_handler = pre,
};

static int __init js_init(void)
{
	int ret;

	kp.symbol_name = target;
	ret = register_kprobe(&kp);
	if (ret) {
		printk(KERN_ERR "MINDONE-JOBSKIP: register_kprobe(\"%s\") returned %d\n", target, ret);
		return ret;
	}
	placed = 1;
	printk(KERN_ERR "MINDONE-JOBSKIP: probe placed on %s\n", target);
	return 0;
}

static void __exit js_exit(void)
{
	unregister_kprobe(&kp);
	printk(KERN_ERR "MINDONE-JOBSKIP: removed, hits=%lu skips=%lu\n", hits, skips);
}

module_init(js_init);
module_exit(js_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Skip GPU job submission (hypothesis test)");
