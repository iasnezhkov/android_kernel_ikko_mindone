// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include "adapter.h"

struct mindone_chr_map {
	struct device *supplier;
	struct regmap *map;
};

struct mindone_chr_irq {
	void (*callback)(void *);
	void *context;
	int number;
};

static int mindone_chr_link_locked(struct device *consumer, struct device *supplier)
{
	if (!device_is_bound(supplier) ||
	    READ_ONCE(supplier->links.status) != DL_DEV_DRIVER_BOUND)
		return -EPROBE_DEFER;
	if (!device_link_add(consumer, supplier, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	return 0;
}

int mindone_chr_boot_tag(void *device, unsigned char *tag)
{
	struct platform_device *pdev = device;
	struct device_node *boot;
	const void *property;
	int size, ret = 0;

	boot = of_parse_phandle(pdev->dev.of_node, "bootmode", 0);
	if (!boot)
		return 0;
	property = of_get_property(boot, "atag,boot", &size);
	if (property) {
		if (size < 16) {
			ret = -EINVAL;
		} else {
			memcpy(tag, property, 16);
			ret = 1;
		}
	}
	of_node_put(boot);
	return ret;
}

int mindone_chr_irq_active(void *device)
{
	struct platform_device *pdev = device;

	return of_property_read_bool(pdev->dev.of_node, "vcdt_int_active");
}

void *mindone_chr_iio_get(void *device)
{
	struct platform_device *pdev = device;

	return devm_iio_channel_get(&pdev->dev, "pmic_vbus");
}

int mindone_chr_iio_read(void *channel, int *value)
{
	return iio_read_channel_processed(channel, value);
}

void *mindone_chr_regmap_get(void *device)
{
	struct platform_device *pdev = device;
	struct device *parent = pdev->dev.parent;
	struct mindone_chr_map *owned;
	struct device *supplier;
	int ret;

	if (!parent || !parent->parent)
		return ERR_PTR(-ENODEV);
	supplier = get_device(parent->parent);
	owned = kzalloc(sizeof(*owned), GFP_KERNEL);
	if (!owned) {
		put_device(supplier);
		return ERR_PTR(-ENOMEM);
	}
	if (!device_trylock(supplier)) {
		ret = -EPROBE_DEFER;
		goto fail;
	}
	ret = mindone_chr_link_locked(&pdev->dev, supplier);
	if (ret)
		goto unlock;
	owned->map = dev_get_regmap(supplier, NULL);
	if (!owned->map || regmap_get_device(owned->map) != supplier) {
		ret = -ENODEV;
		goto unlock;
	}
	owned->supplier = supplier;
	device_unlock(supplier);
	return owned;

unlock:
	device_unlock(supplier);
fail:
	put_device(supplier);
	kfree(owned);
	return ERR_PTR(ret);
}

int mindone_chr_regmap_read(void *handle, unsigned int address, unsigned int *value)
{
	struct mindone_chr_map *owned = handle;

	return regmap_read(owned->map, address, value);
}

void mindone_chr_regmap_put(void *handle)
{
	struct mindone_chr_map *owned = handle;

	put_device(owned->supplier);
	kfree(owned);
}

void *mindone_chr_bc12_get(void *device)
{
	struct platform_device *pdev = device;
	struct platform_device *supplier;
	struct device_node *node;
	struct power_supply *psy;
	int ret;

	node = of_parse_phandle(pdev->dev.of_node, "bc12", 0);
	if (!node)
		return ERR_PTR(-ENODEV);
	supplier = of_find_device_by_node(node);
	of_node_put(node);
	if (!supplier)
		return ERR_PTR(-EPROBE_DEFER);
	if (!device_trylock(&supplier->dev)) {
		ret = -EPROBE_DEFER;
		goto put;
	}
	ret = mindone_chr_link_locked(&pdev->dev, &supplier->dev);
	if (ret)
		goto unlock;
	psy = power_supply_get_by_phandle(pdev->dev.of_node, "bc12");
	if (IS_ERR_OR_NULL(psy)) {
		ret = psy ? PTR_ERR(psy) : -EPROBE_DEFER;
		goto unlock;
	}
	if (psy->dev.parent != &supplier->dev) {
		power_supply_put(psy);
		ret = -ENODEV;
		goto unlock;
	}
	device_unlock(&supplier->dev);
	put_device(&supplier->dev);
	return psy;

unlock:
	device_unlock(&supplier->dev);
put:
	put_device(&supplier->dev);
	return ERR_PTR(ret);
}

int mindone_chr_bc12_set_online(void *supply, int online)
{
	union power_supply_propval value = { .intval = online };

	return power_supply_set_property(supply, POWER_SUPPLY_PROP_ONLINE, &value);
}

static int mindone_chr_bc12_read(void *supply, enum power_supply_property property,
			       int *value)
{
	union power_supply_propval sample;
	int ret = power_supply_get_property(supply, property, &sample);

	if (!ret)
		*value = sample.intval;
	return ret;
}

int mindone_chr_bc12_type(void *supply, int *value)
{
	return mindone_chr_bc12_read(supply, POWER_SUPPLY_PROP_TYPE, value);
}

int mindone_chr_bc12_usb_type(void *supply, int *value)
{
	return mindone_chr_bc12_read(supply, POWER_SUPPLY_PROP_USB_TYPE, value);
}

int mindone_chr_bc12_changed(void *supply, int online)
{
	return mindone_chr_bc12_set_online(supply, online);
}

void mindone_chr_bc12_put(void *supply)
{
	power_supply_put(supply);
}

void mindone_chr_settle(void)
{
	usleep_range(700000, 710000);
}

void mindone_chr_power_off(void)
{
	orderly_poweroff(true);
}

static irqreturn_t mindone_chr_irq_call(int number, void *data)
{
	struct mindone_chr_irq *irq = data;

	irq->callback(irq->context);
	return IRQ_HANDLED;
}

void *mindone_chr_irq_register(void *device, void (*callback)(void *), void *context)
{
	struct platform_device *pdev = device;
	struct mindone_chr_irq *irq;
	int number, ret;

	number = platform_get_irq_byname(pdev, "CHRDET");
	if (number < 0)
		return ERR_PTR(number);
	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq)
		return ERR_PTR(-ENOMEM);
	irq->callback = callback;
	irq->context = context;
	irq->number = number;
	ret = request_threaded_irq(number, NULL, mindone_chr_irq_call,
				   IRQF_TRIGGER_HIGH, "CHRDET", irq);
	if (ret) {
		kfree(irq);
		return ERR_PTR(ret);
	}
	return irq;
}

void mindone_chr_irq_unregister(void *handle)
{
	struct mindone_chr_irq *irq = handle;

	free_irq(irq->number, irq);
	kfree(irq);
}

static int mindone_chr_platform_probe(struct platform_device *pdev)
{
	void *binding = mindone_chr_probe(pdev);

	if (IS_ERR(binding))
		return PTR_ERR(binding);
	platform_set_drvdata(pdev, binding);
	return 0;
}

static void mindone_chr_platform_remove(struct platform_device *pdev)
{
	void *binding = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (binding)
		mindone_chr_remove(binding);
}

static const struct of_device_id mindone_chr_of_match[] = {
	{ .compatible = "mediatek,mtk-chr-det" },
	{},
};
MODULE_DEVICE_TABLE(of, mindone_chr_of_match);

static struct platform_driver mindone_chr_driver = {
	.probe = mindone_chr_platform_probe,
	.remove_new = mindone_chr_platform_remove,
	.driver = {
		.name = "mtk-charger-detection",
		.of_match_table = mindone_chr_of_match,
	},
};

int mindone_chr_driver_register(void)
{
	return platform_driver_register(&mindone_chr_driver);
}

void mindone_chr_driver_unregister(void)
{
	platform_driver_unregister(&mindone_chr_driver);
}
