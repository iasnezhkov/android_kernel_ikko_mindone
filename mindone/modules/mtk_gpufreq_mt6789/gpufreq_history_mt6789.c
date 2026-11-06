// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 */
#include <linux/sched/clock.h>
#include <linux/string.h>
#include <linux/io.h>

#include <gpufreq_v2_legacy.h>
#include <gpufreq_history_common.h>
#include <gpufreq_history_mt6789.h>
#include <gpuppm_legacy.h>
#include <gpufreq_common_legacy.h>

static int g_history_target_opp_stack;
static int g_history_target_opp_top;

void gpufreq_set_history_target_opp(enum gpufreq_target target, int oppidx)
{
	if (target == TARGET_STACK)
		g_history_target_opp_stack = oppidx;
	else
		g_history_target_opp_top = oppidx;
}

int gpufreq_get_history_target_opp(enum gpufreq_target target)
{
	if (target == TARGET_STACK)
		return g_history_target_opp_stack;
	else
		return g_history_target_opp_top;
}
