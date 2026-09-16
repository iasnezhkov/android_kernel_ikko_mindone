// SPDX-License-Identifier: GPL-2.0
/*
 * vblankarm.ko -- re-arm the display controller's vertical sync.
 *
 * WHY (F2363). Measured with ftrace probes: the `vblank->refcount` reference
 * counter is stuck at >=1, while `vblank->enabled == false`. Because of this
 * the 0->1 transition inside `drm_vblank_get()` NEVER happens,
 * `drm_vblank_enable()` is never called (0 calls in 14s), and every attempt
 * by the compositor to get a vsync pulse returns -EINVAL (21 of 21).
 * Consequence: the whole black-screen chain -- no vsync events -> frame not
 * flushed -> overlay layer not configured -> buffer not displayed -> scanout
 * failures -> DSI underrun.
 *
 * WHAT WE DO. Call `drm_crtc_vblank_on()`. The kernel's own documentation
 * (`drm_vblank.c`, function header) explicitly allows this: "calls to
 * drm_crtc_vblank_on() and drm_crtc_vblank_off() can be unbalanced and so can
 * also be unconditionally called in driver load code to reflect the current
 * hardware state of the crtc". So this is a standard way to reconcile state
 * with hardware, not a workaround.
 *
 * HOW WE FIND THE DEVICE. `platform_set_drvdata(pdev, private)` in
 * `mtk_drm_drv.c:6879` stores a `struct mtk_drm_private *`, whose FIRST field
 * is `struct drm_device *drm`. So dereferencing the first pointer gives the
 * DRM device without including the driver's private headers.
 *
 * WHAT THIS DOES NOT DO. It does NOT fix the cause -- it doesn't find who
 * left the reference held. It just re-arms the mechanism. The real fix is
 * finding the unbalanced get(); tracked separately.
 *
 * Init ALWAYS returns success: a stage-1 failure aborts loading of the entire
 * remaining module list (F330). Log tags are LATIN-only (F1973).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>

static char *devname = "14000000.dispsys_config";
module_param(devname, charp, 0444);
MODULE_PARM_DESC(devname, "platform device name of the display controller");

static int pipe_idx;
module_param(pipe_idx, int, 0644);
MODULE_PARM_DESC(pipe_idx, "CRTC index to re-arm (default 0)");

static int found;
module_param(found, int, 0444);
MODULE_PARM_DESC(found, "1 = display device and CRTC located");

static int armed;
module_param(armed, int, 0444);
MODULE_PARM_DESC(armed, "how many times drm_crtc_vblank_on() has been called");

static int match_name(struct device *dev, const void *data)
{
	return sysfs_streq(dev_name(dev), (const char *)data);
}

static struct drm_crtc *locate_crtc(void)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_crtc *crtc;
	void *priv;

	dev = bus_find_device(&platform_bus_type, NULL, devname, match_name);
	if (!dev) {
		pr_notice("MINDONE-VBARM: platform device '%s' not found\n", devname);
		return NULL;
	}

	priv = dev_get_drvdata(dev);
	put_device(dev);
	if (!priv) {
		pr_notice("MINDONE-VBARM: '%s' has no drvdata yet\n", devname);
		return NULL;
	}

	/* first field of struct mtk_drm_private is `struct drm_device *drm` */
	drm = *(struct drm_device **)priv;
	if (!drm) {
		pr_notice("MINDONE-VBARM: drvdata holds NULL drm_device\n");
		return NULL;
	}

	pr_notice("MINDONE-VBARM: drm_device=%px num_crtcs=%u\n", drm, drm->num_crtcs);

	crtc = drm_crtc_from_index(drm, pipe_idx);
	if (!crtc) {
		pr_notice("MINDONE-VBARM: no CRTC at index %d\n", pipe_idx);
		return NULL;
	}

	found = 1;
	pr_notice("MINDONE-VBARM: crtc index=%d id=%u name=%s\n",
		  pipe_idx, crtc->base.id, crtc->name ? crtc->name : "?");
	return crtc;
}

static void do_arm(void)
{
	struct drm_crtc *crtc = locate_crtc();

	if (!crtc)
		return;

	pr_notice("MINDONE-VBARM: calling drm_crtc_vblank_on()\n");
	drm_crtc_vblank_on(crtc);
	armed++;
	pr_notice("MINDONE-VBARM: drm_crtc_vblank_on() returned (arm %d)\n", armed);
}

static int arm_set(const char *val, const struct kernel_param *kp)
{
	do_arm();
	return 0;
}
static const struct kernel_param_ops arm_ops = { .set = arm_set };
module_param_cb(arm, &arm_ops, NULL, 0644);
MODULE_PARM_DESC(arm, "write anything to re-arm vblank now");

static int __init vbarm_init(void)
{
	pr_notice("MINDONE-VBARM: loaded, target='%s' pipe=%d\n", devname, pipe_idx);
	do_arm();
	return 0;
}

static void __exit vbarm_exit(void)
{
	pr_notice("MINDONE-VBARM: unloaded (found=%d armed=%d)\n", found, armed);
}

module_init(vbarm_init);
module_exit(vbarm_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Re-arm CRTC vblank after a stuck refcount (F2363)");
