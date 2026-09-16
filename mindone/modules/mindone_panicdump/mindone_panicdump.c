// SPDX-License-Identifier: GPL-2.0
/*
 * mindone_panicdump -- flush the kernel log to a partition on panic (F1281). A GPU
 * crash is an Oops that panic_on_oops=1 turns into a panic, but /proc/kmsg dies with
 * the system before the trace arrives (ring log dead F1259, /data rolls back F935).
 * The kernel exports `mindone_dump_now()` (writes to the `logo` partition, no
 * filesystem), but its built-in hook only arms via cmdline `mindone_logdump_panic=1`
 * -- a reflash. This module attaches the same hook without reflashing.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>

extern void mindone_dump_now(int mark);

static int mindone_panicdump_mark = 9999;
module_param(mindone_panicdump_mark, int, 0644);
MODULE_PARM_DESC(mindone_panicdump_mark, "MINDONE: mark to write the dump with");

static int mnd_on_panic(struct notifier_block *nb, unsigned long ev, void *p)
{
	pr_emerg("MINDONE-PANICDUMP: panic -- writing log to partition, mark %d\n",
		 mindone_panicdump_mark);
	mindone_dump_now(mindone_panicdump_mark);
	return NOTIFY_DONE;
}

static struct notifier_block mnd_nb = {
	.notifier_call = mnd_on_panic,
	.priority = INT_MAX,   /* as early as possible, before other handlers */
};

static int mindone_panicdump_test;
module_param(mindone_panicdump_test, int, 0644);
MODULE_PARM_DESC(mindone_panicdump_test,
	"MINDONE: 1 = flush the log IMMEDIATELY at load (write-path check)");

static int __init mnd_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &mnd_nb);
	if (mindone_panicdump_test) {
		pr_emerg("MINDONE-PANICDUMP: TEST -- writing log to partition\n");
		mindone_dump_now(mindone_panicdump_mark);
		pr_emerg("MINDONE-PANICDUMP: TEST -- write returned\n");
	}
	pr_info("MINDONE-PANICDUMP: panic hook installed\n");
	return 0;
}

static void __exit mnd_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &mnd_nb);
	pr_info("MINDONE-PANICDUMP: hook removed\n");
}

module_init(mnd_init);
module_exit(mnd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MINDONE: flush the kernel log to a partition on panic");
