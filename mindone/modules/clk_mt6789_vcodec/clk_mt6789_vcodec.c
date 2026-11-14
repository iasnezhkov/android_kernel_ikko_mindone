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

static const struct mtk_gate_regs vde20_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x4,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs vde21_cg_regs = {
	.set_ofs = 0x190,
	.clr_ofs = 0x190,
	.sta_ofs = 0x190,
};

static const struct mtk_gate_regs vde22_cg_regs = {
	.set_ofs = 0x200,
	.clr_ofs = 0x204,
	.sta_ofs = 0x200,
};

static const struct mtk_gate_regs vde23_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x8,
};

MT6789_PWR_GATE_OPS(vde2_setclr_inv_ops, mtk_clk_gate_ops_setclr_inv,
		    MT6789_PWR_DISP | MT6789_PWR_VDEC);

MT6789_PWR_GATE_OPS(vde2_no_setclr_inv_ops, mtk_clk_gate_ops_no_setclr_inv,
		    MT6789_PWR_DISP | MT6789_PWR_VDEC);

#define GATE_VDE20(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &vde20_cg_regs, _shift,	\
		       &vde2_setclr_inv_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_VDE21(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &vde21_cg_regs, _shift,	\
		       &vde2_no_setclr_inv_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_VDE22(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &vde22_cg_regs, _shift,	\
		       &vde2_setclr_inv_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_VDE23(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &vde23_cg_regs, _shift,	\
		       &vde2_setclr_inv_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate vde2_clks[] = {
	GATE_VDE20(CLK_VDE2_VDEC_CKEN, "vde2_vdec_cken", "vdec_ck", 0),
	GATE_VDE20(CLK_VDE2_VDEC_ACTIVE, "vde2_vdec_active", "vdec_ck", 4),
	GATE_VDE20(CLK_VDE2_VDEC_CKEN_ENG, "vde2_vdec_cken_eng", "vdec_ck", 8),
	GATE_VDE21(CLK_VDE2_MINI_MDP_CKEN_CFG_RG, "vde2_mini_mdp", "vdec_ck", 0),
	GATE_VDE22(CLK_VDE2_LAT_CKEN, "vde2_lat_cken", "vdec_ck", 0),
	GATE_VDE22(CLK_VDE2_LAT_ACTIVE, "vde2_lat_active", "vdec_ck", 4),
	GATE_VDE22(CLK_VDE2_LAT_CKEN_ENG, "vde2_lat_cken_eng", "vdec_ck", 8),
	GATE_VDE23(CLK_VDE2_LARB1_CKEN, "vde2_larb1_cken", "vdec_ck", 0),
};

static const struct mtk_clk_desc vde2_desc = {
	.clks = vde2_clks,
	.num_clks = ARRAY_SIZE(vde2_clks),
};

static const struct mtk_gate_regs ven1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(ven1_setclr_inv_ops, mtk_clk_gate_ops_setclr_inv,
		    MT6789_PWR_DISP | MT6789_PWR_VENC);

#define GATE_VEN1(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &ven1_cg_regs, _shift,	\
		       &ven1_setclr_inv_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate ven1_clks[] = {
	GATE_VEN1(CLK_VEN1_CKE0_LARB, "ven1_cke0_larb", "venc_ck", 0),
	GATE_VEN1(CLK_VEN1_CKE1_VENC, "ven1_cke1_venc", "venc_ck", 4),
	GATE_VEN1(CLK_VEN1_CKE2_JPGENC, "ven1_cke2_jpgenc", "venc_ck", 8),
	GATE_VEN1(CLK_VEN1_CKE5_GALS, "ven1_cke5_gals", "venc_ck", 28),
};

static const struct mtk_clk_desc ven1_desc = {
	.clks = ven1_clks,
	.num_clks = ARRAY_SIZE(ven1_clks),
};

static const struct of_device_id of_match_clk_mt6789_vcodec[] = {
	{ .compatible = "mediatek,mt6789-vdecsys", .data = &vde2_desc },
	{ .compatible = "mediatek,mt6789-vencsys", .data = &ven1_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_vcodec);

static int clk_mt6789_vcodec_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_vcodec_drv = {
	.probe = clk_mt6789_vcodec_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-vcodec",
		.of_match_table = of_match_clk_mt6789_vcodec,
	},
};
module_platform_driver(clk_mt6789_vcodec_drv);

MODULE_DESCRIPTION("MediaTek MT6789 vdecsys, vencsys clocks");
MODULE_LICENSE("GPL");
