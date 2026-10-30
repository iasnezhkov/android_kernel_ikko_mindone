// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include "adapter.h"

struct mindone_chr_controller {
	struct mutex detector;
	struct work_struct work;
	struct list_head voltage_node;
	void *iio;
	void *regmap;
	void *bc12;
	void *irq;
	struct power_supply *supply;
	u32 bootmode;
	bool stopping;
	bool active;
};

static DEFINE_MUTEX(mindone_chr_voltage_lock);
static LIST_HEAD(mindone_chr_voltages);

static int mindone_chr_status(int ret)
{
	return ret > 0 ? -EIO : ret;
}

static int mindone_chr_owned_error(void *pointer)
{
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);
	return pointer ? 0 : -ENODEV;
}

static int mindone_chr_detect(struct mindone_chr_controller *controller,
			      bool initial)
{
	unsigned int sample = 0;
	int ret, notification;
	int charger_type = 0, usb_type = 0;
	bool attached;

	ret = mindone_chr_status(mindone_chr_regmap_read(controller->regmap,
						       0xa88, &sample));
	if (ret)
		return ret;
	attached = !!(sample & BIT(5));
	if (initial && !attached) {
		if ((controller->bootmode & ~1U) == 8)
			mindone_chr_power_off();
		return 0;
	}
	ret = mindone_chr_status(mindone_chr_bc12_set_online(controller->bc12,
							  attached));
	if (ret)
		return ret;
	if (attached)
		mindone_chr_settle();
	ret = mindone_chr_status(mindone_chr_bc12_type(controller->bc12,
						     &charger_type));
	if (!ret)
		ret = mindone_chr_status(mindone_chr_bc12_usb_type(controller->bc12,
								 &usb_type));
	notification = mindone_chr_status(mindone_chr_bc12_changed(controller->bc12,
								attached));
	if (ret)
		return ret;
	if (notification)
		return notification;
	pr_notice("Charger attached=%s type=%d usb_type=%d\n",
		  attached ? "true" : "false", charger_type, usb_type);
	return 0;
}

static void mindone_chr_event(struct mindone_chr_controller *controller,
			      bool initial)
{
	int ret;

	if (smp_load_acquire(&controller->stopping))
		return;
	mutex_lock(&controller->detector);
	if (!smp_load_acquire(&controller->stopping)) {
		ret = mindone_chr_detect(controller, initial);
		if (ret)
			pr_warn("Charger detection failed: %d\n", ret);
	}
	mutex_unlock(&controller->detector);
}

static void mindone_chr_initial(struct work_struct *work)
{
	struct mindone_chr_controller *controller =
		container_of(work, struct mindone_chr_controller, work);

	mindone_chr_event(controller, true);
}

static void mindone_chr_interrupt(void *context)
{
	mindone_chr_event(context, false);
}

static int mindone_chr_voltage_get(struct power_supply *supply,
				   enum power_supply_property property,
				   union power_supply_propval *value)
{
	struct mindone_chr_controller *controller;
	int processed = 0, ret = -ENODEV;

	if (property != POWER_SUPPLY_PROP_VOLTAGE_NOW)
		return -EINVAL;
	mutex_lock(&mindone_chr_voltage_lock);
	list_for_each_entry(controller, &mindone_chr_voltages, voltage_node) {
		if (controller->supply != supply)
			continue;
		if (!controller->active ||
		    smp_load_acquire(&controller->stopping))
			break;
		ret = mindone_chr_iio_read(controller->iio, &processed);
		if (ret < 0)
			break;
		if (processed < 0 || processed > INT_MAX / 369) {
			ret = -ERANGE;
			break;
		}
		value->intval = processed * 369 / 39;
		ret = 0;
		break;
	}
	mutex_unlock(&mindone_chr_voltage_lock);
	return ret;
}

static const enum power_supply_property mindone_chr_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static const struct power_supply_desc mindone_chr_supply_desc = {
	.name = "mtk_charger_type",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = mindone_chr_properties,
	.num_properties = ARRAY_SIZE(mindone_chr_properties),
	.get_property = mindone_chr_voltage_get,
};

void mindone_chr_remove(void *binding)
{
	struct mindone_chr_controller *controller = binding;

	smp_store_release(&controller->stopping, true);
	if (controller->irq)
		mindone_chr_irq_unregister(controller->irq);
	flush_work(&controller->work);
	if (controller->supply) {
		mutex_lock(&mindone_chr_voltage_lock);
		list_del(&controller->voltage_node);
		mutex_unlock(&mindone_chr_voltage_lock);
		power_supply_unregister(controller->supply);
	}
	if (controller->regmap)
		mindone_chr_regmap_put(controller->regmap);
	if (controller->bc12)
		mindone_chr_bc12_put(controller->bc12);
	kfree(controller);
}

void *mindone_chr_probe(void *device)
{
	struct platform_device *pdev = device;
	struct power_supply_config config = {
		.of_node = pdev->dev.of_node,
	};
	struct mindone_chr_controller *controller;
	unsigned char tag[16];
	u32 bootmode = 0;
	void *resource;
	int ret;
	bool active;

	ret = mindone_chr_boot_tag(pdev, tag);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret)
		memcpy(&bootmode, tag + 8, sizeof(bootmode));
	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);
	mutex_init(&controller->detector);
	INIT_WORK(&controller->work, mindone_chr_initial);
	INIT_LIST_HEAD(&controller->voltage_node);
	controller->bootmode = bootmode;
	resource = mindone_chr_iio_get(pdev);
	ret = mindone_chr_owned_error(resource);
	if (ret)
		goto fail;
	controller->iio = resource;
	resource = mindone_chr_regmap_get(pdev);
	ret = mindone_chr_owned_error(resource);
	if (ret)
		goto fail;
	controller->regmap = resource;
	resource = mindone_chr_bc12_get(pdev);
	ret = mindone_chr_owned_error(resource);
	if (ret)
		goto fail;
	controller->bc12 = resource;
	resource = power_supply_register(&pdev->dev, &mindone_chr_supply_desc,
					 &config);
	ret = mindone_chr_owned_error(resource);
	if (ret)
		goto fail;
	controller->supply = resource;
	mutex_lock(&mindone_chr_voltage_lock);
	list_add_tail(&controller->voltage_node, &mindone_chr_voltages);
	mutex_unlock(&mindone_chr_voltage_lock);
	active = mindone_chr_irq_active(pdev) != 0;
	if (active) {
		resource = mindone_chr_irq_register(pdev, mindone_chr_interrupt,
						    controller);
		ret = mindone_chr_owned_error(resource);
		if (ret)
			goto fail;
		controller->irq = resource;
	}
	mutex_lock(&mindone_chr_voltage_lock);
	controller->active = true;
	mutex_unlock(&mindone_chr_voltage_lock);
	if (active && !schedule_work(&controller->work)) {
		ret = -EBUSY;
		goto fail;
	}
	return controller;

fail:
	mindone_chr_remove(controller);
	return ERR_PTR(ret);
}

static int __init mindone_chr_init(void)
{
	return mindone_chr_status(mindone_chr_driver_register());
}

static void __exit mindone_chr_exit(void)
{
	mindone_chr_driver_unregister();
}

module_init(mindone_chr_init);
module_exit(mindone_chr_exit);

MODULE_AUTHOR("MindOne Contributors");
MODULE_DESCRIPTION("MTK charger detection with owned resources");
MODULE_LICENSE("GPL");
