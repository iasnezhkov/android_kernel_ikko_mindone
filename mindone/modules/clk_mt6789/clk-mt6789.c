// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/mt6789-clk.h>

#include "clk-mtk.h"
#include "clk-mux.h"
#include "clk-pll.h"
#include "clk-mt6789-pwr.h"

#define CLK_CFG_UPDATE		0x004
#define CLK_CFG_UPDATE1		0x008
#define CLK_CFG_UPDATE2		0x00c
#define CLK_CFG(n)		(0x010 + (n) * 0x10)
#define CLK_CFG_SET(n)		(0x014 + (n) * 0x10)
#define CLK_CFG_CLR(n)		(0x018 + (n) * 0x10)
#define CLK_AUDDIV_0		0x320
#define CLK_AUDDIV_2		0x328
#define CLK_AUDDIV_3		0x334
#define CLK_AUDDIV_4		0x338

static DEFINE_SPINLOCK(mt6789_clk_lock);

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_MFGPLL, "mfgpll_ck", "mfgpll", 1, 1),
	FACTOR(CLK_TOP_MAINPLL_D4, "mainpll_d4", "mainpll", 1, 4),
	FACTOR(CLK_TOP_MAINPLL_D4_D2, "mainpll_d4_d2", "mainpll", 1, 8),
	FACTOR(CLK_TOP_MAINPLL_D4_D4, "mainpll_d4_d4", "mainpll", 1, 16),
	FACTOR(CLK_TOP_MAINPLL_D4_D8, "mainpll_d4_d8", "mainpll", 1, 32),
	FACTOR(CLK_TOP_MAINPLL_D4_D16, "mainpll_d4_d16", "mainpll", 1, 64),
	FACTOR(CLK_TOP_MAINPLL_D5, "mainpll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_MAINPLL_D5_D2, "mainpll_d5_d2", "mainpll", 1, 10),
	FACTOR(CLK_TOP_MAINPLL_D5_D4, "mainpll_d5_d4", "mainpll", 1, 20),
	FACTOR(CLK_TOP_MAINPLL_D5_D8, "mainpll_d5_d8", "mainpll", 1, 40),
	FACTOR(CLK_TOP_MAINPLL_D6, "mainpll_d6", "mainpll", 1, 6),
	FACTOR(CLK_TOP_MAINPLL_D6_D2, "mainpll_d6_d2", "mainpll", 1, 12),
	FACTOR(CLK_TOP_MAINPLL_D6_D4, "mainpll_d6_d4", "mainpll", 1, 24),
	FACTOR(CLK_TOP_MAINPLL_D7, "mainpll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_MAINPLL_D7_D2, "mainpll_d7_d2", "mainpll", 1, 14),
	FACTOR(CLK_TOP_MAINPLL_D7_D4, "mainpll_d7_d4", "mainpll", 1, 28),
	FACTOR(CLK_TOP_MAINPLL_D7_D8, "mainpll_d7_d8", "mainpll", 1, 56),
	FACTOR(CLK_TOP_MAINPLL_D9, "mainpll_d9", "mainpll", 1, 9),
	FACTOR(CLK_TOP_UNIVPLL_D4, "univpll_d4", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D4_D2, "univpll_d4_d2", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D4_D4, "univpll_d4_d4", "univpll", 1, 16),
	FACTOR(CLK_TOP_UNIVPLL_D4_D8, "univpll_d4_d8", "univpll", 1, 32),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL_D5_D2, "univpll_d5_d2", "univpll", 1, 10),
	FACTOR(CLK_TOP_UNIVPLL_D5_D4, "univpll_d5_d4", "univpll", 1, 20),
	FACTOR(CLK_TOP_UNIVPLL_D6, "univpll_d6", "univpll", 1, 6),
	FACTOR(CLK_TOP_UNIVPLL_D6_D2, "univpll_d6_d2", "univpll", 1, 12),
	FACTOR(CLK_TOP_UNIVPLL_D6_D4, "univpll_d6_d4", "univpll", 1, 24),
	FACTOR(CLK_TOP_UNIVPLL_D6_D8, "univpll_d6_d8", "univpll", 1, 48),
	FACTOR(CLK_TOP_UNIVPLL_D6_D16, "univpll_d6_d16", "univpll", 1, 96),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7),
	FACTOR(CLK_TOP_UNIVPLL_192M_D2, "univpll_192m_d2", "univpll", 1, 26),
	FACTOR(CLK_TOP_UNIVPLL_192M_D4, "univpll_192m_d4", "univpll", 1, 52),
	FACTOR(CLK_TOP_UNIVPLL_192M_D8, "univpll_192m_d8", "univpll", 1, 104),
	FACTOR(CLK_TOP_UNIVPLL_192M_D16, "univpll_192m_d16", "univpll", 1, 208),
	FACTOR(CLK_TOP_UNIVPLL_192M_D32, "univpll_192m_d32", "univpll", 1, 416),
	FACTOR(CLK_TOP_APLL1, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1", 1, 8),
	FACTOR(CLK_TOP_APLL2, "apll2_ck", "apll2", 1, 1),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_APLL2_D8, "apll2_d8", "apll2", 1, 8),
	FACTOR(CLK_TOP_MMPLL_D4_D2, "mmpll_d4_d2", "mmpll", 1, 8),
	FACTOR(CLK_TOP_MMPLL_D5_D2, "mmpll_d5_d2", "mmpll", 1, 12),
	FACTOR(CLK_TOP_MMPLL_D6, "mmpll_d6", "mmpll", 1, 6),
	FACTOR(CLK_TOP_MMPLL_D6_D2, "mmpll_d6_d2", "mmpll", 1, 12),
	FACTOR(CLK_TOP_MMPLL_D7, "mmpll_d7", "mmpll", 1, 7),
	FACTOR(CLK_TOP_MMPLL_D9, "mmpll_d9", "mmpll", 1, 9),
	FACTOR(CLK_TOP_NPUPLL, "npupll_ck", "npupll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL, "tvdpll_ck", "tvdpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll", 1, 4),
	FACTOR(CLK_TOP_CLKRTC, "clkrtc", "clk32k", 1, 1),
	FACTOR(CLK_TOP_TCK_26M_MX9, "tck_26m_mx9_ck", "clk26m", 1, 1),
	FACTOR(CLK_TOP_F26M_CK_D2, "f26m_d2", "clk26m", 1, 2),
	FACTOR(CLK_TOP_OSC_D2, "osc_d2", "ulposc", 1, 2),
	FACTOR(CLK_TOP_OSC_D4, "osc_d4", "ulposc", 1, 4),
	FACTOR(CLK_TOP_OSC_D8, "osc_d8", "ulposc", 1, 8),
	FACTOR(CLK_TOP_OSC_D16, "osc_d16", "ulposc", 1, 16),
	FACTOR(CLK_TOP_OSC_D10, "osc_d10", "ulposc", 1, 10),
	FACTOR(CLK_TOP_OSC_D20, "osc_d20", "ulposc", 1, 20),
	FACTOR(CLK_TOP_F26M, "f26m_ck", "clk26m", 1, 1),
	FACTOR(CLK_TOP_AXI, "axi_ck", "axi_sel", 1, 1),
	FACTOR(CLK_TOP_DISP, "disp_ck", "disp_sel", 1, 1),
	FACTOR(CLK_TOP_MDP, "mdp_ck", "mdp_sel", 1, 1),
	FACTOR(CLK_TOP_IMG1, "img1_ck", "img1_sel", 1, 1),
	FACTOR(CLK_TOP_IPE, "ipe_ck", "ipe_sel", 1, 1),
	FACTOR(CLK_TOP_CAM, "cam_ck", "cam_sel", 1, 1),
	FACTOR(CLK_TOP_MFG_REF, "mfg_ref_ck", "mfg_ref_sel", 1, 1),
	FACTOR(CLK_TOP_MFG_PLL, "mfg_pll_ck", "mfg_pll_sel", 1, 1),
	FACTOR(CLK_TOP_UART, "uart_ck", "uart_sel", 1, 1),
	FACTOR(CLK_TOP_SPI, "spi_ck", "spi_sel", 1, 1),
	FACTOR(CLK_TOP_MSDC50_0, "msdc50_0_ck", "msdc50_0_sel", 1, 1),
	FACTOR(CLK_TOP_MSDC30_1, "msdc30_1_ck", "msdc30_1_sel", 1, 1),
	FACTOR(CLK_TOP_AUDIO, "audio_ck", "audio_sel", 1, 1),
	FACTOR(CLK_TOP_PWRAP_ULPOSC, "pwrap_ulposc_ck", "pwrap_ulposc_sel", 1, 1),
	FACTOR(CLK_TOP_DISP_PWM, "disp_pwm_ck", "disp_pwm_sel", 1, 1),
	FACTOR(CLK_TOP_USB_TOP, "usb_ck", "usb_sel", 1, 1),
	FACTOR(CLK_TOP_I2C, "i2c_ck", "i2c_sel", 1, 1),
	FACTOR(CLK_TOP_AUD_ENGEN1, "aud_engen1_ck", "aud_engen1_sel", 1, 1),
	FACTOR(CLK_TOP_AUD_ENGEN2, "aud_engen2_ck", "aud_engen2_sel", 1, 1),
	FACTOR(CLK_TOP_AES_UFSFDE, "aes_ufsfde_ck", "aes_ufsfde_sel", 1, 1),
	FACTOR(CLK_TOP_UFS, "ufs_ck", "ufs_sel", 1, 1),
	FACTOR(CLK_TOP_DPMAIF_MAIN, "dpmaif_main_ck", "dpmaif_main_sel", 1, 1),
	FACTOR(CLK_TOP_VENC, "venc_ck", "venc_sel", 1, 1),
	FACTOR(CLK_TOP_VDEC, "vdec_ck", "vdec_sel", 1, 1),
	FACTOR(CLK_TOP_CAMTM, "camtm_ck", "camtm_sel", 1, 1),
	FACTOR(CLK_TOP_PWM, "pwm_ck", "pwm_sel", 1, 1),
	FACTOR(CLK_TOP_AUDIO_H, "audio_h_ck", "audio_h_sel", 1, 1),
	FACTOR(CLK_TOP_DSI_OCC, "dsi_occ_ck", "dsi_occ_sel", 1, 1),
	FACTOR(CLK_TOP_I2C_PSEUDO, "i2c_pseudo_ck", "ifrao_i2c_pseudo", 1, 1),
	FACTOR(CLK_TOP_APDMA, "apdma_ck", "ifrao_apdma_pseudo", 1, 1),
};

