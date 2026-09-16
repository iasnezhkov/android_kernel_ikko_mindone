// SPDX-License-Identifier: GPL-2.0
/*
 * mtk_chg_det.c -- MTK Charger Type Detection Driver. Reconstructed from
 * disassembly of the prebuilt mtk_chg_det.ko (vermagic 5.10.233, no public
 * source), generalized from MediaTek's mt6357-charger-type.c reference
 * driver. Full trace and confirmed differences: re510-modules and
 * modules/charger-mtk-chg-det/RECONSTRUCTION.md. MINDONE (CHRDET-2908)
 * ported for kernel 6.1.175, one deviation in mtk_chr_det_probe() -- CHRDET-2908.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

/* CHRDET status bit — PMIC regmap, verified from disasm literal operands
 * (regmap_read(regmap, 0xA88, &v); chrdet = (v >> 5) & 1;) in both
 * chrdet_int_handler() and do_charger_detection_work().
 */
#define MTK_RGS_CHRDET_ADDR	0xa88
#define MTK_RGS_CHRDET_MASK	0x1
#define MTK_RGS_CHRDET_SHIFT	5

/* VBUS divider used by get_vbus_voltage()-equivalent scaling in
 * psy_chr_type_get_property(): val * (R1+R2) / R2. Confirmed by the
 * umul-magic reciprocal constant 0x219BB955 seen in the disassembly,
 * which is exactly the compiler's division-by-3900 (=39*100) constant
 * for a (val*369*100)/39/100 style expression with R1=330, R2=39 —
 * identical resistor values to the mt6357-charger-type.c reference driver.
 */
#define R_CHARGER_1	330
#define R_CHARGER_2	39

struct mtk_chr_det_info {
	struct mt6397_chip *chip;
	struct regmap *regmap;
	struct platform_device *pdev;
	struct power_supply *bc12_psy;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;

	struct iio_channel *chan_vbus;
	struct work_struct chr_work;

	struct notifier_block pm_nb;
	bool is_suspend;

	u32 bootmode;
	u32 boottype;
	bool vcdt_int_active;
};

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

static enum power_supply_property chr_type_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

/*
 * bc11_set_register_value()/get_register_value() — present in the shipped
 * .ko as global symbols but never called from anywhere inside it (no
 * internal `bl` reference to either in the disassembly). Kept as
 * non-static helpers to match the original symbol table; harmless dead
 * code, exactly as in the real binary.
 */
void bc11_set_register_value(struct regmap *map, unsigned int addr,
			      unsigned int mask, unsigned int shift,
			      unsigned int val)
{
	regmap_update_bits(map, addr, mask << shift, val << shift);
}

unsigned int get_register_value(struct regmap *map, unsigned int addr,
				 unsigned int mask, unsigned int shift)
{
	unsigned int value = 0;

	regmap_read(map, addr, &value);
	return (value & (mask << shift)) >> shift;
}

/*
 * do_charger_detect() — tells the external "bc12" power supply
 * (info->bc12_psy, resolved via DT phandle "bc12" in probe) that a
 * charger is online/offline, waits for it to settle its own BC1.2
 * detection if turning on, then reads back and logs the TYPE/USB_TYPE it
 * decided. Global (non-static): matches the shipped symbol table.
 */
void do_charger_detect(struct mtk_chr_det_info *info, bool en)
{
	union power_supply_propval prop_online = { .intval = en };
	union power_supply_propval prop_type = { 0 };
	union power_supply_propval prop_usb_type = { 0 };

	power_supply_set_property(info->bc12_psy, POWER_SUPPLY_PROP_ONLINE,
				   &prop_online);

	if (en)
		mdelay(700);

	power_supply_get_property(info->bc12_psy, POWER_SUPPLY_PROP_TYPE,
				   &prop_type);
	power_supply_get_property(info->bc12_psy, POWER_SUPPLY_PROP_USB_TYPE,
				   &prop_usb_type);
	pr_notice("%s: type:%d usb_type:%d\n", __func__,
		  prop_type.intval, prop_usb_type.intval);

	power_supply_changed(info->bc12_psy);
}

/*
 * do_charger_detection_work() — one-shot initial check, scheduled once
 * from probe() via schedule_work(). If a charger is present, kicks
 * do_charger_detect(). If not, and the bootloader entered
 * charger/low-power-off-charging boot mode (bootmode 8 or 9) with no
 * charger actually attached, powers the device back off — after waiting
 * for any in-flight suspend to finish (is_suspend, see file header: this
 * wait is effectively dead code since register_pm_notifier() is never
 * called anywhere in the shipped object, so is_suspend can never become
 * true; reproduced as-is).
 */
