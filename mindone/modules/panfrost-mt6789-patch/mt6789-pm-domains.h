/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MINDONE: adapted from device/mainline/mt6789-pm-domains.h (mt6789-next,
 * commit 2ed59e432779, Linux 6.16-rc5). NOT verbatim: mainline targets a
 * newer driver API (bp_cfg[]+hwip, 5-arg BUS_PROT_WR_IGN, ext_buck_iso_*)
 * vs our tree's older one (bp_infracfg[]/bp_smi[], 4-arg macro). Adapted to
 * the old API, MFG0..MFG3 only (GPU). Bit masks below are PLACEHOLDERS --
 * see MAINLINE-HARVEST §2.1 and PANFROST-PLAN.
 */

#ifndef __SOC_MEDIATEK_MT6789_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT6789_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mt6789-power.h>

/*
 * MINDONE: REAL values (not placeholders), recovered from a trusted vendor
 * source, matched to mt6789-next by REGISTER offsets (1:1; see
 * MAINLINE-HARVEST §2.1, PANFROST-PLAN). EN_1_MFG1<-STEP1_0
 * (PROTECTEN_1 SET 0x2A8/STA 0x258), EN_2_MFG1<-STEP1_1 (PROTECTEN_2 SET
 * 0x714/STA 0x724), EN_MFG1<-STEP2_0 (PROTECTEN SET 0x2A0/STA 0x228),
 * EN_2_MFG1_2ND<-STEP2_1 (same reg as EN_2_MFG1, different bit).
 */
#define MT6789_TOP_AXI_PROT_EN_1_MFG1		BIT(21)
#define MT6789_TOP_AXI_PROT_EN_2_MFG1		(BIT(5) | BIT(6))
#define MT6789_TOP_AXI_PROT_EN_MFG1		(BIT(21) | BIT(22))
#define MT6789_TOP_AXI_PROT_EN_2_MFG1_2ND	BIT(7)

static const struct scpsys_domain_data scpsys_domain_data_mt6789[] = {
	[MT6789_POWER_DOMAIN_MFG0] = {
		.name = "mfg0",
		.sta_mask = BIT(2),
		.ctl_offs = 0x308,
		.pwr_sta_offs = 0x016c,
		.pwr_sta2nd_offs = 0x0170,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF | MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT6789_POWER_DOMAIN_MFG1] = {
		.name = "mfg1",
		.sta_mask = BIT(3),
		.ctl_offs = 0x30C,
		.pwr_sta_offs = 0x016c,
		.pwr_sta2nd_offs = 0x0170,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_infracfg = {
			BUS_PROT_WR_IGN(MT6789_TOP_AXI_PROT_EN_1_MFG1, 0x02A8, 0x02AC, 0x0258),
			BUS_PROT_WR_IGN(MT6789_TOP_AXI_PROT_EN_2_MFG1, 0x0714, 0x0718, 0x0724),
			BUS_PROT_WR_IGN(MT6789_TOP_AXI_PROT_EN_MFG1, 0x02A0, 0x02A4, 0x0228),
			BUS_PROT_WR_IGN(MT6789_TOP_AXI_PROT_EN_2_MFG1_2ND, 0x0714, 0x0718, 0x0724),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF | MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT6789_POWER_DOMAIN_MFG2] = {
		.name = "mfg2",
		.sta_mask = BIT(4),
		.ctl_offs = 0x310,
		.pwr_sta_offs = 0x016c,
		.pwr_sta2nd_offs = 0x0170,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6789_POWER_DOMAIN_MFG3] = {
		.name = "mfg3",
		.sta_mask = BIT(5),
		.ctl_offs = 0x314,
		.pwr_sta_offs = 0x016c,
		.pwr_sta2nd_offs = 0x0170,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
};

static const struct scpsys_soc_data mt6789_scpsys_data = {
	.domains_data = scpsys_domain_data_mt6789,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt6789),
};

#endif /* __SOC_MEDIATEK_MT6789_PM_DOMAINS_H */
