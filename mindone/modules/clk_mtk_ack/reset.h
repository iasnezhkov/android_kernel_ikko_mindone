/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __DRV_CLK_MTK_RESET_H
#define __DRV_CLK_MTK_RESET_H

#include <linux/reset-controller.h>
#include <linux/types.h>

#define RST_NR_PER_BANK 32

#define INFRA_RST0_SET_OFFSET 0x120
#define INFRA_RST1_SET_OFFSET 0x130
#define INFRA_RST2_SET_OFFSET 0x140
#define INFRA_RST3_SET_OFFSET 0x150
#define INFRA_RST4_SET_OFFSET 0x730

enum mtk_reset_version {
	MTK_RST_SIMPLE = 0,
	MTK_RST_SET_CLR,
	MTK_RST_MAX,
};

struct mtk_clk_rst_desc {
	enum mtk_reset_version version;
	u16 *rst_bank_ofs;
	u32 rst_bank_nr;
	u16 *rst_idx_map;
	u32 rst_idx_map_nr;
};

struct mtk_clk_rst_data {
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
	const struct mtk_clk_rst_desc *desc;
};

int mtk_register_reset_controller_with_dev(struct device *dev,
					   const struct mtk_clk_rst_desc *desc);

#endif
