// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <mindone/i2c-mt65xx.h>
#include <mindone/imgsensor-power.h>
#include "aw86006.h"

#define AW_IDENTIFY_ALL		(AW_IDENTIFY_HEADER | AW_IDENTIFY_RECORD | \
				 AW_IDENTIFY_QUERY | AW_IDENTIFY_FLASH)
#define AW_UNTRIED		1
#define AW_HOLD_BYTE		0xac
#define AW_HOLD_START_US	2000
#define AW_HOLD_END_US		6000
#define AW_HOLD_WRITES		20
#define AW_APP_BOOT_MS		50
#define AW_LOCKED_GAP_US	1000
#define AW_APP_WRITE_US		1000
#define AW_APP_SETTLE_US	2000
#define AW_APP_TRIES		5
#define AW_SAMPLE_MS		20
#define AW_RUN_MIN_MS		10
#define AW_RUN_MAX_MS		10000
#define AW_BUS_MIN_HZ		10000
#define AW_BUS_MAX_HZ		100000
#define AW_WAIT_MAX_MS		5000
#define AW_POLL_FAST_US		50000
#define AW_POLL_SHORT_US	2000
#define AW_POLL_LONG_US		10000
#define AW_SYS_ADDR		0x61
#define AW_SYS_TRIES		6
#define AW_SYS_GAP_MS		50
#define AW_REG_CHIP_ID		0x0000
#define AW_REG_OIS		0x0001
#define AW_REG_GYRO_TYPE	0x0007
#define AW_REG_GYRO_SPI		0x0008
#define AW_REG_VERSION		0x0010
#define AW_REG_MODE		0xf300
#define AW_REG_MODE_KEY		0xf31f
#define AW_REG_HALL		0xf44c
#define AW_REG_SYS_STATE	0xff10
#define AW_REG_SYS_MODE		0xff11
#define AW_REG_SYS_KEY		0xff1f
#define AW_SYS_UNLOCK		0xae
#define AW_SYS_NORMAL		0x02
#define AW_MODE_UNLOCK		0xa6
#define AW_MODE_CENTRE		0x04
#define AW_MODE_NONE		0x00

enum {
	AW_RUNTIME_OFF,
	AW_RUNTIME_CENTRE,
	AW_RUNTIME_OIS,
};

static unsigned int aw_identify;
static bool aw_sync;
static atomic_t aw_update = ATOMIC_INIT(0);
static unsigned int aw_runtime;
static unsigned int aw_run_ms = 200;
static int aw_gyro_type = -1;
static int aw_gyro_spi = -1;
static unsigned int aw_bus_hz = AW_BUS_MAX_HZ;
static unsigned int aw_wait_ms = 500;
static char aw_sync_pin[AW_PIN_NAME_LEN] = "vcamaf";
static char aw_fw_name[64] = "aw86006-mindone.prog";

static int aw86006_identify_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, 0, AW_IDENTIFY_ALL);
}

static const struct kernel_param_ops aw86006_identify_ops = {
	.set = aw86006_identify_set,
	.get = param_get_uint,
};

module_param_cb(identify, &aw86006_identify_ops, &aw_identify, 0644);
MODULE_PARM_DESC(identify,
		 "Read-only identification at each module power-up: 1 program header, 2 calibration record, 4 boot loader version and ID queries, 8 the whole 64 KiB flash into the flash file; 0 off");

module_param_named(sync, aw_sync, bool, 0644);
MODULE_PARM_DESC(sync,
		 "Hold the chip in its ROM boot loader with 0xAC writes 2-6 ms after sync_pin turns on (only with identify or update)");

module_param_string(sync_pin, aw_sync_pin, sizeof(aw_sync_pin), 0444);
MODULE_PARM_DESC(sync_pin, "imgsensor pin whose turn-on starts the boot loader window");

static int aw86006_update_set(const char *val, const struct kernel_param *kp)
{
	bool on;
	int ret;

	ret = kstrtobool(val, &on);
	if (ret)
		return ret;
	atomic_set(&aw_update, on);
	return 0;
}

static int aw86006_update_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%c\n",
			 atomic_read(&aw_update) ? 'Y' : 'N');
}

static const struct kernel_param_ops aw86006_update_ops = {
	.set = aw86006_update_set,
	.get = aw86006_update_get,
};

module_param_cb(update, &aw86006_update_ops, NULL, 0644);
MODULE_PARM_DESC(update,
		 "Arm one program update from fw_name at the next module power-up; cleared when that power-up starts");

module_param_string(fw_name, aw_fw_name, sizeof(aw_fw_name), 0444);
MODULE_PARM_DESC(fw_name, "Program image requested by the update");

static int aw86006_runtime_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, AW_RUNTIME_OFF, AW_RUNTIME_OIS);
}

static const struct kernel_param_ops aw86006_runtime_ops = {
	.set = aw86006_runtime_set,
	.get = param_get_uint,
};

module_param_cb(runtime, &aw86006_runtime_ops, &aw_runtime, 0644);
MODULE_PARM_DESC(runtime,
		 "With the program running: 1 centring, 2 OIS on, for run_ms after power-up; 0 off");

static int aw86006_run_ms_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, AW_RUN_MIN_MS, AW_RUN_MAX_MS);
}

static const struct kernel_param_ops aw86006_run_ms_ops = {
	.set = aw86006_run_ms_set,
	.get = param_get_uint,
};