static const char * const axi_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d4",
	"mainpll_d7_d2",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
	"mainpll_d6_d2",
	"osc_d4",
};

static const char * const spm_parents[] = {
	"tck_26m_mx9_ck",
	"osc_d10",
	"mainpll_d7_d4",
	"clkrtc",
};

static const char * const scp_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d4",
	"npupll_ck",
	"mainpll_d6",
	"univpll_d6",
	"mainpll_d4_d2",
	"mainpll_d4",
	"mainpll_d7",
};

static const char * const bus_aximem_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d7_d2",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
	"mainpll_d6",
};

static const char * const disp_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d6_d2",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2",
	"univpll_d4_d2",
	"mmpll_d7",
	"univpll_d6",
	"mainpll_d4",
	"mmpll_d5_d2",
};

static const char * const mdp_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"mainpll_d4_d2",
	"mmpll_d4_d2",
	"mainpll_d6",
	"mainpll_d5",
	"mainpll_d4",
	"tvdpll_ck",
	"univpll_d4",
	"mmpll_d5_d2",
};

static const char * const img1_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d4",
	"tvdpll_ck",
	"mainpll_d4",
	"univpll_d5",
	"mmpll_d6",
	"univpll_d6",
	"mainpll_d6",
	"mmpll_d4_d2",
	"mainpll_d4_d2",
	"mmpll_d6_d2",
	"mmpll_d5_d2",
};

