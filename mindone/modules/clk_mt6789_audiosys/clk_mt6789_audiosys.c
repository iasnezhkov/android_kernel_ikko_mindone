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

static const struct mtk_gate_regs afe0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs afe1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x4,
	.sta_ofs = 0x4,
};

MT6789_PWR_GATE_OPS(afe_no_setclr_ops, mtk_clk_gate_ops_no_setclr,
		    MT6789_PWR_AUDIO);

#define GATE_AFE0(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &afe0_cg_regs, _shift,	\
		       &afe_no_setclr_ops, CLK_OPS_PARENT_ENABLE)

#define GATE_AFE1(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &afe1_cg_regs, _shift,	\
		       &afe_no_setclr_ops, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate afe_clks[] = {
	GATE_AFE0(CLK_AFE_AFE, "afe_afe", "audio_ck", 2),
	GATE_AFE0(CLK_AFE_22M, "afe_22m", "aud_engen1_ck", 8),
	GATE_AFE0(CLK_AFE_24M, "afe_24m", "aud_engen2_ck", 9),
	GATE_AFE0(CLK_AFE_APLL2_TUNER, "afe_apll2_tuner", "aud_engen2_ck", 18),
	GATE_AFE0(CLK_AFE_APLL_TUNER, "afe_apll_tuner", "aud_engen1_ck", 19),
	GATE_AFE0(CLK_AFE_ADC, "afe_adc", "audio_ck", 24),
	GATE_AFE0(CLK_AFE_DAC, "afe_dac", "audio_ck", 25),
	GATE_AFE0(CLK_AFE_DAC_PREDIS, "afe_dac_predis", "audio_ck", 26),
	GATE_AFE0(CLK_AFE_TML, "afe_tml", "audio_ck", 27),
	GATE_AFE0(CLK_AFE_NLE, "afe_nle", "audio_ck", 28),
	GATE_AFE1(CLK_AFE_I2S1_BCLK, "afe_i2s1_bclk", "audio_ck", 4),
	GATE_AFE1(CLK_AFE_I2S2_BCLK, "afe_i2s2_bclk", "audio_ck", 5),
	GATE_AFE1(CLK_AFE_I2S3_BCLK, "afe_i2s3_bclk", "audio_ck", 6),
	GATE_AFE1(CLK_AFE_I2S4_BCLK, "afe_i2s4_bclk", "audio_ck", 7),
	GATE_AFE1(CLK_AFE_GENERAL3_ASRC, "afe_general3_asrc", "audio_ck", 11),
	GATE_AFE1(CLK_AFE_CONNSYS_I2S_ASRC, "afe_connsys_i2s_asrc", "audio_ck", 12),
	GATE_AFE1(CLK_AFE_GENERAL1_ASRC, "afe_general1_asrc", "audio_ck", 13),
	GATE_AFE1(CLK_AFE_GENERAL2_ASRC, "afe_general2_asrc", "audio_ck", 14),
	GATE_AFE1(CLK_AFE_DAC_HIRES, "afe_dac_hires", "audio_h_ck", 15),
	GATE_AFE1(CLK_AFE_ADC_HIRES, "afe_adc_hires", "audio_h_ck", 16),
	GATE_AFE1(CLK_AFE_ADC_HIRES_TML, "afe_adc_hires_tml", "audio_h_ck", 17),
};

static const struct mtk_clk_desc afe_desc = {
	.clks = afe_clks,
	.num_clks = ARRAY_SIZE(afe_clks),
};

static const struct of_device_id of_match_clk_mt6789_audiosys[] = {
	{ .compatible = "mediatek,mt6789-afe", .data = &afe_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_audiosys);

static int clk_mt6789_audiosys_probe(struct platform_device *pdev)
{
	mt6789_clk_pwr_init(&pdev->dev);

	return mtk_clk_simple_probe(pdev);
}

static struct platform_driver clk_mt6789_audiosys_drv = {
	.probe = clk_mt6789_audiosys_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-audiosys",
		.of_match_table = of_match_clk_mt6789_audiosys,
	},
};
module_platform_driver(clk_mt6789_audiosys_drv);

MODULE_DESCRIPTION("MediaTek MT6789 afe clocks");
MODULE_LICENSE("GPL");
