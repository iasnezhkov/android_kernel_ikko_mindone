// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/prefetch.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/dma-mapping.h>
#include <musb_main.h>

#define DRIVER_AUTHOR "Mentor Graphics, Texas Instruments, Nokia"
#define DRIVER_DESC "Inventra Dual-Role USB Controller Driver"

#define MUSB_VERSION "6.0"
#define DRIVER_INFO DRIVER_DESC ", v" MUSB_VERSION
#define MUSB_DRIVER_NAME "musb-hdrc"
const char musb_driver_names[] = MUSB_DRIVER_NAME;

MODULE_DESCRIPTION(DRIVER_INFO);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MUSB_DRIVER_NAME);

static int musb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq = 0;
	int status;
	void __iomem *base;
	void __iomem *pbase;
	struct resource *iomem;

	if (usb_disabled())
		return 0;

	pr_info("%s: version " MUSB_VERSION ", ?dma?, otg (peripheral+host)\n"
		, musb_driver_names);

	/* F979: without this NULL check, this used to be a KERNEL CRASH, not a clean
	 * startup failure. platform_get_resource() returns NULL when no region with
	 * that number exists, and the next line dereferenced the null pointer
	 * (iomem->start). Per F952 the child device is exactly NOT given resources,
	 * so this condition did trigger. Now the driver fails cleanly and boot continues. */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(dev, "MINDONE-USB: no memory region 0, aborting startup\n");
		return -ENODEV;
	}
	base = devm_ioremap(dev, iomem->start, resource_size(iomem));
	/* devm_ioremap() returns NULL on failure, NOT an error pointer;
	 * the previous IS_ERR() check never caught a null pointer at all. */
	if (!base) {
		dev_err(dev, "MINDONE-USB: memory region 0 did not map\n");
		return -ENOMEM;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!iomem) {
		dev_err(dev, "MINDONE-USB: no memory region 1, aborting startup\n");
		return -ENODEV;
	}
	pbase = devm_ioremap(dev, iomem->start, resource_size(iomem));
	if (!pbase) {
		dev_err(dev, "MINDONE-USB: memory region 1 did not map\n");
		return -ENOMEM;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENODEV;

	pr_info("%s mac=0x%lx, phy=0x%lx, irq=%d\n"
		, __func__, (unsigned long)base, (unsigned long)pbase, irq);
	status = musb_init_controller(dev, irq, base, pbase);

	return status;
}

static void musb_shutdown_main(struct platform_device *pdev)
{
	return musb_shutdown(pdev);
}

static void musb_remove_main(struct platform_device *pdev)
{
	musb_remove(pdev); return;
}

extern const struct dev_pm_ops musb_dev_pm_ops;

#define MUSB_DEV_PM_OPS (&musb_dev_pm_ops)

static struct platform_driver musb_driver = {
	.driver = {
		   .name = (char *)musb_driver_names,
		   .bus = &platform_bus_type,
			.owner = THIS_MODULE,
		    .pm = MUSB_DEV_PM_OPS,
		   },
	.probe = musb_probe,
	.remove_new = musb_remove_main,
	.shutdown = musb_shutdown_main,
};
module_platform_driver(musb_driver);

