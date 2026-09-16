// SPDX-License-Identifier: GPL-2.0
/*
 * regulator-vibrator - LED-class vibrator driven by a regulator (MediaTek "regulator-vibrator").
 *
 * The device tree carries soc/regulator_vibrator { compatible = "regulator-vibrator";
 * vib-supply = <&ldo>; min-volt/max-volt; label } and the vendor SELinux policy labels
 * /sys/devices/platform/soc/soc:regulator_vibrator/leds/vibrator as sysfs_vibrator, which is
 * the only path the stock vibrator HAL may touch. The stock first stage loads this driver
 * before the AW haptic IC driver, so the LED named "vibrator" is this one. The LED uses the
 * "transient" trigger (activate/duration/state attributes) like the stock driver.
 */
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

struct reg_vib {
	struct led_classdev cdev;
	struct regulator *vib;
	u32 min_volt;
	u32 max_volt;
	bool on;
	struct mutex lock;
};

static int reg_vib_set(struct led_classdev *cdev, enum led_brightness value)
{
	struct reg_vib *v = container_of(cdev, struct reg_vib, cdev);
	int ret = 0;

	mutex_lock(&v->lock);
	if (value && !v->on) {
		if (v->min_volt && v->max_volt)
			ret = regulator_set_voltage(v->vib, v->min_volt, v->max_volt);
		if (!ret)
			ret = regulator_enable(v->vib);
		if (!ret)
			v->on = true;
	} else if (!value && v->on) {
		ret = regulator_disable(v->vib);
		v->on = false;
	}
	mutex_unlock(&v->lock);
	if (ret)
		dev_err(cdev->dev, "vibrator %s: %d\n", value ? "on" : "off", ret);
	return ret;
}

static int reg_vib_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reg_vib *v;
	const char *label = "vibrator";
	int ret;

	v = devm_kzalloc(dev, sizeof(*v), GFP_KERNEL);
	if (!v)
		return -ENOMEM;
	mutex_init(&v->lock);

	v->vib = devm_regulator_get(dev, "vib");
	if (IS_ERR(v->vib))
		return dev_err_probe(dev, PTR_ERR(v->vib), "vib-supply\n");

	of_property_read_u32(dev->of_node, "min-volt", &v->min_volt);
	of_property_read_u32(dev->of_node, "max-volt", &v->max_volt);
	of_property_read_string(dev->of_node, "label", &label);

	v->cdev.name = "vibrator";
	v->cdev.max_brightness = LED_FULL;
	v->cdev.brightness_set_blocking = reg_vib_set;
	v->cdev.default_trigger = "transient";
	v->cdev.flags = LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, &v->cdev);
	if (ret)
		return dev_err_probe(dev, ret, "led register\n");

	platform_set_drvdata(pdev, v);
	dev_info(dev, "regulator vibrator '%s': %u-%u uV\n", label, v->min_volt, v->max_volt);
	return 0;
}

static void reg_vib_shutdown(struct platform_device *pdev)
{
	struct reg_vib *v = platform_get_drvdata(pdev);

	if (v && v->on) {
		regulator_disable(v->vib);
		v->on = false;
	}
}

static const struct of_device_id reg_vib_of_match[] = {
	{ .compatible = "regulator-vibrator" },
	{ }
};
MODULE_DEVICE_TABLE(of, reg_vib_of_match);

static struct platform_driver reg_vib_driver = {
	.probe = reg_vib_probe,
	.shutdown = reg_vib_shutdown,
	.driver = {
		.name = "regulator-vibrator",
		.of_match_table = reg_vib_of_match,
	},
};
module_platform_driver(reg_vib_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Regulator-driven LED-class vibrator (MediaTek regulator-vibrator binding)");
