// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 MediaTek Inc. */
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "flashlight-core.h"

#define LM3643_NAME "flashlights-lm3643"
#define LM3643_CHANNELS 2
#define LM3643_LEVELS 26
#define LM3643_TORCH_LEVELS 7
#define LM3643_TIMEOUT_MS 400
#define LM3643_ENABLE 0x01
#define LM3643_FLASH 0x03
#define LM3643_TORCH 0x05
#define LM3643_TIMING 0x08
#define LM3643_FLAG1 0x0a
#define LM3643_FLAG2 0x0b
#define LM3643_ID 0x0c
#define LM3643_DEVICE_ID 0x02

struct lm3643;

struct lm3643_channel {
	struct delayed_work work;
	struct lm3643 *chip;
	struct flashlight_device_id id;
	unsigned long deadline;
	unsigned int timeout;
	int level;
};

struct lm3643 {
	struct i2c_client *client;
	struct pinctrl *pinctrl;
	struct pinctrl_state *high;
	struct pinctrl_state *low;
	struct regulator *vin;
	struct lm3643_channel channel[LM3643_CHANNELS];
	unsigned int users;
	unsigned int enabled;
	bool powered;
	bool supplied;
};

static DEFINE_MUTEX(lm3643_lock);
static struct lm3643 *lm3643_chip;

static const u8 lm3643_torch[LM3643_TORCH_LEVELS] = {
	0x0f, 0x20, 0x31, 0x42, 0x52, 0x63, 0x74,
};

static const u8 lm3643_flash[LM3643_LEVELS] = {
	0x01, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x10, 0x14,
	0x19, 0x1d, 0x21, 0x25, 0x2a, 0x2e, 0x32, 0x37, 0x3b,
	0x3f, 0x43, 0x48, 0x4c, 0x50, 0x54, 0x59, 0x5d,
};

static unsigned int lm3643_current_ua(int level)
{
	if (level < LM3643_TORCH_LEVELS)
		return 977 + 1400 * lm3643_torch[level];

	return 10900 + 11725 * lm3643_flash[level];
}

static int lm3643_write(struct lm3643 *chip, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, value);
	if (ret || reg < LM3643_FLASH || reg > LM3643_TORCH + 1)
		return ret;
	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0)
		return ret;
	if (ret != value) {
		dev_err(&chip->client->dev,
			"register 0x%02x readback 0x%02x expected 0x%02x\n",
			reg, ret, value);
		return -EIO;
	}
	return 0;
}

static int lm3643_power_off(struct lm3643 *chip)
{
	int ret = 0, err, i;

	if (chip->powered)
		ret = lm3643_write(chip, LM3643_ENABLE, 0);
	for (i = 0; i < LM3643_CHANNELS; i++) {
		chip->channel[i].deadline = 0;
		cancel_delayed_work(&chip->channel[i].work);
	}
	chip->enabled = 0;
	err = pinctrl_select_state(chip->pinctrl, chip->low);
	if (!ret)
		ret = err;
	chip->powered = false;
	if (chip->supplied) {
		err = regulator_disable(chip->vin);
		if (!err)
			chip->supplied = false;
		if (!ret)
			ret = err;
	}
	return ret;
}

