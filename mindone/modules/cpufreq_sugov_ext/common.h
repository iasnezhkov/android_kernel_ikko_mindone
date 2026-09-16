/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#ifndef _SCHED_COMMON_H
#define _SCHED_COMMON_H

#if IS_ENABLED(CONFIG_NONLINEAR_FREQ_CTL)
extern void mtk_map_util_freq(void *data, unsigned long util, unsigned long freq,
			struct cpumask *cpumask, unsigned long *next_freq);
#else
#define mtk_map_util_freq(data, util, freq, cap, next_freq)
#endif /* CONFIG_NONLINEAR_FREQ_CTL */

/* util classes of mtk_cpu_util(); the kernel enum of the same meaning went away in 6.6 */
enum mtk_cpu_util_type {
	MTK_FREQUENCY_UTIL,
	MTK_ENERGY_UTIL,
};

#if IS_ENABLED(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
unsigned long mtk_cpu_util(int cpu, unsigned long util_cfs,
				unsigned long max, enum mtk_cpu_util_type type,
				struct task_struct *p);
#endif
__always_inline
unsigned long mtk_uclamp_rq_util_with(struct rq *rq, unsigned long util,
				  struct task_struct *p);

#define EAS_NODE_NAME "eas_info"
#define EAS_PROP_CSRAM "csram-base"
#define EAS_PROP_OFFS_CAP "offs-cap"
#define EAS_PROP_OFFS_THERMAL_S "offs-thermal-limit"

struct eas_info {
	unsigned int csram_base;
	unsigned int offs_cap;
	unsigned int offs_thermal_limit_s;
	bool available;
};

void parse_eas_data(struct eas_info *info);

#endif /* _SCHED_COMMON_H */