module_param_cb(run_ms, &aw86006_run_ms_ops, &aw_run_ms, 0644);
MODULE_PARM_DESC(run_ms, "Hard limit of a runtime session in ms (10..10000)");

static int aw86006_byte_set(const char *val, const struct kernel_param *kp)
{
	int value;
	int ret;

	ret = kstrtoint(val, 0, &value);
	if (ret)
		return ret;
	if (value < -1 || value > U8_MAX)
		return -EINVAL;
	*(int *)kp->arg = value;
	return 0;
}

static const struct kernel_param_ops aw86006_byte_ops = {
	.set = aw86006_byte_set,
	.get = param_get_int,
};

module_param_cb(gyro_type, &aw86006_byte_ops, &aw_gyro_type, 0644);
MODULE_PARM_DESC(gyro_type, "Value for register 0x0007 before a runtime session; -1 leaves it");
module_param_cb(gyro_spi, &aw86006_byte_ops, &aw_gyro_spi, 0644);
MODULE_PARM_DESC(gyro_spi, "Value for register 0x0008 before a runtime session; -1 leaves it");

static int aw86006_bus_hz_set(const char *val, const struct kernel_param *kp)
{
	unsigned int hz;
	int ret;

	ret = kstrtouint(val, 0, &hz);
	if (ret)
		return ret;
	if (hz && (hz < AW_BUS_MIN_HZ || hz > AW_BUS_MAX_HZ))
		return -EINVAL;
	*(unsigned int *)kp->arg = hz;
	return 0;
}

static const struct kernel_param_ops aw86006_bus_hz_ops = {
	.set = aw86006_bus_hz_set,
	.get = param_get_uint,
};

module_param_cb(bus_hz, &aw86006_bus_hz_ops, &aw_bus_hz, 0444);
MODULE_PARM_DESC(bus_hz, "i2c_mt65xx slow_target rate for every transfer to the chip (10000..100000); 0 keeps the DT bus speed");

static int aw86006_wait_ms_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, 0, AW_WAIT_MAX_MS);
}

static const struct kernel_param_ops aw86006_wait_ms_ops = {
	.set = aw86006_wait_ms_set,
	.get = param_get_uint,
};

module_param_cb(wait_ms, &aw86006_wait_ms_ops, &aw_wait_ms, 0644);
MODULE_PARM_DESC(wait_ms,
		 "Before any data transfer, wait up to this many ms after sync_pin for 0x69 to acknowledge an address-only write (0..5000)");

static const char *const aw86006_mode_names[] = {
	"unknown", "no answer", "rom boot loader", "program", "absent",
};

static const char *const aw86006_area_names[] = {
	"not read", "read failed", "blank", "invalid", "valid",
};

static const char *const aw86006_hold_names[] = {
	"off", "acked", "missed", "slow_target refused",
};

static const char *const aw86006_update_names[] = {
	"none", "no firmware file", "image refused", "needs the rom boot loader",
	"current", "failed", "written and verified, power-cycle the module",
};

static const char *const aw86006_run_names[] = {
	"idle", "no program running", "active", "done", "stopped",
	"stopped at power-off", "failed",
};

bool aw86006_stopping(struct aw86006 *aw)
{
	return READ_ONCE(aw->abort) || !aw->powered;
}

static void aw86006_log_xfer(struct aw86006 *aw, ktime_t at,
			     const struct i2c_msg *msgs, int num, int ret)
{
	struct aw86006_xfer *x;
	int i;

	if (aw->ntrace >= AW_TRACE_LEN) {
		aw->trace_more++;
		return;
	}
	x = &aw->trace[aw->ntrace++];
	x->at = at;
	x->addr = msgs[0].addr;
	x->wlen = 0;
	x->rlen = 0;
	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			x->rlen += msgs[i].len;
		else
			x->wlen += msgs[i].len;
	}
	x->ret = ret;
}

static int aw86006_transfer(struct aw86006 *aw, struct i2c_msg *msgs, int num,
			    bool locked)
{
	struct i2c_adapter *adap = aw->client->adapter;
	ktime_t at = ktime_get();
	int ret;

	if (!aw->powered)
		return -ENODEV;
	if (locked)
		ret = __i2c_transfer(adap, msgs, num);
	else
		ret = i2c_transfer(adap, msgs, num);
	if (ret == num)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;
	aw86006_log_xfer(aw, at, msgs, num, ret);
	return ret;
}

static int aw86006_ping(struct aw86006 *aw, u16 addr)
{
	u8 none = 0;
	struct i2c_msg msg = { .addr = addr, .len = 0, .buf = &none };

	return aw86006_transfer(aw, &msg, 1, false);
}

int aw86006_rom_xfer(struct aw86006 *aw, u8 *tx, u16 tx_len,
		     unsigned int wait_us, u8 *rx, u16 rx_len)
{
	struct i2c_adapter *adap = aw->client->adapter;
	u16 addr = aw->client->addr;
	u8 zero = 0;
	struct i2c_msg req = { .addr = addr, .len = tx_len, .buf = tx };
	struct i2c_msg rsp[] = {
		{ .addr = addr, .len = 1, .buf = &zero },
		{ .addr = addr, .flags = I2C_M_RD, .len = rx_len, .buf = rx },
	};
	int ret;

	memset(rx, 0, rx_len);
	if (wait_us > AW_LOCKED_GAP_US) {
		ret = aw86006_transfer(aw, &req, 1, false);
		if (!ret) {
			msleep(DIV_ROUND_UP(wait_us, USEC_PER_MSEC));
			ret = aw86006_transfer(aw, rsp, ARRAY_SIZE(rsp), false);
		}
		return ret;
	}
	i2c_lock_bus(adap, I2C_LOCK_SEGMENT);
	ret = aw86006_transfer(aw, &req, 1, true);
	if (!ret) {
		usleep_range(wait_us, wait_us + 50);
		ret = aw86006_transfer(aw, rsp, ARRAY_SIZE(rsp), true);
	}
	i2c_unlock_bus(adap, I2C_LOCK_SEGMENT);
	return ret;
}

