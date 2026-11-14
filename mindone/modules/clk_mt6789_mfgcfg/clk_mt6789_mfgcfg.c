// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/mt6789-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mt6789-pwr.h"

static const struct mtk_gate_regs mfgcfg_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(mfgcfg_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_MFG0 | MT6789_PWR_MFG1 | MT6789_PWR_MFG3);

#define GATE_MFGCFG(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mfgcfg_cg_regs, _shift,	\
		       &mfgcfg_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate mfgcfg_clks[] = {
	GATE_MFGCFG(CLK_MFGCFG_BG3D, "mfgcfg_bg3d", "mfg_pll_ck", 0),
};

static const struct mtk_clk_desc mfgcfg_desc = {
	.clks = mfgcfg_clks,
	.num_clks = ARRAY_SIZE(mfgcfg_clks),
};

static const struct of_device_id of_match_clk_mt6789_mfgcfg[] = {
	{ .compatible = "mediatek,mt6789-mfg", .data = &mfgcfg_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_mfgcfg);

static int clk_mt6789_mfgcfg_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_mfgcfg_drv = {
	.probe = clk_mt6789_mfgcfg_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-mfgcfg",
		.of_match_table = of_match_clk_mt6789_mfgcfg,
	},
};
module_platform_driver(clk_mt6789_mfgcfg_drv);

MODULE_DESCRIPTION("MediaTek MT6789 mfg clocks");
MODULE_LICENSE("GPL");
