// SPDX-License-Identifier: GPL-2.0
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sysfs.h>

#include "clk-mt6789-pwr.h"

#define MT6789_SPM_COMPATIBLE		"mediatek,mt6789-scpsys"
#define MT6789_SPM_PWR_STATUS		0x16c
#define MT6789_SPM_PWR_STATUS_2ND	0x170

#define MT6789_PWR_CON_RST_B		BIT(0)
#define MT6789_PWR_CON_ISO		BIT(1)
#define MT6789_PWR_CON_ON		BIT(2)
#define MT6789_PWR_CON_ON_2ND		BIT(3)
#define MT6789_PWR_CON_CLK_DIS		BIT(4)
#define MT6789_PWR_CON_MASK		(MT6789_PWR_CON_RST_B | MT6789_PWR_CON_ISO |	\
					 MT6789_PWR_CON_ON | MT6789_PWR_CON_ON_2ND |	\
					 MT6789_PWR_CON_CLK_DIS)
#define MT6789_PWR_CON_UP		(MT6789_PWR_CON_RST_B | MT6789_PWR_CON_ON |	\
					 MT6789_PWR_CON_ON_2ND)

struct mt6789_pwr_domain {
	const char *name;
	u32 sta_mask;
	u32 con_ofs;
};

static const struct mt6789_pwr_domain mt6789_pwr_domains[] = {
	{ "MFG0", BIT(2), 0x308 },
	{ "MFG1", BIT(3), 0x30c },
	{ "MFG3", BIT(5), 0x314 },
	{ "ISP", BIT(13), 0x334 },
	{ "IPE", BIT(15), 0x33c },
	{ "VDEC", BIT(16), 0x340 },
	{ "VENC", BIT(18), 0x348 },
	{ "DISP", BIT(21), 0x354 },
	{ "AUDIO", BIT(22), 0x358 },
	{ "CAM", BIT(23), 0x35c },
	{ "CAM_RAWA", BIT(24), 0x360 },
	{ "CAM_RAWB", BIT(25), 0x364 },
};

static DEFINE_MUTEX(mt6789_pwr_lock);
static void __iomem *mt6789_spm;

bool mt6789_clk_pwr_up(u32 domains)
{
	const struct mt6789_pwr_domain *d;
	void __iomem *spm;
	u32 sta;
	int i;

	if (!domains)
		return true;

	spm = READ_ONCE(mt6789_spm);
	if (!spm)
		return false;

	sta = readl(spm + MT6789_SPM_PWR_STATUS) & readl(spm + MT6789_SPM_PWR_STATUS_2ND);

	for (i = 0; i < ARRAY_SIZE(mt6789_pwr_domains); i++) {
		if (!(domains & BIT(i)))
			continue;

		d = &mt6789_pwr_domains[i];

		if (!(sta & d->sta_mask))
			return false;

		if ((readl(spm + d->con_ofs) & MT6789_PWR_CON_MASK) != MT6789_PWR_CON_UP)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(mt6789_clk_pwr_up);

void mt6789_clk_pwr_init(struct device *dev)
{
	struct device_node *node;
	void __iomem *spm;

	mutex_lock(&mt6789_pwr_lock);
	if (!mt6789_spm) {
		node = of_find_compatible_node(NULL, NULL, MT6789_SPM_COMPATIBLE);
		spm = node ? of_iomap(node, 0) : NULL;
		of_node_put(node);
		WRITE_ONCE(mt6789_spm, spm);
	}
	spm = mt6789_spm;
	mutex_unlock(&mt6789_pwr_lock);

	if (!spm)
		dev_warn(dev, "SPM is not mapped, CG status is never read\n");
	else
		dev_info(dev, "CG status is read only while its SPM power domains are up\n");
}
EXPORT_SYMBOL_GPL(mt6789_clk_pwr_init);

void mt6789_clk_pwr_exit(void)
{
	void __iomem *spm;

	mutex_lock(&mt6789_pwr_lock);
	spm = mt6789_spm;
	WRITE_ONCE(mt6789_spm, NULL);
	mutex_unlock(&mt6789_pwr_lock);

	if (spm)
		iounmap(spm);
}

static int mt6789_pwr_state_get(char *buf, const struct kernel_param *kp)
{
	void __iomem *spm = READ_ONCE(mt6789_spm);
	int len, i;

	if (!spm)
		return sysfs_emit(buf, "SPM not mapped\n");

	len = sysfs_emit(buf, "PWR_STATUS 0x%08x PWR_STATUS_2ND 0x%08x\n",
			 readl(spm + MT6789_SPM_PWR_STATUS),
			 readl(spm + MT6789_SPM_PWR_STATUS_2ND));

	for (i = 0; i < ARRAY_SIZE(mt6789_pwr_domains); i++)
		len += sysfs_emit_at(buf, len, "%s %s\n", mt6789_pwr_domains[i].name,
				     mt6789_clk_pwr_up(BIT(i)) ? "up" : "down");

	return len;
}

static const struct kernel_param_ops mt6789_pwr_state_ops = {
	.get = mt6789_pwr_state_get,
};
module_param_cb(pwr_state, &mt6789_pwr_state_ops, NULL, 0444);
MODULE_PARM_DESC(pwr_state,
	"SPM power status words and whether each power domain counts as up for CG status reads");