static int aw86006_app_read(struct aw86006 *aw, u16 reg, u8 *val, u16 len)
{
	u8 ptr[2];
	struct i2c_msg msgs[] = {
		{ .addr = aw->client->addr, .len = sizeof(ptr), .buf = ptr },
		{ .addr = aw->client->addr, .flags = I2C_M_RD, .len = len,
		  .buf = val },
	};

	put_unaligned_be16(reg, ptr);
	return aw86006_transfer(aw, msgs, ARRAY_SIZE(msgs), false);
}

static int aw86006_app_write(struct aw86006 *aw, u16 reg, u8 val)
{
	u8 buf[3];
	struct i2c_msg msg = {
		.addr = aw->client->addr, .len = sizeof(buf), .buf = buf,
	};
	int ret;

	put_unaligned_be16(reg, buf);
	buf[2] = val;
	ret = aw86006_transfer(aw, &msg, 1, false);
	usleep_range(AW_APP_WRITE_US, AW_APP_WRITE_US + 100);
	return ret;
}

static int aw86006_app_set(struct aw86006 *aw, u16 reg, u8 val, u16 check,
			   u8 expect)
{
	u8 got;
	int ret = -EIO;
	int i;

	for (i = 0; i < AW_APP_TRIES; i++) {
		ret = aw86006_app_write(aw, reg, val);
		if (ret)
			continue;
		usleep_range(AW_APP_SETTLE_US, AW_APP_SETTLE_US + 100);
		ret = aw86006_app_read(aw, check, &got, 1);
		if (!ret && got == expect)
			return 0;
		if (!ret)
			ret = -EIO;
	}
	return ret;
}

static int aw86006_slow(struct aw86006 *aw)
{
	struct i2c_client *client = aw->client;

	if (aw->slow_set || !aw_bus_hz)
		return 0;
	aw->slow_ret = mtk_i2c_set_slow_target(client->adapter->nr,
					       client->addr, aw_bus_hz);
	aw->slow_set = !aw->slow_ret;
	if (aw->slow_ret)
		dev_err(&client->dev, "slow_target %d:0x%02x:%u refused: %d\n",
			client->adapter->nr, client->addr, aw_bus_hz,
			aw->slow_ret);
	return aw->slow_ret;
}

static void aw86006_hold(struct aw86006 *aw)
{
	u8 cmd[] = { AW_HOLD_BYTE, AW_HOLD_BYTE, AW_HOLD_BYTE };
	struct i2c_msg msg = {
		.addr = aw->client->addr, .len = sizeof(cmd), .buf = cmd,
	};
	s64 at = ktime_us_delta(ktime_get(), aw->pin_on);

	aw->hold_first_us = -1;
	aw->hold_writes = 0;
	aw->hold_acks = 0;
	if (at < AW_HOLD_START_US)
		usleep_range(AW_HOLD_START_US - at,
			     AW_HOLD_START_US - at + 50);
	while (aw->hold_writes < AW_HOLD_WRITES) {
		at = ktime_us_delta(ktime_get(), aw->pin_on);
		if (at > AW_HOLD_END_US)
			break;
		aw->hold_writes++;
		if (aw86006_transfer(aw, &msg, 1, false))
			continue;
		if (!aw->hold_acks++)
			aw->hold_first_us = at;
	}
	aw->hold = aw->hold_acks ? AW_HOLD_ACKED : AW_HOLD_MISSED;
}

static bool aw86006_presence(struct aw86006 *aw)
{
	s64 limit = (s64)READ_ONCE(aw_wait_ms) * USEC_PER_MSEC;
	s64 at;
	unsigned int gap;

	for (;;) {
		at = ktime_us_delta(ktime_get(), aw->pin_on);
		if (!aw86006_ping(aw, aw->client->addr)) {
			aw->answer_us = at;
			return true;
		}
		aw->polls++;
		if (aw86006_stopping(aw) || at >= limit)
			return false;
		gap = at < AW_POLL_FAST_US ? AW_POLL_SHORT_US : AW_POLL_LONG_US;
		usleep_range(gap, gap + 100);
	}
}

static void aw86006_sys_probe(struct aw86006 *aw)
{
	s64 up = ktime_ms_delta(ktime_get(), aw->pin_on);

	if (up < AW_APP_BOOT_MS)
		msleep(AW_APP_BOOT_MS - up);
	while (aw->sys_polls < AW_SYS_TRIES) {
		aw->sys_ret = aw86006_ping(aw, AW_SYS_ADDR);
		aw->sys_polls++;
		if (!aw->sys_ret || aw86006_stopping(aw) ||
		    aw->sys_polls == AW_SYS_TRIES)
			return;
		msleep(AW_SYS_GAP_MS);
	}
}

