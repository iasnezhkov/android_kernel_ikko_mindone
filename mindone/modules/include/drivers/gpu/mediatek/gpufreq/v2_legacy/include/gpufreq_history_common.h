/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */
#ifndef __GPUFREQ_HISTORY_COMMON_H__
#define __GPUFREQ_HISTORY_COMMON_H__

#define GPUFREQ_HISTORY_ENABLE            (0)

enum gpufreq_history_state {
	HISTORY_FREE = 0,
	HISTORY_VOLT_PARK = 1 << 0,
	HISTORY_VSRAM_PARK = 1 << 1,
	HISTORY_PTP3 = 1 << 2,
	HISTORY_CHANGE_VOLT_TOP = 1 << 3,
	HISTORY_CHANGE_FREQ_TOP = 1 << 4,
	HISTORY_CHANGE_VOLT_STACK = 1 << 5,
	HISTORY_CHANGE_FREQ_STACK = 1 << 6,
	HISTORY_DEBUG = 1 << 7,
};

void gpufreq_set_history_target_opp(enum gpufreq_target target,
	int oppidx);
int gpufreq_get_history_target_opp(enum gpufreq_target target);
#endif
