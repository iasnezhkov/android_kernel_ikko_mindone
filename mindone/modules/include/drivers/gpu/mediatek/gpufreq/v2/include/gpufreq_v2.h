/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 */
#ifndef __GPUFREQ_V2_H__
#define __GPUFREQ_V2_H__

#include "../../v2_legacy/include/gpufreq_v2_legacy.h"

#define POWER_OFF GPU_PWR_OFF
#define POWER_ON GPU_PWR_ON

#define vaging margin
#define LIMIT_FPSGO LIMIT_POWERHAL
#define DVFS_DEBUG_KEEP DVFS_FIX_OPP

void gpufreq_pdc_control(enum gpufreq_power_state power);
void gpufreq_fake_spm_mtcmos_control(enum gpufreq_power_state power);

#endif
