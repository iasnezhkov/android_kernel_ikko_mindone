// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>

#include "flashlight-core.h"
#include "flashlight-dt.h"

/* define device tree */
/* TODO: modify temp device tree name */
#ifndef DUMMY_GPIO_DTNAME
#define DUMMY_GPIO_DTNAME "mediatek,flashlights_dummy_gpio"
#endif

/* TODO: define driver name */
#define DUMMY_NAME "flashlights-dummy-gpio"

/* define registers */
/* TODO: define register */

/* define mutex and work queue */
static DEFINE_MUTEX(dummy_mutex);
static struct work_struct dummy_work;

/* define pinctrl */
/* TODO: define pinctrl */
#define DUMMY_PINCTRL_PIN_XXX 0
#define DUMMY_PINCTRL_PINSTATE_LOW 0
#define DUMMY_PINCTRL_PINSTATE_HIGH 1
/* MINDONE-DUMMYGPIO-STROBE (F3258): the gpio_flashled node has four states --
 * gpio_flashled_on/off (torch) and gpio_strobe_on/off (flash). The template only
 * knew the first pair. */
#define DUMMY_PINCTRL_PIN_STROBE 1

/* Output is binary: one brightness level. The level's current is UNKNOWN (needs the
 * board schematic), so FLASH_IOC_GET_DUTY_CURRENT is deliberately NOT implemented:
 * we won't invent milliamps -- let the framework honestly see "not supported". */
#define DUMMY_LEVEL_NUM   1
#define DUMMY_LEVEL_TORCH 1
#define DUMMY_HW_TIMEOUT  0
/* MINDONE-DUMMYGPIO-NAMES (F3257): our source is the vendor TEMPLATE with
 * placeholder names "xxx_high"/"xxx_low" not present in the DT -- hence
 * "Failed to init (xxx_high)" and an unbound gpio_flashled node. The stock
 * flashlights-dummy-gpio.ko (5.10.233) has the REAL names --
 * gpio_flashled_on/off and gpio_strobe_on/off -- matching our node's
 * pinctrl-names. Confirmed working on factory firmware; the stock module set
 * has only flashlight.ko and this driver, so the flash is GPIO-driven. */
#define DUMMY_PINCTRL_STATE_XXX_HIGH "gpio_flashled_on"
#define DUMMY_PINCTRL_STATE_XXX_LOW  "gpio_flashled_off"
static struct pinctrl *dummy_pinctrl;
static struct pinctrl_state *dummy_xxx_high;
static struct pinctrl_state *dummy_xxx_low;
#define DUMMY_PINCTRL_STATE_STROBE_HIGH "gpio_strobe_on"
#define DUMMY_PINCTRL_STATE_STROBE_LOW  "gpio_strobe_off"
static struct pinctrl_state *dummy_strobe_high;
static struct pinctrl_state *dummy_strobe_low;

/* define usage count */
static int use_count;

/* platform data */
struct dummy_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};


/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int dummy_pinctrl_init(struct platform_device *pdev)
{
	int ret = 0;

	/* get pinctrl */
	dummy_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(dummy_pinctrl)) {
		pr_info("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(dummy_pinctrl);
		return ret;
	}

	/* TODO: Flashlight XXX pin initialization */
	dummy_xxx_high = pinctrl_lookup_state(
			dummy_pinctrl, DUMMY_PINCTRL_STATE_XXX_HIGH);
	if (IS_ERR(dummy_xxx_high)) {
		pr_info("Failed to init (%s)\n", DUMMY_PINCTRL_STATE_XXX_HIGH);
		ret = PTR_ERR(dummy_xxx_high);
	}
	dummy_xxx_low = pinctrl_lookup_state(
			dummy_pinctrl, DUMMY_PINCTRL_STATE_XXX_LOW);
	if (IS_ERR(dummy_xxx_low)) {
		pr_info("Failed to init (%s)\n", DUMMY_PINCTRL_STATE_XXX_LOW);
		ret = PTR_ERR(dummy_xxx_low);
	}

	/* MINDONE-DUMMYGPIO-STROBE (F3258): the flash is optional -- its absence
	 * must not fail the torch, so ret is NOT touched here. */
	dummy_strobe_high = pinctrl_lookup_state(
			dummy_pinctrl, DUMMY_PINCTRL_STATE_STROBE_HIGH);
	if (IS_ERR(dummy_strobe_high))
		pr_info("No state (%s) -- flash unavailable\n",
				DUMMY_PINCTRL_STATE_STROBE_HIGH);
	dummy_strobe_low = pinctrl_lookup_state(
			dummy_pinctrl, DUMMY_PINCTRL_STATE_STROBE_LOW);
	if (IS_ERR(dummy_strobe_low))
		pr_info("No state (%s)\n",
				DUMMY_PINCTRL_STATE_STROBE_LOW);

	return ret;
}

