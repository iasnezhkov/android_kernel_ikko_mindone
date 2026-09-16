// SPDX-License-Identifier: GPL-2.0
/*
 * fix_charger_selfres.ko -- quiets the charger log and stops a false "charging" report in
 * OTG mode. Version for BAKING INTO THE IMAGE: finds its targets itself, no arguments needed.
 *
 * HOW THIS DIFFERS FROM THE PREVIOUS VERSION. That one patched addresses into the binary over
 * magic markers, with the addresses taken by a userspace script (`/proc/kallsyms`). That works
 * for Magisk, but not for baking into the image: stage-1 boot loads modules WITHOUT arguments
 * and without scripts (F639). This uses the standard mechanism instead -- the kprobe machinery
 * resolves the NAME itself (`kprobe.symbol_name`), including symbols from other modules.
 * Verified on a live system on the USB fix (F1790): found the target on the first try.
 *
 * MAIN SAFETY RULE (F330). The stage-1 module loader runs in STRICT mode: the first failure
 * aborts loading of the ENTIRE remaining list. Therefore:
 *   - init ALWAYS returns success, even if not a single kprobe was placed;
 *   - targets that are not yet present are picked up by a timer retry;
 *   - partial success is a normal outcome, not an error.
 *
 * WHAT THIS VERSION CANNOT DO, stated honestly. Two targets share the SAME name
 * (`eta6965_dump_register`) in two different modules -- by name they are indistinguishable,
 * resolution returns the first one. So the second stays unhooked. Whether that matters is
 * shown by MEASURING the log volume, not by reasoning: if the log is quiet enough after boot,
 * the second hook is not needed.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define PSP_ONLINE 4
#define EPC_INTERVAL 60

static int otg_active;
static u64 epc_count;

static unsigned long retry_ms = 500;
module_param(retry_ms, ulong, 0644);
MODULE_PARM_DESC(retry_ms, "kprobe placement retry period, ms");

static unsigned long max_tries = 240;	/* 240 x 500 ms = 2 minutes */
module_param(max_tries, ulong, 0644);

/* WARMUP IS DEFINED IN TIME, NOT IN CALL COUNT. In the image, the hook is placed IN THE
 * MIDDLE of boot, not on an already-running system as during measurement. The initial
 * charger negotiation goes through `external_power_changed`, and muting it at startup
 * would change something we did not intend to fix.
 *
 * The first attempt counted warmup in CALLS (300) -- which turned out not to be what it
 * looked like. MEASUREMENT (F1799) established: the function is called about TWICE a
 * second, not thirty times, so 300 calls is nearly three minutes, not ten seconds. The
 * condition never fired before warmup expired. A time unit is unambiguous and independent
 * of call frequency. */
static unsigned long warmup_sec = 45;
module_param(warmup_sec, ulong, 0644);
MODULE_PARM_DESC(warmup_sec, "seconds after module start to not intervene (0 -- immediately)");

static unsigned long throttle_from;	/* the point in time from which we start muting */

static unsigned long tries;
module_param(tries, ulong, 0444);
static int placed;
module_param(placed, int, 0444);
MODULE_PARM_DESC(placed, "how many kprobes were successfully placed");

/* Skip the target entirely: return 0 to its caller. */
static int skip_function(struct kprobe *p, struct pt_regs *regs)
{
	regs->regs[0] = 0;
	instruction_pointer_set(regs, regs->regs[30]);
	return 1;
}
static int set_otg(struct kprobe *p, struct pt_regs *regs) { otg_active = 1; return 0; }
static int clr_otg(struct kprobe *p, struct pt_regs *regs) { otg_active = 0; return 0; }

/* Do not report "charging" while we ourselves are the power SOURCE in OTG mode. */
static int fix_online(struct kprobe *p, struct pt_regs *regs)
{
	if (otg_active && regs->regs[1] == PSP_ONLINE) {
		*(u32 *)regs->regs[2] = 0;
		regs->regs[0] = 0;
		instruction_pointer_set(regs, regs->regs[30]);
		return 1;
	}
	return 0;
}

/* Skip 59 calls out of 60 to break the feedback loop. Each call drags along HUNDREDS of
 * log lines (2867 lines in 10s at ~2 calls/sec), so we need to mute the call itself, not
 * the printing inside it. */
static int throttle_epc(struct kprobe *p, struct pt_regs *regs)
{
	epc_count++;
	if (time_before(jiffies, throttle_from))
		return 0;			/* warmup: do not intervene */
	if (epc_count % EPC_INTERVAL != 0) {
		regs->regs[0] = 0;
		instruction_pointer_set(regs, regs->regs[30]);
		return 1;
	}
	return 0;
}

struct target {
	const char *name;
	int (*handler)(struct kprobe *, struct pt_regs *);
	struct kprobe kp;
	int done;
};

static struct target targets[] = {
	{ "eta6965_dump_register",             skip_function },
	{ "eta6965_enable_vbus",               set_otg       },
	{ "eta6965_disable_vbus",              clr_otg       },
	{ "eta6965_charger_get_property",      fix_online    },
	{ "mtk_charger_external_power_changed", throttle_epc },
};
#define NTARGETS ARRAY_SIZE(targets)

static struct timer_list retry_timer;

static void place_all(void)
{
	int i;

	tries++;
	for (i = 0; i < NTARGETS; i++) {
		if (targets[i].done)
			continue;
		targets[i].kp.symbol_name = targets[i].name;
		targets[i].kp.pre_handler = targets[i].handler;
		if (register_kprobe(&targets[i].kp) == 0) {
			targets[i].done = 1;
			placed++;
			pr_info("fix_charger: kprobe placed on \"%s\" (try %lu)\n",
				targets[i].name, tries);
		}
	}
}

static void retry_fn(struct timer_list *t)
{
	place_all();
	if (placed < NTARGETS && tries < max_tries)
		mod_timer(&retry_timer, jiffies + msecs_to_jiffies(retry_ms));
	else
		pr_info("fix_charger: placed %d of %zu in %lu tries\n",
			placed, NTARGETS, tries);
}

static int __init fixchg_init(void)
{
	throttle_from = jiffies + msecs_to_jiffies(warmup_sec * 1000);
	place_all();
	if (placed < NTARGETS) {
		/* Return SUCCESS: otherwise the strict stage-1 loader aborts the
		 * entire remaining module list (F330). Wait for targets via the timer. */
		pr_info("fix_charger: placed %d of %zu, retrying every %lu ms\n",
			placed, NTARGETS, retry_ms);
		timer_setup(&retry_timer, retry_fn, 0);
		mod_timer(&retry_timer, jiffies + msecs_to_jiffies(retry_ms));
	} else {
		pr_info("fix_charger: all %zu kprobes placed on the first try\n", NTARGETS);
	}
	return 0;
}

static void __exit fixchg_exit(void)
{
	int i;

	del_timer_sync(&retry_timer);
	for (i = 0; i < NTARGETS; i++)
		if (targets[i].done)
			unregister_kprobe(&targets[i].kp);
	pr_info("fix_charger: removed %d kprobes\n", placed);
}

module_init(fixchg_init);
module_exit(fixchg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Quieter charger log and no false 'charging' in OTG (self-resolving kprobes)");