static const char * const ipe_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4",
	"mmpll_d6",
	"univpll_d6",
	"mainpll_d6",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"mmpll_d6_d2",
	"mmpll_d5_d2",
};

static const char * const cam_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4",
	"mmpll_d6",
	"univpll_d4",
	"univpll_d5",
	"univpll_d6",
	"mmpll_d7",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"npupll_ck",
};

static const char * const mfg_ref_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d6_d2",
	"mainpll_d6",
	"mainpll_d5_d2",
};

static const char * const mfg_pll_parents[] = {
	"mfg_ref_ck",
	"mfgpll_ck",
};

static const char * const camtg_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_192m_d8",
	"univpll_d6_d8",
	"univpll_192m_d4",
	"univpll_d6_d16",
	"f26m_d2",
	"univpll_192m_d16",
	"univpll_192m_d32",
};

static const char * const uart_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d6_d8",
};

static const char * const spi_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d5_d4",
	"mainpll_d6_d4",
	"msdcpll_d4",
	"msdcpll_d2",
	"mainpll_d6_d2",
	"mainpll_d4_d4",
	"univpll_d5_d4",
};

static const char * const msdc5hclk_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d2",
	"mainpll_d6_d2",
};

static const char * const msdc50_0_parents[] = {
	"tck_26m_mx9_ck",
	"msdcpll_ck",
	"msdcpll_d2",
	"univpll_d4_d4",
	"mainpll_d6_d2",
	"univpll_d4_d2",
};

static const char * const msdc30_1_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d6_d2",
	"mainpll_d6_d2",
	"mainpll_d7_d2",
	"msdcpll_d2",
};

static const char * const audio_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d5_d8",
	"mainpll_d7_d8",
	"mainpll_d4_d16",
};

static const char * const aud_intbus_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d4",
	"mainpll_d7_d4",
};

static const char * const pwrap_ulposc_parents[] = {
	"osc_d10",
	"tck_26m_mx9_ck",
	"osc_d4",
	"osc_d8",
	"osc_d16",
};

static const char * const atb_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
};

static const char * const sspm_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d7_d2",
	"mainpll_d6_d2",
	"mainpll_d5_d2",
	"mainpll_d9",
	"mainpll_d4_d2",
};

static const char * const scam_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d5_d4",
};

static const char * const disp_pwm_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d6_d4",
	"osc_d2",
	"osc_d4",
	"osc_d16",
};

static const char * const usb_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d5_d4",
	"univpll_d6_d4",
	"univpll_d5_d2",
};

static const char * const i2c_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d8",
	"univpll_d5_d4",
};

