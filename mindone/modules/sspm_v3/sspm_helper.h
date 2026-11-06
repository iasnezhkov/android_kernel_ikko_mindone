/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

#ifndef __SSPM_HELPER_H__
#define __SSPM_HELPER_H__

#include <linux/types.h>

struct sspm_regs {
	void __iomem *cfg;
	void __iomem *mboxshare;
	unsigned int cfgregsize;
	int irq;
};

struct sspm_work_struct {
	struct work_struct work;
	unsigned int flags;
};

extern struct sspm_regs sspmreg;

extern int sspm_plt_init(void);
extern unsigned int is_sspm_ready(void);
extern void memcpy_to_sspm(void __iomem *trg, const void *src, int size);
extern void memcpy_from_sspm(void *trg, const void __iomem *src, int size);
extern void sspm_schedule_work(struct sspm_work_struct *sspm_ws);
#endif
