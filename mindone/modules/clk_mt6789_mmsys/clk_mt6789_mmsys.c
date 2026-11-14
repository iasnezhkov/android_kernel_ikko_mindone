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

static const struct mtk_gate_regs mm0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mm1_cg_regs = {
	.set_ofs = 0x1a4,
	.clr_ofs = 0x1a8,
	.sta_ofs = 0x1a0,
};

MT6789_PWR_GATE_OPS(mm_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP);

#define GATE_MM0(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mm0_cg_regs, _shift,	\
		       &mm_setclr_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_MM1(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mm1_cg_regs, _shift,	\
		       &mm_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate mm_clks[] = {
	GATE_MM0(CLK_MM_DISP_MUTEX0, "mm_disp_mutex0", "disp_ck", 0),
	GATE_MM0(CLK_MM_APB_BUS, "mm_apb_bus", "disp_ck", 1),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "disp_ck", 2),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "disp_ck", 3),
	GATE_MM0(CLK_MM_DISP_OVL0_2L, "mm_disp_ovl0_2l", "disp_ck", 4),
	GATE_MM0(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "disp_ck", 5),
	GATE_MM0(CLK_MM_DISP_RSZ0, "mm_disp_rsz0", "disp_ck", 7),
	GATE_MM0(CLK_MM_DISP_AAL0, "mm_disp_aal0", "disp_ck", 8),
	GATE_MM0(CLK_MM_DISP_CCORR0, "mm_disp_ccorr0", "disp_ck", 9),
	GATE_MM0(CLK_MM_DISP_COLOR0, "mm_disp_color0", "disp_ck", 10),
	GATE_MM0(CLK_MM_SMI_INFRA, "mm_smi_infra", "disp_ck", 11),
	GATE_MM0(CLK_MM_DISP_DSC_WRAP0, "mm_disp_dsc_wrap0", "disp_ck", 12),
	GATE_MM0(CLK_MM_DISP_GAMMA0, "mm_disp_gamma0", "disp_ck", 13),
	GATE_MM0(CLK_MM_DISP_POSTMASK0, "mm_disp_postmask0", "disp_ck", 14),
	GATE_MM0(CLK_MM_DISP_DITHER0, "mm_disp_dither0", "disp_ck", 16),
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "disp_ck", 17),
	GATE_MM0(CLK_MM_DSI0, "mm_dsi0", "disp_ck", 19),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG0, "mm_disp_fake_eng0", "disp_ck", 20),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG1, "mm_disp_fake_eng1", "disp_ck", 21),
	GATE_MM0(CLK_MM_SMI_GALS, "mm_smi_gals", "disp_ck", 22),
	GATE_MM0(CLK_MM_SMI_IOMMU, "mm_smi_iommu", "disp_ck", 24),
	GATE_MM1(CLK_MM_DSI0_DSI_CK_DOMAIN, "mm_dsi0_dsi_domain", "dsi_occ_ck", 0),
	GATE_MM1(CLK_MM_DISP_26M, "mm_disp_26m_ck", "disp_ck", 10),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct mtk_gate_regs mdp0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mdp1_cg_regs = {
	.set_ofs = 0x124,
	.clr_ofs = 0x128,
	.sta_ofs = 0x120,
};

MT6789_PWR_GATE_OPS(mdp_setclr_ops, mtk_clk_gate_ops_setclr,
		    MT6789_PWR_DISP);

#define GATE_MDP0(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mdp0_cg_regs, _shift,	\
		       &mdp_setclr_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_MDP1(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mdp1_cg_regs, _shift,	\
		       &mdp_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate mdp_clks[] = {
	GATE_MDP0(CLK_MDP_RDMA0, "mdp_rdma0", "mdp_ck", 0),
	GATE_MDP0(CLK_MDP_TDSHP0, "mdp_tdshp0", "mdp_ck", 1),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC0, "mdp_img_dl_async0", "mdp_ck", 2),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC1, "mdp_img_dl_async1", "mdp_ck", 3),
	GATE_MDP0(CLK_MDP_SMI0, "mdp_smi0", "mdp_ck", 6),
	GATE_MDP0(CLK_MDP_APB_BUS, "mdp_apb_bus", "mdp_ck", 7),
	GATE_MDP0(CLK_MDP_WROT0, "mdp_wrot0", "mdp_ck", 8),
	GATE_MDP0(CLK_MDP_RSZ0, "mdp_rsz0", "mdp_ck", 9),
	GATE_MDP0(CLK_MDP_HDR0, "mdp_hdr0", "mdp_ck", 10),
	GATE_MDP0(CLK_MDP_MUTEX0, "mdp_mutex0", "mdp_ck", 11),
	GATE_MDP0(CLK_MDP_WROT1, "mdp_wrot1", "mdp_ck", 12),
	GATE_MDP0(CLK_MDP_RSZ1, "mdp_rsz1", "mdp_ck", 13),
	GATE_MDP0(CLK_MDP_FAKE_ENG0, "mdp_fake_eng0", "mdp_ck", 14),
	GATE_MDP0(CLK_MDP_AAL0, "mdp_aal0", "mdp_ck", 15),
	GATE_MDP1(CLK_MDP_IMG_DL_RELAY0_ASYNC0, "mdp_img_dl_rel0_as0", "mdp_ck", 0),
	GATE_MDP1(CLK_MDP_IMG_DL_RELAY1_ASYNC1, "mdp_img_dl_rel1_as1", "mdp_ck", 8),
};

static const struct mtk_clk_desc mdp_desc = {
	.clks = mdp_clks,
	.num_clks = ARRAY_SIZE(mdp_clks),
};

static const struct of_device_id of_match_clk_mt6789_mmsys[] = {
	{ .compatible = "mediatek,mt6789-mmsys", .data = &mm_desc },
	{ .compatible = "mediatek,mt6789-mdpsys", .data = &mdp_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_mmsys);

static int clk_mt6789_mmsys_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_mmsys_drv = {
	.probe = clk_mt6789_mmsys_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-mmsys",
		.of_match_table = of_match_clk_mt6789_mmsys,
	},
};
module_platform_driver(clk_mt6789_mmsys_drv);

MODULE_DESCRIPTION("MediaTek MT6789 mmsys, mdpsys clocks");
MODULE_LICENSE("GPL");
