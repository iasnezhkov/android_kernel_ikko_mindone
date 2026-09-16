// SPDX-License-Identifier: GPL-2.0
/*
 * pdrev.ko -- RE-ARMABLE skip of a kernel function via kprobe.
 *
 * WHY (F1626, F1631, F1633). USB-PD power negotiation breaks the data link:
 * the source sends fresh-revision messages 22/23/24, the phone replies with a
 * protocol-level error, and the port electrically reconnects. Fully disabling
 * the power policy fixes USB, but drops the input current limit from the
 * negotiated 3A to 1.5A and loses the contract entirely. Working approach:
 * let the contract CLOSE, then stop the policy BEFORE the failing messages
 * arrive -- controlled by an external `armed` flag (userspace polls `pd_type`
 * and arms the module).
 *
 * COST OF THE PREVIOUS REVISION (fixed by this patch). The old `pre()` checked
 * `!armed && !time_after_eq(jiffies, armed_at)`, i.e. EITHER the flag OR
 * elapsed time. `armed_at` was computed ONCE at insmod (~12.75s after boot);
 * past that point `time_after_eq()` becomes true FOREVER, and the function is
 * skipped regardless of `armed` -- userspace can no longer disarm it. That was
 * the real cause behind "policy stopped forever until reboot": it was actually
 * stopped by the expired timer, never by `armed`.
 *
 * FIX: `pre()` now looks ONLY at `armed`, freely toggleable any number of
 * times for the module's whole lifetime (re-arm on cable unplug/replug, see
 * modules/magisk-usbfix/service.sh). The fallback clock limit (`arm_ms`)
 * remains, but as a ONE-SHOT kernel timer: it sets `armed=1` if the flag never
 * arrived in time, and after that single firing never touches `pre()` again.
 *
 * SELF-RESOLVING TARGET (what's different in this version). The previous
 * version required an `addr=` parameter because `kallsyms_lookup_name` isn't
 * exported on GKI 5.10. No need to work around it: the kprobe machinery
 * resolves the name itself via `symbol_name`, including module symbols;
 * `addr` remains only as a manual override. This matters because stage-1 boot
 * loads modules WITHOUT arguments (F639), so a module with a mandatory
 * parameter can't be baked into the image -- this version can.
 * The target lives in ANOTHER module (`tcpc_class`), which may load after us,
 * so on registration failure we don't give up -- we retry on a timer until
 * the symbol appears.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

static unsigned long addr;
module_param(addr, ulong, 0);
MODULE_PARM_DESC(addr, "target function address (optional; usually resolved by name)");

static char *target = "pd_policy_engine_run";
module_param(target, charp, 0444);
MODULE_PARM_DESC(target, "name of the function to hook");

static unsigned long retry_ms = 500;
module_param(retry_ms, ulong, 0644);
MODULE_PARM_DESC(retry_ms, "registration retry period, ms (target may load after us)");

static unsigned long tries;
module_param(tries, ulong, 0444);
static int registered;
module_param(registered, int, 0444);

static unsigned long arm_ms;
module_param(arm_ms, ulong, 0644);
MODULE_PARM_DESC(arm_ms, "FALLBACK limit: how many ms after MODULE LOAD to force-arm if the flag never arrived (fires once)");

static unsigned long hits;
module_param(hits, ulong, 0444);
MODULE_PARM_DESC(hits, "how many times the function was called (check)");

static unsigned long skips;
module_param(skips, ulong, 0444);
MODULE_PARM_DESC(skips, "how many times it was skipped (check)");

static int armed;
module_param(armed, int, 0644);
MODULE_PARM_DESC(armed, "1 = skip (freeze) the function; 0 = run normally. Read on EVERY call -- can be toggled any number of times over the module's lifetime (re-arm on cable unplug/replug)");

static struct timer_list fallback_timer;

static int pre(struct kprobe *p, struct pt_regs *regs)
{
	hits++;
	/* The one source of truth is the live `armed` flag. No hidden clock
	 * comparison here anymore (see the comment above about the cost of
	 * the previous revision). */
	if (!armed)
		return 0;
	/* skip the body: return 0 to the caller */
	regs->regs[0] = 0;
	instruction_pointer_set(regs, regs->regs[30]);
	skips++;
	return 1;
}

static struct kprobe kp = { .pre_handler = pre };

/* The fallback limit FIRES ONCE, only if `armed` was never set by userspace
 * in time. After that it never intervenes again -- control passes entirely
 * to writes to /sys/module/pdrev/parameters/armed, as many times as needed. */
static void fallback_fn(struct timer_list *t)
{
	if (!armed) {
		armed = 1;
		pr_info("pdrev: fallback limit (%lu ms since module load) fired, force-armed\n", arm_ms);
	}
}

/* Registration retry: the target lives in another module and may load after
 * us. We must not give up -- otherwise a module baked into the image would be
 * useless on any boot where the load order differs. */
static struct timer_list retry_timer;

static int try_register(void)
{
	int ret;

	tries++;
	if (addr)
		kp.addr = (kprobe_opcode_t *)addr;
	else
		kp.symbol_name = target;

	ret = register_kprobe(&kp);
	if (ret == 0) {
		registered = 1;
		timer_setup(&fallback_timer, fallback_fn, 0);
		mod_timer(&fallback_timer, jiffies + msecs_to_jiffies(arm_ms));
		pr_info("pdrev: kprobe placed on %s (%px) on try %lu; fallback limit in %lu ms\n",
			addr ? "given address" : target, kp.addr, tries, arm_ms);
	}
	return ret;
}

static void retry_fn(struct timer_list *t)
{
	if (registered)
		return;
	if (try_register() != 0)
		mod_timer(&retry_timer, jiffies + msecs_to_jiffies(retry_ms));
}

static int __init pdrev_init(void)
{
	int ret = try_register();

	if (ret == 0)
		return 0;

	/* Return SUCCESS, not an error: the module stays loaded and waits for the
	 * target to appear. An error would unload us, and a fix baked into the
	 * image would never work if the load order put us first. */
	pr_info("pdrev: target \"%s\" not found yet (code %d), retrying every %lu ms\n",
		target, ret, retry_ms);
	timer_setup(&retry_timer, retry_fn, 0);
	mod_timer(&retry_timer, jiffies + msecs_to_jiffies(retry_ms));
	return 0;
}

static void __exit pdrev_exit(void)
{
	del_timer_sync(&retry_timer);
	del_timer_sync(&fallback_timer);
	if (registered)
		unregister_kprobe(&kp);
	pr_info("pdrev: removed, calls %lu, skipped %lu, registration tries %lu\n",
		hits, skips, tries);
}
module_init(pdrev_init);
module_exit(pdrev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rearmable kprobe skip (survives cable replug)");
