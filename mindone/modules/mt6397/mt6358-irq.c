// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 MediaTek Inc.

#include <linux/interrupt.h>
#include "linux/mfd/mt6357/core.h"
#include "linux/mfd/mt6357/registers.h"
#include "linux/mfd/mt6358/core.h"
#include "linux/mfd/mt6358/registers.h"
#include "linux/mfd/mt6359p/core.h"
#include "linux/mfd/mt6359p/registers.h"
#include "linux/mfd/mt6366/core.h"
#include "linux/mfd/mt6397/core.h"
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>

#define MTK_PMIC_REG_WIDTH 16

static struct irq_top_t mt6357_ints[] = {
	MT6357_TOP_GEN(BUCK),
	MT6357_TOP_GEN(LDO),
	MT6357_TOP_GEN(PSC),
	MT6357_TOP_GEN(SCK),
	MT6357_TOP_GEN(BM),
	MT6357_TOP_GEN(HK),
	MT6357_TOP_GEN(AUD),
	MT6357_TOP_GEN(MISC),
};

static struct irq_top_t mt6358_ints[] = {
	MT6358_TOP_GEN(BUCK),
	MT6358_TOP_GEN(LDO),
	MT6358_TOP_GEN(PSC),
	MT6358_TOP_GEN(SCK),
	MT6358_TOP_GEN(BM),
	MT6358_TOP_GEN(HK),
	MT6358_TOP_GEN(AUD),
	MT6358_TOP_GEN(MISC),
};

static struct irq_top_t mt6359p_ints[] = {
	MT6359P_TOP_GEN(BUCK),
	MT6359P_TOP_GEN(LDO),
	MT6359P_TOP_GEN(PSC),
	MT6359P_TOP_GEN(SCK),
	MT6359P_TOP_GEN(BM),
	MT6359P_TOP_GEN(HK),
	MT6359P_TOP_GEN(AUD),
	MT6359P_TOP_GEN(MISC),
};

static struct irq_top_t mt6366_ints[] = {
	MT6366_TOP_GEN(BUCK),
	MT6366_TOP_GEN(LDO),
	MT6366_TOP_GEN(PSC),
	MT6366_TOP_GEN(SCK),
	MT6366_TOP_GEN(BM),
	MT6366_TOP_GEN(HK),
	MT6366_TOP_GEN(AUD),
	MT6366_TOP_GEN(MISC),
};


static struct pmic_irq_data mt6357_irqd = {
	.num_top = ARRAY_SIZE(mt6357_ints),
	.num_pmic_irqs = MT6357_IRQ_NR,
	.top_int_status_reg = MT6357_TOP_INT_STATUS0,
	.pmic_ints = mt6357_ints,
};

static struct pmic_irq_data mt6358_irqd = {
	.num_top = ARRAY_SIZE(mt6358_ints),
	.num_pmic_irqs = MT6358_IRQ_NR,
	.top_int_status_reg = MT6358_TOP_INT_STATUS0,
	.pmic_ints = mt6358_ints,
};

static struct pmic_irq_data mt6359p_irqd = {
	.num_top = ARRAY_SIZE(mt6359p_ints),
	.num_pmic_irqs = MT6359P_IRQ_NR,
	.top_int_status_reg = MT6359P_TOP_INT_STATUS0,
	.pmic_ints = mt6359p_ints,
};

static struct pmic_irq_data mt6366_irqd = {
	.num_top = ARRAY_SIZE(mt6366_ints),
	.num_pmic_irqs = MT6366_IRQ_NR,
	.top_int_status_reg = MT6358_TOP_INT_STATUS0,
	.pmic_ints = mt6366_ints,
};

static void pmic_irq_enable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	irqd->enable_hwirq[hwirq] = true;
}

static void pmic_irq_disable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	irqd->enable_hwirq[hwirq] = false;
}

static void pmic_irq_lock(struct irq_data *data)
{
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);

	mutex_lock(&chip->irqlock);
}

static void pmic_irq_sync_unlock(struct irq_data *data)
{
	unsigned int i, top_gp, gp_offset, en_reg, int_regs, shift;
	int ret;
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	for (i = 0; i < irqd->num_pmic_irqs; i++) {
		if (irqd->enable_hwirq[i] == irqd->cache_hwirq[i])
			continue;

		/* Find out the IRQ group */
		top_gp = 0;
		while ((top_gp + 1) < irqd->num_top &&
		       i >= irqd->pmic_ints[top_gp + 1].hwirq_base)
			top_gp++;

		/* Find the IRQ registers */
		gp_offset = i - irqd->pmic_ints[top_gp].hwirq_base;
		int_regs = gp_offset / MTK_PMIC_REG_WIDTH;
		shift = gp_offset % MTK_PMIC_REG_WIDTH;
		en_reg = irqd->pmic_ints[top_gp].en_reg +
			 (irqd->pmic_ints[top_gp].en_reg_shift * int_regs);

		ret = regmap_update_bits(chip->regmap, en_reg, BIT(shift),
				   irqd->enable_hwirq[i] << shift);

		/* MINDONE-PMIC-RESYNC (F3341): the pwrap/SPMI write's return code used to be
		 * ignored, updating the cache unconditionally. A single write dropped
		 * around resume left the enable bit 0 in hardware while the cache
		 * claimed 1 -- every later enable was skipped as "no change" and the
		 * power key (hwirq 48/50) stayed dead until reboot. Keep the cache
		 * dirty on failure so the next sync retries. */
		if (ret) {
			dev_err(chip->dev,
				"MINDONE-PMIC-RESYNC: en_reg 0x%x bit %u write failed (%d), kept dirty\n",
				en_reg, shift, ret);
			continue;
		}

		irqd->cache_hwirq[i] = irqd->enable_hwirq[i];
	}
	mutex_unlock(&chip->irqlock);
}