static const char * const seninf_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d4_d4",
	"univpll_d6_d2",
	"univpll_d4_d2",
	"npupll_ck",
	"mmpll_d7",
	"mmpll_d6",
	"univpll_d5",
};

static const char * const dxcc_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d2",
	"mainpll_d4_d4",
	"mainpll_d4_d8",
};

static const char * const aud_engen1_parents[] = {
	"tck_26m_mx9_ck",
	"apll1_d2",
	"apll1_d4",
	"apll1_d8",
};

static const char * const aud_engen2_parents[] = {
	"tck_26m_mx9_ck",
	"apll2_d2",
	"apll2_d4",
	"apll2_d8",
};

static const char * const aes_ufsfde_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4",
	"mainpll_d4_d2",
	"mainpll_d6",
	"mainpll_d4_d4",
	"univpll_d4_d2",
	"univpll_d6",
};

static const char * const ufs_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d4",
	"mainpll_d4_d8",
	"univpll_d4_d4",
	"mainpll_d6_d2",
	"mainpll_d5_d2",
	"msdcpll_d2",
};

static const char * const aud_1_parents[] = {
	"tck_26m_mx9_ck",
	"apll1_ck",
};

static const char * const aud_2_parents[] = {
	"tck_26m_mx9_ck",
	"apll2_ck",
};

static const char * const dpmaif_main_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d4_d4",
	"mainpll_d6",
	"mainpll_d4_d2",
	"univpll_d4_d2",
};

static const char * const venc_parents[] = {
	"tck_26m_mx9_ck",
	"mmpll_d7",
	"mainpll_d6",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"univpll_d6",
	"mmpll_d6",
	"mainpll_d5_d2",
	"mainpll_d6_d2",
	"mmpll_d9",
	"univpll_d4_d4",
	"mainpll_d4",
	"univpll_d4",
	"univpll_d5",
	"univpll_d5_d2",
	"mainpll_d5",
};

static const char * const vdec_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_192m_d2",
	"univpll_d5_d4",
	"mainpll_d5",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2",
	"mainpll_d4_d2",
	"univpll_d4_d2",
	"univpll_d7",
	"mmpll_d7",
	"mmpll_d6",
	"univpll_d5",
	"mainpll_d4",
	"univpll_d4",
	"univpll_d6",
};

static const char * const camtm_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d7",
	"univpll_d6_d2",
	"univpll_d4_d2",
};

static const char * const pwm_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d4_d8",
};

static const char * const audio_h_parents[] = {
	"tck_26m_mx9_ck",
	"univpll_d7",
	"apll1_ck",
	"apll2_ck",
};

static const char * const spmi_mst_parents[] = {
	"tck_26m_mx9_ck",
	"f26m_d2",
	"osc_d8",
	"osc_d10",
	"osc_d16",
	"osc_d20",
	"clkrtc",
};

static const char * const dvfsrc_parents[] = {
	"tck_26m_mx9_ck",
	"osc_d10",
};

static const char * const aes_msdcfde_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d4_d2",
	"mainpll_d6",
	"mainpll_d4_d4",
	"univpll_d4_d2",
	"univpll_d6",
};

static const char * const mcupm_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d6_d4",
	"mainpll_d6_d2",
};

static const char * const dsi_occ_parents[] = {
	"tck_26m_mx9_ck",
	"mainpll_d6_d2",
	"univpll_d5_d2",
	"univpll_d4_d2",
};

static const char * const apll_i2s_mck_parents[] = {
	"aud_1_sel",
	"aud_2_sel",
};

