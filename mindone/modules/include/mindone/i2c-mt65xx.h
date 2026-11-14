/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 The LineageOS Project */
#ifndef MINDONE_I2C_MT65XX_H
#define MINDONE_I2C_MT65XX_H

#include <linux/types.h>

int mtk_i2c_set_slow_target(int nr, u16 addr, unsigned int hz);

#endif