static int lm3643_power_on(struct lm3643 *chip)
{
	int ret, i;

	if (chip->powered)
		return 0;
	if (chip->vin && !chip->supplied) {
		ret = regulator_enable(chip->vin);
		if (ret)
			return ret;
		chip->supplied = true;
	}
	ret = pinctrl_select_state(chip->pinctrl, chip->high);
	if (ret)
		goto fail;
	msleep(20);
	ret = i2c_smbus_read_byte_data(chip->client, LM3643_ID);
	if (ret < 0)
		goto fail;
	if (ret != LM3643_DEVICE_ID) {
		dev_err(&chip->client->dev, "unexpected device ID 0x%02x\n", ret);
		ret = -ENODEV;
		goto fail;
	}
	chip->powered = true;
	ret = lm3643_write(chip, LM3643_ENABLE, 0);
	if (ret)
		goto fail;
	ret = lm3643_write(chip, LM3643_TIMING, 0x0f);
	if (ret)
		goto fail;
	for (i = 0; i < LM3643_CHANNELS; i++) {
		int level = chip->channel[i].level;

		ret = lm3643_write(chip, LM3643_TORCH + i,
			level < LM3643_TORCH_LEVELS ? lm3643_torch[level] : 0);
		if (ret)
			goto fail;
		ret = lm3643_write(chip, LM3643_FLASH + i, lm3643_flash[level]);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	lm3643_power_off(chip);
	return ret;
}

static int lm3643_check(struct lm3643 *chip, unsigned int mask,
			int channel, int level)
{
	unsigned int total = 0;
	int mode = -1, i;

	for (i = 0; i < LM3643_CHANNELS; i++) {
		int duty = i == channel ? level : chip->channel[i].level;
		int flash = duty >= LM3643_TORCH_LEVELS;

		if (!(mask & BIT(i)))
			continue;
		if (mode >= 0 && mode != flash)
			return -EBUSY;
		mode = flash;
		total += lm3643_current_ua(duty);
	}
	return total > 1500000 ? -ERANGE : 0;
}

static int lm3643_apply(struct lm3643 *chip, unsigned int mask)
{
	u8 value = mask;
	int i, ret;

	for (i = 0; i < LM3643_CHANNELS; i++) {
		if (mask & BIT(i)) {
			value |= chip->channel[i].level < LM3643_TORCH_LEVELS ?
				 0x08 : 0x0c;
			break;
		}
	}
	ret = lm3643_write(chip, LM3643_ENABLE, value);
	if (ret) {
		lm3643_power_off(chip);
		return ret;
	}
	chip->enabled = mask;
	return 0;
}

static int lm3643_level(struct lm3643 *chip, int channel, int level)
{
	int ret;

	level = clamp(level, 0, LM3643_LEVELS - 1);
	ret = lm3643_check(chip, chip->enabled, channel, level);
	if (ret)
		return ret;
	if ((chip->enabled & BIT(channel)) &&
	    (chip->channel[channel].level < LM3643_TORCH_LEVELS) !=
	    (level < LM3643_TORCH_LEVELS))
		return -EBUSY;
	ret = lm3643_write(chip, LM3643_TORCH + channel,
		level < LM3643_TORCH_LEVELS ? lm3643_torch[level] : 0);
	if (!ret)
		ret = lm3643_write(chip, LM3643_FLASH + channel,
				   lm3643_flash[level]);
	if (ret) {
		lm3643_power_off(chip);
		return ret;
	}
	chip->channel[channel].level = level;
	return 0;
}

static int lm3643_onoff(struct lm3643 *chip, int channel, bool on)
{
	struct lm3643_channel *ch = &chip->channel[channel];
	unsigned int mask = chip->enabled & ~BIT(channel);
	unsigned int timeout = ch->timeout;
	int ret;

	if (on) {
		mask |= BIT(channel);
		ret = lm3643_check(chip, mask, channel, ch->level);
		if (ret)
			return ret;
		if (ch->level >= LM3643_TORCH_LEVELS)
			timeout = timeout ? min(timeout, LM3643_TIMEOUT_MS) :
				  LM3643_TIMEOUT_MS;
	}
	ret = lm3643_apply(chip, mask);
	if (ret)
		return ret;
	ch->deadline = 0;
	cancel_delayed_work(&ch->work);
	if (on && timeout) {
		unsigned long delay = msecs_to_jiffies(timeout);

		ch->deadline = jiffies + delay;
		if (!ch->deadline)
			ch->deadline = 1;
		mod_delayed_work(system_wq, &ch->work, delay);
	}
	return 0;
}

static void lm3643_timeout(struct work_struct *work)
{
	struct lm3643_channel *ch = container_of(to_delayed_work(work),
					struct lm3643_channel, work);
	struct lm3643 *chip = ch->chip;
	unsigned long now;

	mutex_lock(&lm3643_lock);
	if (chip->powered && ch->deadline) {
		now = jiffies;
		if (time_before(now, ch->deadline))
			mod_delayed_work(system_wq, &ch->work, ch->deadline - now);
		else
			lm3643_onoff(chip, ch->id.channel, false);
	}
	mutex_unlock(&lm3643_lock);
}

static int lm3643_set_driver(int set)
{
	struct lm3643 *chip;
	int ret = 0;

	mutex_lock(&lm3643_lock);
	chip = lm3643_chip;
	if (!chip) {
		ret = -ENODEV;
		goto out;
	}
	if (set) {
		ret = lm3643_power_on(chip);
		if (!ret)
			chip->users++;
	} else if (chip->users && !--chip->users) {
		ret = lm3643_power_off(chip);
	}
out:
	mutex_unlock(&lm3643_lock);
	return ret;
}

static int lm3643_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *a = (void *)arg;
	struct lm3643 *chip;
	int ret = 0, channel = a->channel;

	if (channel < 0 || channel >= LM3643_CHANNELS)
		return -EINVAL;
	mutex_lock(&lm3643_lock);
	chip = lm3643_chip;
	if (!chip) {
		ret = -ENODEV;
		goto out;
	}
	switch (cmd) {
	case FLASH_IOC_GET_DUTY_NUMBER:
		a->arg = LM3643_LEVELS;
		break;
	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		a->arg = LM3643_TORCH_LEVELS - 1;
		break;
	case FLASH_IOC_GET_DUTY_CURRENT:
		a->arg = DIV_ROUND_CLOSEST(lm3643_current_ua(
			clamp(a->arg, 0, LM3643_LEVELS - 1)), 1000);
		break;
	case FLASH_IOC_GET_HW_TIMEOUT:
		a->arg = LM3643_TIMEOUT_MS;
		break;
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		if (a->arg < 0 || a->arg > 60000)
			ret = -EINVAL;
		else
			chip->channel[channel].timeout = a->arg;
		break;
	default:
		if (!chip->powered || !chip->users) {
			ret = -EACCES;
			break;
		}
		switch (cmd) {
		case FLASH_IOC_SET_DUTY:
			ret = lm3643_level(chip, channel, a->arg);
			break;
		case FLASH_IOC_SET_ONOFF:
			if (a->arg != 0 && a->arg != 1)
				ret = -EINVAL;
			else
				ret = lm3643_onoff(chip, channel, a->arg);
			break;
		case FLASH_IOC_GET_HW_FAULT:
		case FLASH_IOC_GET_HW_FAULT2:
			ret = i2c_smbus_read_byte_data(chip->client,
				cmd == FLASH_IOC_GET_HW_FAULT ? LM3643_FLAG1 : LM3643_FLAG2);
			if (ret >= 0) {
				a->arg = ret;
				ret = 0;
			}
			break;
		default:
			ret = -ENOTTY;
		}
	}
out:
	mutex_unlock(&lm3643_lock);
	return ret;
}

