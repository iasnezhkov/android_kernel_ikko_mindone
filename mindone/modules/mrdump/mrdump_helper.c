// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/rculist.h>
#include <linux/sched/signal.h>
#include <linux/sched/clock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include <asm/memory.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/smp_plat.h>
#include <asm/stacktrace.h>
#include <asm/system_misc.h>

#include <debug_kinfo.h>
#include <mt-plat/aee.h>
#include <mt-plat/mboot_params.h>
#include <sched/sched.h>
#include "mrdump_private.h"

#define MRDUMP_KINFO_DRIVER	"debug-kinfo"

static void *kinfo_vaddr;
static struct kernel_info mrdump_ki;

static unsigned long p_text;
static unsigned long p_stext;
static unsigned long p_etext;
static unsigned long p_end;
static unsigned long p_num_syms;
static unsigned long p_banner;
static struct list_head *p_modules;
static void *p_log_ptr;
static raw_spinlock_t *p_die_lock;

static unsigned int ks_num;
static const u8 *ks_names;
static unsigned long ks_names_len;
static const unsigned int *ks_markers;
static const char *ks_token_table;
static const u16 *ks_token_index;
static const int *ks_offsets;
static const u8 *ks_seqs;
static unsigned long ks_relative_base;

static void mrdump_ka_work_func(struct work_struct *work);
static DECLARE_WORK(ka_work, mrdump_ka_work_func);

static unsigned long mrdump_image_va(u64 pa)
{
	return (unsigned long)pa + kimage_voffset;
}

static bool mrdump_in_image(unsigned long addr)
{
	return addr >= p_text && addr < p_end;
}

static u32 mrdump_kinfo_checksum(const struct kernel_info *ki)
{
	u32 word, sum = 0;
	size_t i;

	for (i = 0; i + sizeof(word) <= sizeof(*ki); i += sizeof(word)) {
		memcpy(&word, (const u8 *)ki + i, sizeof(word));
		sum ^= word;
	}
	return sum;
}

static bool mrdump_kinfo_sane(const struct kernel_info *ki)
{
	return ki->bit_per_long == BITS_PER_LONG &&
	       ki->name_len == KSYM_NAME_LEN &&
	       !ki->enabled_absolute_percpu &&
	       ki->num_syms &&
	       ki->_text_pa <= ki->_stext_pa &&
	       ki->_stext_pa < ki->_etext_pa &&
	       ki->_etext_pa <= ki->_names_pa &&
	       ki->_names_pa < ki->_markers_pa &&
	       ki->_markers_pa < ki->_token_table_pa &&
	       ki->_token_table_pa < ki->_token_index_pa &&
	       ki->_token_index_pa < ki->_offsets_pa &&
	       ki->_offsets_pa < ki->_seqs_of_names_pa &&
	       ki->_seqs_of_names_pa + 3ULL * ki->num_syms <= ki->_end_pa;
}

static bool mrdump_ks_name(unsigned int seq, char *buf, size_t size)
{
	unsigned long off = ks_markers[seq >> 8];
	unsigned int i, len;
	const char *tok;
	const u8 *data;
	bool skipped = false;

	for (i = 0; i < (seq & 0xFF); i++) {
		if (off + 1 >= ks_names_len)
			return false;
		len = ks_names[off];
		if (len & 0x80)
			len = ((len & 0x7F) | (ks_names[off + 1] << 7)) + 1;
		off += len + 1;
	}
	if (off + 1 >= ks_names_len)
		return false;

	data = ks_names + off;
	len = *data++;
	if (len & 0x80)
		len = (len & 0x7F) | (*data++ << 7);
	if (data + len > ks_names + ks_names_len)
		return false;

	while (len--) {
		for (tok = ks_token_table + ks_token_index[*data++]; *tok; tok++) {
			if (!skipped) {
				skipped = true;
				continue;
			}
			if (size <= 1)
				goto out;
			*buf++ = *tok;
			size--;
		}
	}
out:
	*buf = '\0';
	return true;
}

static unsigned int mrdump_ks_seq(unsigned int i)
{
	const u8 *s = ks_seqs + 3 * i;

	return (s[0] << 16) | (s[1] << 8) | s[2];
}

