/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_CPUFREQ_H
#define MINDONE_COMPAT_CPUFREQ_H

#include <linux/cpufreq.h>
#include <linux/version.h>

static inline int mindone_cpufreq_table_target(struct cpufreq_policy *policy,
					       unsigned int target_freq,
					       unsigned int relation)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return cpufreq_frequency_table_target(policy, target_freq, policy->min,
					      policy->max, relation);
#else
	return cpufreq_frequency_table_target(policy, target_freq, relation);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_CPUFREQ_GENERIC_ATTR
#define MINDONE_CPUFREQ_UNKNOWN_LATENCY_US (CPUFREQ_DEFAULT_TRANSITION_LATENCY_NS / NSEC_PER_USEC)
#else
#define MINDONE_CPUFREQ_GENERIC_ATTR .attr = cpufreq_generic_attr,
#define MINDONE_CPUFREQ_UNKNOWN_LATENCY_US CPUFREQ_ETERNAL
#endif

#endif
