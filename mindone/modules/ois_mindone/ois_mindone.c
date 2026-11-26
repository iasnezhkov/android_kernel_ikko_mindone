// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/ktime.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <mindone/i2c-mt65xx.h>
#include <mindone/imgsensor-power.h>

#define OIS_BUS_HZ		100000
#define OIS_CHIP_ID		0x5a30
#define OIS_BOOT_DELAY_MIN_MS	100
#define OIS_BOOT_DELAY_MAX_MS	2000
#define OIS_BOOT_RETRY_MS	30
#define OIS_BOOT_RETRIES	6
#define OIS_REG26_MAX		0x000f

#define OIS_REG_CTRL		0x00
#define OIS_REG_CHIP_ID		0x01
#define OIS_REG_ENABLE		0x0a
#define OIS_REG_FLAG19		0x19
#define OIS_REG_POS_X		0x20
#define OIS_REG_POS_Y		0x21
#define OIS_REG_MODE		0x26
#define OIS_REG_GYRO_X		0x34
#define OIS_REG_GYRO_Y		0x36
#define OIS_REG_DRV_X_HI	0x77
#define OIS_REG_DRV_X_LO	0x78
#define OIS_REG_DRV_Y_HI	0x79
#define OIS_REG_DRV_Y_LO	0x7a
#define OIS_REG_TICK		0x7e

enum ois_mode {
	OIS_MODE_OFF = 0,
	OIS_MODE_CENTER = 1,
	OIS_MODE_OIS = 2,
};

static const char *const ois_mode_names[] = {
	[OIS_MODE_OFF] = "off",
	[OIS_MODE_CENTER] = "center",
	[OIS_MODE_OIS] = "ois",
};

static unsigned int default_mode = OIS_MODE_OIS;
static unsigned int boot_delay_ms = 350;
static int reg26_off = -1;
static int reg26_center = -1;
static int reg26_ois = -1;

static int ois_default_mode_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, OIS_MODE_OFF, OIS_MODE_OIS);
}

static const struct kernel_param_ops ois_default_mode_ops = {
	.set = ois_default_mode_set,
	.get = param_get_uint,
};

module_param_cb(default_mode, &ois_default_mode_ops, &default_mode, 0644);
MODULE_PARM_DESC(default_mode, "Initial OIS mode: 0 off, 1 center, 2 ois");

static int ois_boot_delay_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, OIS_BOOT_DELAY_MIN_MS,
				     OIS_BOOT_DELAY_MAX_MS);
}

static const struct kernel_param_ops ois_boot_delay_ops = {
	.set = ois_boot_delay_set,
	.get = param_get_uint,
};

module_param_cb(boot_delay_ms, &ois_boot_delay_ops, &boot_delay_ms, 0644);
MODULE_PARM_DESC(boot_delay_ms, "Delay in ms after camera power-on before probing 0x69");

static int ois_reg26_set(const char *val, const struct kernel_param *kp)
{
	int v, ret;

	ret = kstrtoint(val, 0, &v);
	if (ret)
		return ret;
	if (v < -1 || v > OIS_REG26_MAX)
		return -EINVAL;
	WRITE_ONCE(*(int *)kp->arg, v);
	return 0;
}

static const struct kernel_param_ops ois_reg26_ops = {
	.set = ois_reg26_set,
	.get = param_get_int,
};

module_param_cb(reg26_off, &ois_reg26_ops, &reg26_off, 0644);
MODULE_PARM_DESC(reg26_off, "Optional value (0..15) for reg 0x26 in off mode; -1 leaves unchanged");
module_param_cb(reg26_center, &ois_reg26_ops, &reg26_center, 0644);
MODULE_PARM_DESC(reg26_center, "Optional value (0..15) for reg 0x26 in center mode; -1 leaves unchanged");
module_param_cb(reg26_ois, &ois_reg26_ops, &reg26_ois, 0644);
MODULE_PARM_DESC(reg26_ois, "Optional value (0..15) for reg 0x26 in ois mode; -1 leaves unchanged");

struct ois_mindone {
	struct i2c_client *client;
	struct notifier_block nb;
	struct delayed_work boot_work;
	struct mutex lock;
	spinlock_t pwr_lock;
	atomic_t powered;
	atomic_t seq;
	ktime_t power_on_at;
	int sensor_idx;
	bool ready;
	bool logged_once;
	enum ois_mode target_mode;
	enum ois_mode active_mode;
	u16 chip_id;
	u16 reg0a;
	u16 reg26;
	s64 boot_ms;
	int last_err;
};