static bool mrdump_ks_is(unsigned int i, const char *name, char *buf)
{
	unsigned int seq;

	if (i >= ks_num)
		return false;
	seq = mrdump_ks_seq(i);
	return seq < ks_num && mrdump_ks_name(seq, buf, KSYM_NAME_LEN) &&
	       !strcmp(name, buf);
}

static unsigned long mrdump_ks_lookup(const char *name)
{
	char buf[KSYM_NAME_LEN];
	unsigned int lo = 0, hi = ks_num, mid, seq;
	unsigned long addr;
	int cmp;

	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		seq = mrdump_ks_seq(mid);
		if (seq >= ks_num || !mrdump_ks_name(seq, buf, sizeof(buf)))
			return 0;
		cmp = strcmp(name, buf);
		if (cmp > 0) {
			lo = mid + 1;
		} else if (cmp < 0) {
			hi = mid;
		} else {
			if ((mid && mrdump_ks_is(mid - 1, name, buf)) ||
			    mrdump_ks_is(mid + 1, name, buf))
				return 0;
			addr = ks_relative_base + (u32)ks_offsets[seq];
			return mrdump_in_image(addr) ? addr : 0;
		}
	}
	return 0;
}

static bool mrdump_ks_init(void)
{
	const struct kernel_info *ki = &mrdump_ki;

	ks_num = ki->num_syms;
	ks_names = (const u8 *)mrdump_image_va(ki->_names_pa);
	ks_names_len = ki->_markers_pa - ki->_names_pa;
	ks_markers = (const unsigned int *)mrdump_image_va(ki->_markers_pa);
	ks_token_table = (const char *)mrdump_image_va(ki->_token_table_pa);
	ks_token_index = (const u16 *)mrdump_image_va(ki->_token_index_pa);
	ks_offsets = (const int *)mrdump_image_va(ki->_offsets_pa);
	ks_seqs = (const u8 *)mrdump_image_va(ki->_seqs_of_names_pa);
	ks_relative_base = mrdump_image_va(ki->_relative_pa);

	return mrdump_ks_lookup("_text") == p_text &&
	       mrdump_ks_lookup("_stext") == p_stext &&
	       mrdump_ks_lookup("_etext") == p_etext;
}

static struct list_head *mrdump_modules_head(void)
{
	struct list_head *pos, *head = NULL;

	rcu_read_lock();
	list_for_each_rcu(pos, &THIS_MODULE->list) {
		if (mrdump_in_image((unsigned long)pos)) {
			head = pos;
			break;
		}
	}
	rcu_read_unlock();
	return head;
}

static void mrdump_ka_work_func(struct work_struct *work)
{
	const struct kernel_all_info *all = kinfo_vaddr;

	if (p_text || READ_ONCE(all->magic_number) != DEBUG_KINFO_MAGIC)
		return;

	memcpy(&mrdump_ki, &all->info, sizeof(mrdump_ki));
	if (mrdump_kinfo_checksum(&mrdump_ki) != all->combined_checksum ||
	    !mrdump_kinfo_sane(&mrdump_ki)) {
		pr_err("mrdump: debug-kinfo data is not valid\n");
		return;
	}

	p_stext = mrdump_image_va(mrdump_ki._stext_pa);
	p_etext = mrdump_image_va(mrdump_ki._etext_pa);
	p_end = mrdump_image_va(mrdump_ki._end_pa);
	p_text = mrdump_image_va(mrdump_ki._text_pa);
	p_modules = mrdump_modules_head();

	if (mrdump_ks_init()) {
		p_num_syms = mrdump_ks_lookup("kallsyms_num_syms");
		if (p_num_syms >= (unsigned long)ks_names)
			p_num_syms = 0;
		p_log_ptr = (void *)mrdump_ks_lookup("prb");
		p_die_lock = (raw_spinlock_t *)mrdump_ks_lookup("die_lock");
		p_banner = mrdump_ks_lookup("linux_banner");
	} else {
		pr_err("mrdump: kallsyms do not match debug-kinfo, no symbol lookup\n");
	}

	mrdump_cblock_late_init();
	init_ko_addr_list_late();
	mrdump_mini_add_klog();
	mrdump_mini_add_kallsyms();
	mrdump_mini_add_version();
	pr_info("mrdump: kernel image described, modules %s, symbols %s\n",
		p_modules ? "found" : "missing", p_num_syms ? "found" : "missing");
}

