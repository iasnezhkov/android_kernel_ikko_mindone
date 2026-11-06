/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015 MediaTek Inc.
 */

#ifndef __aee_helper_h
#define __aee_helper_h

#include <linux/types.h>
#include <asm/stacktrace.h>

struct aee_kallsyms_layout {
	unsigned long start;
	u32 size;
	u32 num_syms_off;
	u32 names_off;
	u32 markers_off;
	u32 token_table_off;
	u32 token_index_off;
	u32 offsets_off;
};

extern const char *aee_arch_vma_name(struct vm_area_struct *vma);

extern unsigned long aee_get_stext(void);
extern unsigned long aee_get_etext(void);
extern unsigned long aee_get_text(void);
extern unsigned long aee_get_linux_banner(void);
extern void *aee_log_buf_addr_get(void);
extern struct list_head *aee_get_modules(void);
extern bool aee_get_kallsyms_layout(struct aee_kallsyms_layout *layout);
extern void aee_show_regs(struct pt_regs *regs);
extern void aee_reinit_die_lock(void);

int mrdump_ka_init(void *vaddr);
void mrdump_ka_exit(void);
extern void init_ko_addr_list_late(void);
extern void mrdump_mini_add_klog(void);
extern void mrdump_mini_add_kallsyms(void);
extern void mrdump_mini_add_version(void);
extern void sysrq_sched_debug_show_at_AEE(void);
#endif
