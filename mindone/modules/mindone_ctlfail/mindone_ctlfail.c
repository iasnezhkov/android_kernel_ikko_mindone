// SPDX-License-Identifier: GPL-2.0
/*
 * Diagnostic control module: its init deliberately fails.
 *
 * Purpose: prove that on this build `init` actually runs and that a fatal
 * module-load error still produces a ramoops dump. Android treats a failure in
 * modules.load as fatal, and with androidboot.init_fatal_panic=true that must
 * panic the kernel. If the dump appears, the logging channel works for this
 * image and the silent reset seen later happens after module loading. If no
 * dump appears, userspace never got that far.
 *
 * Not part of any product image - only the control experiment.
 */
#include <linux/module.h>
#include <linux/kernel.h>

static int __init mindone_ctlfail_init(void)
{
	pr_err("MINDONE-CTLFAIL: refusing to load on purpose (control experiment)\n");
	return -EINVAL;
}

static void __exit mindone_ctlfail_exit(void) { }

module_init(mindone_ctlfail_init);
module_exit(mindone_ctlfail_exit);
MODULE_DESCRIPTION("mind_one control module that fails to load on purpose");
MODULE_LICENSE("GPL v2");
