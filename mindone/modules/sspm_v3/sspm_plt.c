// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011-2015 MediaTek Inc.
 */

#include <linux/types.h>
/* declared BEFORE first use: the first revision placed this before sspm_plt_init,
 * but the mark ended up higher, in the sysfs function, and the build failed with
 * "call to undeclared function".
 */
extern void mindone_sspm_mark(int step);
#include <linux/module.h>       /* needed by all modules */
#include <linux/init.h>         /* needed by module macros */
#include <linux/fs.h>           /* needed by file_operations* */
#include <linux/miscdevice.h>   /* needed by miscdevice* */
#include <linux/device.h>       /* needed by device_* */
#include <linux/vmalloc.h>      /* needed by kmalloc */
#include <linux/uaccess.h>      /* needed by copy_to_user */
#include <linux/fs.h>           /* needed by file_operations* */
#include <linux/slab.h>         /* needed by kmalloc */
#include <linux/poll.h>         /* needed by poll */
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/scmi_protocol.h>
#include "sspm_define.h"
#include "sspm_helper.h"
#include "sspm_reservedmem.h"
#include "sspm_reservedmem_define.h"
#if SSPM_LOGGER_SUPPORT
#include "sspm_logger.h"
#endif
#include "sspm_sysfs.h"
#include "tinysys-scmi.h"


#if SSPM_PLT_SERV_SUPPORT

int scmi_plt_id;

struct plt_ctrl_s {
	unsigned int magic;
	unsigned int size;
	unsigned int mem_sz;
#if SSPM_LOGGER_SUPPORT
	unsigned int logger_ofs;
#endif
};

static ssize_t sspm_alive_show(struct device *kobj,
	struct device_attribute *attr, char *buf)
{
	struct plt_msg_s msg_data;
	int ret;
	struct scmi_tinysys_info_st *tinfo = get_scmi_tinysys_info();

	/* F3767: guard against a not-yet-registered scmi provider (same class as F3703). */
	if (!tinfo)
		return snprintf(buf, PAGE_SIZE, "Unknown\n");

	msg_data.cmd = 0xDEAD;

	ret = scmi_tinysys_common_set(tinfo->ph, scmi_plt_id,
		msg_data.cmd, 0, 0, 0, 0);

	return snprintf(buf, PAGE_SIZE, "%s\n",	ret ? "Dead" : "Alive");
}
DEVICE_ATTR_RO(sspm_alive);

int sspm_plt_init(void)
{
	mindone_sspm_mark(11);

	phys_addr_t phys_addr, virt_addr, mem_sz;
	struct plt_msg_s msg_data;
	struct plt_ctrl_s *plt_ctl;
	struct scmi_tinysys_info_st *tinfo = get_scmi_tinysys_info();
	int ret;
	unsigned int last_ofs;
#if SSPM_LOGGER_SUPPORT
	unsigned int last_sz;
#endif
	unsigned int *mark;
	unsigned char *b;

	ret = sspm_sysfs_create_file(&dev_attr_sspm_alive);
	if (unlikely(ret != 0))
		goto error;

	phys_addr = sspm_reserve_mem_get_phys(SSPM_MEM_ID);
	if (phys_addr == 0) {
		pr_err("SSPM: Can't get logger phys mem\n");
		goto error;
	}

	virt_addr = sspm_reserve_mem_get_virt(SSPM_MEM_ID);
	if (virt_addr == 0) {
		pr_err("SSPM: Can't get logger virt mem\n");
		goto error;
	}

	mem_sz = sspm_reserve_mem_get_size(SSPM_MEM_ID);
	if (mem_sz == 0) {
		pr_err("SSPM: Can't get logger mem size\n");
		goto error;
	}

	mindone_sspm_mark(12);
	b = (unsigned char *) (uintptr_t)virt_addr;
	for (last_ofs = 0; last_ofs < sizeof(*plt_ctl); last_ofs++)
		b[last_ofs] = 0x0;

	mark = (unsigned int *) (uintptr_t)virt_addr;
	*mark = PLT_INIT;
	mark = (unsigned int *) ((unsigned char *) (uintptr_t)
		virt_addr + mem_sz - 4);
	*mark = PLT_INIT;

	plt_ctl = (struct plt_ctrl_s *) (uintptr_t)virt_addr;
	plt_ctl->magic = PLT_INIT;
	plt_ctl->size = sizeof(*plt_ctl);
	plt_ctl->mem_sz = mem_sz;

	mindone_sspm_mark(13);
	last_ofs = plt_ctl->size;


	pr_debug("SSPM: %s(): after plt, ofs=%u\n", __func__, last_ofs);

#if SSPM_LOGGER_SUPPORT
	plt_ctl->logger_ofs = last_ofs;
	last_sz = sspm_logger_init(virt_addr + last_ofs, mem_sz - last_ofs);

	if (last_sz == 0) {
		pr_err("SSPM: sspm_logger_init return fail\n");
		goto error;
	}

	last_ofs += last_sz;
	pr_debug("SSPM: %s(): after logger, ofs=%u\n", __func__, last_ofs);
#endif

	mindone_sspm_mark(14);
	/* TESTING A HYPOTHESIS (F437). Between marks 14 and 15 the only thing that can
	 * fail is dereferencing `tinfo`, taken from get_scmi_tinysys_info() and never
	 * checked. If it's empty, the dereference crashes the kernel, and without a
	 * console that's indistinguishable from a hang. Here the emptiness becomes a
	 * DISTINGUISHABLE answer: mark 17 = tinfo empty (hypothesis confirmed), mark 18
	 * = tinfo intact (something else on that line is at fault).
	 */
	if (!tinfo || !tinfo->sdev)
		mindone_sspm_mark(17);
	else
		mindone_sspm_mark(18);
	msg_data.cmd = PLT_INIT;
	msg_data.u.ctrl.phys = phys_addr;
	msg_data.u.ctrl.size = mem_sz;

	of_property_read_u32(tinfo->sdev->dev.of_node,
		"scmi_plt", &scmi_plt_id);

	mindone_sspm_mark(15);
	ret = scmi_tinysys_common_set(tinfo->ph, scmi_plt_id,
		msg_data.cmd, msg_data.u.ctrl.phys, msg_data.u.ctrl.size, 0, 0);

	mindone_sspm_mark(16);
	if (ret) {
		pr_err("SSPM: plt init fail (ret=%d)\n", ret);
		goto error;
	}

#if SSPM_LOGGER_SUPPORT
	sspm_logger_init_done();
#endif

	return 0;

error:
	return -1;
}
#endif
