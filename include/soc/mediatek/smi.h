/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#ifndef MTK_IOMMU_SMI_H
#define MTK_IOMMU_SMI_H

#include <linux/bitops.h>
#include <linux/device.h>

#if IS_ENABLED(CONFIG_MTK_SMI)

enum iommu_atf_cmd {
	IOMMU_ATF_CMD_CONFIG_SMI_LARB,		/* For mm master to en/disable iommu */
	IOMMU_ATF_CMD_MAX,
};

#define MTK_SMI_MMU_EN(port)	BIT(port)

struct mtk_smi_larb_iommu {
	struct device *dev;
	unsigned int   mmu;
	/* MINDONE: 64, not 32. MediaTek reference tree (mt6789/mt8781-era SoCs)
	 * uses this same struct with bank[64] (confirmed by direct diff,
	 * 2026-08-19). Additive/non-ABI-breaking: CONFIG_MTK_SMI is not built
	 * into the shipped vmlinux, so no linked code depends on the smaller
	 * bound; field order/types kept identical to upstream. */
	unsigned char  bank[64];
};

#endif

#endif