/* MINDONE-PMIC-RESYNC (F3341): after every suspend cycle, rewrite ALL PMIC
 * interrupt enable registers from the SW enable state. s2idle proved
 * (F3339/F3341) the PMIC interrupt chain can die after resume, consistent
 * with an enable-bit write lost around resume plus the cache poisoning fixed
 * above. Idempotent, ~12 pwrap writes, runs after resume completes. */
static struct mt6397_chip *mindone_resync_chip;

static int mindone_pmic_irq_pm_event(struct notifier_block *nb,
				     unsigned long event, void *unused)
{
	struct mt6397_chip *chip = READ_ONCE(mindone_resync_chip);
	struct pmic_irq_data *irqd;
	unsigned int top_gp, j, bit, hw, base, lim, en_reg, val;
	int ret, healed = 0, failed = 0;

	if (event != PM_POST_SUSPEND || !chip)
		return NOTIFY_DONE;

	irqd = chip->irq_data;
	mutex_lock(&chip->irqlock);
	for (top_gp = 0; top_gp < irqd->num_top; top_gp++) {
		base = irqd->pmic_ints[top_gp].hwirq_base;
		lim = (top_gp + 1 < irqd->num_top) ?
			irqd->pmic_ints[top_gp + 1].hwirq_base :
			irqd->num_pmic_irqs;
		for (j = 0; j < irqd->pmic_ints[top_gp].num_int_regs; j++) {
			en_reg = irqd->pmic_ints[top_gp].en_reg +
				 irqd->pmic_ints[top_gp].en_reg_shift * j;
			val = 0;
			for (bit = 0; bit < MTK_PMIC_REG_WIDTH; bit++) {
				hw = base + MTK_PMIC_REG_WIDTH * j + bit;
				if (hw >= lim)
					break;
				if (irqd->enable_hwirq[hw])
					val |= BIT(bit);
			}
			/* Full-register write is safe (not a clobber): the TOP_INT_CON
			 * enable registers are dedicated 16-bit IRQ-enable words — the
			 * init path itself uses regmap_write(en_reg, 0) to mask all, so
			 * rewriting the whole computed mask cannot disturb other functions. */
			ret = regmap_write(chip->regmap, en_reg, val);
			if (ret) {
				dev_err(chip->dev,
					"MINDONE-PMIC-RESYNC: en_reg 0x%x rewrite failed (%d)\n",
					en_reg, ret);
				failed++;
				continue;
			}
			for (bit = 0; bit < MTK_PMIC_REG_WIDTH; bit++) {
				hw = base + MTK_PMIC_REG_WIDTH * j + bit;
				if (hw >= lim)
					break;
				if (irqd->cache_hwirq[hw] !=
				    irqd->enable_hwirq[hw])
					healed++;
				irqd->cache_hwirq[hw] = irqd->enable_hwirq[hw];
			}
		}
	}
	mutex_unlock(&chip->irqlock);

	if (healed || failed)
		dev_err(chip->dev,
			"MINDONE-PMIC-RESYNC: post-suspend resync healed=%d failed=%d\n",
			healed, failed);

	return NOTIFY_DONE;
}

static struct notifier_block mindone_resync_nb = {
	.notifier_call = mindone_pmic_irq_pm_event,
};

static struct irq_chip mt6358_irq_chip = {
	.name = "mt6358-irq",
	.flags = IRQCHIP_SKIP_SET_WAKE,
	.irq_enable = pmic_irq_enable,
	.irq_disable = pmic_irq_disable,
	.irq_bus_lock = pmic_irq_lock,
	.irq_bus_sync_unlock = pmic_irq_sync_unlock,
};