static int lm3643_open(void)
{
	return 0;
}

static ssize_t lm3643_strobe(struct flashlight_arg arg)
{
	struct lm3643 *chip;
	unsigned int timeout;
	int ret, err;

	if (arg.channel < 0 || arg.channel >= LM3643_CHANNELS ||
	    arg.dur < 0 || arg.dur > 3000)
		return -EINVAL;
	mutex_lock(&lm3643_lock);
	chip = lm3643_chip;
	if (!chip) {
		ret = -ENODEV;
		goto out;
	}
	if (chip->enabled) {
		ret = -EBUSY;
		goto out;
	}
	ret = lm3643_power_on(chip);
	if (ret)
		goto out;
	chip->users++;
	ret = lm3643_level(chip, arg.channel, arg.level);
	if (ret)
		goto put;
	timeout = chip->channel[arg.channel].timeout;
	chip->channel[arg.channel].timeout = 0;
	ret = lm3643_onoff(chip, arg.channel, true);
	chip->channel[arg.channel].timeout = timeout;
	if (!ret) {
		msleep(chip->channel[arg.channel].level < LM3643_TORCH_LEVELS ?
			arg.dur : min(arg.dur, LM3643_TIMEOUT_MS));
		ret = lm3643_onoff(chip, arg.channel, false);
	}
put:
	if (!--chip->users) {
		err = lm3643_power_off(chip);
		if (!ret)
			ret = err;
	}
out:
	mutex_unlock(&lm3643_lock);
	return ret;
}

static struct flashlight_operations lm3643_ops = {
	.flashlight_open = lm3643_open,
	.flashlight_release = lm3643_open,
	.flashlight_ioctl = lm3643_ioctl,
	.flashlight_strobe_store = lm3643_strobe,
	.flashlight_set_driver = lm3643_set_driver,
};