static void aw86006_app_probe(struct aw86006 *aw)
{
	s64 up = ktime_ms_delta(ktime_get(), aw->pin_on);
	u8 ver[4];

	if (up < AW_APP_BOOT_MS)
		msleep(AW_APP_BOOT_MS - up);
	if (aw86006_app_read(aw, AW_REG_CHIP_ID, &aw->app_id, 1) ||
	    aw86006_app_read(aw, AW_REG_VERSION, ver, sizeof(ver)))
		return;
	aw->app_version = get_unaligned_le32(ver);
	if (aw->app_id == AW_APP_ID)
		aw->mode = AW_MODE_APP;
}

static void aw86006_flash(struct aw86006 *aw, const u8 *img, size_t size)
{
	struct device *dev = &aw->client->dev;
	u8 *after;
	int ret;

	dev_info(dev, "writing %zu bytes of %s\n", size, aw_fw_name);
	ret = aw86006_rom_update(aw, img, size);
	aw->update_ret = ret;
	if (ret) {
		aw->update = AW_UPDATE_FAILED;
		dev_err(dev, "update failed at 0x%08x: %d\n", aw->update_addr,
			ret);
		return;
	}
	aw->update = AW_UPDATE_DONE;
	memcpy(aw->header, img + AW_MOVE_LEN, AW_HEADER_LEN);
	aw->header_state = aw86006_area_state(aw86006_header_parse(aw->header,
								   &aw->header_info));
	dev_info(dev, "program 0x%08x written and verified\n",
		 aw->header_info.app_version);
	if (aw->record_state != AW_AREA_BLANK &&
	    aw->record_state != AW_AREA_INVALID &&
	    aw->record_state != AW_AREA_VALID)
		return;
	after = kmalloc(AW_RECORD_LEN, GFP_KERNEL);
	if (!after)
		return;
	if (aw86006_rom_read_area(aw, AW_FLASH_RECORD, after, AW_RECORD_LEN))
		aw->record_after = AW_AREA_FAILED;
	else if (memcmp(after, aw->record, AW_RECORD_LEN))
		aw->record_after = AW_AREA_INVALID;
	else
		aw->record_after = AW_AREA_VALID;
	kfree(after);
}

static void aw86006_update(struct aw86006 *aw)
{
	struct aw86006_info info = {};
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, aw_fw_name, &aw->client->dev);
	if (ret) {
		aw->update = AW_UPDATE_NO_FILE;
		aw->update_ret = ret;
		return;
	}
	ret = aw86006_image_check(fw->data, fw->size, &info);
	aw->update_ret = ret;
	aw->update_version = info.app_version;
	if (ret)
		aw->update = AW_UPDATE_BAD_IMAGE;
	else if (aw->mode == AW_MODE_APP)
		aw->update = aw->app_version == info.app_version ?
			AW_UPDATE_CURRENT : AW_UPDATE_NEEDS_ROM;
	else if (aw->mode != AW_MODE_ROM ||
		 aw->header_state == AW_AREA_UNREAD ||
		 aw->header_state == AW_AREA_FAILED)
		aw->update = AW_UPDATE_NEEDS_ROM;
	else if (aw->header_state == AW_AREA_VALID &&
		 !memcmp(aw->header, fw->data + AW_MOVE_LEN, AW_HEADER_LEN))
		aw->update = AW_UPDATE_CURRENT;
	else
		aw86006_flash(aw, fw->data, fw->size);
	release_firmware(fw);
}

static void aw86006_run_end(struct aw86006 *aw, enum aw86006_run why)
{
	int ret;

	ret = aw86006_app_write(aw, AW_REG_OIS, 0);
	if (ret)
		ret = aw86006_app_write(aw, AW_REG_OIS, 0);
	if (aw->run_mode == AW_RUNTIME_CENTRE) {
		aw86006_app_write(aw, AW_REG_MODE_KEY, AW_MODE_UNLOCK);
		aw86006_app_write(aw, AW_REG_MODE, AW_MODE_NONE);
	}
	if (ret && !aw->run_ret)
		aw->run_ret = ret;
	aw->running = false;
	aw->run = why;
}

static int aw86006_run_begin(struct aw86006 *aw, unsigned int mode)
{
	int gyro_type = READ_ONCE(aw_gyro_type);
	int gyro_spi = READ_ONCE(aw_gyro_spi);
	int ret = 0;

	if (gyro_type >= 0)
		ret = aw86006_app_set(aw, AW_REG_GYRO_TYPE, gyro_type,
				      AW_REG_GYRO_TYPE, gyro_type);
	if (!ret && gyro_spi >= 0)
		ret = aw86006_app_set(aw, AW_REG_GYRO_SPI, gyro_spi,
				      AW_REG_GYRO_SPI, gyro_spi);
	if (!ret)
		ret = aw86006_app_set(aw, AW_REG_SYS_KEY, AW_SYS_UNLOCK,
				      AW_REG_SYS_KEY, 1);
	if (!ret)
		ret = aw86006_app_set(aw, AW_REG_SYS_MODE, AW_SYS_NORMAL,
				      AW_REG_SYS_STATE, 0);
	if (ret)
		return ret;
	if (mode == AW_RUNTIME_OIS)
		return aw86006_app_set(aw, AW_REG_OIS, 1, AW_REG_OIS, 1);
	ret = aw86006_app_set(aw, AW_REG_OIS, 0, AW_REG_OIS, 0);
	if (!ret)
		ret = aw86006_app_set(aw, AW_REG_MODE_KEY, AW_MODE_UNLOCK,
				      AW_REG_MODE_KEY, 1);
	if (!ret)
		ret = aw86006_app_set(aw, AW_REG_MODE, AW_MODE_CENTRE,
				      AW_REG_MODE, AW_MODE_CENTRE);
	if (!ret)
		ret = aw86006_app_set(aw, AW_REG_OIS, 1, AW_REG_OIS, 1);
	return ret;
}