static void do_charger_detection_work(struct work_struct *data)
{
	struct mtk_chr_det_info *info =
		container_of(data, struct mtk_chr_det_info, chr_work);
	unsigned int val = 0;
	unsigned int chrdet;

	regmap_read(info->regmap, MTK_RGS_CHRDET_ADDR, &val);
	chrdet = (val >> MTK_RGS_CHRDET_SHIFT) & MTK_RGS_CHRDET_MASK;
	pr_notice("%s: reg=0x%x chrdet=%d\n", __func__, val, chrdet);

	if (chrdet) {
		do_charger_detect(info, true);
		return;
	}

	if ((info->bootmode & ~1) != 8)
		return;

	pr_info("%s: Unplug Charger/USB\n", __func__);
	while (info->is_suspend) {
		pr_info("%s: wait for resume before power off\n", __func__);
		msleep(20);
	}
	pr_info("%s: power off\n", __func__);
	kernel_power_off();
}

/*
 * chrdet_int_handler() — ongoing CHRDET IRQ handler. Unlike
 * do_charger_detection_work(), does NOT power the device off on unplug;
 * it always forwards the current state to do_charger_detect(). Global
 * (non-static): matches shipped symbol table.
 */
irqreturn_t chrdet_int_handler(int irq, void *data)
{
	struct mtk_chr_det_info *info = data;
	unsigned int val = 0;
	unsigned int chrdet;

	regmap_read(info->regmap, MTK_RGS_CHRDET_ADDR, &val);
	chrdet = (val >> MTK_RGS_CHRDET_SHIFT) & MTK_RGS_CHRDET_MASK;

	if (!chrdet && (info->bootmode & ~1) == 8)
		pr_info("%s: Unplug Charger/USB\n", __func__);

	pr_notice("%s: chrdet:%d\n", __func__, chrdet);
	do_charger_detect(info, chrdet);

	return IRQ_HANDLED;
}

/*
 * psy_chr_type_get_property() — the locally registered "mtk_charger_type"
 * power supply's only real property is VOLTAGE_NOW (vbus, scaled through
 * the R_CHARGER_1/R_CHARGER_2 divider via the pmic_vbus IIO channel);
 * everything else returns -EINVAL (confirmed: single `cmp psp,#12`
 * branch, no other case handled).
 */
static int psy_chr_type_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct mtk_chr_det_info *info = power_supply_get_drvdata(psy);
	int vbus = 0;
	int ret;

	/* Traced on every get_property call - several times a second for the life of the device.
	 * The fault paths below still print; this one only said that a read happened. */
	pr_debug("%s: prop:%d\n", __func__, psp);

	if (psp != POWER_SUPPLY_PROP_VOLTAGE_NOW)
		return -EINVAL;

	if (!IS_ERR_OR_NULL(info->chan_vbus)) {
		ret = iio_read_channel_processed(info->chan_vbus, &vbus);
		if (ret < 0)
			pr_notice("%s: read fail, ret=%d\n", __func__, ret);
	} else {
		pr_notice("%s: chan error\n", __func__);
	}

	val->intval = (vbus * (R_CHARGER_1 + R_CHARGER_2)) / R_CHARGER_2;
	pr_debug("%s: vbus=%d\n", __func__, val->intval);

	return 0;
}

/*
 * mtk_chr_det_pm_event() — PM notifier callback, wired into
 * info->pm_nb.notifier_call by probe() but never actually registered via
 * register_pm_notifier() anywhere in the shipped object (see file
 * header). Kept for symbol/behavioral fidelity; harmless unreachable code
 * as in the real binary.
 */