static int lm3643_parse_dt(struct lm3643 *chip)
{
	struct device_node *np;
	unsigned int seen = 0;
	u32 channel, type, ct, part;
	int i;

	for_each_available_child_of_node(chip->client->dev.of_node, np) {
		if (of_property_read_u32(np, "reg", &channel) ||
		    channel >= LM3643_CHANNELS || (seen & BIT(channel)) ||
		    of_property_read_u32(np, "type", &type) ||
		    of_property_read_u32(np, "ct", &ct) ||
		    of_property_read_u32(np, "part", &part) ||
		    type >= FLASHLIGHT_TYPE_MAX || ct >= FLASHLIGHT_CT_MAX ||
		    part >= FLASHLIGHT_PART_MAX) {
			of_node_put(np);
			return -EINVAL;
		}
		chip->channel[channel].id = (struct flashlight_device_id) {
			.type = type,
			.ct = ct,
			.part = part,
			.name = LM3643_NAME,
			.channel = channel,
		};
		seen |= BIT(channel);
	}
	if (seen != GENMASK(LM3643_CHANNELS - 1, 0))
		return -EINVAL;
	for (i = 1; i < LM3643_CHANNELS; i++) {
		struct flashlight_device_id *a = &chip->channel[0].id;
		struct flashlight_device_id *b = &chip->channel[i].id;

		if (a->type == b->type && a->ct == b->ct && a->part == b->part)
			return -EINVAL;
	}
	return 0;
}

static int lm3643_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lm3643 *chip;
	int ret, i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->client = client;
	for (i = 0; i < LM3643_CHANNELS; i++) {
		chip->channel[i].chip = chip;
		INIT_DELAYED_WORK(&chip->channel[i].work, lm3643_timeout);
	}
	ret = lm3643_parse_dt(chip);
	if (ret)
		return ret;
	chip->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(chip->pinctrl))
		return PTR_ERR(chip->pinctrl);
	chip->high = pinctrl_lookup_state(chip->pinctrl, "hwen_high");
	if (IS_ERR(chip->high))
		return PTR_ERR(chip->high);
	chip->low = pinctrl_lookup_state(chip->pinctrl, "hwen_low");
	if (IS_ERR(chip->low))
		return PTR_ERR(chip->low);
	ret = pinctrl_select_state(chip->pinctrl, chip->low);
	if (ret)
		return ret;
	if (of_find_property(dev->of_node, "vin-supply", NULL)) {
		chip->vin = devm_regulator_get(dev, "vin");
		if (IS_ERR(chip->vin))
			return PTR_ERR(chip->vin);
	}
	mutex_lock(&lm3643_lock);
	if (lm3643_chip) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = lm3643_power_on(chip);
	if (ret)
		goto unlock;
	dev_info(dev, "device ID 0x%02x verified\n", LM3643_DEVICE_ID);
	ret = lm3643_power_off(chip);
	if (ret)
		goto unlock;
	i2c_set_clientdata(client, chip);
	lm3643_chip = chip;
	mutex_unlock(&lm3643_lock);
	for (i = 0; i < LM3643_CHANNELS; i++) {
		ret = flashlight_dev_register_by_device_id(&chip->channel[i].id,
						   &lm3643_ops);
		if (ret)
			goto unregister;
	}
	return 0;
unregister:
	mutex_lock(&lm3643_lock);
	lm3643_chip = NULL;
	lm3643_power_off(chip);
	mutex_unlock(&lm3643_lock);
	while (i--)
		flashlight_dev_unregister_by_device_id(&chip->channel[i].id);
	for (i = 0; i < LM3643_CHANNELS; i++)
		cancel_delayed_work_sync(&chip->channel[i].work);
	return ret;
unlock:
	mutex_unlock(&lm3643_lock);
	return ret;
}

static void lm3643_shutdown(struct i2c_client *client)
{
	struct lm3643 *chip = i2c_get_clientdata(client);

	mutex_lock(&lm3643_lock);
	lm3643_chip = NULL;
	if (lm3643_power_off(chip))
		dev_err(&client->dev, "failed to power off\n");
	chip->users = 0;
	mutex_unlock(&lm3643_lock);
}

static void lm3643_remove(struct i2c_client *client)
{
	struct lm3643 *chip = i2c_get_clientdata(client);
	int i;

	lm3643_shutdown(client);
	for (i = 0; i < LM3643_CHANNELS; i++) {
		flashlight_dev_unregister_by_device_id(&chip->channel[i].id);
		cancel_delayed_work_sync(&chip->channel[i].work);
	}
}

static const struct of_device_id lm3643_of_match[] = {
	{ .compatible = "mediatek,lm3643" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm3643_of_match);

static const struct i2c_device_id lm3643_i2c_ids[] = {
	{ "lm3643", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3643_i2c_ids);

static struct i2c_driver lm3643_driver = {
	.driver = {
		.name = LM3643_NAME,
		.of_match_table = lm3643_of_match,
	},
	.probe = lm3643_probe,
	.remove = lm3643_remove,
	.shutdown = lm3643_shutdown,
	.id_table = lm3643_i2c_ids,
};
module_i2c_driver(lm3643_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LM3643 MediaTek flashlight core driver");
