/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef __CLK_MT6789_PWR_H
#define __CLK_MT6789_PWR_H

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/types.h>

struct device;

#define MT6789_PWR_MFG0		BIT(0)
#define MT6789_PWR_MFG1		BIT(1)
#define MT6789_PWR_MFG3		BIT(2)
#define MT6789_PWR_ISP		BIT(3)
#define MT6789_PWR_IPE		BIT(4)
#define MT6789_PWR_VDEC		BIT(5)
#define MT6789_PWR_VENC		BIT(6)
#define MT6789_PWR_DISP		BIT(7)
#define MT6789_PWR_AUDIO	BIT(8)
#define MT6789_PWR_CAM		BIT(9)
#define MT6789_PWR_CAM_RAWA	BIT(10)
#define MT6789_PWR_CAM_RAWB	BIT(11)

void mt6789_clk_pwr_init(struct device *dev);
void mt6789_clk_pwr_exit(void);
bool mt6789_clk_pwr_up(u32 domains);

#define MT6789_PWR_GATE_OPS(_name, _base, _domains)			\
static int _name##_is_enabled(struct clk_hw *hw)			\
{									\
	return mt6789_clk_pwr_up(_domains) && (_base).is_enabled(hw);	\
}									\
									\
static int _name##_enable(struct clk_hw *hw)				\
{									\
	return (_base).enable(hw);					\
}									\
									\
static void _name##_disable(struct clk_hw *hw)				\
{									\
	(_base).disable(hw);						\
}									\
									\
static const struct clk_ops _name = {					\
	.is_enabled = _name##_is_enabled,				\
	.enable = _name##_enable,					\
	.disable = _name##_disable,					\
}

#endif