static void aw86006_run_start(struct aw86006 *aw, unsigned int mode)
{
	unsigned int run_ms = READ_ONCE(aw_run_ms);
	int ret;

	aw->run_mode = mode;
	if (aw->mode != AW_MODE_APP) {
		aw->run = AW_RUN_NO_PROGRAM;
		return;
	}
	aw->running = true;
	ret = aw86006_run_begin(aw, mode);
	if (ret) {
		aw->run_ret = ret;
		aw86006_run_end(aw, AW_RUN_FAILED);
		return;
	}
	aw->run = AW_RUN_ACTIVE;
	aw->run_end = ktime_add_ms(ktime_get(), run_ms);
	if (!aw86006_app_read(aw, AW_REG_HALL, aw->hall_first, AW_HALL_LEN)) {
		memcpy(aw->hall_last, aw->hall_first, AW_HALL_LEN);
		aw->samples = 1;
	}
	schedule_delayed_work(&aw->sample_work,
			      msecs_to_jiffies(min_t(unsigned int, run_ms,
						     AW_SAMPLE_MS)));
}

static void aw86006_sample(struct work_struct *work)
{
	struct aw86006 *aw = container_of(to_delayed_work(work),
					  struct aw86006, sample_work);
	s64 left;

	mutex_lock(&aw->lock);
	if (!aw->running)
		goto out;
	if (!aw86006_app_read(aw, AW_REG_HALL, aw->hall_last, AW_HALL_LEN) &&
	    !aw->samples++)
		memcpy(aw->hall_first, aw->hall_last, AW_HALL_LEN);
	left = ktime_ms_delta(aw->run_end, ktime_get());
	if (left <= 0)
		aw86006_run_end(aw, AW_RUN_DONE);
	else
		schedule_delayed_work(&aw->sample_work,
				      msecs_to_jiffies(min_t(s64, left,
							     AW_SAMPLE_MS)));
out:
	mutex_unlock(&aw->lock);
}

static void aw86006_halt(struct aw86006 *aw, enum aw86006_run why)
{
	WRITE_ONCE(aw->abort, true);
	mutex_lock(&aw->lock);
	if (aw->running)
		aw86006_run_end(aw, why);
	mutex_unlock(&aw->lock);
	cancel_delayed_work_sync(&aw->sample_work);
}

static void aw86006_reset_results(struct aw86006 *aw)
{
	aw->mode = AW_MODE_UNKNOWN;
	aw->app_id = 0;
	aw->app_version = 0;
	aw->connect_ret = AW_UNTRIED;
	aw->query_ret[0] = AW_UNTRIED;
	aw->query_ret[1] = AW_UNTRIED;
	aw->header_state = AW_AREA_UNREAD;
	aw->record_state = AW_AREA_UNREAD;
	aw->record_after = AW_AREA_UNREAD;
	aw->update = AW_UPDATE_NONE;
	aw->update_ret = 0;
	aw->run = AW_RUN_IDLE;
	aw->run_ret = 0;
	aw->samples = 0;
	aw->answer_us = -1;
	aw->polls = 0;
	aw->sys_ret = AW_UNTRIED;
	aw->sys_polls = 0;
	aw->flash_complete = false;
	aw->flash_bad = 0;
	aw->flash_first_bad = 0;
	aw->flash_ret = 0;
}

static void aw86006_sequence(struct aw86006 *aw, ktime_t now)
{
	if (!aw->fresh)
		return;
	aw->fresh = false;
	aw->off_gap_us = aw->off_at ? ktime_us_delta(now, aw->off_at) : -1;
	aw->on_at = 0;
	aw->npins = 0;
	aw->ntrace = 0;
	aw->trace_more = 0;
	aw->answer_us = -1;
	aw->polls = 0;
	aw->sys_ret = AW_UNTRIED;
	aw->sys_polls = 0;
}

static void aw86006_pin_on(struct aw86006 *aw, const char *pin)
{
	ktime_t now = ktime_get();
	struct aw86006_pin *p;

	if (!pin)
		return;
	mutex_lock(&aw->lock);
	aw86006_sequence(aw, now);
	if (aw->npins < AW_PINS_LEN) {
		p = &aw->pins[aw->npins++];
		strscpy(p->name, pin, sizeof(p->name));
		p->at = now;
	}
	if (strcmp(pin, aw_sync_pin) || aw->pin_seen)
		goto out;
	aw->pin_seen = true;
	aw->pin_on = now;
	aw->powered = true;
	aw->hold = AW_HOLD_NONE;
	if (!READ_ONCE(aw_sync) ||
	    (!READ_ONCE(aw_identify) && !atomic_read(&aw_update)))
		goto out;
	if (aw86006_slow(aw))
		aw->hold = AW_HOLD_FAILED;
	else
		aw86006_hold(aw);
out:
	mutex_unlock(&aw->lock);
}