static int ois_read_reg(struct ois_mindone *ois, u8 reg, u16 *val)
{
	u8 buf[2];
	struct i2c_msg msgs[2] = {
		{
			.addr = ois->client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = ois->client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(buf),
			.buf = buf,
		},
	};
	int ret;

	ret = i2c_transfer(ois->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return ret < 0 ? ret : -EIO;
	*val = get_unaligned_be16(buf);
	return 0;
}

static int ois_write_reg(struct ois_mindone *ois, u8 reg, u16 val)
{
	u8 buf[3];
	struct i2c_msg msg = {
		.addr = ois->client->addr,
		.flags = 0,
		.len = sizeof(buf),
		.buf = buf,
	};
	int ret;

	if (reg != OIS_REG_ENABLE && reg != OIS_REG_MODE)
		return -EACCES;
	buf[0] = reg;
	put_unaligned_be16(val, &buf[1]);
	ret = i2c_transfer(ois->client->adapter, &msg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;
	return 0;
}

static int ois_apply_locked(struct ois_mindone *ois, enum ois_mode mode)
{
	u16 want_0a;
	int want_26;
	int ret;

	switch (mode) {
	case OIS_MODE_OFF:
		want_0a = 0x0000;
		want_26 = READ_ONCE(reg26_off);
		break;
	case OIS_MODE_CENTER:
		want_0a = 0x0000;
		want_26 = READ_ONCE(reg26_center);
		break;
	case OIS_MODE_OIS:
		want_0a = 0x0001;
		want_26 = READ_ONCE(reg26_ois);
		break;
	default:
		return -EINVAL;
	}

	if (want_26 >= 0) {
		ret = ois_write_reg(ois, OIS_REG_MODE, (u16)want_26);
		if (ret) {
			ois->last_err = ret;
			return ret;
		}
		ois->reg26 = (u16)want_26;
	}

	ret = ois_write_reg(ois, OIS_REG_ENABLE, want_0a);
	if (ret) {
		ois->last_err = ret;
		return ret;
	}
	ois->reg0a = want_0a;
	ois->active_mode = mode;
	ois->last_err = 0;
	return 0;
}

static void ois_boot_work(struct work_struct *work)
{
	struct ois_mindone *ois = container_of(to_delayed_work(work),
					       struct ois_mindone, boot_work);
	int seq = atomic_read(&ois->seq);
	u16 id = 0, r0a = 0, r26 = 0;
	ktime_t pwr_at;
	unsigned long flags;
	int i, ret = -ENODEV;

	for (i = 0; i < OIS_BOOT_RETRIES; i++) {
		if (!atomic_read(&ois->powered) || atomic_read(&ois->seq) != seq)
			return;
		ret = ois_read_reg(ois, OIS_REG_CHIP_ID, &id);
		if (!ret)
			break;
		if (i + 1 < OIS_BOOT_RETRIES)
			msleep(OIS_BOOT_RETRY_MS);
	}

	spin_lock_irqsave(&ois->pwr_lock, flags);
	pwr_at = ois->power_on_at;
	spin_unlock_irqrestore(&ois->pwr_lock, flags);

	mutex_lock(&ois->lock);
	if (!atomic_read(&ois->powered) || atomic_read(&ois->seq) != seq)
		goto unlock;

	if (ret || id != OIS_CHIP_ID) {
		ois->chip_id = ret ? 0 : id;
		ois->ready = false;
		ois->active_mode = OIS_MODE_OFF;
		ois->last_err = ret ? ret : -ENODEV;
		dev_warn(&ois->client->dev, "probe failed: ret=%d id=0x%04x\n",
			 ret, ois->chip_id);
		goto unlock;
	}

	ois->chip_id = id;
	ois->boot_ms = ktime_ms_delta(ktime_get(), pwr_at);
	if (!ois_read_reg(ois, OIS_REG_ENABLE, &r0a))
		ois->reg0a = r0a;
	if (!ois_read_reg(ois, OIS_REG_MODE, &r26))
		ois->reg26 = r26;
	ois->ready = true;

	ret = ois_apply_locked(ois, ois->target_mode);
	if (ret) {
		dev_warn(&ois->client->dev, "apply mode %s failed: %d\n",
			 ois_mode_names[ois->target_mode], ret);
		goto unlock;
	}

	if (!ois->logged_once) {
		ois->logged_once = true;
		dev_info(&ois->client->dev,
			 "chip 0x%04x ready in %lld ms, mode=%s r0a=0x%04x r26=0x%04x\n",
			 ois->chip_id, ois->boot_ms,
			 ois_mode_names[ois->active_mode], ois->reg0a, ois->reg26);
	} else {
		dev_dbg(&ois->client->dev,
			"ready in %lld ms, mode=%s r0a=0x%04x r26=0x%04x\n",
			ois->boot_ms, ois_mode_names[ois->active_mode],
			ois->reg0a, ois->reg26);
	}

unlock:
	mutex_unlock(&ois->lock);
}

static int ois_power_event(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	struct ois_mindone *ois = container_of(nb, struct ois_mindone, nb);
	const struct imgsensor_power_event *event = data;
	unsigned long flags;

	switch (action) {
	case IMGSENSOR_POWER_ON:
		spin_lock_irqsave(&ois->pwr_lock, flags);
		ois->power_on_at = ktime_get();
		ois->sensor_idx = event ? event->sensor_idx : -1;
		spin_unlock_irqrestore(&ois->pwr_lock, flags);
		atomic_inc(&ois->seq);
		atomic_set(&ois->powered, 1);
		mod_delayed_work(system_wq, &ois->boot_work,
				 msecs_to_jiffies(READ_ONCE(boot_delay_ms)));
		break;
	case IMGSENSOR_POWER_OFF:
		atomic_inc(&ois->seq);
		atomic_set(&ois->powered, 0);
		cancel_delayed_work_sync(&ois->boot_work);
		mutex_lock(&ois->lock);
		if (ois->ready && ois->active_mode != OIS_MODE_OFF)
			ois_apply_locked(ois, OIS_MODE_OFF);
		ois->ready = false;
		ois->active_mode = OIS_MODE_OFF;
		mutex_unlock(&ois->lock);
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ois_mindone *ois = dev_get_drvdata(dev);
	enum ois_mode mode;
	int ret;

	ret = mutex_lock_interruptible(&ois->lock);
	if (ret)
		return ret;
	mode = ois->target_mode;
	mutex_unlock(&ois->lock);
	return sysfs_emit(buf, "%s\n", ois_mode_names[mode]);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct ois_mindone *ois = dev_get_drvdata(dev);
	enum ois_mode mode;
	int ret = 0;

	if (sysfs_streq(buf, "off"))
		mode = OIS_MODE_OFF;
	else if (sysfs_streq(buf, "center"))
		mode = OIS_MODE_CENTER;
	else if (sysfs_streq(buf, "ois"))
		mode = OIS_MODE_OIS;
	else
		return -EINVAL;

	ret = mutex_lock_interruptible(&ois->lock);
	if (ret)
		return ret;
	ois->target_mode = mode;
	if (ois->ready && atomic_read(&ois->powered)) {
		ret = ois_apply_locked(ois, mode);
		if (ret && !atomic_read(&ois->powered))
			ret = 0;
	}
	mutex_unlock(&ois->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(mode);

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ois_mindone *ois = dev_get_drvdata(dev);
	unsigned long flags;
	int sensor_idx, ret;
	ssize_t n;

	ret = mutex_lock_interruptible(&ois->lock);
	if (ret)
		return ret;
	spin_lock_irqsave(&ois->pwr_lock, flags);
	sensor_idx = ois->sensor_idx;
	spin_unlock_irqrestore(&ois->pwr_lock, flags);
	n = sysfs_emit(buf,
		       "powered=%d ready=%d sensor_idx=%d chip_id=0x%04x target=%s active=%s reg0a=0x%04x reg26=0x%04x boot_ms=%lld err=%d\n",
		       atomic_read(&ois->powered), ois->ready ? 1 : 0,
		       sensor_idx, ois->chip_id,
		       ois_mode_names[ois->target_mode],
		       ois_mode_names[ois->active_mode],
		       ois->reg0a, ois->reg26, ois->boot_ms, ois->last_err);
	mutex_unlock(&ois->lock);
	return n;
}
static DEVICE_ATTR_RO(status);

static ssize_t telemetry_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct ois_mindone *ois = dev_get_drvdata(dev);
	u16 id, r00, r0a, r19, r26, px, py, gx, gy, fx_hi, fx_lo, fy_hi, fy_lo, tick;
	int ret;

	ret = mutex_lock_interruptible(&ois->lock);
	if (ret)
		return ret;
	if (!ois->ready || !atomic_read(&ois->powered)) {
		mutex_unlock(&ois->lock);
		return sysfs_emit(buf, "offline\n");
	}
	ret = ois_read_reg(ois, OIS_REG_CHIP_ID, &id) ||
	      ois_read_reg(ois, OIS_REG_CTRL, &r00) ||
	      ois_read_reg(ois, OIS_REG_ENABLE, &r0a) ||
	      ois_read_reg(ois, OIS_REG_FLAG19, &r19) ||
	      ois_read_reg(ois, OIS_REG_MODE, &r26) ||
	      ois_read_reg(ois, OIS_REG_POS_X, &px) ||
	      ois_read_reg(ois, OIS_REG_POS_Y, &py) ||
	      ois_read_reg(ois, OIS_REG_GYRO_X, &gx) ||
	      ois_read_reg(ois, OIS_REG_GYRO_Y, &gy) ||
	      ois_read_reg(ois, OIS_REG_DRV_X_HI, &fx_hi) ||
	      ois_read_reg(ois, OIS_REG_DRV_X_LO, &fx_lo) ||
	      ois_read_reg(ois, OIS_REG_DRV_Y_HI, &fy_hi) ||
	      ois_read_reg(ois, OIS_REG_DRV_Y_LO, &fy_lo) ||
	      ois_read_reg(ois, OIS_REG_TICK, &tick);
	mutex_unlock(&ois->lock);
	if (ret)
		return sysfs_emit(buf, "offline\n");
	return sysfs_emit(buf,
			  "id=0x%04x r00=0x%04x r0a=0x%04x r19=0x%04x r26=0x%04x x=%d y=%d gx=%d gy=%d fx=0x%04x%04x fy=0x%04x%04x tick=%u\n",
			  id, r00, r0a, r19, r26,
			  (s16)px, (s16)py, (s16)gx, (s16)gy,
			  fx_hi, fx_lo, fy_hi, fy_lo, tick);
}
static DEVICE_ATTR_RO(telemetry);

static struct attribute *ois_mindone_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_status.attr,
	&dev_attr_telemetry.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ois_mindone);

static int ois_mindone_probe(struct i2c_client *client)
{
	struct ois_mindone *ois;
	int ret;

	ois = devm_kzalloc(&client->dev, sizeof(*ois), GFP_KERNEL);
	if (!ois)
		return -ENOMEM;

	ois->client = client;
	mutex_init(&ois->lock);
	spin_lock_init(&ois->pwr_lock);
	INIT_DELAYED_WORK(&ois->boot_work, ois_boot_work);
	ois->sensor_idx = -1;
	ois->target_mode = READ_ONCE(default_mode);
	ois->active_mode = OIS_MODE_OFF;
	ois->nb.notifier_call = ois_power_event;
	i2c_set_clientdata(client, ois);

	ret = mtk_i2c_set_slow_target(client->adapter->nr, client->addr,
				      OIS_BUS_HZ);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "i2c slow_target setup\n");

	ret = imgsensor_power_register_notifier(&ois->nb);
	if (ret) {
		mtk_i2c_set_slow_target(client->adapter->nr, client->addr, 0);
		return dev_err_probe(&client->dev, ret,
				     "imgsensor power notifier\n");
	}
	return 0;
}

static void ois_mindone_remove(struct i2c_client *client)
{
	struct ois_mindone *ois = i2c_get_clientdata(client);

	imgsensor_power_unregister_notifier(&ois->nb);
	atomic_inc(&ois->seq);
	atomic_set(&ois->powered, 0);
	cancel_delayed_work_sync(&ois->boot_work);
	mutex_lock(&ois->lock);
	if (ois->ready && ois->active_mode != OIS_MODE_OFF)
		ois_apply_locked(ois, OIS_MODE_OFF);
	ois->ready = false;
	ois->active_mode = OIS_MODE_OFF;
	mutex_unlock(&ois->lock);
	mtk_i2c_set_slow_target(client->adapter->nr, client->addr, 0);
}

static const struct of_device_id ois_mindone_of_match[] = {
	{ .compatible = "mindone,ois-5a30" },
	{ }
};
MODULE_DEVICE_TABLE(of, ois_mindone_of_match);

static const struct i2c_device_id ois_mindone_id[] = {
	{ "ois-5a30" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ois_mindone_id);

static struct i2c_driver ois_mindone_driver = {
	.driver = {
		.name = "ois-mindone",
		.of_match_table = ois_mindone_of_match,
		.dev_groups = ois_mindone_groups,
	},
	.probe = ois_mindone_probe,
	.remove = ois_mindone_remove,
	.id_table = ois_mindone_id,
};
module_i2c_driver(ois_mindone_driver);

MODULE_DESCRIPTION("iKKO MindOne 0x5A30 standalone OIS MCU driver");
MODULE_LICENSE("GPL");