static const struct mtk_mux top_muxes[] = {
	MUX_CLR_SET_UPD(CLK_TOP_AXI_SEL, "axi_sel", axi_parents,
		CLK_CFG(0), CLK_CFG_SET(0), CLK_CFG_CLR(0), 0, 3, CLK_CFG_UPDATE, 0),
	MUX_CLR_SET_UPD(CLK_TOP_SPM_SEL, "spm_sel", spm_parents,
		CLK_CFG(0), CLK_CFG_SET(0), CLK_CFG_CLR(0), 8, 2, CLK_CFG_UPDATE, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCP_SEL, "scp_sel", scp_parents,
		CLK_CFG(0), CLK_CFG_SET(0), CLK_CFG_CLR(0), 16, 3, 23, CLK_CFG_UPDATE, 2),
	MUX_CLR_SET_UPD(CLK_TOP_BUS_AXIMEM_SEL, "bus_aximem_sel", bus_aximem_parents,
		CLK_CFG(0), CLK_CFG_SET(0), CLK_CFG_CLR(0), 24, 3, CLK_CFG_UPDATE, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_SEL, "disp_sel", disp_parents,
		CLK_CFG(1), CLK_CFG_SET(1), CLK_CFG_CLR(1), 0, 4, 7, CLK_CFG_UPDATE, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MDP_SEL, "mdp_sel", mdp_parents,
		CLK_CFG(1), CLK_CFG_SET(1), CLK_CFG_CLR(1), 8, 4, 15, CLK_CFG_UPDATE, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IMG1_SEL, "img1_sel", img1_parents,
		CLK_CFG(1), CLK_CFG_SET(1), CLK_CFG_CLR(1), 16, 4, 23, CLK_CFG_UPDATE, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IPE_SEL, "ipe_sel", ipe_parents,
		CLK_CFG(2), CLK_CFG_SET(2), CLK_CFG_CLR(2), 0, 4, 7, CLK_CFG_UPDATE, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAM_SEL, "cam_sel", cam_parents,
		CLK_CFG(2), CLK_CFG_SET(2), CLK_CFG_CLR(2), 16, 4, 23, CLK_CFG_UPDATE, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_REF_SEL, "mfg_ref_sel", mfg_ref_parents,
		CLK_CFG(4), CLK_CFG_SET(4), CLK_CFG_CLR(4), 16, 2, 23, CLK_CFG_UPDATE, 18),
	MUX_CLR_SET_UPD(CLK_TOP_MFG_PLL_SEL, "mfg_pll_sel", mfg_pll_parents,
		CLK_CFG(4), CLK_CFG_SET(4), CLK_CFG_CLR(4), 18, 1, 0, -1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_parents,
		CLK_CFG(4), CLK_CFG_SET(4), CLK_CFG_CLR(4), 24, 3, 31, CLK_CFG_UPDATE, 19),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG2_SEL, "camtg2_sel", camtg_parents,
		CLK_CFG(5), CLK_CFG_SET(5), CLK_CFG_CLR(5), 0, 3, 7, CLK_CFG_UPDATE, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG3_SEL, "camtg3_sel", camtg_parents,
		CLK_CFG(5), CLK_CFG_SET(5), CLK_CFG_CLR(5), 8, 3, 15, CLK_CFG_UPDATE, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG4_SEL, "camtg4_sel", camtg_parents,
		CLK_CFG(5), CLK_CFG_SET(5), CLK_CFG_CLR(5), 16, 3, 23, CLK_CFG_UPDATE, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG5_SEL, "camtg5_sel", camtg_parents,
		CLK_CFG(5), CLK_CFG_SET(5), CLK_CFG_CLR(5), 24, 3, 31, CLK_CFG_UPDATE, 23),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG6_SEL, "camtg6_sel", camtg_parents,
		CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 0, 3, 7, CLK_CFG_UPDATE, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents,
		CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 8, 1, 15, CLK_CFG_UPDATE, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents,
		CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 16, 3, 23, CLK_CFG_UPDATE, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_HCLK_SEL, "msdc5hclk_sel", msdc5hclk_parents,
		CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 24, 2, 31, CLK_CFG_UPDATE, 27),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", msdc50_0_parents,
		CLK_CFG(7), CLK_CFG_SET(7), CLK_CFG_CLR(7), 0, 3, 7, CLK_CFG_UPDATE, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_1_parents,
		CLK_CFG(7), CLK_CFG_SET(7), CLK_CFG_CLR(7), 8, 3, 15, CLK_CFG_UPDATE, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
		CLK_CFG(7), CLK_CFG_SET(7), CLK_CFG_CLR(7), 24, 2, 31, CLK_CFG_UPDATE1, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents,
		CLK_CFG(8), CLK_CFG_SET(8), CLK_CFG_CLR(8), 0, 2, 7, CLK_CFG_UPDATE1, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWRAP_ULPOSC_SEL, "pwrap_ulposc_sel", pwrap_ulposc_parents,
		CLK_CFG(8), CLK_CFG_SET(8), CLK_CFG_CLR(8), 8, 3, 15, CLK_CFG_UPDATE1, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ATB_SEL, "atb_sel", atb_parents,
		CLK_CFG(8), CLK_CFG_SET(8), CLK_CFG_CLR(8), 16, 2, 23, CLK_CFG_UPDATE1, 3),
	MUX_CLR_SET_UPD(CLK_TOP_SSPM_SEL, "sspm_sel", sspm_parents,
		CLK_CFG(8), CLK_CFG_SET(8), CLK_CFG_CLR(8), 24, 3, CLK_CFG_UPDATE1, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCAM_SEL, "scam_sel", scam_parents,
		CLK_CFG(9), CLK_CFG_SET(9), CLK_CFG_CLR(9), 8, 1, 15, CLK_CFG_UPDATE1, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM_SEL, "disp_pwm_sel", disp_pwm_parents,
		CLK_CFG(9), CLK_CFG_SET(9), CLK_CFG_CLR(9), 16, 3, 23, CLK_CFG_UPDATE1, 7),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP_SEL, "usb_sel", usb_parents,
		CLK_CFG(9), CLK_CFG_SET(9), CLK_CFG_CLR(9), 24, 2, 31, CLK_CFG_UPDATE1, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents,
		CLK_CFG(10), CLK_CFG_SET(10), CLK_CFG_CLR(10), 8, 2, 15, CLK_CFG_UPDATE1, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF_SEL, "seninf_sel", seninf_parents,
		CLK_CFG(10), CLK_CFG_SET(10), CLK_CFG_CLR(10), 16, 3, 23, CLK_CFG_UPDATE1, 11),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF1_SEL, "seninf1_sel", seninf_parents,
		CLK_CFG(10), CLK_CFG_SET(10), CLK_CFG_CLR(10), 24, 3, 31, CLK_CFG_UPDATE1, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF2_SEL, "seninf2_sel", seninf_parents,
		CLK_CFG(11), CLK_CFG_SET(11), CLK_CFG_CLR(11), 0, 3, 7, CLK_CFG_UPDATE1, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF3_SEL, "seninf3_sel", seninf_parents,
		CLK_CFG(11), CLK_CFG_SET(11), CLK_CFG_CLR(11), 8, 3, 15, CLK_CFG_UPDATE1, 14),
	MUX_CLR_SET_UPD(CLK_TOP_DXCC_SEL, "dxcc_sel", dxcc_parents,
		CLK_CFG(11), CLK_CFG_SET(11), CLK_CFG_CLR(11), 24, 2, CLK_CFG_UPDATE1, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN1_SEL, "aud_engen1_sel", aud_engen1_parents,
		CLK_CFG(12), CLK_CFG_SET(12), CLK_CFG_CLR(12), 0, 2, 7, CLK_CFG_UPDATE1, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN2_SEL, "aud_engen2_sel", aud_engen2_parents,
		CLK_CFG(12), CLK_CFG_SET(12), CLK_CFG_CLR(12), 8, 2, 15, CLK_CFG_UPDATE1, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_UFSFDE_SEL, "aes_ufsfde_sel", aes_ufsfde_parents,
		CLK_CFG(12), CLK_CFG_SET(12), CLK_CFG_CLR(12), 16, 3, 23, CLK_CFG_UPDATE1, 19),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UFS_SEL, "ufs_sel", ufs_parents,
		CLK_CFG(12), CLK_CFG_SET(12), CLK_CFG_CLR(12), 24, 3, 31, CLK_CFG_UPDATE1, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents,
		CLK_CFG(13), CLK_CFG_SET(13), CLK_CFG_CLR(13), 0, 1, 7, CLK_CFG_UPDATE1, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_2_SEL, "aud_2_sel", aud_2_parents,
		CLK_CFG(13), CLK_CFG_SET(13), CLK_CFG_CLR(13), 8, 1, 15, CLK_CFG_UPDATE1, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPMAIF_MAIN_SEL, "dpmaif_main_sel", dpmaif_main_parents,
		CLK_CFG(13), CLK_CFG_SET(13), CLK_CFG_CLR(13), 24, 3, 31, CLK_CFG_UPDATE1, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VENC_SEL, "venc_sel", venc_parents,
		CLK_CFG(14), CLK_CFG_SET(14), CLK_CFG_CLR(14), 0, 4, 7, CLK_CFG_UPDATE1, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VDEC_SEL, "vdec_sel", vdec_parents,
		CLK_CFG(14), CLK_CFG_SET(14), CLK_CFG_CLR(14), 8, 4, 15, CLK_CFG_UPDATE1, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTM_SEL, "camtm_sel", camtm_parents,
		CLK_CFG(14), CLK_CFG_SET(14), CLK_CFG_CLR(14), 16, 2, 23, CLK_CFG_UPDATE1, 27),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents,
		CLK_CFG(14), CLK_CFG_SET(14), CLK_CFG_CLR(14), 24, 1, 31, CLK_CFG_UPDATE1, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_H_SEL, "audio_h_sel", audio_h_parents,
		CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 0, 2, 7, CLK_CFG_UPDATE1, 29),
	MUX_CLR_SET_UPD(CLK_TOP_SPMI_MST_SEL, "spmi_mst_sel", spmi_mst_parents,
		CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 8, 3, CLK_CFG_UPDATE1, 30),
	MUX_CLR_SET_UPD(CLK_TOP_DVFSRC_SEL, "dvfsrc_sel", dvfsrc_parents,
		CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 16, 1, CLK_CFG_UPDATE2, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_MSDCFDE_SEL, "aes_msdcfde_sel", aes_msdcfde_parents,
		CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 24, 3, 31, CLK_CFG_UPDATE2, 1),
	MUX_CLR_SET_UPD(CLK_TOP_MCUPM_SEL, "mcupm_sel", mcupm_parents,
		CLK_CFG(16), CLK_CFG_SET(16), CLK_CFG_CLR(16), 0, 2, CLK_CFG_UPDATE2, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSI_OCC_SEL, "dsi_occ_sel", dsi_occ_parents,
		CLK_CFG(16), CLK_CFG_SET(16), CLK_CFG_CLR(16), 16, 2, 23, CLK_CFG_UPDATE2, 4),
};