static void aw86006_power_on(struct aw86006 *aw)
{
	unsigned int what = READ_ONCE(aw_identify);
	unsigned int run = READ_ONCE(aw_runtime);
	ktime_t now;
	bool update;

	mutex_lock(&aw->lock);
	now = ktime_get();
	aw86006_sequence(aw, now);
	aw->on_at = now;
	aw->powered = true;
	WRITE_ONCE(aw->abort, false);
	if (!aw->pin_seen) {
		aw->pin_on = aw->on_at;
		aw->hold = AW_HOLD_NONE;
	}
	update = atomic_xchg(&aw_update, 0);
	if (!what && !update && run == AW_RUNTIME_OFF)
		goto out;
	aw86006_reset_results(aw);
	if (aw86006_slow(aw)) {
		if (update) {
			aw->update = AW_UPDATE_FAILED;
			aw->update_ret = aw->slow_ret;
		}
		goto out;
	}
	if (update)
		what |= AW_IDENTIFY_HEADER | AW_IDENTIFY_RECORD;
	if ((what & AW_IDENTIFY_FLASH) && !aw->flash) {
		aw->flash = devm_kzalloc(&aw->client->dev, AW_FLASH_SIZE,
					 GFP_KERNEL);
		if (!aw->flash)
			aw->flash_ret = -ENOMEM;
	}
	if (aw->hold == AW_HOLD_ACKED) {
		if (what)
			aw86006_rom_identify(aw, what);
	} else if (!aw86006_presence(aw)) {
		aw->mode = AW_MODE_ABSENT;
	} else {
		aw86006_sys_probe(aw);
		if (!aw->sys_ret)
			aw86006_app_probe(aw);
		else if (what)
			aw86006_rom_identify(aw, what);
	}
	if (update)
		aw86006_update(aw);
	if (run != AW_RUNTIME_OFF)
		aw86006_run_start(aw, run);
	dev_info(&aw->client->dev, "power-up: hold %s, answer %lld us after %s (%u polls), 0x61 %d, %s, program %s, update %s, runtime %s\n",
		 aw86006_hold_names[aw->hold], aw->answer_us, aw_sync_pin,
		 aw->polls, aw->sys_ret, aw86006_mode_names[aw->mode],
		 aw86006_area_names[aw->header_state],
		 aw86006_update_names[aw->update], aw86006_run_names[aw->run]);
out:
	mutex_unlock(&aw->lock);
}

static void aw86006_power_off(struct aw86006 *aw)
{
	aw86006_halt(aw, AW_RUN_POWER_OFF);
	mutex_lock(&aw->lock);
	aw->powered = false;
	aw->pin_seen = false;
	aw->fresh = true;
	aw->off_at = ktime_get();
	mutex_unlock(&aw->lock);
}

static int aw86006_power_event(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct aw86006 *aw = container_of(nb, struct aw86006, nb);
	const struct imgsensor_power_event *event = data;

	switch (action) {
	case IMGSENSOR_POWER_PIN_ON:
		aw86006_pin_on(aw, event->pin);
		break;
	case IMGSENSOR_POWER_ON:
		aw86006_power_on(aw);
		break;
	case IMGSENSOR_POWER_OFF:
		aw86006_power_off(aw);
		break;
	}
	return NOTIFY_DONE;
}

static int aw86006_emit_hex(char *buf, int at, const u8 *data, size_t len)
{
	size_t i;
	int n = 0;

	for (i = 0; i < len; i++)
		n += sysfs_emit_at(buf, at + n, i ? " %02x" : "%02x", data[i]);
	return n;
}

static int aw86006_emit_version(char *buf, int at, u32 version)
{
	return sysfs_emit_at(buf, at, "%u.%u.%u.%u", version >> 24,
			     (version >> 16) & 0xff, (version >> 8) & 0xff,
			     version & 0xff);
}

