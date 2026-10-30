// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/device.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include "mtk_disp_notify.h"

static char *ufs_dev_name = "11270000.ufshci";
module_param(ufs_dev_name, charp, 0444);
MODULE_PARM_DESC(ufs_dev_name, "UFS host platform device name");
static int screen_on_khz = 192000;
static int screen_on_khz_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops screen_on_khz_ops = {
	.set = screen_on_khz_set,
	.get = param_get_int,
};
module_param_cb(screen_on_khz, &screen_on_khz_ops, &screen_on_khz, 0644);
MODULE_PARM_DESC(screen_on_khz, "UFS min frequency (kHz) while the display is on; 0 = never pin");

static struct device *ufs_dev;
static bool ufs_gone;
static struct dev_pm_qos_request min_req;
static bool req_active;
static struct notifier_block disp_nb;
static struct notifier_block bus_nb;
static DEFINE_MUTEX(lock);

static int screen_on_khz_set(const char *val, const struct kernel_param *kp)
{
	int value, ret;

	ret = kstrtoint(val, 0, &value);
	if (ret)
		return ret;
	if (value < 0)
		return -EINVAL;
	mutex_lock(&lock);
	*(int *)kp->arg = value;
	mutex_unlock(&lock);
	return 0;
}

static void drop_request_locked(void)
{
	if (!req_active)
		return;
	dev_pm_qos_remove_request(&min_req);
	req_active = false;
}

static void set_floor(bool on)
{
	int ret;

	mutex_lock(&lock);
	if (!ufs_dev || ufs_gone)
		goto out;
	if (!on) {
		if (req_active)
			pr_info("mindone_ufs_screen: display off -> UFS scaling free\n");
		drop_request_locked();
		goto out;
	}
	if (!screen_on_khz || req_active)
		goto out;
	ret = dev_pm_qos_add_request(ufs_dev, &min_req, DEV_PM_QOS_MIN_FREQUENCY, screen_on_khz);
	if (ret < 0) {
		pr_warn("mindone_ufs_screen: PM QoS request failed: %d\n", ret);
		goto out;
	}
	req_active = true;
	pr_info("mindone_ufs_screen: display on -> UFS floor %d kHz\n", screen_on_khz);
out:
	mutex_unlock(&lock);
}

static int disp_cb(struct notifier_block *nb, unsigned long event, void *v)
{
	int *blank = v;

	if (event != MTK_DISP_EVENT_BLANK || !blank)
		return NOTIFY_DONE;
	if (*blank == MTK_DISP_BLANK_UNBLANK)
		set_floor(true);
	else if (*blank == MTK_DISP_BLANK_POWERDOWN)
		set_floor(false);
	return NOTIFY_OK;
}

static int bus_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	if (action != BUS_NOTIFY_DEL_DEVICE)
		return NOTIFY_DONE;
	mutex_lock(&lock);
	if (ufs_dev && data == ufs_dev && !ufs_gone) {
		drop_request_locked();
		ufs_gone = true;
		pr_warn("mindone_ufs_screen: %s removed, idle\n", ufs_dev_name);
	}
	mutex_unlock(&lock);
	return NOTIFY_OK;
}

static void release_device(void)
{
	struct device *dev;

	mutex_lock(&lock);
	drop_request_locked();
	dev = ufs_dev;
	ufs_dev = NULL;
	mutex_unlock(&lock);
	put_device(dev);
}

static int __init mindone_ufs_screen_init(void)
{
	struct device *dev;
	int ret;

	bus_nb.notifier_call = bus_cb;
	ret = bus_register_notifier(&platform_bus_type, &bus_nb);
	if (ret)
		return ret;
	dev = bus_find_device_by_name(&platform_bus_type, NULL, ufs_dev_name);
	if (!dev) {
		pr_warn("mindone_ufs_screen: %s not found, idle\n", ufs_dev_name);
		bus_unregister_notifier(&platform_bus_type, &bus_nb);
		return 0;
	}
	mutex_lock(&lock);
	ufs_dev = dev;
	mutex_unlock(&lock);
	set_floor(true);
	disp_nb.notifier_call = disp_cb;
	ret = mtk_disp_notifier_register("mindone_ufs_screen", &disp_nb);
	if (ret) {
		bus_unregister_notifier(&platform_bus_type, &bus_nb);
		release_device();
		return ret;
	}
	return 0;
}

static void __exit mindone_ufs_screen_exit(void)
{
	if (!ufs_dev)
		return;
	WARN_ON(mtk_disp_notifier_unregister(&disp_nb));
	bus_unregister_notifier(&platform_bus_type, &bus_nb);
	release_device();
}

module_init(mindone_ufs_screen_init);
module_exit(mindone_ufs_screen_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iKKO MindOne: pin UFS clock while the display is on, scale only while off");