static const struct mtk_composite top_composites[] = {
	MUX(CLK_TOP_APLL_I2S0_MCK_SEL, "apll_i2s0_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 16, 1),
	MUX(CLK_TOP_APLL_I2S1_MCK_SEL, "apll_i2s1_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 17, 1),
	MUX(CLK_TOP_APLL_I2S2_MCK_SEL, "apll_i2s2_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 18, 1),
	MUX(CLK_TOP_APLL_I2S3_MCK_SEL, "apll_i2s3_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 19, 1),
	MUX(CLK_TOP_APLL_I2S4_MCK_SEL, "apll_i2s4_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 20, 1),
	MUX(CLK_TOP_APLL_I2S5_MCK_SEL, "apll_i2s5_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 21, 1),
	MUX(CLK_TOP_APLL_I2S6_MCK_SEL, "apll_i2s6_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 22, 1),
	MUX(CLK_TOP_APLL_I2S7_MCK_SEL, "apll_i2s7_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 23, 1),
	MUX(CLK_TOP_APLL_I2S8_MCK_SEL, "apll_i2s8_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 24, 1),
	MUX(CLK_TOP_APLL_I2S9_MCK_SEL, "apll_i2s9_mck_sel", apll_i2s_mck_parents,
	    CLK_AUDDIV_0, 25, 1),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV0, "apll12_div0", "apll_i2s0_mck_sel",
		 CLK_AUDDIV_0, 0, CLK_AUDDIV_2, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV1, "apll12_div1", "apll_i2s1_mck_sel",
		 CLK_AUDDIV_0, 1, CLK_AUDDIV_2, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV2, "apll12_div2", "apll_i2s2_mck_sel",
		 CLK_AUDDIV_0, 2, CLK_AUDDIV_2, 8, 16),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV3, "apll12_div3", "apll_i2s3_mck_sel",
		 CLK_AUDDIV_0, 3, CLK_AUDDIV_2, 8, 24),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV4, "apll12_div4", "apll_i2s4_mck_sel",
		 CLK_AUDDIV_0, 4, CLK_AUDDIV_3, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_CK_DIVB, "apll12_divb", "apll12_div4",
		 CLK_AUDDIV_0, 5, CLK_AUDDIV_3, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV5, "apll12_div5", "apll_i2s5_mck_sel",
		 CLK_AUDDIV_0, 6, CLK_AUDDIV_3, 8, 16),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV6, "apll12_div6", "apll_i2s6_mck_sel",
		 CLK_AUDDIV_0, 7, CLK_AUDDIV_3, 8, 24),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV7, "apll12_div7", "apll_i2s7_mck_sel",
		 CLK_AUDDIV_0, 8, CLK_AUDDIV_4, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV8, "apll12_div8", "apll_i2s8_mck_sel",
		 CLK_AUDDIV_0, 9, CLK_AUDDIV_4, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV9, "apll12_div9", "apll_i2s9_mck_sel",
		 CLK_AUDDIV_0, 10, CLK_AUDDIV_4, 8, 16),
};

static const struct mtk_clk_desc topck_desc = {
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.composite_clks = top_composites,
	.num_composite_clks = ARRAY_SIZE(top_composites),
	.clk_lock = &mt6789_clk_lock,
};

#define MT6789_PLL_FMAX		(3800UL * MHZ)
#define MT6789_PLL_FMIN		(1500UL * MHZ)
#define MT6789_INTEGER_BITS	8

#define PLL(_id, _name, _reg, _en_reg, _en_mask, _pll_en_bit,		\
	    _pwr_reg, _flags, _rst_bar_mask, _pd_reg, _pd_shift,	\
	    _tuner_reg, _tuner_en_reg, _tuner_en_bit,			\
	    _pcw_reg, _pcw_shift, _pcwbits) {				\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.en_reg = _en_reg,					\
		.en_mask = _en_mask,					\
		.pll_en_bit = _pll_en_bit,				\
		.pwr_reg = _pwr_reg,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT6789_PLL_FMAX,				\
		.fmin = MT6789_PLL_FMIN,				\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = MT6789_INTEGER_BITS,			\
	}

static const struct mtk_pll_data apmixed_plls[] = {
	PLL(CLK_APMIXED_ARMPLL_LL, "armpll_ll", 0x208, 0x208, 0x0, 0,
	    0x214, PLL_AO, BIT(0), 0x20c, 24,
	    0x0, 0x0, 0, 0x20c, 0, 22),
	PLL(CLK_APMIXED_ARMPLL_BL0, "armpll_bl0", 0x218, 0x218, 0x0, 0,
	    0x224, PLL_AO, BIT(0), 0x21c, 24,
	    0x0, 0x0, 0, 0x21c, 0, 22),
	PLL(CLK_APMIXED_CCIPLL, "ccipll", 0x258, 0x258, 0x0, 0,
	    0x264, PLL_AO, BIT(0), 0x25c, 24,
	    0x0, 0x0, 0, 0x25c, 0, 22),
	PLL(CLK_APMIXED_MPLL, "mpll", 0x390, 0x390, 0x0, 0,
	    0x39c, PLL_AO, BIT(0), 0x394, 24,
	    0x0, 0x0, 0, 0x394, 0, 22),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x340, 0x340, 0xff000000, 0,
	    0x34c, HAVE_RST_BAR | PLL_AO, BIT(23), 0x344, 24,
	    0x0, 0x0, 0, 0x344, 0, 22),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x308, 0x308, 0xff000000, 0,
	    0x314, HAVE_RST_BAR, BIT(23), 0x30c, 24,
	    0x0, 0x0, 0, 0x30c, 0, 22),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x350, 0x350, 0x0, 0,
	    0x35c, 0, BIT(0), 0x354, 24,
	    0x0, 0x0, 0, 0x354, 0, 22),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x360, 0x360, 0xff000000, 0,
	    0x36c, HAVE_RST_BAR, BIT(23), 0x364, 24,
	    0x0, 0x0, 0, 0x364, 0, 22),
	PLL(CLK_APMIXED_NPUPLL, "npupll", 0x3b4, 0x3b4, 0xff000000, 0,
	    0x3c0, 0, BIT(0), 0x3b8, 24,
	    0x0, 0x0, 0, 0x3b8, 0, 22),
	PLL(CLK_APMIXED_MFGPLL, "mfgpll", 0x268, 0x268, 0x0, 0,
	    0x274, 0, BIT(0), 0x26c, 24,
	    0x0, 0x0, 0, 0x26c, 0, 22),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x380, 0x380, 0x0, 0,
	    0x38c, 0, BIT(0), 0x384, 24,
	    0x0, 0x0, 0, 0x384, 0, 22),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x318, 0x318, 0x0, 0,
	    0x328, 0, BIT(0), 0x31c, 24,
	    0x40, 0xc, 0, 0x320, 0, 32),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x32c, 0x32c, 0x0, 0,
	    0x33c, 0, BIT(0), 0x330, 24,
	    0x44, 0xc, 5, 0x334, 0, 32),
	PLL(CLK_APMIXED_USBPLL, "usbpll", 0x3c4, 0x3cc, 0x0, 2,
	    0x3cc, HAVE_RST_BAR, BIT(23), 0x3c4, 24,
	    0x0, 0x0, 0, 0x3c4, 0, 22),
};

