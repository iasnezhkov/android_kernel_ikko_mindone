// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_GPU_IPA_H__
#define __MTK_GPU_IPA_H__

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>

extern struct devfreq_cooling_power mtk_common_cooling_power_ops;

int mtk_common_get_real_power(struct devfreq *df,
                              u32 *power,
                              unsigned long freq /* Hz */,
                              unsigned long voltage /* mV */);


#endif /* __MTK_GPU_IPA_H__ */