static int aw86006_emit_rom(struct aw86006 *aw, char *buf, int n)
{
	static const char *const queries[] = { "rom version", "rom id" };
	const struct aw86006_info *info = &aw->header_info;
	unsigned int i;
	int at = n;

	if (aw->connect_ret != AW_UNTRIED) {
		n += sysfs_emit_at(buf, n, "connect: %d ", aw->connect_ret);
		n += aw86006_emit_hex(buf, n, aw->connect_raw, AW_ROM_ACK);
		n += sysfs_emit_at(buf, n, "\n");
	}
	for (i = 0; i < ARRAY_SIZE(queries); i++) {
		if (aw->query_ret[i] == AW_UNTRIED)
			continue;
		n += sysfs_emit_at(buf, n, "%s: %d 0x%08x ", queries[i],
				   aw->query_ret[i],
				   get_unaligned_le32(aw->query_raw[i] + 10));
		n += aw86006_emit_hex(buf, n, aw->query_raw[i], AW_ROM_ACK);
		n += sysfs_emit_at(buf, n, "\n");
	}
	n += sysfs_emit_at(buf, n, "program: %s",
			   aw86006_area_names[aw->header_state]);
	if (aw->header_state == AW_AREA_VALID) {
		n += sysfs_emit_at(buf, n, " v");
		n += aw86006_emit_version(buf, n, info->app_version);
		n += sysfs_emit_at(buf, n, " id 0x%02x loader 0x%08x length 0x%x",
				   info->app_id, info->move_version,
				   info->app_length);
	} else if (aw->header_state == AW_AREA_FAILED) {
		n += sysfs_emit_at(buf, n, " (%d)", aw->header_ret);
	}
	n += sysfs_emit_at(buf, n, "\nrecord: %s",
			   aw86006_area_names[aw->record_state]);
	if (aw->record_state == AW_AREA_VALID ||
	    aw->record_state == AW_AREA_INVALID)
		n += sysfs_emit_at(buf, n, " (%u words, sum 0x%08x, stored 0x%08x)",
				   aw->record_words, aw->record_sum,
				   get_unaligned_le32(aw->record + 8));
	else if (aw->record_state == AW_AREA_FAILED)
		n += sysfs_emit_at(buf, n, " (%d)", aw->record_ret);
	n += sysfs_emit_at(buf, n, "\n");
	if (aw->flash_complete)
		n += sysfs_emit_at(buf, n, "flash: %u bytes read, %u chunks failed",
				   AW_FLASH_SIZE, aw->flash_bad);
	else if (aw->flash_ret)
		n += sysfs_emit_at(buf, n, "flash: not read (%d)", aw->flash_ret);
	if (aw->flash_bad)
		n += sysfs_emit_at(buf, n, " (first at 0x%08x, %d)",
				   aw->flash_first_bad, aw->flash_ret);
	if (aw->flash_complete || aw->flash_ret)
		n += sysfs_emit_at(buf, n, "\n");
	if (aw->record_after != AW_AREA_UNREAD)
		n += sysfs_emit_at(buf, n, "record after update: %s\n",
				   aw->record_after == AW_AREA_VALID ? "unchanged" :
				   aw->record_after == AW_AREA_INVALID ? "CHANGED" :
				   "read failed");
	return n - at;
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct aw86006 *aw = dev_get_drvdata(dev);
	int n = 0;
	int ret;

	ret = mutex_lock_interruptible(&aw->lock);
	if (ret)
		return ret;
	n += sysfs_emit_at(buf, n, "power: %s\nhold: %s",
			   aw->powered ? "on" : "off",
			   aw86006_hold_names[aw->hold]);
	if (aw->hold == AW_HOLD_ACKED || aw->hold == AW_HOLD_MISSED)
		n += sysfs_emit_at(buf, n, " (first ack %lld us, %u of %u writes)",
				   aw->hold_first_us, aw->hold_acks,
				   aw->hold_writes);
	if (aw_bus_hz)
		n += sysfs_emit_at(buf, n, "\nslow_target: %d at %u Hz\n",
				   aw->slow_ret, aw_bus_hz);
	else
		n += sysfs_emit_at(buf, n, "\nslow_target: off\n");
	if (aw->answer_us >= 0)
		n += sysfs_emit_at(buf, n,
				   "answer: 0x%02x acked %lld us after %s (%u polls not acked)\n",
				   aw->client->addr, aw->answer_us, aw_sync_pin,
				   aw->polls);
	else if (aw->polls)
		n += sysfs_emit_at(buf, n, "answer: 0x%02x not acked in %u polls\n",
				   aw->client->addr, aw->polls);
	if (aw->sys_ret != AW_UNTRIED)
		n += sysfs_emit_at(buf, n, "0x%02x: %s (%d, %u polls)\n",
				   AW_SYS_ADDR, aw->sys_ret ? "not acked" : "acked",
				   aw->sys_ret, aw->sys_polls);
	if (aw->off_gap_us >= 0)
		n += sysfs_emit_at(buf, n, "off before power-up: %lld ms\n",
				   aw->off_gap_us / USEC_PER_MSEC);
	n += sysfs_emit_at(buf, n, "mode: %s\n", aw86006_mode_names[aw->mode]);
	if (aw->mode == AW_MODE_APP) {
		n += sysfs_emit_at(buf, n, "chip: 0x%02x v", aw->app_id);
		n += aw86006_emit_version(buf, n, aw->app_version);
		n += sysfs_emit_at(buf, n, "\n");
	}
	n += aw86006_emit_rom(aw, buf, n);
	n += sysfs_emit_at(buf, n, "update: %s",
			   aw86006_update_names[aw->update]);
	if (aw->update != AW_UPDATE_NONE)
		n += sysfs_emit_at(buf, n, " (%d, image 0x%08x, at 0x%08x)",
				   aw->update_ret, aw->update_version,
				   aw->update_addr);
	n += sysfs_emit_at(buf, n, "\nruntime: %s", aw86006_run_names[aw->run]);
	if (aw->run != AW_RUN_IDLE && aw->run != AW_RUN_NO_PROGRAM) {
		n += sysfs_emit_at(buf, n, " (mode %u, %d, %u hall samples, first ",
				   aw->run_mode, aw->run_ret, aw->samples);
		n += aw86006_emit_hex(buf, n, aw->hall_first, AW_HALL_LEN);
		n += sysfs_emit_at(buf, n, ", last ");
		n += aw86006_emit_hex(buf, n, aw->hall_last, AW_HALL_LEN);
		n += sysfs_emit_at(buf, n, ")");
	}
	n += sysfs_emit_at(buf, n, "\n");
	mutex_unlock(&aw->lock);
	return n;
}
static DEVICE_ATTR_RO(state);

static ssize_t stop_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct aw86006 *aw = dev_get_drvdata(dev);
	bool stop;
	int ret;

	ret = kstrtobool(buf, &stop);
	if (ret)
		return ret;
	if (stop)
		aw86006_halt(aw, AW_RUN_STOPPED);
	return count;
}
static DEVICE_ATTR_WO(stop);

