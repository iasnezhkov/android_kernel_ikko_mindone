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

static const struct mtk_gate_regs imgsys1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(imgsys1_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP | MT6789_PWR_ISP);

#define GATE_IMGSYS1(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &imgsys1_cg_regs, _shift,	\
		       &imgsys1_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate imgsys1_clks[] = {
	GATE_IMGSYS1(CLK_IMGSYS1_LARB9, "imgsys1_larb9", "img1_ck", 0),
	GATE_IMGSYS1(CLK_IMGSYS1_LARB10, "imgsys1_larb10", "img1_ck", 1),
	GATE_IMGSYS1(CLK_IMGSYS1_DIP, "imgsys1_dip", "img1_ck", 2),
	GATE_IMGSYS1(CLK_IMGSYS1_GALS, "imgsys1_gals", "img1_ck", 12),
};

static const struct mtk_clk_desc imgsys1_desc = {
	.clks = imgsys1_clks,
	.num_clks = ARRAY_SIZE(imgsys1_clks),
};

static const struct mtk_gate_regs ipe_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(ipe_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP | MT6789_PWR_IPE);

#define GATE_IPE(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &ipe_cg_regs, _shift,	\
		       &ipe_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate ipe_clks[] = {
	GATE_IPE(CLK_IPE_LARB19, "ipe_larb19", "ipe_ck", 0),
	GATE_IPE(CLK_IPE_LARB20, "ipe_larb20", "ipe_ck", 1),
	GATE_IPE(CLK_IPE_SMI_SUBCOM, "ipe_smi_subcom", "ipe_ck", 2),
	GATE_IPE(CLK_IPE_FD, "ipe_fd", "ipe_ck", 3),
	GATE_IPE(CLK_IPE_FE, "ipe_fe", "ipe_ck", 4),
	GATE_IPE(CLK_IPE_RSC, "ipe_rsc", "ipe_ck", 5),
	GATE_IPE(CLK_IPE_DPE, "ipe_dpe", "ipe_ck", 6),
	GATE_IPE(CLK_IPE_GALS, "ipe_gals", "img1_ck", 8),
};

static const struct mtk_clk_desc ipe_desc = {
	.clks = ipe_clks,
	.num_clks = ARRAY_SIZE(ipe_clks),
};

static const struct of_device_id of_match_clk_mt6789_img[] = {
	{ .compatible = "mediatek,mt6789-imgsys1", .data = &imgsys1_desc },
	{ .compatible = "mediatek,mt6789-ipesys", .data = &ipe_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_img);

static int clk_mt6789_img_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_img_drv = {
	.probe = clk_mt6789_img_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-img",
		.of_match_table = of_match_clk_mt6789_img,
	},
};
module_platform_driver(clk_mt6789_img_drv);

MODULE_DESCRIPTION("MediaTek MT6789 imgsys1, ipesys clocks");
MODULE_LICENSE("GPL");