static int mrdump_kinfo_bound(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct device *dev = data;

	if (action == BUS_NOTIFY_BOUND_DRIVER && dev->driver &&
	    !strcmp(dev->driver->name, MRDUMP_KINFO_DRIVER))
		schedule_work(&ka_work);
	return NOTIFY_DONE;
}

static struct notifier_block mrdump_kinfo_nb = {
	.notifier_call = mrdump_kinfo_bound,
};

int mrdump_ka_init(void *vaddr)
{
	int ret;

	kinfo_vaddr = vaddr;
	ret = bus_register_notifier(&platform_bus_type, &mrdump_kinfo_nb);
	if (ret)
		return ret;
	schedule_work(&ka_work);
	return 0;
}

void mrdump_ka_exit(void)
{
	bus_unregister_notifier(&platform_bus_type, &mrdump_kinfo_nb);
	cancel_work_sync(&ka_work);
}

unsigned long aee_get_text(void)
{
	return p_text;
}

unsigned long aee_get_stext(void)
{
	return p_stext;
}

unsigned long aee_get_etext(void)
{
	return p_etext;
}

unsigned long aee_get_linux_banner(void)
{
	return p_banner;
}

struct list_head *aee_get_modules(void)
{
	return p_modules;
}

void *aee_log_buf_addr_get(void)
{
	return p_log_ptr;
}

bool aee_get_kallsyms_layout(struct aee_kallsyms_layout *layout)
{
	unsigned long start = p_num_syms;

	if (!start)
		return false;

	layout->start = start;
	layout->size = (unsigned long)ks_seqs + 3UL * ks_num - start;
	layout->num_syms_off = 0;
	layout->names_off = (unsigned long)ks_names - start;
	layout->markers_off = (unsigned long)ks_markers - start;
	layout->token_table_off = (unsigned long)ks_token_table - start;
	layout->token_index_off = (unsigned long)ks_token_index - start;
	layout->offsets_off = (unsigned long)ks_offsets - start;
	return true;
}

void aee_reinit_die_lock(void)
{
	if (!p_die_lock) {
		aee_sram_printk("%s failed to get die_lock\n", __func__);
		return;
	}
	raw_spin_lock_init(p_die_lock);
}

const char *aee_arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}
EXPORT_SYMBOL(aee_arch_vma_name);

static void print_pstate(struct pt_regs *regs)
{
	u64 pstate = regs->pstate;

	if (compat_user_mode(regs)) {
		pr_info("pstate: %08llx (%c%c%c%c %c %s %s %c%c%c)\n",
			pstate,
			pstate & PSR_AA32_N_BIT ? 'N' : 'n',
			pstate & PSR_AA32_Z_BIT ? 'Z' : 'z',
			pstate & PSR_AA32_C_BIT ? 'C' : 'c',
			pstate & PSR_AA32_V_BIT ? 'V' : 'v',
			pstate & PSR_AA32_Q_BIT ? 'Q' : 'q',
			pstate & PSR_AA32_T_BIT ? "T32" : "A32",
			pstate & PSR_AA32_E_BIT ? "BE" : "LE",
			pstate & PSR_AA32_A_BIT ? 'A' : 'a',
			pstate & PSR_AA32_I_BIT ? 'I' : 'i',
			pstate & PSR_AA32_F_BIT ? 'F' : 'f');
	} else {
		pr_info("pstate: %08llx (%c%c%c%c %c%c%c%c %cPAN %cUAO)\n",
			pstate,
			pstate & PSR_N_BIT ? 'N' : 'n',
			pstate & PSR_Z_BIT ? 'Z' : 'z',
			pstate & PSR_C_BIT ? 'C' : 'c',
			pstate & PSR_V_BIT ? 'V' : 'v',
			pstate & PSR_D_BIT ? 'D' : 'd',
			pstate & PSR_A_BIT ? 'A' : 'a',
			pstate & PSR_I_BIT ? 'I' : 'i',
			pstate & PSR_F_BIT ? 'F' : 'f',
			pstate & PSR_PAN_BIT ? '+' : '-',
			pstate & PSR_UAO_BIT ? '+' : '-');
	}
}