static int clk_mt6789_apmixed_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	int ret;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_clk_register_plls(node, apmixed_plls, ARRAY_SIZE(apmixed_plls), clk_data);
	if (ret)
		goto free_data;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_plls;

	platform_set_drvdata(pdev, clk_data);

	return 0;

unregister_plls:
	mtk_clk_unregister_plls(apmixed_plls, ARRAY_SIZE(apmixed_plls), clk_data);
free_data:
	mtk_free_clk_data(clk_data);
	return ret;
}

static void clk_mt6789_apmixed_remove(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);
	mtk_clk_unregister_plls(apmixed_plls, ARRAY_SIZE(apmixed_plls), clk_data);
	mtk_free_clk_data(clk_data);
}

static const struct of_device_id of_match_clk_mt6789_apmixed[] = {
	{ .compatible = "mediatek,mt6789-apmixedsys" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_apmixed);

static struct platform_driver clk_mt6789_apmixed_drv = {
	.probe = clk_mt6789_apmixed_probe,
	.remove = clk_mt6789_apmixed_remove,
	.driver = {
		.name = "clk-mt6789-apmixed",
		.of_match_table = of_match_clk_mt6789_apmixed,
	},
};

static const struct of_device_id of_match_clk_mt6789_topck[] = {
	{ .compatible = "mediatek,mt6789-topckgen", .data = &topck_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6789_topck);

static struct platform_driver clk_mt6789_topck_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6789-topck",
		.of_match_table = of_match_clk_mt6789_topck,
	},
};

static struct platform_driver * const clk_mt6789_drivers[] = {
	&clk_mt6789_apmixed_drv,
	&clk_mt6789_topck_drv,
};

static int __init clk_mt6789_init(void)
{
	return platform_register_drivers(clk_mt6789_drivers, ARRAY_SIZE(clk_mt6789_drivers));
}
module_init(clk_mt6789_init);

static void __exit clk_mt6789_exit(void)
{
	platform_unregister_drivers(clk_mt6789_drivers, ARRAY_SIZE(clk_mt6789_drivers));
	mt6789_clk_pwr_exit();
}
module_exit(clk_mt6789_exit);

MODULE_DESCRIPTION("MediaTek MT6789 apmixedsys and topckgen clocks");
MODULE_LICENSE("GPL");
