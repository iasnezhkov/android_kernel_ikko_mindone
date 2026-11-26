// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/property.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include "adapter.h"

#define MINDONE_CHR_RETRY_MAX	10
#define MINDONE_CHR_RETRY_MS	100
#define MINDONE_CHR_WAKE_MS	2000

struct mindone_chr_controller {
	struct mutex detector;
	spinlock_t state_lock;
	struct delayed_work work;
	struct list_head voltage_node;
	void *iio;
	void *regmap;
	void *bc12;
	void *irq;
	void *ws;
	struct power_supply *supply;
	u32 bootmode;
	bool stopping;
	bool active;
	bool initial_pending;
	bool suspended;
	bool pending_resume;
	bool last_attached_valid;
	bool last_attached;
	unsigned int retry_count;
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
			      bool initial, bool *attached_out)
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
	if (attached_out)
		*attached_out = attached;
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
	unsigned long flags;
	bool attached = false;
	bool retry = false;
	int ret = 0;

	if (smp_load_acquire(&controller->stopping))
		return;
	mutex_lock(&controller->detector);
	if (!smp_load_acquire(&controller->stopping)) {
		spin_lock_irqsave(&controller->state_lock, flags);
		if (controller->suspended) {
			controller->pending_resume = true;
			spin_unlock_irqrestore(&controller->state_lock, flags);
			mutex_unlock(&controller->detector);
			return;
		}
		spin_unlock_irqrestore(&controller->state_lock, flags);
		ret = mindone_chr_detect(controller, initial, &attached);
		if (ret == -ENODATA || ret == -EAGAIN) {
			spin_lock_irqsave(&controller->state_lock, flags);
			if (controller->suspended) {
				controller->pending_resume = true;
			} else if (!smp_load_acquire(&controller->stopping) &&
				   controller->retry_count < MINDONE_CHR_RETRY_MAX) {
				controller->retry_count++;
				retry = true;
			}
			spin_unlock_irqrestore(&controller->state_lock, flags);
			if (retry) {
				mindone_chr_ws_hold(controller->ws,
						    MINDONE_CHR_WAKE_MS);
				mod_delayed_work(system_wq, &controller->work,
						 msecs_to_jiffies(MINDONE_CHR_RETRY_MS));
			} else {
				pr_warn("Charger detection deferred/exhausted: %d\n",
					ret);
			}
		} else if (ret) {
			pr_warn("Charger detection failed: %d\n", ret);
		} else {
			spin_lock_irqsave(&controller->state_lock, flags);
			controller->retry_count = 0;
			controller->last_attached = attached;
			controller->last_attached_valid = true;
			spin_unlock_irqrestore(&controller->state_lock, flags);
		}
	}
	mutex_unlock(&controller->detector);
}

static void mindone_chr_work_fn(struct work_struct *work)
{
	struct mindone_chr_controller *controller =
		container_of(to_delayed_work(work),
			     struct mindone_chr_controller, work);
	unsigned long flags;
	bool initial;

	spin_lock_irqsave(&controller->state_lock, flags);
	if (controller->suspended) {
		controller->pending_resume = true;
		spin_unlock_irqrestore(&controller->state_lock, flags);
		return;
	}
	initial = controller->initial_pending;
	controller->initial_pending = false;
	spin_unlock_irqrestore(&controller->state_lock, flags);
	mindone_chr_event(controller, initial);
}

static void mindone_chr_interrupt(void *context)
{
	struct mindone_chr_controller *controller = context;
	unsigned long flags;

	spin_lock_irqsave(&controller->state_lock, flags);
	if (controller->suspended) {
		controller->pending_resume = true;
		spin_unlock_irqrestore(&controller->state_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&controller->state_lock, flags);
	mindone_chr_event(controller, false);
}

int mindone_chr_suspend(void *binding)
{
	struct mindone_chr_controller *controller = binding;
	unsigned long flags;
	bool canceled;

	spin_lock_irqsave(&controller->state_lock, flags);
	controller->suspended = true;
	spin_unlock_irqrestore(&controller->state_lock, flags);

	canceled = cancel_delayed_work_sync(&controller->work);
	if (canceled) {
		spin_lock_irqsave(&controller->state_lock, flags);
		controller->pending_resume = true;
		spin_unlock_irqrestore(&controller->state_lock, flags);
	}

	mutex_lock(&controller->detector);
	mutex_unlock(&controller->detector);

	canceled = cancel_delayed_work_sync(&controller->work);
	if (canceled) {
		spin_lock_irqsave(&controller->state_lock, flags);
		controller->pending_resume = true;
		spin_unlock_irqrestore(&controller->state_lock, flags);
	}
	return 0;
}

int mindone_chr_resume(void *binding)
{
	struct mindone_chr_controller *controller = binding;
	unsigned int sample = 0;
	unsigned long flags;
	bool read_ok, attached = false, need;

	read_ok = mindone_chr_status(mindone_chr_regmap_read(controller->regmap,
							     0xa88, &sample)) == 0;
	if (read_ok)
		attached = !!(sample & BIT(5));

	spin_lock_irqsave(&controller->state_lock, flags);
	controller->suspended = false;
	need = controller->pending_resume;
	controller->pending_resume = false;
	if (read_ok && (!controller->last_attached_valid ||
			attached != controller->last_attached))
		need = true;
	if (need)
		controller->retry_count = 0;
	spin_unlock_irqrestore(&controller->state_lock, flags);

	if (need && !smp_load_acquire(&controller->stopping)) {
		mindone_chr_ws_hold(controller->ws, MINDONE_CHR_WAKE_MS);
		mod_delayed_work(system_wq, &controller->work, 0);
	}
	return 0;
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
	cancel_delayed_work_sync(&controller->work);
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
	if (controller->ws)
		mindone_chr_ws_unregister(controller->ws);
	kfree(controller);
}

void *mindone_chr_probe(void *device)
{
	struct platform_device *pdev = device;
	struct power_supply_config config = {
		.fwnode = dev_fwnode(&pdev->dev),
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
	spin_lock_init(&controller->state_lock);
	INIT_DELAYED_WORK(&controller->work, mindone_chr_work_fn);
	INIT_LIST_HEAD(&controller->voltage_node);
	controller->bootmode = bootmode;
	controller->ws = mindone_chr_ws_register();
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
	if (active) {
		controller->initial_pending = true;
		mod_delayed_work(system_wq, &controller->work, 0);
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
