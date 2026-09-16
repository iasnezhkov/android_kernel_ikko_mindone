// SPDX-License-Identifier: GPL-2.0
// MINDONE-NTTY (F3206->F3207): restore N_TTY to slot 0 of the line discipline table --
// the MTK STP ldisc port on 6.1 registered n_mtkstp with num=0 and replaced N_TTY for all
// new ttys (pty slave: read->0, write->0 bytes). Live workaround until stage-1 is rebuilt.
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
static struct tty_ldisc_ops mo_ntty_ops;
/* wmt/stp registers n_mtkstp in slot 0 even AFTER our module loads (consys start ~4.3s,
 * restarts) -- so we repeat the N_TTY restore on a schedule until vendor_boot is rebuilt
 * with .num=N_MTKSTP */
static const unsigned int mo_delays_s[] = { 10, 20, 45, 90, 180 };
static unsigned int mo_step;
static struct delayed_work mo_work;
static void mo_rereg(struct work_struct *w)
{
	int rc = tty_register_ldisc(&mo_ntty_ops);
	pr_notice("MINDONE-NTTY: re-registered N_TTY again (step %u) -> %d\n", mo_step, rc);
	if (++mo_step < ARRAY_SIZE(mo_delays_s))
		schedule_delayed_work(&mo_work, mo_delays_s[mo_step] * HZ);
}
static int __init mo_ntty_init(void)
{
	int rc;
	n_tty_inherit_ops(&mo_ntty_ops);
	mo_ntty_ops.owner = THIS_MODULE;
	mo_ntty_ops.num = N_TTY;
	mo_ntty_ops.name = "n_tty";
	rc = tty_register_ldisc(&mo_ntty_ops);
	pr_notice("MINDONE-NTTY: re-registered N_TTY (num=%d) -> %d\n", mo_ntty_ops.num, rc);
	if (!rc) {
		INIT_DELAYED_WORK(&mo_work, mo_rereg);
		schedule_delayed_work(&mo_work, mo_delays_s[0] * HZ);
	}
	return rc;
}
static void __exit mo_ntty_exit(void) { cancel_delayed_work_sync(&mo_work); pr_notice("MINDONE-NTTY: exit (slot 0 left as is)\n"); }
module_init(mo_ntty_init);
module_exit(mo_ntty_exit);
MODULE_LICENSE("GPL");
