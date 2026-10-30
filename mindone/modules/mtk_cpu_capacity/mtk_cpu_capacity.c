// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 MediaTek Inc. */
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/arch_topology.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/energy_model.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "mtk_cpu_capacity.h"

#define MAX_PD_COUNT		3
#define CAPACITY_TBL_SIZE	0x100
#define CAPACITY_ENTRY_SIZE	0x2

struct pd_capacity_info {
	int nr_caps;
	unsigned int *util_opp;
	unsigned int *util_freq;
	unsigned long *caps;
	struct cpumask cpus;
};

static struct pd_capacity_info pd_capacity_tbl[MAX_PD_COUNT];
static int pd_count;
static struct proc_dir_entry *capacity_dir;

unsigned long pd_get_opp_capacity(int cpu, int opp)
{
	struct pd_capacity_info *info;
	int i;

	if (cpu < 0 || cpu >= nr_cpu_ids)
		return 0;

	for (i = 0; i < pd_count; i++) {
		info = &pd_capacity_tbl[i];
		if (!cpumask_test_cpu(cpu, &info->cpus))
			continue;
		if (opp < 0 || opp >= info->nr_caps)
			return info->caps[0];
		return info->caps[opp];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pd_get_opp_capacity);

static void free_capacity_table(void)
{
	int i;

	for (i = 0; i < MAX_PD_COUNT; i++) {
		kfree(pd_capacity_tbl[i].caps);
		kfree(pd_capacity_tbl[i].util_opp);
		kfree(pd_capacity_tbl[i].util_freq);
	}
}

static int init_capacity_table(void __iomem *base)
{
	struct pd_capacity_info *info;
	struct em_perf_domain *pd;
	struct em_perf_state *states;
	unsigned long offset = 0;
	unsigned long cap;
	int cpu, j;
	long k, next;

	for_each_possible_cpu(cpu) {
		pd = em_cpu_get(cpu);
		if (!pd)
			return -ENODEV;
		if (cpu != cpumask_first(to_cpumask(pd->cpus)))
			continue;
		if (pd_count == MAX_PD_COUNT || pd->nr_perf_states <= 0)
			return -EINVAL;
		if ((unsigned long)pd->nr_perf_states >=
		    (CAPACITY_TBL_SIZE - offset) / CAPACITY_ENTRY_SIZE)
			return -EINVAL;

		info = &pd_capacity_tbl[pd_count++];
		info->nr_caps = pd->nr_perf_states;
		cpumask_copy(&info->cpus, to_cpumask(pd->cpus));
		info->caps = kcalloc(info->nr_caps, sizeof(*info->caps),
				     GFP_KERNEL);
		if (!info->caps)
			return -ENOMEM;

		for (j = 0; j < info->nr_caps; j++) {
			cap = ioread16(base + offset);
			if (!cap || (j && cap > info->caps[j - 1]))
				return -EINVAL;
			info->caps[j] = cap;
			offset += CAPACITY_ENTRY_SIZE;
		}
		if (ioread16(base + offset) != info->caps[info->nr_caps - 1])
			return -EINVAL;
		offset += CAPACITY_ENTRY_SIZE;

		info->util_opp = kcalloc(info->caps[0] + 1,
					sizeof(*info->util_opp), GFP_KERNEL);
		info->util_freq = kcalloc(info->caps[0] + 1,
					 sizeof(*info->util_freq), GFP_KERNEL);
		if (!info->util_opp || !info->util_freq)
			return -ENOMEM;

		rcu_read_lock();
		states = em_perf_state_from_pd(pd);
		for (j = 0; j < info->nr_caps; j++) {
			next = -1;
			if (j < info->nr_caps - 1)
				next = info->caps[j + 1];
			for (k = info->caps[j]; k > next; k--) {
				info->util_opp[k] = j;
				info->util_freq[k] =
					states[info->nr_caps - j - 1].frequency;
			}
		}
		rcu_read_unlock();
	}

	return pd_count ? 0 : -ENODEV;
}

static int pd_capacity_tbl_show(struct seq_file *m, void *v)
{
	struct pd_capacity_info *info;
	int i, j;

	for (i = 0; i < pd_count; i++) {
		info = &pd_capacity_tbl[i];
		seq_printf(m, "Pd table: %d\n", i);
		seq_printf(m, "cpus: %*pbl\n", cpumask_pr_args(&info->cpus));
		seq_printf(m, "nr_caps: %d\n", info->nr_caps);
		for (j = 0; j < info->nr_caps; j++)
			seq_printf(m, "%d: %lu, %u\n", j, info->caps[j],
				   info->util_freq[info->caps[j]]);
	}

	return 0;
}

static int pd_capacity_tbl_open(struct inode *inode, struct file *file)
{
	return single_open(file, pd_capacity_tbl_show, NULL);
}

static const struct proc_ops pd_capacity_tbl_ops = {
	.proc_open = pd_capacity_tbl_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init mtk_cpu_capacity_init(void)
{
	struct device_node *node;
	void __iomem *base;
	u32 csram, offset, thermal_offset;
	int ret, i, cpu;

	node = of_find_node_by_name(NULL, "eas_info");
	if (!node)
		return -ENODEV;
	ret = of_property_read_u32(node, "csram-base", &csram);
	ret |= of_property_read_u32(node, "offs-cap", &offset);
	ret |= of_property_read_u32_index(node, "offs-thermal-limit", 0,
					  &thermal_offset);
	of_node_put(node);
	if (ret)
		return -EINVAL;

	base = ioremap((phys_addr_t)csram + offset, CAPACITY_TBL_SIZE);
	if (!base)
		return -ENOMEM;
	ret = init_capacity_table(base);
	iounmap(base);
	if (ret)
		goto free_table;

	capacity_dir = proc_mkdir("mtk_scheduler", NULL);
	if (!capacity_dir) {
		ret = -ENOMEM;
		goto free_table;
	}
	if (!proc_create("pd_capacity_tbl", 0644, capacity_dir,
			 &pd_capacity_tbl_ops)) {
		ret = -ENOMEM;
		goto remove_proc;
	}

	for (i = 0; i < pd_count; i++) {
		for_each_cpu(cpu, &pd_capacity_tbl[i].cpus) {
			per_cpu(cpu_scale, cpu) = pd_capacity_tbl[i].caps[0];
			pr_info("mtk_cpu_capacity: cpu%d capacity=%lu\n", cpu,
				pd_capacity_tbl[i].caps[0]);
		}
	}

	return 0;

remove_proc:
	proc_remove(capacity_dir);
free_table:
	free_capacity_table();
	return ret;
}

static void __exit mtk_cpu_capacity_exit(void)
{
	proc_remove(capacity_dir);
	free_capacity_table();
}

module_init(mtk_cpu_capacity_init);
module_exit(mtk_cpu_capacity_exit);

MODULE_DESCRIPTION("MediaTek SRAM CPU capacity information");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: mediatek_cpufreq_hw");
