// SPDX-License-Identifier: GPL-2.0
/*
 * mindone_ufs_screen - screen-gated UFS clock scaling for the iKKO MindOne (MT6789).
 *
 * The UFS devfreq clock scaling (26 <-> 192 MHz, F4256) saves power at idle but costs UX when
 * it is active while the user interacts: storage drops to 26 MHz between flings and the next
 * page-in stalls, cold starts wait for the ramp (UX rig F4279/F4280: 22 -> 5 freezes, 3 -> 0 slow
 * launches once the floor is pinned at 192 MHz). This module keeps the floor at 192 MHz while the
 * display is on and removes it while the display is off, so scaling only runs when nobody is
 * looking - the best of both. It uses a DEV_PM_QOS_MIN_FREQUENCY request on the UFS host device,
 * which devfreq honours exactly like a write to min_freq in sysfs, and the MediaTek display blank
 * notifier for the screen state. Requires ufs_mediatek.mindone_clkscale=1 (devfreq registered);
 * without it the module has nothing to gate and simply stays idle.
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include "mtk_disp_notify.h"

static char *ufs_dev_name = "11270000.ufshci";
module_param(ufs_dev_name, charp, 0444);
MODULE_PARM_DESC(ufs_dev_name, "UFS host platform device name");
static int screen_on_khz = 192000;
module_param(screen_on_khz, int, 0644);
MODULE_PARM_DESC(screen_on_khz, "UFS min frequency (kHz) while the display is on; 0 = never pin");

static struct device *ufs_dev;
static struct dev_pm_qos_request min_req;
static bool req_active;
static struct notifier_block disp_nb;
static bool disp_registered;
static DEFINE_MUTEX(lock);

static void set_floor(bool on)
{
	mutex_lock(&lock);
	if (!ufs_dev || !screen_on_khz)
		goto out;
	if (on && !req_active) {
		/* Returns 1 when the aggregate constraint changed, 0 when it did not, <0 on error:
		 * the request is registered in both non-negative cases (F4283). */
		if (dev_pm_qos_add_request(ufs_dev, &min_req, DEV_PM_QOS_MIN_FREQUENCY, screen_on_khz) >= 0) {
			req_active = true;
			pr_info("mindone_ufs_screen: display on -> UFS floor %d kHz\n", screen_on_khz);
		} else {
			pr_warn("mindone_ufs_screen: PM QoS request failed\n");
		}
	} else if (!on && req_active) {
		dev_pm_qos_remove_request(&min_req);
		req_active = false;
		pr_info("mindone_ufs_screen: display off -> UFS scaling free\n");
	}
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

static int __init mindone_ufs_screen_init(void)
{
	/* Never fail insmod: Android init treats a failed modules.load entry as fatal (F4258). */
	ufs_dev = bus_find_device_by_name(&platform_bus_type, NULL, ufs_dev_name);
	if (!ufs_dev) {
		pr_warn("mindone_ufs_screen: %s not found, idle\n", ufs_dev_name);
		return 0;
	}
	set_floor(true);	/* the display is on when modules load */
	disp_nb.notifier_call = disp_cb;
	if (mtk_disp_notifier_register("mindone_ufs_screen", &disp_nb))
		pr_warn("mindone_ufs_screen: display notifier not available, floor stays pinned\n");
	else
		disp_registered = true;
	return 0;
}

static void __exit mindone_ufs_screen_exit(void)
{
	if (disp_registered)
		mtk_disp_notifier_unregister(&disp_nb);
	set_floor(false);
	if (ufs_dev)
		put_device(ufs_dev);
}

module_init(mindone_ufs_screen_init);
module_exit(mindone_ufs_screen_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iKKO MindOne: pin UFS clock while the display is on, scale only while off");
