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

static const struct mtk_gate_regs cam_m_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(cam_m_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP | MT6789_PWR_CAM);

#define GATE_CAM_M(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &cam_m_cg_regs, _shift,	\
		       &cam_m_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate cam_m_clks[] = {
	GATE_CAM_M(CLK_CAM_M_LARB13, "cam_m_larb13", "cam_ck", 0),
	GATE_CAM_M(CLK_CAM_M_LARB14, "cam_m_larb14", "cam_ck", 2),
	GATE_CAM_M(CLK_CAM_M_CAM, "cam_m_cam", "cam_ck", 6),
	GATE_CAM_M(CLK_CAM_M_CAMTG, "cam_m_camtg", "cam_ck", 7),
	GATE_CAM_M(CLK_CAM_M_SENINF, "cam_m_seninf", "cam_ck", 8),
	GATE_CAM_M(CLK_CAM_M_CAMSV1, "cam_m_camsv1", "cam_ck", 10),
	GATE_CAM_M(CLK_CAM_M_CAMSV2, "cam_m_camsv2", "cam_ck", 11),
	GATE_CAM_M(CLK_CAM_M_CAMSV3, "cam_m_camsv3", "cam_ck", 12),
	GATE_CAM_M(CLK_CAM_M_MRAW0, "cam_m_mraw0", "cam_ck", 15),
	GATE_CAM_M(CLK_CAM_M_FAKE_ENG, "cam_m_fake_eng", "cam_ck", 17),
	GATE_CAM_M(CLK_CAM_M_CAM2MM_GALS, "cam_m_cam2mm_gals", "cam_ck", 19),
};

static const struct mtk_clk_desc cam_m_desc = {
	.clks = cam_m_clks,
	.num_clks = ARRAY_SIZE(cam_m_clks),
};

static const struct mtk_gate_regs cam_ra_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(cam_ra_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP | MT6789_PWR_CAM | MT6789_PWR_CAM_RAWA);

#define GATE_CAM_RA(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &cam_ra_cg_regs, _shift,	\
		       &cam_ra_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate cam_ra_clks[] = {
	GATE_CAM_RA(CLK_CAM_RA_LARBX, "cam_ra_larbx", "cam_ck", 0),
	GATE_CAM_RA(CLK_CAM_RA_CAM, "cam_ra_cam", "cam_ck", 1),
	GATE_CAM_RA(CLK_CAM_RA_CAMTG, "cam_ra_camtg", "camtm_ck", 2),
};

static const struct mtk_clk_desc cam_ra_desc = {
	.clks = cam_ra_clks,
	.num_clks = ARRAY_SIZE(cam_ra_clks),
};

static const struct mtk_gate_regs cam_rb_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

MT6789_PWR_GATE_OPS(cam_rb_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP | MT6789_PWR_CAM | MT6789_PWR_CAM_RAWB);

#define GATE_CAM_RB(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &cam_rb_cg_regs, _shift,	\
		       &cam_rb_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate cam_rb_clks[] = {
	GATE_CAM_RB(CLK_CAM_RB_LARBX, "cam_rb_larbx", "cam_ck", 0),
	GATE_CAM_RB(CLK_CAM_RB_CAM, "cam_rb_cam", "cam_ck", 1),
	GATE_CAM_RB(CLK_CAM_RB_CAMTG, "cam_rb_camtg", "camtm_ck", 2),
};

static const struct mtk_clk_desc cam_rb_desc = {
	.clks = cam_rb_clks,
	.num_clks = ARRAY_SIZE(cam_rb_clks),
};

static const struct of_device_id of_match_clk_mt6789_cam[] = {
	{ .compatible = "mediatek,mt6789-camsys_main", .data = &cam_m_desc },
	{ .compatible = "mediatek,mt6789-camsys_rawa", .data = &cam_ra_desc },
	{ .compatible = "mediatek,mt6789-camsys_rawb", .data = &cam_rb_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_cam);

static int clk_mt6789_cam_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_cam_drv = {
	.probe = clk_mt6789_cam_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-cam",
		.of_match_table = of_match_clk_mt6789_cam,
	},
};
module_platform_driver(clk_mt6789_cam_drv);

MODULE_DESCRIPTION("MediaTek MT6789 camsys_main, camsys_rawa, camsys_rawb clocks");
MODULE_LICENSE("GPL");
