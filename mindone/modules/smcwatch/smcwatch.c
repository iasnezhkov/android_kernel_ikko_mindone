// SPDX-License-Identifier: GPL-2.0
/*
 * smcwatch.ko -- catch a TRIP INTO SECURE WORLD WITHOUT RETURN.
 *
 * WHY. Of three possibilities under F1836/F1945, two were closed by
 * measurement, and the third -- a trip to EL3 without return -- was NEVER
 * TESTED for the whole track. Its signature matches what's observed exactly:
 * the CPU stops executing instructions at our level, all four software
 * watchdogs stay silent (F845), and the hardware watchdog eventually resets
 * the chip.
 *
 * WHY THIS IS PLAUSIBLE SPECIFICALLY FOR THE GPU. The device loads the whole
 * secure-world stack (`gz_trusty_mod`, `gz_tz_system`, `gz_ipc_mod`,
 * `trusted_mem`, `mtk_sec_heap`, `iommu_gz`), and the GPU has a companion
 * `mali_prot_alloc_mt6789` -- protected memory that works THROUGH secure
 * world. The stock frequency driver's own comment also states the MFG0 domain
 * is powered off IN ATF.
 *
 * MAIN RISK -- LOG FLOODING. PSCI goes through `__arm_smccc_smc`: every CPU
 * idle entry is a call. Printing all of them would flood the log and the ring
 * buffer would overwrite the last lines the experiment is for (same trap as
 * with musb). So PSCI is filtered BY SERVICE NUMBER and only counted.
 *
 * The service number is in x0. Bits 29:24 are the call "owner" (SMC Calling
 * Convention): 0x4 standard service (PSCI) -> filtered, counted; 0x2 SiP
 * service -> PRINTED (MediaTek calls land here); 0x3 OEM service -> PRINTED;
 * 0x32/0x33 and others -> PRINTED.
 *
 * SIGNAL DECLARED BEFORE MEASURING: last log line is `SMC-ENTER` with no
 * paired `SMC-LEAVE` => trip to EL3 WITHOUT RETURN, confirmed, with the
 * service number found; every `SMC-ENTER` paired => secure world returns
 * control, hypothesis REJECTED; no calls at all => the GPU's secure-world
 * path is not exercised.
 *
 * Liveness check: the `psci` counter must grow on its own (the CPU idles
 * constantly). If it's zero, the hook never landed, and silence elsewhere
 * means nothing.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

static unsigned long psci;		/* filtered-out PSCI calls -- this is the liveness check */
module_param(psci, ulong, 0444);
MODULE_PARM_DESC(psci, "number of filtered PSCI calls (check: must grow)");

static unsigned long shown;		/* printed entries */
module_param(shown, ulong, 0444);
static unsigned long left;		/* printed returns */
module_param(left, ulong, 0444);

static int limit = 400;			/* print cap, to avoid flooding the log */
module_param(limit, int, 0644);

/* call owner = bits 29:24 of the service number */
#define SMC_OWNER(id)	(((id) >> 24) & 0x3f)
#define OWNER_STD	0x4		/* PSCI and other standard service */

static int on_enter(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long id = regs->regs[0];

	if (SMC_OWNER(id) == OWNER_STD) {
		psci++;
		return 0;
	}
	shown++;
	if (shown <= limit)
		printk(KERN_ERR "MINDONE-SMC-ENTER id=0x%08llx a1=0x%llx a2=0x%llx a3=0x%llx from=%pS\n",
		       (unsigned long long)id,
		       (unsigned long long)regs->regs[1],
		       (unsigned long long)regs->regs[2],
		       (unsigned long long)regs->regs[3],
		       (void *)regs->regs[30]);
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "__arm_smccc_smc",
	.pre_handler = on_enter,
};

static int ret_enter(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long id = regs->regs[0];

	/* remember the service number so the return can be matched to its entry */
	*(unsigned long *)ri->data = id;
	return SMC_OWNER(id) == OWNER_STD ? 1 : 0;	/* 1 = do not track the return */
}

static int ret_leave(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long id = *(unsigned long *)ri->data;

	left++;
	if (left <= limit)
		printk(KERN_ERR "MINDONE-SMC-LEAVE id=0x%08llx\n", (unsigned long long)id);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name = "__arm_smccc_smc",
	.entry_handler = ret_enter,
	.handler = ret_leave,
	.data_size = sizeof(unsigned long),
	.maxactive = 64,
};

static int __init smcw_init(void)
{
	int r1, r2;

	r1 = register_kprobe(&kp);
	r2 = register_kretprobe(&krp);
	printk(KERN_ERR "MINDONE-SMC: entry %s, return %s\n",
	       r1 ? "NOT PLACED" : "placed",
	       r2 ? "NOT PLACED" : "placed");
	if (r1 && r2)
		return -ENODEV;
	return 0;
}

static void __exit smcw_exit(void)
{
	if (!IS_ERR(kp.addr))
		unregister_kprobe(&kp);
	unregister_kretprobe(&krp);
	printk(KERN_ERR "MINDONE-SMC: removed. PSCI=%lu entry=%lu return=%lu\n", psci, shown, left);
}

module_init(smcw_init);
module_exit(smcw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Watch secure-world (SMC) calls, filtering out PSCI");
