// SPDX-License-Identifier: GPL-2.0
// MINDONE-PTYDBG (F3206): dump tty (pty slave) state by path -- ldisc/count/flags/termios
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/file.h>
static struct tty_struct *mo_file_tty(struct file *f)
{
	struct tty_file_private *priv = f->private_data;
	return priv ? priv->tty : NULL;
}
static char *path = "/dev/pts/7";
module_param(path, charp, 0644);
static int __init ptydbg_init(void)
{
	struct file *f = filp_open(path, O_RDWR | O_NONBLOCK | O_NOCTTY, 0);
	struct tty_struct *t, *o;
	if (IS_ERR(f)) { pr_notice("PTYDBG: open %s -> %ld\n", path, PTR_ERR(f)); return -ENODEV; }
	t = mo_file_tty(f);
	if (!t) { pr_notice("PTYDBG: no tty for %s\n", path); filp_close(f, NULL); return -ENODEV; }
	o = t->link;
	pr_notice("PTYDBG: %s tty=%s idx=%d count=%d flags=0x%lx ldisc=%px ldisc_ops=%s c_line=%d VMIN=%d VTIME=%d icanon=%d hupped=%d ioerr=%d otherclosed=%d ldisc_open=%d\n",
		path, t->name, t->index, t->count, t->flags, t->ldisc,
		(t->ldisc && t->ldisc->ops) ? t->ldisc->ops->name : "NULL",
		t->termios.c_line, t->termios.c_cc[VMIN], t->termios.c_cc[VTIME],
		!!(t->termios.c_lflag & ICANON),
		test_bit(TTY_HUPPED, &t->flags), test_bit(TTY_IO_ERROR, &t->flags),
		test_bit(TTY_OTHER_CLOSED, &t->flags), test_bit(TTY_LDISC_OPEN, &t->flags));
	if (o)
		pr_notice("PTYDBG: link=%s count=%d flags=0x%lx ldisc=%px ops=%s otherclosed=%d ptylock=%d packet=%d\n",
			o->name, o->count, o->flags, o->ldisc, (o->ldisc && o->ldisc->ops) ? o->ldisc->ops->name : "NULL",
			test_bit(TTY_OTHER_CLOSED, &o->flags), test_bit(TTY_PTY_LOCK, &o->flags), o->ctrl.packet);
	filp_close(f, NULL);
	return -EAGAIN; /* do not stay resident */
}
module_init(ptydbg_init);
MODULE_LICENSE("GPL");