static int dummy_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (IS_ERR(dummy_pinctrl)) {
		pr_info("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case DUMMY_PINCTRL_PIN_XXX:
		if (state == DUMMY_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(dummy_xxx_low))
			pinctrl_select_state(dummy_pinctrl, dummy_xxx_low);
		else if (state == DUMMY_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(dummy_xxx_high))
			pinctrl_select_state(dummy_pinctrl, dummy_xxx_high);
		else
			pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case DUMMY_PINCTRL_PIN_STROBE:
		if (state == DUMMY_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(dummy_strobe_low))
			pinctrl_select_state(dummy_pinctrl, dummy_strobe_low);
		else if (state == DUMMY_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(dummy_strobe_high))
			pinctrl_select_state(dummy_pinctrl, dummy_strobe_high);
		else
			pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	pr_debug("pin(%d) state(%d)\n", pin, state);

	return ret;
}


/******************************************************************************
 * dummy operations
 *****************************************************************************/
/* MINDONE-DUMMYGPIO-ENABLE (F3258) -- THE MAIN CAUSE OF THE NON-WORKING TORCH.
 * In the vendor template all five functions below were IDENTICAL:
 *     int pin = 0, state = 0;  return dummy_pinctrl_set(pin, state);
 * i.e. enable/disable/set-level all called PINSTATE_LOW ("turn off"). The
 * framework accepted the torch command (rc=0), the driver honestly toggled the
 * pin -- always to ZERO. Below, enable uses PINSTATE_HIGH; set_level no longer
 * touches the pin.
 */

/* Last requested brightness and the mask of enabled channels. Both "channels"
 * (ct 0/1) on this board sit on the same gpio_flashled pin, so we only turn the
 * pin off when BOTH channels are off -- otherwise disabling the flash would
 * also turn off the torch.
 */
static int dummy_duty;
static unsigned int dummy_on_mask;

/* flashlight enable function */
static int dummy_enable_ch(int channel)
{
	dummy_on_mask |= BIT(channel);
	return dummy_pinctrl_set(DUMMY_PINCTRL_PIN_XXX,
			DUMMY_PINCTRL_PINSTATE_HIGH);
}

static int dummy_enable(void)
{
	return dummy_enable_ch(0);
}

/* flashlight disable function */
static int dummy_disable_ch(int channel)
{
	dummy_on_mask &= ~BIT(channel);
	if (dummy_on_mask)
		return 0;	/* another channel is still on */
	return dummy_pinctrl_set(DUMMY_PINCTRL_PIN_XXX,
			DUMMY_PINCTRL_PINSTATE_LOW);
}

static int dummy_disable(void)
{
	dummy_on_mask = 0;
	return dummy_pinctrl_set(DUMMY_PINCTRL_PIN_XXX,
			DUMMY_PINCTRL_PINSTATE_LOW);
}

/* set flashlight level.
 * Output is binary, it does not set brightness: we just remember the level and
 * do NOT turn off the light (in the template this function turned off the pin
 * right before FLASH_IOC_SET_ONOFF).
 */
static int dummy_set_level(int level)
{
	dummy_duty = level;
	pr_debug("set level %d\n", level);
	return 0;
}

/* flashlight init */
static int dummy_init(void)
{
	dummy_on_mask = 0;
	return dummy_pinctrl_set(DUMMY_PINCTRL_PIN_XXX,
			DUMMY_PINCTRL_PINSTATE_LOW);
}

/* flashlight uninit */
static int dummy_uninit(void)
{
	dummy_on_mask = 0;
	dummy_pinctrl_set(DUMMY_PINCTRL_PIN_STROBE,
			DUMMY_PINCTRL_PINSTATE_LOW);
	return dummy_pinctrl_set(DUMMY_PINCTRL_PIN_XXX,
			DUMMY_PINCTRL_PINSTATE_LOW);
}

/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static struct hrtimer dummy_timer;
static unsigned int dummy_timeout_ms;

static void dummy_work_disable(struct work_struct *data)
{
	pr_debug("work queue callback\n");
	dummy_disable();
}

static enum hrtimer_restart dummy_timer_func(struct hrtimer *timer)
{
	schedule_work(&dummy_work);
	return HRTIMER_NORESTART;
}


/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int dummy_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;
	unsigned int s;
	unsigned int ns;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_debug("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		dummy_timeout_ms = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_debug("FLASH_IOC_SET_DUTY(%d): %d\n",
				channel, (int)fl_arg->arg);
		dummy_set_level(fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_debug("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if (fl_arg->arg == 1) {
			if (dummy_timeout_ms) {
				s = dummy_timeout_ms / 1000;
				ns = dummy_timeout_ms % 1000 * 1000000;
				ktime = ktime_set(s, ns);
				hrtimer_start(&dummy_timer, ktime,
						HRTIMER_MODE_REL);
			}
			dummy_enable_ch(channel);
		} else {
			dummy_disable_ch(channel);
			hrtimer_cancel(&dummy_timer);
		}
		break;
	/* MINDONE-DUMMYGPIO-CAPS (F3258): the framework queries capabilities;
	 * command 240 = FLASH_IOC_GET_HW_TIMEOUT was missing from the template
	 * and fell through to -ENOTTY ("No such command and arg(0): (240, -1)"
	 * in the log).
	 */
	case FLASH_IOC_GET_DUTY_NUMBER:
		fl_arg->arg = DUMMY_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		fl_arg->arg = DUMMY_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		/* no hardware watchdog on the pin -- the software hrtimer turns it off */
		fl_arg->arg = DUMMY_HW_TIMEOUT;
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int dummy_open(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int dummy_release(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int dummy_set_driver(int set)
{
	int ret = 0;

	/* set chip and usage count */
	mutex_lock(&dummy_mutex);
	if (set) {
		if (!use_count)
			ret = dummy_init();
		use_count++;
		pr_debug("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = dummy_uninit();
		if (use_count < 0)
			use_count = 0;
		pr_debug("Unset driver: %d\n", use_count);
	}
	mutex_unlock(&dummy_mutex);

	return ret;
}

static ssize_t dummy_strobe_store(struct flashlight_arg arg)
{
	dummy_set_driver(1);
	dummy_set_level(arg.level);
	dummy_timeout_ms = 0;
	dummy_enable();
	msleep(arg.dur);
	dummy_disable();
	dummy_set_driver(0);

	return 0;
}

static struct flashlight_operations dummy_ops = {
	dummy_open,
	dummy_release,
	dummy_ioctl,
	dummy_strobe_store,
	dummy_set_driver
};


/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
static int dummy_chip_init(void)
{
	/* NOTE: Chip initialication move to "set driver" for power saving.
	 * dummy_init();
	 */

	return 0;
}

static int dummy_parse_dt(struct device *dev,
		struct dummy_platform_data *pdata)
{
	struct device_node *np, *cnp;
	struct device_node *np_owned = NULL;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node || !pdata)
		return -ENODEV;

	np = dev->of_node;

	/* MINDONE-DUMMYGPIO-NODE (F3257). The gpio_flashled anchor node carries only
	 * pinctrl states (gpio_flashled_on/off, gpio_strobe_on/off) and has NO
	 * children, so parsing produced "Parse no dt, node.", no framework entries
	 * were created, and the torch never enabled. The LED descriptions -- flash@0
	 * and flash@1 children with type/ct/part properties -- are declared on the
	 * lm3643@63 node ("mediatek,lm3643"). Take them from there instead. Same fix
	 * needed by the flashlights_lm3643 port (F3253). */
	if (!of_get_child_count(np)) {
		np_owned = of_find_compatible_node(NULL, NULL,
						   "mediatek,lm3643");
		if (np_owned) {
			pr_info("MINDONE-DUMMYGPIO: anchor has no children, using mediatek,lm3643 node\n");
			np = np_owned;
		}
	}

	pdata->channel_num = of_get_child_count(np);
	if (!pdata->channel_num) {
		pr_info("Parse no dt, node.\n");
		if (np_owned)
			of_node_put(np_owned);
		return 0;
	}
	pr_info("Channel number(%d).\n", pdata->channel_num);

	if (of_property_read_u32(np, "decouple", &decouple))
		pr_info("Parse no dt, decouple.\n");

	pdata->dev_id = devm_kzalloc(dev,
			pdata->channel_num *
			sizeof(struct flashlight_device_id),
			GFP_KERNEL);
	if (!pdata->dev_id)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type", &pdata->dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp, "ct", &pdata->dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp, "part", &pdata->dev_id[i].part))
			goto err_node_put;
		snprintf(pdata->dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				DUMMY_NAME);
		pdata->dev_id[i].channel = i;
		pdata->dev_id[i].decouple = decouple;

		pr_info("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				pdata->dev_id[i].type, pdata->dev_id[i].ct,
				pdata->dev_id[i].part, pdata->dev_id[i].name,
				pdata->dev_id[i].channel,
				pdata->dev_id[i].decouple);
		i++;
	}

	if (np_owned)
		of_node_put(np_owned);
	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}

static int dummy_probe(struct platform_device *pdev)
{
	struct dummy_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int err;
	int i;

	pr_debug("Probe start.\n");

	/* init pinctrl */
	if (dummy_pinctrl_init(pdev)) {
		pr_debug("Failed to init pinctrl.\n");
		err = -EFAULT;
		goto err;
	}

	/* init platform data */
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err;
		}
		pdev->dev.platform_data = pdata;
		err = dummy_parse_dt(&pdev->dev, pdata);
		if (err)
			goto err;
	}

	/* init work queue */
	INIT_WORK(&dummy_work, dummy_work_disable);

	/* init timer */
	hrtimer_init(&dummy_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dummy_timer.function = dummy_timer_func;
	dummy_timeout_ms = 100;

	/* init chip hw */
	dummy_chip_init();

	/* clear usage count */
	use_count = 0;

	/* register flashlight device */
	if (pdata->channel_num) {
		for (i = 0; i < pdata->channel_num; i++)
			if (flashlight_dev_register_by_device_id(
						&pdata->dev_id[i],
						&dummy_ops)) {
				err = -EFAULT;
				goto err;
			}
	} else {
		if (flashlight_dev_register(DUMMY_NAME, &dummy_ops)) {
			err = -EFAULT;
			goto err;
		}
	}

	pr_debug("Probe done.\n");

	return 0;
err:
	return err;
}

static void dummy_remove(struct platform_device *pdev)
{
	struct dummy_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i;

	pr_debug("Remove start.\n");

	pdev->dev.platform_data = NULL;

	/* unregister flashlight device */
	if (pdata && pdata->channel_num)
		for (i = 0; i < pdata->channel_num; i++)
			flashlight_dev_unregister_by_device_id(
					&pdata->dev_id[i]);
	else
		flashlight_dev_unregister(DUMMY_NAME);

	/* flush work queue */
	flush_work(&dummy_work);

	pr_debug("Remove done.\n");

	return;
}

#ifdef CONFIG_OF
static const struct of_device_id dummy_gpio_of_match[] = {
	{.compatible = DUMMY_GPIO_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, dummy_gpio_of_match);
#else
static struct platform_device dummy_gpio_platform_device[] = {
	{
		.name = DUMMY_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, dummy_gpio_platform_device);
#endif

static struct platform_driver dummy_platform_driver = {
	.probe = dummy_probe,
	.remove_new = dummy_remove,
	.driver = {
		.name = DUMMY_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = dummy_gpio_of_match,
#endif
	},
};

static int __init flashlight_dummy_init(void)
{
	int ret;

	pr_debug("Init start.\n");

#ifndef CONFIG_OF
	ret = platform_device_register(&dummy_gpio_platform_device);
	if (ret) {
		pr_info("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&dummy_platform_driver);
	if (ret) {
		pr_info("Failed to register platform driver\n");
		return ret;
	}

	pr_debug("Init done.\n");

	return 0;
}

static void __exit flashlight_dummy_exit(void)
{
	pr_debug("Exit start.\n");

	platform_driver_unregister(&dummy_platform_driver);

	pr_debug("Exit done.\n");
}

module_init(flashlight_dummy_init);
module_exit(flashlight_dummy_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Wang <Simon-TCH.Wang@mediatek.com>");
MODULE_DESCRIPTION("MTK Flashlight DUMMY GPIO Driver");

