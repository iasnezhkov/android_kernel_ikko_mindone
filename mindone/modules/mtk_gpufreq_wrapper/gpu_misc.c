// SPDX-License-Identifier: GPL-2.0
#include <gpufreq_v2.h>
#include <gpu_misc.h>

#ifndef CREATE_TRACE_POINTS
#define CREATE_TRACE_POINTS
#endif
#include <gpu_hardstop.h>

void gpufreq_hardstop_dump_slog(void)
{

#if IS_ENABLED(CONFIG_MTK_AEE_FEATURE)
	unsigned int gpu_freq;
	unsigned int gpu_volt;
	unsigned int gpu_vsram;
	unsigned int stack_freq;
	unsigned int stack_volt;
	unsigned int stack_vsram;

	gpu_freq = gpufreq_get_cur_freq(TARGET_GPU);
	gpu_volt = gpufreq_get_cur_volt(TARGET_GPU);
	gpu_vsram = gpufreq_get_cur_vsram(TARGET_GPU);
	stack_freq = gpufreq_get_cur_freq(TARGET_STACK);
	stack_volt = gpufreq_get_cur_volt(TARGET_STACK);
	stack_vsram = gpufreq_get_cur_vsram(TARGET_STACK);

	trace_gpu_hardstop("gpuexp", "hs",
		gpu_freq,
		gpu_volt,
		gpu_vsram,
		stack_freq,
		stack_volt,
		stack_vsram);
#endif
}
EXPORT_SYMBOL(gpufreq_hardstop_dump_slog);
