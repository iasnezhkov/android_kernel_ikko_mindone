// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <asm/div64.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include <linux/moduleparam.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/kmsg_dump.h>
#include <uapi/linux/sched/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "aee.h"

/*
 * printk_ctrl:
 * 0: uart printk disable
 * 1: uart printk enable
 * 2: uart printk always enable
 */

static struct proc_dir_entry *entry;

#ifdef CONFIG_MTK_PRINTK_UART_CONSOLE
static int printk_ctrl_disable;
module_param_named(disable_uart, printk_ctrl_disable, int, 0644);

bool mt_get_uartlog_status(void)
{
	if (printk_ctrl_disable == 1)
		return false;
	else if ((printk_ctrl_disable == 0) || (printk_ctrl_disable == 2))
		return true;
	return true;
}
EXPORT_SYMBOL_GPL(mt_get_uartlog_status);

/* 0:disable uart, 1:enable uart */
void update_uartlog_status(bool new_value, int value)
{
	struct console *bcon = NULL;

	if (new_value == false || printk_ctrl_disable == 2) {
		pr_info("use default valut %d to set uart status.\n", printk_ctrl_disable == 1 ? 0 : 1);
	} else if (value == 0) { /* disable uart log */
		printk_ctrl_disable = 1;
	} else if (value == 1) { /* enable uart log */
		printk_ctrl_disable = 0;
	} else {
		pr_info("invalid value %d, use default value %d.\n",
			value, printk_ctrl_disable == 1 ? 0 : 1);
	}

#if IS_ENABLED(CONFIG_MTK_PRINTK_DEBUG)
	set_printk_uart_status(!printk_ctrl_disable);
#endif

	if (printk_ctrl_disable == 1) {
		for_each_console(bcon) {
			pr_info("console name: %s, status 0x%x.\n", bcon->name, bcon->flags);
			if (!strncmp(bcon->name, "ttyS", 4)) {
				bcon->flags &= ~CON_ENABLED;
				return;
			}
		}
	} else {
		for_each_console(bcon) {
			pr_info("console name: %s. status 0x%x.\n", bcon->name, bcon->flags);
			if (!strncmp(bcon->name, "ttyS", 4)) {
				bcon->flags |= CON_ENABLED;
				return;
			}
		}
	}
}
EXPORT_SYMBOL_GPL(update_uartlog_status);

#else
bool mt_get_uartlog_status(void)
{
	return false;
}
EXPORT_SYMBOL_GPL(mt_get_uartlog_status);

void update_uartlog_status(bool new_value, int value)
{

}
EXPORT_SYMBOL_GPL(update_uartlog_status);
#endif


void set_detect_count(int val)
{

}
EXPORT_SYMBOL_GPL(set_detect_count);

int get_detect_count(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(get_detect_count);
void set_logtoomuch_enable(void)
{

}
EXPORT_SYMBOL_GPL(set_logtoomuch_enable);

void set_logtoomuch_disable(void)
{

}
EXPORT_SYMBOL_GPL(set_logtoomuch_disable);

bool get_logtoomuch_status(void)
{
	return false;
}
EXPORT_SYMBOL_GPL(get_logtoomuch_status);



static int mt_printk_ctrl_show(struct seq_file *m, void *v)
{
	seq_puts(m, "=== mt printk controller ===\n");
#ifdef CONFIG_MTK_PRINTK_UART_CONSOLE
	seq_puts(m, "0:   printk uart disable\n");
	seq_puts(m, "1:   printk uart enable\n");
#endif
	seq_puts(m, "=== mt printk controller ===\n\n");
#ifdef CONFIG_MTK_PRINTK_UART_CONSOLE
	seq_printf(m, "printk uart enable: %d\n", mt_get_uartlog_status());
#endif
	return 0;
}

static ssize_t mt_printk_ctrl_write(struct file *filp,
	const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = kstrtoul(buf, 10, (unsigned long *)&val);

	if (ret < 0)
		return ret;

	switch (val) {
#ifdef CONFIG_MTK_PRINTK_UART_CONSOLE
	case 0:
		update_uartlog_status(true, 0);
		break;
	case 1:
		update_uartlog_status(true, 1);
		break;
	case 5:
		printk_ctrl_disable = 2;
		update_uartlog_status(false, 0);
		break;
#endif
	default:
		break;
	}
	return cnt;
}

/*** Seq operation of mtprof ****/
static int mt_printk_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, mt_printk_ctrl_show, inode->i_private);
}

static const struct proc_ops mt_printk_ctrl_fops = {
	.proc_open = mt_printk_ctrl_open,
	.proc_write = mt_printk_ctrl_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init mt_printk_ctrl_init(void)
{

	entry = proc_create("mtprintk", 0664, NULL, &mt_printk_ctrl_fops);
	if (!entry)
		return -ENOMEM;
	return 0;
}


#ifdef MODULE
static void __exit mt_printk_ctrl_exit(void)
{
	if (entry)
		proc_remove(entry);

pr_notice("log_much exit.");

}

module_init(mt_printk_ctrl_init);
module_exit(mt_printk_ctrl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Printk enhance");
MODULE_AUTHOR("MediaTek Inc.");
#else
device_initcall(mt_printk_ctrl_init);
#endif