#define MEM_FMT "%04lx: %08x %08x %08x %08x %08x %08x %08x %08x\n"
#define MEM_RANGE (128)
static void show_data(unsigned long addr, int nbytes, const char *name)
{
	int i, j, invalid, nlines;
	u32 *p, data[8] = {0};

	if (addr < (UL(0xffffffffffffffff) - (UL(1) << VA_BITS) + 1) ||
			addr > UL(0xFFFFFFFFFFFFF000))
		return;

	pr_info("%s: %#lx:\n", name, addr);
	addr -= MEM_RANGE;
	p = (u32 *)(addr & (~0xfUL));
	nbytes += (addr & 0xf);
	nlines = (nbytes + 31) / 32;
	for (i = 0; i < nlines; i++, p += 8) {
		for (j = invalid = 0; j < 8; j++) {
			if (copy_from_kernel_nofault(&data[j], &p[j],
						     sizeof(data[0]))) {
				data[j] = 0x12345678;
				invalid++;
			}
		}
		if (invalid != 8)
			pr_info(MEM_FMT, (unsigned long)p & 0xffff,
				data[0], data[1], data[2], data[3],
				data[4], data[5], data[6], data[7]);
	}
}

void aee_show_regs(struct pt_regs *regs)
{
	int i, top_reg;
	u64 lr, sp;

	if (compat_user_mode(regs)) {
		lr = regs->compat_lr;
		sp = regs->compat_sp;
		top_reg = 12;
	} else {
		lr = regs->regs[30];
		sp = regs->sp;
		top_reg = 29;
	}

	print_pstate(regs);

	if (!user_mode(regs)) {
		pr_info("pc : [0x%llx] %pS\n", regs->pc, (void *)regs->pc);
		pr_info("lr : [0x%llx] %pS\n", lr, (void *)lr);
	} else {
		pr_info("pc : %016llx\n", regs->pc);
		pr_info("lr : %016llx\n", lr);
	}

	pr_info("sp : %016llx\n", sp);

	if (system_uses_irq_prio_masking())
		pr_info("pmr_save: %08llx\n", regs->pmr_save);

	i = top_reg;

	while (i >= 1) {
		pr_info("x%-2d: %016llx x%-2d: %016llx\n",
			i, regs->regs[i], i - 1, regs->regs[i - 1]);
		i -= 2;
	}
	if (!user_mode(regs)) {
		unsigned int i;

		show_data(regs->pc, MEM_RANGE * 2, "PC");
		show_data(regs->regs[30], MEM_RANGE * 2, "LR");
		show_data(regs->sp, MEM_RANGE * 2, "SP");
		for (i = 0; i < 30; i++) {
			char name[4];

			if (snprintf(name, sizeof(name), "X%u", i) > 0)
				show_data(regs->regs[i], MEM_RANGE * 2, name);
		}
	}
	pr_info("\n");
}

#define TRYLOCK_NUM 10

static long long nsec_high(unsigned long long nsec)
{
	if ((long long)nsec < 0) {
		nsec = -nsec;
		do_div(nsec, 1000000);
		return -nsec;
	}
	do_div(nsec, 1000000);

	return nsec;
}

static unsigned long nsec_low(unsigned long long nsec)
{
	if ((long long)nsec < 0)
		nsec = -nsec;

	return do_div(nsec, 1000000);
}

#define SPLIT_NS(x) nsec_high(x), nsec_low(x)

static char group_path[PATH_MAX];

static char *task_group_path(struct task_group *tg)
{
	cgroup_path(tg->css.cgroup, group_path, PATH_MAX);

	return group_path;
}

static DEFINE_SPINLOCK(sched_debug_lock);

static char print_at_AEE_buffer[160];

#define SEQ_printf_at_AEE(m, x...)		\
do {						\
	snprintf(print_at_AEE_buffer, sizeof(print_at_AEE_buffer), x);	\
	aee_sram_fiq_log(print_at_AEE_buffer);	\
} while (0)

static void
print_task_at_AEE(struct seq_file *m, struct rq *rq, struct task_struct *p)
{
	SEQ_printf_at_AEE(m, "%c%15s %5d %9lld.%06ld %9lld ",
		rq->curr == p ? 'R' : ' ',
		p->comm,
		task_pid_nr(p),
		SPLIT_NS(p->se.vruntime),
		(long long)(p->nvcsw + p->nivcsw));

	SEQ_printf_at_AEE(m, "%5d ", p->prio);

	SEQ_printf_at_AEE(m, "%9lld.%06ld %9lld.%06ld %9lld.%06ld ",
		SPLIT_NS(p->stats.wait_sum),
		SPLIT_NS(p->se.sum_exec_runtime),
		SPLIT_NS(p->stats.sum_sleep_runtime));

	SEQ_printf_at_AEE(m, "%s\n", task_group_path(task_group(p)));
}