static int mtk_chr_det_pm_event(struct notifier_block *notifier,
				 unsigned long pm_event, void *unused)
{
	struct mtk_chr_det_info *info =
		container_of(notifier, struct mtk_chr_det_info, pm_nb);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		info->is_suspend = true;
		pr_notice("%s: enter suspend\n", __func__);
		break;
	case PM_POST_SUSPEND:
		info->is_suspend = false;
		pr_notice("%s: exit suspend\n", __func__);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int mtk_chr_det_probe(struct platform_device *pdev)
{
	struct mtk_chr_det_info *info;
	struct iio_channel *chan_vbus;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *boot_node;
	struct tag_bootmode *tag;
	int irq;
	int ret;

	pr_notice("%s: starts\n", __func__);

	/* Probe-deferral check only; the channel is fetched again below and
	 * stored for real use. Matches the shipped binary, which calls
	 * devm_iio_channel_get(&pdev->dev, "pmic_vbus") twice.
	 */
	chan_vbus = devm_iio_channel_get(dev, "pmic_vbus");
	if (IS_ERR(chan_vbus)) {
		pr_notice("%s: requests probe deferral ret:%ld\n", __func__,
			  PTR_ERR(chan_vbus));
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->chip = dev_get_drvdata(pdev->dev.parent);
	info->regmap = info->chip->regmap;
	info->pdev = pdev;
	dev_set_drvdata(dev, info);

	boot_node = of_parse_phandle(np, "bootmode", 0);
	if (!boot_node) {
		pr_notice("%s: failed to get boot mode phandle\n", __func__);
	} else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,
							       "atag,boot",
							       NULL);
		if (!tag) {
			pr_notice("%s: failed to get atag,boot\n", __func__);
		} else {
			pr_notice("%s: size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
				  __func__, tag->size, tag->tag,
				  tag->bootmode, tag->boottype);
			info->bootmode = tag->bootmode;
			info->boottype = tag->boottype;
		}
	}

	info->bc12_psy = devm_power_supply_get_by_phandle(dev, "bc12");
	/*
	 * MINDONE (CHRDET-2908): the ONE deliberate deviation from byte-for-byte
	 * fidelity here. The shipped binary does `return PTR_ERR(info->bc12_psy)`
	 * without distinguishing NULL from IS_ERR; since PTR_ERR(NULL)==0, a
	 * not-yet-registered "bc12" phandle psy would look like a "successful"
	 * probe with a dangling NULL pointer, dereferenced on the first
	 * do_charger_detect() call. Return -EPROBE_DEFER; see CHRDET-2908.
	 */
	if (!info->bc12_psy) {
		pr_notice("%s: bc12_psy not ready yet, deferring\n", __func__);
		return -EPROBE_DEFER;
	}
	if (IS_ERR(info->bc12_psy)) {
		pr_notice("%s: get bc12_psy fail, ret=%ld\n", __func__,
			  PTR_ERR(info->bc12_psy));
		return PTR_ERR(info->bc12_psy);
	}

	info->psy_desc.name = "mtk_charger_type";
	info->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc.properties = chr_type_properties;
	info->psy_desc.num_properties = ARRAY_SIZE(chr_type_properties);
	info->psy_desc.get_property = psy_chr_type_get_property;
	info->psy_cfg.drv_data = info;
	info->psy_cfg.of_node = np;

	/* Return value intentionally not checked — matches the shipped
	 * binary, which does not branch on power_supply_register()'s
	 * result here.
	 */
	info->psy = power_supply_register(dev, &info->psy_desc,
					   &info->psy_cfg);

	info->chan_vbus = devm_iio_channel_get(dev, "pmic_vbus");
	if (IS_ERR(info->chan_vbus))
		pr_notice("%s: chan_vbus auxadc get fail, ret=%ld\n",
			  __func__, PTR_ERR(info->chan_vbus));

	info->pm_nb.notifier_call = mtk_chr_det_pm_event;

	info->vcdt_int_active = of_find_property(np, "vcdt_int_active",
						  NULL) != NULL;
	if (info->vcdt_int_active) {
		INIT_WORK(&info->chr_work, do_charger_detection_work);
		schedule_work(&info->chr_work);

		irq = platform_get_irq_byname(pdev, "CHRDET");
		ret = devm_request_threaded_irq(dev, irq, NULL,
						 chrdet_int_handler,
						 IRQF_TRIGGER_HIGH, "CHRDET",
						 info);
		if (ret < 0)
			pr_notice("%s: request chrdet irq fail\n", __func__);
	}

	pr_notice("%s: done\n", __func__);

	return 0;
}

static void mtk_chr_det_remove(struct platform_device *pdev)
{
	struct mtk_chr_det_info *info = platform_get_drvdata(pdev);

	if (info)
		devm_kfree(&pdev->dev, info);
}

static const struct of_device_id mtk_chr_det_of_match[] = {
	{ .compatible = "mediatek,mtk-chr-det", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_chr_det_of_match);

static struct platform_driver mtk_chr_det_driver = {
	.probe = mtk_chr_det_probe,
	.remove_new = mtk_chr_det_remove,
	.driver = {
		.name = "mtk-charger-detection",
		.of_match_table = mtk_chr_det_of_match,
	},
};

module_platform_driver(mtk_chr_det_driver);

MODULE_AUTHOR("gerard.huangn <gerard.huang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Type Detection Driver");
MODULE_LICENSE("GPL");