static ssize_t trace_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct aw86006 *aw = dev_get_drvdata(dev);
	const struct aw86006_xfer *x;
	unsigned int i;
	int n = 0;
	int ret;

	ret = mutex_lock_interruptible(&aw->lock);
	if (ret)
		return ret;
	n += sysfs_emit_at(buf, n, "us after %s:", aw_sync_pin);
	for (i = 0; i < aw->npins; i++)
		n += sysfs_emit_at(buf, n, " %s %lld", aw->pins[i].name,
				   ktime_us_delta(aw->pins[i].at, aw->pin_on));
	if (aw->on_at)
		n += sysfs_emit_at(buf, n, " on %lld",
				   ktime_us_delta(aw->on_at, aw->pin_on));
	n += sysfs_emit_at(buf, n, "\n");
	for (i = 0; i < aw->ntrace; i++) {
		x = &aw->trace[i];
		n += sysfs_emit_at(buf, n, "%lld 0x%02x w%u r%u %d\n",
				   ktime_us_delta(x->at, aw->pin_on), x->addr,
				   x->wlen, x->rlen, x->ret);
	}
	if (aw->trace_more)
		n += sysfs_emit_at(buf, n, "+%u more\n", aw->trace_more);
	mutex_unlock(&aw->lock);
	return n;
}
static DEVICE_ATTR_RO(trace);

static ssize_t aw86006_dump(struct aw86006 *aw, char *buf, loff_t off,
			    size_t count, const u8 *data, size_t len)
{
	ssize_t ret;

	ret = mutex_lock_interruptible(&aw->lock);
	if (ret)
		return ret;
	ret = memory_read_from_buffer(buf, count, &off, data, len);
	mutex_unlock(&aw->lock);
	return ret;
}

static ssize_t header_read(struct file *file, struct kobject *kobj,
			   const struct bin_attribute *attr, char *buf,
			   loff_t off, size_t count)
{
	struct aw86006 *aw = dev_get_drvdata(kobj_to_dev(kobj));

	return aw86006_dump(aw, buf, off, count, aw->header, AW_HEADER_LEN);
}

static const BIN_ATTR_RO(header, AW_HEADER_LEN);

static ssize_t record_read(struct file *file, struct kobject *kobj,
			   const struct bin_attribute *attr, char *buf,
			   loff_t off, size_t count)
{
	struct aw86006 *aw = dev_get_drvdata(kobj_to_dev(kobj));

	return aw86006_dump(aw, buf, off, count, aw->record, AW_RECORD_LEN);
}

static const BIN_ATTR_RO(record, AW_RECORD_LEN);

static ssize_t flash_read(struct file *file, struct kobject *kobj,
			  const struct bin_attribute *attr, char *buf,
			  loff_t off, size_t count)
{
	struct aw86006 *aw = dev_get_drvdata(kobj_to_dev(kobj));

	if (!READ_ONCE(aw->flash))
		return -ENODATA;
	return aw86006_dump(aw, buf, off, count, aw->flash, AW_FLASH_SIZE);
}

static const BIN_ATTR_RO(flash, AW_FLASH_SIZE);

static struct attribute *aw86006_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_stop.attr,
	&dev_attr_trace.attr,
	NULL,
};

static const struct bin_attribute *const aw86006_bin_attrs[] = {
	&bin_attr_header,
	&bin_attr_record,
	&bin_attr_flash,
	NULL,
};

static const struct attribute_group aw86006_group = {
	.attrs = aw86006_attrs,
	.bin_attrs = aw86006_bin_attrs,
};
__ATTRIBUTE_GROUPS(aw86006);

static int aw86006_probe(struct i2c_client *client)
{
	struct aw86006 *aw;
	int ret;

	aw = devm_kzalloc(&client->dev, sizeof(*aw), GFP_KERNEL);
	if (!aw)
		return -ENOMEM;
	aw->client = client;
	mutex_init(&aw->lock);
	INIT_DELAYED_WORK(&aw->sample_work, aw86006_sample);
	aw->connect_ret = AW_UNTRIED;
	aw->query_ret[0] = AW_UNTRIED;
	aw->query_ret[1] = AW_UNTRIED;
	aw->sys_ret = AW_UNTRIED;
	aw->answer_us = -1;
	aw->off_gap_us = -1;
	aw->fresh = true;
	aw->nb.notifier_call = aw86006_power_event;
	i2c_set_clientdata(client, aw);
	ret = imgsensor_power_register_notifier(&aw->nb);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "imgsensor power notifier\n");
	return 0;
}

static void aw86006_remove(struct i2c_client *client)
{
	struct aw86006 *aw = i2c_get_clientdata(client);

	imgsensor_power_unregister_notifier(&aw->nb);
	aw86006_halt(aw, AW_RUN_STOPPED);
	if (aw->slow_set)
		mtk_i2c_set_slow_target(client->adapter->nr, client->addr, 0);
}

static const struct of_device_id aw86006_of_match[] = {
	{ .compatible = "mindone,aw86006-ois" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw86006_of_match);

static const struct i2c_device_id aw86006_id[] = {
	{ "aw86006-ois" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw86006_id);

static struct i2c_driver aw86006_driver = {
	.driver = {
		.name = "aw86006-ois",
		.of_match_table = aw86006_of_match,
		.dev_groups = aw86006_groups,
	},
	.probe = aw86006_probe,
	.remove = aw86006_remove,
	.id_table = aw86006_id,
};
module_i2c_driver(aw86006_driver);

MODULE_DESCRIPTION("Awinic AW86006 OIS controller of the MindOne camera module");
MODULE_LICENSE("GPL");