static int spin_trylock_n_irqsave(spinlock_t *lock,
		unsigned long *flags, struct seq_file *m, char *msg)
{
	int locked, trylock_cnt = 0;

	do {
		locked = spin_trylock_irqsave(lock, *flags);
		trylock_cnt++;
		mdelay(10);

	} while ((!locked) && (trylock_cnt < TRYLOCK_NUM));

	if (!locked)
		SEQ_printf_at_AEE(m, "Warning: fail to get lock in %s\n", msg);

	return locked;
}

static void print_rq_at_AEE(struct seq_file *m, struct rq *rq, int rq_cpu)
{
	struct task_struct *g, *p;

	SEQ_printf_at_AEE(m, "\n");
	SEQ_printf_at_AEE(m, "runnable tasks:\n");
	SEQ_printf_at_AEE(m,
	"            task   PID         tree-key  switches  prio        wait-time         sum-exec        sum-sleep\n");
	SEQ_printf_at_AEE(m, "---------------------------------------------------\n");

	rcu_read_lock();
	for_each_process_thread(g, p) {
		if (!p->on_rq || task_cpu(p) != rq_cpu)
			continue;

		print_task_at_AEE(m, rq, p);
	}
	rcu_read_unlock();
}

static void print_cfs_group_stats_at_AEE(struct seq_file *m,
		int cpu, struct task_group *tg)
{
	struct sched_entity *se = tg->se[cpu];

#define P(F)		SEQ_printf_at_AEE(m, "  .%-30s: %lld\n",	#F, (long long)F)
#define PN(F)		SEQ_printf_at_AEE(m, "  .%-30s: %lld.%06lu\n", #F, SPLIT_NS((long long)F))

	if (!se)
		return;

	PN(se->exec_start);
	PN(se->vruntime);
	PN(se->sum_exec_runtime);
	P(se->load.weight);
	P(se->runnable_weight);
	P(se->avg.load_avg);
	P(se->avg.util_avg);
#undef PN
#undef P
}

static void print_cfs_rq_at_AEE(struct seq_file *m, int cpu, struct cfs_rq *cfs_rq)
{
	SEQ_printf_at_AEE(m, "\n");
	SEQ_printf_at_AEE(m, "cfs_rq[%d]:%s\n", cpu, task_group_path(cfs_rq->tg));

	SEQ_printf_at_AEE(m, "  .%-30s: %d\n",
			"nr_running", cfs_rq->nr_running);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", "load", cfs_rq->load.weight);
	SEQ_printf_at_AEE(m, "  .%-30s: %lu\n", "load_avg",
			cfs_rq->avg.load_avg);
	SEQ_printf_at_AEE(m, "  .%-30s: %lu\n", "util_avg",
			cfs_rq->avg.util_avg);
	SEQ_printf_at_AEE(m, "  .%-30s: %u\n", "util_est",
			cfs_rq->avg.util_est);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", "removed.load_avg",
			cfs_rq->removed.load_avg);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", "removed.util_avg",
			cfs_rq->removed.util_avg);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", "removed.runnable_avg",
			cfs_rq->removed.runnable_avg);
	SEQ_printf_at_AEE(m, "  .%-30s: %lu\n", "tg_load_avg_contrib",
			cfs_rq->tg_load_avg_contrib);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", "tg_load_avg",
			atomic_long_read(&cfs_rq->tg->load_avg));
	SEQ_printf_at_AEE(m, "  .%-30s: %d\n", "throttled",
			cfs_rq->throttled);
	SEQ_printf_at_AEE(m, "  .%-30s: %d\n", "throttle_count",
			cfs_rq->throttle_count);

	print_cfs_group_stats_at_AEE(m, cpu, cfs_rq->tg);
}

static void print_cfs_stats_at_AEE(struct seq_file *m, int cpu)
{
	rcu_read_lock();
	print_cfs_rq_at_AEE(m, cpu, &cpu_rq(cpu)->cfs);
	rcu_read_unlock();
}

