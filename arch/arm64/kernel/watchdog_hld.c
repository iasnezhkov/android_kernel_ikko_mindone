// SPDX-License-Identifier: GPL-2.0
/* MINDONE: this file didn't exist in the stock 6.1.175 baseline -- arm64
 * had no CONFIG_HARDLOCKUP_DETECTOR_PERF support at all. hw_nmi_get_sample_period()
 * below is upstream commit d7a0fe9ef6d6 ("arm64: enable perf events based
 * hard lockup detector") verbatim, needed because the generic detector
 * core calls it and no arch/arm64 definition existed. The
 * late_initcall_sync hook at the bottom is our own addition -- see
 * kernel/watchdog.c:lockup_detector_retry_init(). */
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/nmi.h>

/*
 * Safe maximum CPU frequency in case a particular platform doesn't implement
 * cpufreq driver. Although, architecture doesn't put any restrictions on
 * maximum frequency but 5 GHz seems to be safe maximum given the available
 * Arm CPUs in the market which are clocked much less than 5 GHz. On the other
 * hand, we can't make it much higher as it would lead to a large hard-lockup
 * detection timeout on parts which are running slower (eg. 1GHz on
 * Developerbox) and doesn't possess a cpufreq driver.
 */
#define SAFE_MAX_CPU_FREQ	5000000000UL // 5 GHz
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	unsigned int cpu = smp_processor_id();
	unsigned long max_cpu_freq;

	max_cpu_freq = cpufreq_get_hw_max_freq(cpu) * 1000UL;
	if (!max_cpu_freq)
		max_cpu_freq = SAFE_MAX_CPU_FREQ;

	return (u64)max_cpu_freq * watchdog_thresh;
}

/* mindone: see kernel/watchdog.c:lockup_detector_retry_init(). */
static int __init mindone_hardlockup_detector_retry(void)
{
	lockup_detector_retry_init();
	return 0;
}
late_initcall_sync(mindone_hardlockup_detector_retry);