static void mt6358_irq_sp_handler(struct mt6397_chip *chip,
				  unsigned int top_gp)
{
	unsigned int irq_status, sta_reg, status;
	unsigned int hwirq, virq;
	int i, j, ret;
	struct pmic_irq_data *irqd = chip->irq_data;

	for (i = 0; i < irqd->pmic_ints[top_gp].num_int_regs; i++) {
		sta_reg = irqd->pmic_ints[top_gp].sta_reg +
			irqd->pmic_ints[top_gp].sta_reg_shift * i;

		ret = regmap_read(chip->regmap, sta_reg, &irq_status);
		if (ret) {
			dev_err(chip->dev,
				"Failed to read IRQ status, ret=%d\n", ret);
			return;
		}

		if (!irq_status)
			continue;

		status = irq_status;
		do {
			j = __ffs(status);

			hwirq = irqd->pmic_ints[top_gp].hwirq_base +
				MTK_PMIC_REG_WIDTH * i + j;

			virq = irq_find_mapping(chip->irq_domain, hwirq);

			log_threaded_irq_wakeup_reason(virq, chip->irq);

			if (virq)
				handle_nested_irq(virq);
			dev_info(chip->dev,
				"Reg[0x%x]=0x%x,hwirq=%d,type=%d\n",
				sta_reg, irq_status, hwirq,
				irq_get_trigger_type(virq));

			status &= ~BIT(j);
		} while (status);

		regmap_write(chip->regmap, sta_reg, irq_status);
	}
}

static irqreturn_t mt6358_irq_handler(int irq, void *data)
{
	struct mt6397_chip *chip = data;
	struct pmic_irq_data *irqd = chip->irq_data;
	unsigned int bit, i, top_irq_status = 0;
	int ret;

	ret = regmap_read(chip->regmap,
			  irqd->top_int_status_reg,
			  &top_irq_status);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read status from the device, ret=%d\n", ret);
		return IRQ_NONE;
	}

	for (i = 0; i < irqd->num_top; i++) {
		bit = BIT(irqd->pmic_ints[i].top_offset);
		if (top_irq_status & bit) {
			mt6358_irq_sp_handler(chip, i);
			top_irq_status &= ~bit;
			if (!top_irq_status)
				break;
		}
	}

	return IRQ_HANDLED;
}

static int pmic_irq_domain_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hw)
{
	struct mt6397_chip *mt6397 = d->host_data;

	irq_set_chip_data(irq, mt6397);
	irq_set_chip_and_handler(irq, &mt6358_irq_chip, handle_level_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mt6358_irq_domain_ops = {
	.map = pmic_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

int mt6358_irq_init(struct mt6397_chip *chip)
{
	int i, j, ret;
	struct pmic_irq_data *irqd;

	switch (chip->chip_id) {
	case MT6357_CHIP_ID:
		chip->irq_data = &mt6357_irqd;
		break;

	case MT6358_CHIP_ID:
		chip->irq_data = &mt6358_irqd;
		break;

	case MT6359P_CHIP_ID:
		chip->irq_data = &mt6359p_irqd;
		break;

	case MT6366_CHIP_ID:
		chip->irq_data = &mt6366_irqd;
		break;

	default:
		dev_err(chip->dev, "unsupported chip: 0x%x\n", chip->chip_id);
		return -ENODEV;
	}

	mutex_init(&chip->irqlock);
	irqd = chip->irq_data;
	irqd->enable_hwirq = devm_kcalloc(chip->dev,
					  irqd->num_pmic_irqs,
					  sizeof(*irqd->enable_hwirq),
					  GFP_KERNEL);
	if (!irqd->enable_hwirq) {
		dev_dbg(chip->dev, "enable hwirq fail\n");
		return -ENOMEM;
	}

	irqd->cache_hwirq = devm_kcalloc(chip->dev,
					 irqd->num_pmic_irqs,
					 sizeof(*irqd->cache_hwirq),
					 GFP_KERNEL);
	if (!irqd->cache_hwirq) {
		dev_dbg(chip->dev, "%s cache hwirq fail\n", __func__);
		return -ENOMEM;
	}

	/* Disable all interrupts for initializing */
	for (i = 0; i < irqd->num_top; i++) {
		for (j = 0; j < irqd->pmic_ints[i].num_int_regs; j++)
			regmap_write(chip->regmap,
				     irqd->pmic_ints[i].en_reg +
				     irqd->pmic_ints[i].en_reg_shift * j, 0);
	}

	chip->irq_domain = irq_domain_add_linear(chip->dev->of_node,
						 irqd->num_pmic_irqs,
						 &mt6358_irq_domain_ops, chip);
	if (!chip->irq_domain) {
		dev_err(chip->dev, "Could not create IRQ domain\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					mt6358_irq_handler, IRQF_ONESHOT,
					mt6358_irq_chip.name, chip);
	if (ret) {
		dev_err(chip->dev, "Failed to register IRQ=%d, ret=%d\n",
			chip->irq, ret);
		return ret;
	}

	enable_irq_wake(chip->irq);

	/* MINDONE-PMIC-RESYNC: arm the post-suspend enable-register resync.
	 * mt6397 is mfd core, it never unloads on this device — no
	 * unregister path needed (and none exists in this init-only file). */
	WRITE_ONCE(mindone_resync_chip, chip);
	register_pm_notifier(&mindone_resync_nb);

	return ret;
}