static void print_rt_stats_at_AEE(struct seq_file *m, int cpu)
{
	struct rt_rq *rt_rq = &cpu_rq(cpu)->rt;

	SEQ_printf_at_AEE(m, "\n");
	SEQ_printf_at_AEE(m, "rt_rq[%d]:\n", cpu);
	SEQ_printf_at_AEE(m, "  .%-30s: %lld\n", "rt_nr_running",
			(long long)rt_rq->rt_nr_running);
}

static void print_dl_stats_at_AEE(struct seq_file *m, int cpu)
{
	struct dl_rq *dl_rq = &cpu_rq(cpu)->dl;
	struct dl_bw *dl_bw = &cpu_rq(cpu)->rd->dl_bw;

	SEQ_printf_at_AEE(m, "\n");
	SEQ_printf_at_AEE(m, "dl_rq[%d]:\n", cpu);
	SEQ_printf_at_AEE(m, "  .%-30s: %lu\n", "dl_nr_running",
			(unsigned long)dl_rq->dl_nr_running);
	SEQ_printf_at_AEE(m, "  .%-30s: %lld\n", "dl_bw->bw", dl_bw->bw);
	SEQ_printf_at_AEE(m, "  .%-30s: %lld\n", "dl_bw->total_bw", dl_bw->total_bw);
}

static void print_cpu_at_AEE(struct seq_file *m, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;
	int locked;

	SEQ_printf_at_AEE(m, "cpu#%d: %s\n", cpu,
			cpu_is_offline(cpu) ? "Offline" : "Online");

#define P(x) \
do { \
	if (sizeof(rq->x) == 4) \
		SEQ_printf_at_AEE(m, "  .%-30s: %ld\n", \
		#x, (long)(rq->x)); \
	else \
		SEQ_printf_at_AEE(m, "  .%-30s: %lld\n", \
		#x, (long long)(rq->x)); \
} while (0)

#define PN(x) \
	SEQ_printf_at_AEE(m, "  .%-30s: %lld.%06lu\n", #x, SPLIT_NS(rq->x))

	P(nr_running);
	P(nr_switches);
	P(nr_uninterruptible);
	PN(next_balance);
	SEQ_printf_at_AEE(m, "  .%-30s: %ld\n",
			"curr->pid", (long)(task_pid_nr(rq->curr)));
	PN(clock);
	PN(clock_task);
#undef P
#undef PN

#define P64(n) SEQ_printf_at_AEE(m, "  .%-30s: %lld\n", #n, rq->n)
	P64(avg_idle);
	P64(max_idle_balance_cost);
#undef P64

	locked = spin_trylock_n_irqsave(&sched_debug_lock,
			&flags, m, "print_cpu_at_AEE");
	print_cfs_stats_at_AEE(m, cpu);
	print_rt_stats_at_AEE(m, cpu);
	print_dl_stats_at_AEE(m, cpu);

	rcu_read_lock();
	print_rq_at_AEE(m, rq, cpu);
	SEQ_printf_at_AEE(m, "============================================\n");
	rcu_read_unlock();

	if (locked)
		spin_unlock_irqrestore(&sched_debug_lock, flags);
}

static void sched_debug_header_at_AEE(struct seq_file *m)
{
	u64 ktime, sched_clk, cpu_clk;
	unsigned long flags;

	local_irq_save(flags);
	ktime = ktime_to_ns(ktime_get());
	sched_clk = sched_clock();
	cpu_clk = local_clock();
	local_irq_restore(flags);

	SEQ_printf_at_AEE(m, "Sched Debug Version: v0.11, %s %.*s\n",
		init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);

#define P(x) \
	SEQ_printf_at_AEE(m, "%-40s: %lld\n", #x, (long long)(x))
#define PN(x) \
	SEQ_printf_at_AEE(m, "%-40s: %lld.%06lu\n", #x, SPLIT_NS(x))
	PN(ktime);
	PN(sched_clk);
	PN(cpu_clk);
	P(jiffies);
#undef PN
#undef P

	SEQ_printf_at_AEE(m, "\n");
}

void sysrq_sched_debug_show_at_AEE(void)
{
	int cpu;

	sched_debug_header_at_AEE(NULL);
	rcu_read_lock();
	for_each_possible_cpu(cpu) {
		print_cpu_at_AEE(NULL, cpu);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(sysrq_sched_debug_show_at_AEE);
