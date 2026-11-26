// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>
#include <dt-bindings/thermal/mediatek,lvts-thermal.h>

#define LVTS_MONCTL0(__base)	(__base + 0x0000)
#define LVTS_MONCTL1(__base)	(__base + 0x0004)
#define LVTS_MONCTL2(__base)	(__base + 0x0008)
#define LVTS_MONINT(__base)		(__base + 0x000C)
#define LVTS_MONINTSTS(__base)	(__base + 0x0010)
#define LVTS_MONIDET0(__base)	(__base + 0x0014)
#define LVTS_MONIDET1(__base)	(__base + 0x0018)
#define LVTS_MONIDET2(__base)	(__base + 0x001C)
#define LVTS_MONIDET3(__base)	(__base + 0x0020)
#define LVTS_H2NTHRE(__base)	(__base + 0x0024)
#define LVTS_HTHRE(__base)		(__base + 0x0028)
#define LVTS_OFFSETH(__base)	(__base + 0x0030)
#define LVTS_OFFSETL(__base)	(__base + 0x0034)
#define LVTS_MSRCTL0(__base)	(__base + 0x0038)
#define LVTS_MSRCTL1(__base)	(__base + 0x003C)
#define LVTS_TSSEL(__base)		(__base + 0x0040)
#define LVTS_CALSCALE(__base)	(__base + 0x0048)
#define LVTS_ID(__base)			(__base + 0x004C)
#define LVTS_CONFIG(__base)		(__base + 0x0050)
#define LVTS_EDATA00(__base)	(__base + 0x0054)
#define LVTS_EDATA01(__base)	(__base + 0x0058)
#define LVTS_EDATA02(__base)	(__base + 0x005C)
#define LVTS_EDATA03(__base)	(__base + 0x0060)
#define LVTS_MSR0(__base)		(__base + 0x0090)
#define LVTS_MSR1(__base)		(__base + 0x0094)
#define LVTS_MSR2(__base)		(__base + 0x0098)
#define LVTS_MSR3(__base)		(__base + 0x009C)
#define LVTS_IMMD0(__base)		(__base + 0x00A0)
#define LVTS_IMMD1(__base)		(__base + 0x00A4)
#define LVTS_IMMD2(__base)		(__base + 0x00A8)
#define LVTS_IMMD3(__base)		(__base + 0x00AC)
#define LVTS_RDATA0(__base)		(__base + 0x00B0)
#define LVTS_PROTCTL(__base)	(__base + 0x00C0)
#define LVTS_PROTTA(__base)		(__base + 0x00C4)
#define LVTS_PROTTB(__base)		(__base + 0x00C8)
#define LVTS_PROTTC(__base)		(__base + 0x00CC)
#define LVTS_CLKEN(__base)		(__base + 0x00E4)

#define LVTS_PERIOD_UNIT			0
#define LVTS_GROUP_INTERVAL			0
#define LVTS_FILTER_INTERVAL		0
#define LVTS_SENSOR_INTERVAL		0
#define LVTS_HW_FILTER				0x0
#define LVTS_TSSEL_CONF				0x13121110
#define LVTS_CALSCALE_CONF			0x300
#define LVTS_MT6789_MONCTL1			0x0010000C
#define LVTS_MT6789_MONCTL2			0x00010001
#define LVTS_MT6789_HW_REBOOT_TEMP	113500
#define LVTS_HW_REBOOT_DISABLE		(-274000)

#define LVTS_INT_STAGE3				BIT(31)
#define LVTS_PROTOFFSET_MASK		GENMASK(15, 0)
#define LVTS_PROTCTL_SEL_MASK		GENMASK(19, 16)
#define LVTS_DEVICE_ACCESS_STATUS	BIT(24)
#define LVTS_DEVICE_SENSING_STATUS	BIT(25)
#define LVTS_MSRCTL1_IDLE_MASK		(BIT(10) | BIT(7) | BIT(0))
#define LVTS_DEV_RESET_ALL			0xC103FFFF
#define LVTS_DEV_MANUAL_RCK_V5		0xC1030E00
#define LVTS_DEV_SELECT_RCK(id)		(0xC1030D00U | (u32)(id))
#define LVTS_DEV_NO_RCK				0xC1030D10
#define LVTS_DEV_SINGLE_MODE_V5		0xC10306F8
#define LVTS_DEV_LOW_POWER_SINGLE	0xC10306B8
#define LVTS_DEV_COUNTING_WIN1		0xC1030500
#define LVTS_DEV_COUNTING_WIN2		0xC1030420
#define LVTS_DEV_KICK_RCK			0xC1030302
#define LVTS_DEV_READ_REG0			0xC1020000

#define LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR0		BIT(3)
#define LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR1		BIT(8)
#define LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR2		BIT(13)
#define LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR3		BIT(25)
#define LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR0		BIT(2)
#define LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR1		BIT(7)
#define LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR2		BIT(12)
#define LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR3		BIT(24)

#define LVTS_INT_SENSOR0			0x0009001F
#define LVTS_INT_SENSOR1			0x001203E0
#define LVTS_INT_SENSOR2			0x00247C00
#define LVTS_INT_SENSOR3			0x1FC00000

#define LVTS_SENSOR_MAX				4
#define LVTS_GOLDEN_TEMP_MAX		62
#define LVTS_GOLDEN_TEMP_DEFAULT	50
#define LVTS_COEFF_A_MT8195			-250460
#define LVTS_COEFF_B_MT8195			250460
#define LVTS_COEFF_A_MT7988			-204650
#define LVTS_COEFF_B_MT7988			204650

#define LVTS_MSR_IMMEDIATE_MODE		0
#define LVTS_MSR_FILTERED_MODE		1

#define LVTS_MSR_READ_TIMEOUT_US	1500
#define LVTS_MSR_READ_WAIT_US		50

#define LVTS_MINIMUM_THRESHOLD		20000

static int golden_temp = LVTS_GOLDEN_TEMP_DEFAULT;
static int golden_temp_offset;

struct lvts_sensor_data {
	int dt_id;
	u8 cal_offsets[3];
};

struct lvts_ctrl_data {
	struct lvts_sensor_data lvts_sensor[LVTS_SENSOR_MAX];
	u8 valid_sensor_mask;
	int offset;
	int mode;
	u8 dominator_sensor;
	u8 rck_word_index;
};

#define VALID_SENSOR_MAP(s0, s1, s2, s3) \
	.valid_sensor_mask = (((s0) ? BIT(0) : 0) | \
			      ((s1) ? BIT(1) : 0) | \
			      ((s2) ? BIT(2) : 0) | \
			      ((s3) ? BIT(3) : 0))

#define lvts_for_each_valid_sensor(i, lvts_ctrl) \
	for ((i) = 0; (i) < LVTS_SENSOR_MAX; (i)++) \
		if (!((lvts_ctrl)->valid_sensor_mask & BIT(i))) \
			continue; \
		else

struct lvts_data {
	const struct lvts_ctrl_data *lvts_ctrl;
	const u32 *conn_cmd;
	const u32 *init_cmd;
	int num_lvts_ctrl;
	int num_conn_cmd;
	int num_init_cmd;
	int temp_factor;
	int temp_offset;
	int gt_calib_bit_offset;
	unsigned int def_calibration;
	unsigned int def_count_rc;
	u32 monctl1;
	u32 monctl2;
	u8 default_golden_temp;
	int hw_reboot_trip_point;
	bool manual_rck_v5;
	bool has_soc_max;
};

struct lvts_domain;

struct lvts_sensor {
	struct thermal_zone_device *tz;
	void __iomem *msr;
	void __iomem *base;
	int id;
	int dt_id;
	int low_thresh;
	int high_thresh;
};

struct lvts_ctrl {
	struct lvts_sensor sensors[LVTS_SENSOR_MAX];
	const struct lvts_data *lvts_data;
	struct lvts_domain *domain;
	u32 calibration[LVTS_SENSOR_MAX];
	u32 count_r[LVTS_SENSOR_MAX];
	u32 count_rc;
	u8 valid_sensor_mask;
	u8 dominator_sensor;
	int mode;
	int hw_reboot_trip_point;
	bool rck_done;
	spinlock_t lock;
	void __iomem *base;
	int low_thresh;
	int high_thresh;
};

struct lvts_domain {
	struct lvts_ctrl *lvts_ctrl;
	const struct lvts_data *lvts_data;
	struct thermal_zone_device *soc_max_tz;
	struct reset_control *reset;
	struct clk *clk;
	int num_lvts_ctrl;
	bool use_fake_efuse;
	void __iomem *base;
	size_t calib_len;
	u8 *calib;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dom_dentry;
#endif
};

#ifdef CONFIG_MTK_LVTS_THERMAL_DEBUGFS

#define LVTS_DEBUG_FS_REGS(__reg)		\
{						\
	.name = __stringify(__reg),		\
	.offset = __reg(0),			\
}

static const struct debugfs_reg32 lvts_regs[] = {
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL0),
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL1),
	LVTS_DEBUG_FS_REGS(LVTS_MONCTL2),
	LVTS_DEBUG_FS_REGS(LVTS_MONINT),
	LVTS_DEBUG_FS_REGS(LVTS_MONINTSTS),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET0),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET1),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET2),
	LVTS_DEBUG_FS_REGS(LVTS_MONIDET3),
	LVTS_DEBUG_FS_REGS(LVTS_H2NTHRE),
	LVTS_DEBUG_FS_REGS(LVTS_HTHRE),
	LVTS_DEBUG_FS_REGS(LVTS_OFFSETH),
	LVTS_DEBUG_FS_REGS(LVTS_OFFSETL),
	LVTS_DEBUG_FS_REGS(LVTS_MSRCTL0),
	LVTS_DEBUG_FS_REGS(LVTS_MSRCTL1),
	LVTS_DEBUG_FS_REGS(LVTS_TSSEL),
	LVTS_DEBUG_FS_REGS(LVTS_CALSCALE),
	LVTS_DEBUG_FS_REGS(LVTS_ID),
	LVTS_DEBUG_FS_REGS(LVTS_CONFIG),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA00),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA01),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA02),
	LVTS_DEBUG_FS_REGS(LVTS_EDATA03),
	LVTS_DEBUG_FS_REGS(LVTS_MSR0),
	LVTS_DEBUG_FS_REGS(LVTS_MSR1),
	LVTS_DEBUG_FS_REGS(LVTS_MSR2),
	LVTS_DEBUG_FS_REGS(LVTS_MSR3),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD0),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD1),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD2),
	LVTS_DEBUG_FS_REGS(LVTS_IMMD3),
	LVTS_DEBUG_FS_REGS(LVTS_PROTCTL),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTA),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTB),
	LVTS_DEBUG_FS_REGS(LVTS_PROTTC),
	LVTS_DEBUG_FS_REGS(LVTS_CLKEN),
};

static void lvts_debugfs_exit(void *data)
{
	struct lvts_domain *lvts_td = data;

	debugfs_remove_recursive(lvts_td->dom_dentry);
}

static int lvts_debugfs_init(struct device *dev, struct lvts_domain *lvts_td)
{
	struct debugfs_regset32 *regset;
	struct lvts_ctrl *lvts_ctrl;
	struct dentry *dentry;
	char name[64];
	int i;

	lvts_td->dom_dentry = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(lvts_td->dom_dentry))
		return 0;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		lvts_ctrl = &lvts_td->lvts_ctrl[i];

		sprintf(name, "controller%d", i);
		dentry = debugfs_create_dir(name, lvts_td->dom_dentry);
		if (IS_ERR(dentry))
			continue;

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			continue;

		regset->base = lvts_ctrl->base;
		regset->regs = lvts_regs;
		regset->nregs = ARRAY_SIZE(lvts_regs);

		debugfs_create_regset32("registers", 0400, dentry, regset);
	}

	return devm_add_action_or_reset(dev, lvts_debugfs_exit, lvts_td);
}

#else

static inline int lvts_debugfs_init(struct device *dev,
				    struct lvts_domain *lvts_td)
{
	return 0;
}

#endif

static int lvts_raw_to_temp(u32 raw_temp, int temp_factor)
{
	int temperature;

	temperature = ((s64)(raw_temp & 0xFFFF) * temp_factor) >> 14;
	temperature += golden_temp_offset;

	return temperature;
}

static u32 lvts_temp_to_raw(int temperature, int temp_factor)
{
	s64 raw_temp = ((s64)(golden_temp_offset - temperature)) << 14;

	raw_temp = div_s64(raw_temp, -temp_factor);

	return raw_temp < 0 ? 0 : (u32)raw_temp;
}

static int lvts_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct lvts_sensor *lvts_sensor = thermal_zone_device_priv(tz);
	struct lvts_ctrl *lvts_ctrl = container_of(lvts_sensor, struct lvts_ctrl,
						   sensors[lvts_sensor->id]);
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	void __iomem *msr = lvts_sensor->msr;
	u32 value;
	int rc;

	rc = readl_poll_timeout(msr, value, value & BIT(16),
				LVTS_MSR_READ_WAIT_US, LVTS_MSR_READ_TIMEOUT_US);

	if (rc)
		return -EAGAIN;

	*temp = lvts_raw_to_temp(value & 0xFFFF, lvts_data->temp_factor);

	return 0;
}

static void lvts_update_irq_mask(struct lvts_ctrl *lvts_ctrl)
{
	static const u32 high_offset_inten_masks[] = {
		LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR0,
		LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR1,
		LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR2,
		LVTS_MONINT_OFFSET_HIGH_INTEN_SENSOR3,
	};
	static const u32 low_offset_inten_masks[] = {
		LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR0,
		LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR1,
		LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR2,
		LVTS_MONINT_OFFSET_LOW_INTEN_SENSOR3,
	};
	u32 value = 0;
	int i;

	value = readl(LVTS_MONINT(lvts_ctrl->base));

	lvts_for_each_valid_sensor(i, lvts_ctrl) {
		if (lvts_ctrl->sensors[i].high_thresh == lvts_ctrl->high_thresh
		    && lvts_ctrl->sensors[i].low_thresh == lvts_ctrl->low_thresh) {

			if (lvts_ctrl->low_thresh == -INT_MAX)
				value &= ~low_offset_inten_masks[i];
			else
				value |= low_offset_inten_masks[i];
			if (lvts_ctrl->high_thresh == INT_MAX)
				value &= ~high_offset_inten_masks[i];
			else
				value |= high_offset_inten_masks[i];
		} else {
			value &= ~(low_offset_inten_masks[i] | high_offset_inten_masks[i]);
		}
	}

	writel(value, LVTS_MONINT(lvts_ctrl->base));
}

static bool lvts_should_update_thresh(struct lvts_ctrl *lvts_ctrl, int high)
{
	int i;

	if (high > lvts_ctrl->high_thresh)
		return true;

	lvts_for_each_valid_sensor(i, lvts_ctrl)
		if (lvts_ctrl->sensors[i].high_thresh == lvts_ctrl->high_thresh
		    && lvts_ctrl->sensors[i].low_thresh == lvts_ctrl->low_thresh)
			return false;

	return true;
}

static int lvts_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct lvts_sensor *lvts_sensor = thermal_zone_device_priv(tz);
	struct lvts_ctrl *lvts_ctrl = container_of(lvts_sensor, struct lvts_ctrl,
						   sensors[lvts_sensor->id]);
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	void __iomem *base = lvts_sensor->base;
	u32 raw_low = lvts_temp_to_raw(low != -INT_MAX ? low : LVTS_MINIMUM_THRESHOLD,
				       lvts_data->temp_factor);
	u32 raw_high = high != INT_MAX ?
		       lvts_temp_to_raw(high, lvts_data->temp_factor) : 0;
	unsigned long flags;
	bool should_update_thresh;

	raw_low = min_t(u32, raw_low, 0x7FFF);
	raw_high = min_t(u32, raw_high, 0x7FFF);

	spin_lock_irqsave(&lvts_ctrl->lock, flags);
	lvts_sensor->low_thresh = low;
	lvts_sensor->high_thresh = high;

	should_update_thresh = lvts_should_update_thresh(lvts_ctrl, high);
	if (should_update_thresh) {
		lvts_ctrl->high_thresh = high;
		lvts_ctrl->low_thresh = low;
	}
	lvts_update_irq_mask(lvts_ctrl);

	if (!should_update_thresh) {
		spin_unlock_irqrestore(&lvts_ctrl->lock, flags);
		return 0;
	}

	pr_debug("%s: Setting low limit temperature interrupt: %d\n",
		 thermal_zone_device_type(tz), low);
	writel(raw_low, LVTS_OFFSETL(base));

	pr_debug("%s: Setting high limit temperature interrupt: %d\n",
		 thermal_zone_device_type(tz), high);
	writel(raw_high, LVTS_OFFSETH(base));
	spin_unlock_irqrestore(&lvts_ctrl->lock, flags);

	return 0;
}

static irqreturn_t lvts_ctrl_irq_handler(struct lvts_ctrl *lvts_ctrl)
{
	irqreturn_t iret = IRQ_NONE;
	u32 value;
	static const u32 masks[] = {
		LVTS_INT_SENSOR0,
		LVTS_INT_SENSOR1,
		LVTS_INT_SENSOR2,
		LVTS_INT_SENSOR3
	};
	int i;

	value = readl(LVTS_MONINTSTS(lvts_ctrl->base));

	for (i = 0; i < ARRAY_SIZE(masks); i++) {

		if (!(value & masks[i]))
			continue;

		if (lvts_ctrl->sensors[i].tz)
			thermal_zone_device_update(lvts_ctrl->sensors[i].tz,
						   THERMAL_TRIP_VIOLATED);
		iret = IRQ_HANDLED;
	}

	if (value & LVTS_INT_STAGE3) {
		if (lvts_ctrl->domain && lvts_ctrl->domain->soc_max_tz)
			thermal_zone_device_update(lvts_ctrl->domain->soc_max_tz,
						   THERMAL_TRIP_VIOLATED);
		iret = IRQ_HANDLED;
	}

	writel(value, LVTS_MONINTSTS(lvts_ctrl->base));

	return iret;
}

static irqreturn_t lvts_irq_handler(int irq, void *data)
{
	struct lvts_domain *lvts_td = data;
	irqreturn_t aux, iret = IRQ_NONE;
	int i;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		aux = lvts_ctrl_irq_handler(&lvts_td->lvts_ctrl[i]);
		if (aux != IRQ_HANDLED)
			continue;

		iret = IRQ_HANDLED;
	}

	return iret;
}

static const struct thermal_zone_device_ops lvts_ops = {
	.get_temp = lvts_get_temp,
	.set_trips = lvts_set_trips,
};

static int lvts_sensor_init(struct device *dev, struct lvts_ctrl *lvts_ctrl,
					const struct lvts_ctrl_data *lvts_ctrl_data)
{
	struct lvts_sensor *lvts_sensor = lvts_ctrl->sensors;

	void __iomem *msr_regs[] = {
		LVTS_MSR0(lvts_ctrl->base),
		LVTS_MSR1(lvts_ctrl->base),
		LVTS_MSR2(lvts_ctrl->base),
		LVTS_MSR3(lvts_ctrl->base)
	};

	void __iomem *imm_regs[] = {
		LVTS_IMMD0(lvts_ctrl->base),
		LVTS_IMMD1(lvts_ctrl->base),
		LVTS_IMMD2(lvts_ctrl->base),
		LVTS_IMMD3(lvts_ctrl->base)
	};

	int i;

	lvts_for_each_valid_sensor(i, lvts_ctrl_data) {

		int dt_id = lvts_ctrl_data->lvts_sensor[i].dt_id;

		lvts_sensor[i].id = i;

		lvts_sensor[i].dt_id = dt_id;

		lvts_sensor[i].base = lvts_ctrl->base;

		lvts_sensor[i].msr = lvts_ctrl_data->mode == LVTS_MSR_IMMEDIATE_MODE ?
			imm_regs[i] : msr_regs[i];

		lvts_sensor[i].low_thresh = INT_MIN;
		lvts_sensor[i].high_thresh = INT_MIN;
	}

	lvts_ctrl->valid_sensor_mask = lvts_ctrl_data->valid_sensor_mask;

	return 0;
}

static int lvts_calibration_init(struct device *dev, struct lvts_ctrl *lvts_ctrl,
					const struct lvts_ctrl_data *lvts_ctrl_data,
					u8 *efuse_calibration,
					size_t calib_len)
{
	int i;
	u32 gt;

	if (lvts_ctrl->lvts_data->manual_rck_v5) {
		size_t rc_off = (size_t)lvts_ctrl_data->rck_word_index * 4;

		if (rc_off + 3 > calib_len)
			return -EINVAL;

		lvts_for_each_valid_sensor(i, lvts_ctrl_data) {
			const struct lvts_sensor_data *sensor =
				&lvts_ctrl_data->lvts_sensor[i];

			if (sensor->cal_offsets[0] >= calib_len ||
			    sensor->cal_offsets[1] >= calib_len)
				return -EINVAL;

			if (lvts_ctrl->domain->use_fake_efuse) {
				lvts_ctrl->count_r[i] = lvts_ctrl->lvts_data->def_calibration;
			} else {
				lvts_ctrl->count_r[i] =
					((u32)efuse_calibration[sensor->cal_offsets[0]] << 0) |
					((u32)efuse_calibration[sensor->cal_offsets[1]] << 8);
			}
		}

		if (lvts_ctrl->domain->use_fake_efuse) {
			lvts_ctrl->count_rc = lvts_ctrl->lvts_data->def_count_rc;
		} else {
			lvts_ctrl->count_rc =
				((u32)efuse_calibration[rc_off + 0] << 0) |
				((u32)efuse_calibration[rc_off + 1] << 8) |
				((u32)efuse_calibration[rc_off + 2] << 16);
		}
		return 0;
	}

	gt = (((u32 *)efuse_calibration)[0] >> lvts_ctrl->lvts_data->gt_calib_bit_offset) & 0xff;

	lvts_for_each_valid_sensor(i, lvts_ctrl_data) {
		const struct lvts_sensor_data *sensor =
					&lvts_ctrl_data->lvts_sensor[i];

		if (sensor->cal_offsets[0] >= calib_len ||
		    sensor->cal_offsets[1] >= calib_len ||
		    sensor->cal_offsets[2] >= calib_len)
			return -EINVAL;

		if (gt) {
			lvts_ctrl->calibration[i] =
				(efuse_calibration[sensor->cal_offsets[0]] << 0) +
				(efuse_calibration[sensor->cal_offsets[1]] << 8) +
				(efuse_calibration[sensor->cal_offsets[2]] << 16);
		} else if (lvts_ctrl->lvts_data->def_calibration) {
			lvts_ctrl->calibration[i] = lvts_ctrl->lvts_data->def_calibration;
		} else {
			dev_err(dev, "efuse contains invalid calibration data and no default given.\n");
			return -ENODATA;
		}
	}

	return 0;
}

static int lvts_calibration_read(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	struct device_node *np = dev_of_node(dev);
	struct nvmem_cell *cell;
	struct property *prop;
	const char *cell_name;

	of_property_for_each_string(np, "nvmem-cell-names", prop, cell_name) {
		size_t len;
		u8 *efuse;

		cell = of_nvmem_cell_get(np, cell_name);
		if (IS_ERR(cell)) {
			dev_err(dev, "Failed to get cell '%s'\n", cell_name);
			return PTR_ERR(cell);
		}

		efuse = nvmem_cell_read(cell, &len);

		nvmem_cell_put(cell);

		if (IS_ERR(efuse)) {
			dev_err(dev, "Failed to read cell '%s'\n", cell_name);
			return PTR_ERR(efuse);
		}

		lvts_td->calib = devm_krealloc(dev, lvts_td->calib,
					       lvts_td->calib_len + len, GFP_KERNEL);
		if (!lvts_td->calib) {
			kfree(efuse);
			return -ENOMEM;
		}

		memcpy(lvts_td->calib + lvts_td->calib_len, efuse, len);

		lvts_td->calib_len += len;

		kfree(efuse);
	}

	return 0;
}

static int lvts_golden_temp_init(struct device *dev, struct lvts_domain *lvts_td,
				 u8 *calib, const struct lvts_data *lvts_data)
{
	u32 gt;

	if (lvts_td->calib_len < sizeof(u32))
		return -EINVAL;

	gt = (((u32 *)calib)[0] >> lvts_data->gt_calib_bit_offset) & 0xff;

	if (lvts_data->manual_rck_v5) {
		bool all_zero = (gt == 0);
		int c, s;

		for (c = 0; all_zero && c < lvts_data->num_lvts_ctrl; c++) {
			const struct lvts_ctrl_data *cd = &lvts_data->lvts_ctrl[c];
			size_t rc_off = (size_t)cd->rck_word_index * 4;

			if (rc_off + 3 > lvts_td->calib_len)
				return -EINVAL;
			if (calib[rc_off] || calib[rc_off + 1] || calib[rc_off + 2])
				all_zero = false;
			lvts_for_each_valid_sensor(s, cd) {
				const struct lvts_sensor_data *sd = &cd->lvts_sensor[s];

				if (sd->cal_offsets[0] >= lvts_td->calib_len ||
				    sd->cal_offsets[1] >= lvts_td->calib_len)
					return -EINVAL;
				if (calib[sd->cal_offsets[0]] || calib[sd->cal_offsets[1]]) {
					all_zero = false;
					break;
				}
			}
		}

		lvts_td->use_fake_efuse = all_zero;
		golden_temp = all_zero ?
			(lvts_data->default_golden_temp ? lvts_data->default_golden_temp : LVTS_GOLDEN_TEMP_DEFAULT) :
			(int)gt;
	} else {
		if (gt && gt < LVTS_GOLDEN_TEMP_MAX)
			golden_temp = gt;
	}

	golden_temp_offset = golden_temp * 500 + lvts_data->temp_offset;

	dev_info(dev, "%sgolden temp=%d\n",
		 (lvts_data->manual_rck_v5 ? !lvts_td->use_fake_efuse : gt != 0) ? "" : "fake ",
		 golden_temp);

	return 0;
}

static int lvts_ctrl_init(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	size_t size = sizeof(*lvts_td->lvts_ctrl) * lvts_data->num_lvts_ctrl;
	struct lvts_ctrl *lvts_ctrl;
	int i, ret;

	ret = lvts_calibration_read(dev, lvts_td, lvts_data);
	if (ret)
		return ret;

	ret = lvts_golden_temp_init(dev, lvts_td, lvts_td->calib, lvts_data);
	if (ret)
		return ret;

	lvts_ctrl = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!lvts_ctrl)
		return -ENOMEM;

	for (i = 0; i < lvts_data->num_lvts_ctrl; i++) {

		lvts_ctrl[i].base = lvts_td->base + lvts_data->lvts_ctrl[i].offset;
		lvts_ctrl[i].lvts_data = lvts_data;
		lvts_ctrl[i].domain = lvts_td;
		lvts_ctrl[i].dominator_sensor = lvts_data->lvts_ctrl[i].dominator_sensor;
		lvts_ctrl[i].hw_reboot_trip_point = lvts_data->hw_reboot_trip_point;
		spin_lock_init(&lvts_ctrl[i].lock);

		ret = lvts_sensor_init(dev, &lvts_ctrl[i],
				       &lvts_data->lvts_ctrl[i]);
		if (ret)
			return ret;

		ret = lvts_calibration_init(dev, &lvts_ctrl[i],
					    &lvts_data->lvts_ctrl[i],
					    lvts_td->calib,
					    lvts_td->calib_len);
		if (ret)
			return ret;

		lvts_ctrl[i].mode = lvts_data->lvts_ctrl[i].mode;

		lvts_ctrl[i].low_thresh = INT_MIN;
		lvts_ctrl[i].high_thresh = INT_MIN;
	}

	devm_kfree(dev, lvts_td->calib);

	lvts_td->lvts_ctrl = lvts_ctrl;
	lvts_td->num_lvts_ctrl = lvts_data->num_lvts_ctrl;

	return 0;
}

static void lvts_ctrl_monitor_enable(struct device *dev, struct lvts_ctrl *lvts_ctrl, bool enable)
{

	static const u8 sensor_filt_bitmap[] = { BIT(0), BIT(1), BIT(2), BIT(3) };
	u32 sensor_map = 0;
	int i;

	if (lvts_ctrl->mode != LVTS_MSR_FILTERED_MODE)
		return;

	if (enable) {
		lvts_for_each_valid_sensor(i, lvts_ctrl)
			sensor_map |= sensor_filt_bitmap[i];
	}

	writel(sensor_map | BIT(9), LVTS_MONCTL0(lvts_ctrl->base));
}

static void lvts_write_device_cmd(struct lvts_ctrl *lvts_ctrl, u32 cmd)
{
	u32 val;

	writel(cmd, LVTS_CONFIG(lvts_ctrl->base));
	udelay(5);
	readl_poll_timeout_atomic(LVTS_CONFIG(lvts_ctrl->base), val,
				  !(val & LVTS_DEVICE_ACCESS_STATUS), 2, 200);
}

static u32 lvts_read_device_reg0(struct lvts_ctrl *lvts_ctrl)
{
	u32 val;

	writel(LVTS_DEV_READ_REG0, LVTS_CONFIG(lvts_ctrl->base));
	readl_poll_timeout_atomic(LVTS_CONFIG(lvts_ctrl->base), val,
				  !(val & LVTS_DEVICE_ACCESS_STATUS), 2, 200);
	return readl(LVTS_RDATA0(lvts_ctrl->base));
}

static void lvts_ctrl_wait_idle(struct lvts_ctrl *lvts_ctrl)
{
	u32 val;
	int tries;

	for (tries = 0; tries < 2; tries++) {
		if (!readl_poll_timeout_atomic(LVTS_MSRCTL1(lvts_ctrl->base), val,
					       !(val & LVTS_MSRCTL1_IDLE_MASK),
					       2, 200))
			break;
	}
}

static void lvts_write_config(struct lvts_ctrl *lvts_ctrl, const u32 *cmds, int nr_cmds)
{
	int i;

	for (i = 0; i < nr_cmds; i++) {
		if (lvts_ctrl->lvts_data->manual_rck_v5) {
			lvts_write_device_cmd(lvts_ctrl, cmds[i]);
		} else {
			writel(cmds[i], LVTS_CONFIG(lvts_ctrl->base));
			usleep_range(2, 4);
		}
	}
}

static void lvts_ctrl_rck_v5(struct lvts_ctrl *lvts_ctrl)
{
	int i;

	if (lvts_ctrl->rck_done) {
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_MANUAL_RCK_V5);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_NO_RCK);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_SINGLE_MODE_V5);
		return;
	}

	lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_MANUAL_RCK_V5);
	lvts_for_each_valid_sensor(i, lvts_ctrl) {
		u32 val, rc_data, rc_now;
		int poll_rc;

		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_SELECT_RCK(i));
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_SINGLE_MODE_V5);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_COUNTING_WIN1);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_COUNTING_WIN2);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_KICK_RCK);
		udelay(50);
		poll_rc = readl_poll_timeout_atomic(LVTS_CONFIG(lvts_ctrl->base), val,
						    !(val & LVTS_DEVICE_SENSING_STATUS),
						    2, 200);
		rc_data = poll_rc ? 0 : (lvts_read_device_reg0(lvts_ctrl) & GENMASK(23, 0));
		udelay(5);

		if (rc_data == 0) {
			rc_now = lvts_ctrl->count_rc;
		} else if (lvts_ctrl->count_rc == 0) {
			rc_now = rc_data;
		} else {
			u64 delta = lvts_ctrl->count_rc > rc_data ?
				div_u64((u64)lvts_ctrl->count_rc * 1000, rc_data) :
				div_u64((u64)rc_data * 1000, lvts_ctrl->count_rc);
			rc_now = delta > 1061 ? lvts_ctrl->count_rc : rc_data;
		}

		lvts_ctrl->calibration[i] = ((u64)rc_now * lvts_ctrl->count_r[i]) >> 14;
	}

	lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_NO_RCK);
	lvts_ctrl->rck_done = true;
}

static void lvts_ctrl_disable_hw_reboot_locked(struct lvts_ctrl *lvts_ctrl)
{
	u32 val;

	val = readl(LVTS_PROTCTL(lvts_ctrl->base));
	writel(val | 0x3FFF, LVTS_PROTCTL(lvts_ctrl->base));
	val = readl(LVTS_MONINT(lvts_ctrl->base));
	writel(val & ~LVTS_INT_STAGE3, LVTS_MONINT(lvts_ctrl->base));
}

static void lvts_ctrl_set_hw_reboot(struct lvts_ctrl *lvts_ctrl, int trip_point)
{
	unsigned long flags;
	u32 val, config, msr_raw;

	spin_lock_irqsave(&lvts_ctrl->lock, flags);
	lvts_ctrl->hw_reboot_trip_point = trip_point;
	if (trip_point == LVTS_HW_REBOOT_DISABLE) {
		lvts_ctrl_disable_hw_reboot_locked(lvts_ctrl);
		spin_unlock_irqrestore(&lvts_ctrl->lock, flags);
		return;
	}

	lvts_ctrl_disable_hw_reboot_locked(lvts_ctrl);
	val = readl(LVTS_PROTCTL(lvts_ctrl->base));
	config = (((u32)lvts_ctrl->dominator_sensor << 2) + 0x2U) << 16;
	writel((val & ~LVTS_PROTCTL_SEL_MASK) | config, LVTS_PROTCTL(lvts_ctrl->base));

	msr_raw = lvts_temp_to_raw(trip_point, lvts_ctrl->lvts_data->temp_factor);
	msr_raw = min_t(u32, msr_raw, 0x7FFF);
	writel(msr_raw, LVTS_PROTTC(lvts_ctrl->base));

	val = readl(LVTS_MONINT(lvts_ctrl->base));
	writel(val | LVTS_INT_STAGE3, LVTS_MONINT(lvts_ctrl->base));
	val = readl(LVTS_PROTCTL(lvts_ctrl->base));
	writel(val & ~LVTS_PROTOFFSET_MASK, LVTS_PROTCTL(lvts_ctrl->base));
	spin_unlock_irqrestore(&lvts_ctrl->lock, flags);
}

static int lvts_soc_max_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct lvts_domain *lvts_td = thermal_zone_device_priv(tz);
	int i, j, cur, max_temp = THERMAL_TEMP_INVALID;
	bool found = false;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
		struct lvts_ctrl *ctrl = &lvts_td->lvts_ctrl[i];

		lvts_for_each_valid_sensor(j, ctrl) {
			if (!ctrl->sensors[j].tz)
				continue;
			if (lvts_get_temp(ctrl->sensors[j].tz, &cur))
				continue;
			if (!found || cur > max_temp) {
				max_temp = cur;
				found = true;
			}
		}
	}

	if (!found)
		return -EAGAIN;

	*temp = max_temp;
	return 0;
}

static int lvts_soc_max_set_trip_temp(struct thermal_zone_device *tz,
				      const struct thermal_trip *trip, int temp)
{
	struct lvts_domain *lvts_td = thermal_zone_device_priv(tz);
	int i;

	if (trip->type != THERMAL_TRIP_CRITICAL)
		return 0;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
		lvts_ctrl_set_hw_reboot(&lvts_td->lvts_ctrl[i], temp);

	return 0;
}

static const struct thermal_zone_device_ops lvts_soc_max_ops = {
	.get_temp = lvts_soc_max_get_temp,
	.set_trip_temp = lvts_soc_max_set_trip_temp,
};

static int lvts_irq_init(struct lvts_ctrl *lvts_ctrl)
{

	writel(0, LVTS_MONINT(lvts_ctrl->base));

	return 0;
}

static int lvts_domain_reset(struct device *dev, struct reset_control *reset)
{
	int ret;

	ret = reset_control_assert(reset);
	if (ret)
		return ret;

	return reset_control_deassert(reset);
}

static int lvts_ctrl_set_enable(struct lvts_ctrl *lvts_ctrl, int enable)
{

	writel(enable, LVTS_CLKEN(lvts_ctrl->base));

	return 0;
}

static int lvts_ctrl_connect(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	u32 id;

	lvts_write_config(lvts_ctrl, lvts_data->conn_cmd, lvts_data->num_conn_cmd);

	id = readl(LVTS_ID(lvts_ctrl->base));
	if (!(id & BIT(7)))
		return -EIO;

	return 0;
}

static int lvts_ctrl_initialize(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;

	lvts_write_config(lvts_ctrl, lvts_data->init_cmd, lvts_data->num_init_cmd);

	return 0;
}

static int lvts_ctrl_calibrate(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	int i;
	void __iomem *lvts_edata[] = {
		LVTS_EDATA00(lvts_ctrl->base),
		LVTS_EDATA01(lvts_ctrl->base),
		LVTS_EDATA02(lvts_ctrl->base),
		LVTS_EDATA03(lvts_ctrl->base)
	};

	for (i = 0; i < LVTS_SENSOR_MAX; i++)
		writel(lvts_ctrl->calibration[i], lvts_edata[i]);

	return 0;
}

static int lvts_ctrl_configure(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	const struct lvts_data *lvts_data = lvts_ctrl->lvts_data;
	u32 value;

	if (lvts_data->manual_rck_v5) {
		writel(BIT(9), LVTS_MONCTL0(lvts_ctrl->base));
		lvts_ctrl_wait_idle(lvts_ctrl);
		lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_LOW_POWER_SINGLE);
	}

	value = LVTS_TSSEL_CONF;
	writel(value, LVTS_TSSEL(lvts_ctrl->base));

	value = LVTS_CALSCALE_CONF;
	writel(value, LVTS_CALSCALE(lvts_ctrl->base));

	value = LVTS_HW_FILTER << 9 |  LVTS_HW_FILTER << 6 |
			LVTS_HW_FILTER << 3 | LVTS_HW_FILTER;
	writel(value, LVTS_MSRCTL0(lvts_ctrl->base));

	value = lvts_data->monctl1 ? lvts_data->monctl1 :
		(LVTS_GROUP_INTERVAL << 20 | LVTS_PERIOD_UNIT);
	writel(value, LVTS_MONCTL1(lvts_ctrl->base));

	value = lvts_data->monctl2 ? lvts_data->monctl2 :
		(LVTS_FILTER_INTERVAL << 16 | LVTS_SENSOR_INTERVAL);
	writel(value, LVTS_MONCTL2(lvts_ctrl->base));

	return lvts_irq_init(lvts_ctrl);
}

static int lvts_ctrl_start(struct device *dev, struct lvts_ctrl *lvts_ctrl)
{
	struct lvts_sensor *lvts_sensors = lvts_ctrl->sensors;
	struct thermal_zone_device *tz;
	u32 sensor_map = 0;
	int i;

	u32 sensor_imm_bitmap[] = { BIT(4), BIT(5), BIT(6), BIT(9) };
	u32 sensor_filt_bitmap[] = { BIT(0), BIT(1), BIT(2), BIT(3) };

	u32 *sensor_bitmap = lvts_ctrl->mode == LVTS_MSR_IMMEDIATE_MODE ?
			     sensor_imm_bitmap : sensor_filt_bitmap;

	if (lvts_ctrl->lvts_data->manual_rck_v5)
		lvts_ctrl_monitor_enable(dev, lvts_ctrl, true);

	lvts_for_each_valid_sensor(i, lvts_ctrl) {

		int dt_id = lvts_sensors[i].dt_id;

		tz = devm_thermal_of_zone_register(dev, dt_id, &lvts_sensors[i],
						   &lvts_ops);
		if (IS_ERR(tz)) {

			if (PTR_ERR(tz) == -ENODEV)
				continue;

			return PTR_ERR(tz);
		}

		lvts_sensors[i].tz = tz;

		sensor_map |= sensor_bitmap[i];
	}

	if (lvts_ctrl->mode == LVTS_MSR_IMMEDIATE_MODE) {

		writel(sensor_map, LVTS_MSRCTL1(lvts_ctrl->base));
	} else {

		writel(sensor_map | BIT(9), LVTS_MONCTL0(lvts_ctrl->base));
	}

	if (lvts_ctrl->lvts_data->hw_reboot_trip_point)
		lvts_ctrl_set_hw_reboot(lvts_ctrl, lvts_ctrl->hw_reboot_trip_point);

	return 0;
}

static int lvts_domain_hw_init(struct device *dev, struct lvts_domain *lvts_td,
			       const struct lvts_data *lvts_data)
{
	struct lvts_ctrl *lvts_ctrl;
	int i, ret;

	ret = lvts_domain_reset(dev, lvts_td->reset);
	if (ret) {
		dev_dbg(dev, "Failed to reset domain");
		return ret;
	}

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		lvts_ctrl = &lvts_td->lvts_ctrl[i];

		ret = lvts_ctrl_set_enable(lvts_ctrl, true);
		if (ret) {
			dev_dbg(dev, "Failed to enable LVTS clock");
			return ret;
		}

		ret = lvts_ctrl_connect(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to connect to LVTS controller");
			return ret;
		}

		ret = lvts_ctrl_initialize(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to initialize controller");
			return ret;
		}

		if (lvts_data->manual_rck_v5)
			lvts_ctrl_rck_v5(lvts_ctrl);

		ret = lvts_ctrl_calibrate(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to calibrate controller");
			return ret;
		}

		ret = lvts_ctrl_configure(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to configure controller");
			return ret;
		}
	}

	return 0;
}

static void lvts_domain_hw_stop(void *data)
{
	struct lvts_domain *lvts_td = data;
	int i;

	if (lvts_td->lvts_data->manual_rck_v5) {
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
			lvts_ctrl_monitor_enable(NULL, &lvts_td->lvts_ctrl[i], false);
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
			lvts_ctrl_wait_idle(&lvts_td->lvts_ctrl[i]);
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
			lvts_write_device_cmd(&lvts_td->lvts_ctrl[i], LVTS_DEV_RESET_ALL);
			lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);
		}
		return;
	}

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);
}

static int lvts_domain_init(struct device *dev, struct lvts_domain *lvts_td,
					const struct lvts_data *lvts_data)
{
	struct lvts_ctrl *lvts_ctrl;
	int i, ret;

	lvts_td->lvts_data = lvts_data;

	ret = lvts_ctrl_init(dev, lvts_td, lvts_data);
	if (ret)
		return ret;

	ret = lvts_domain_hw_init(dev, lvts_td, lvts_data);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, lvts_domain_hw_stop, lvts_td);
	if (ret)
		return ret;

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {

		lvts_ctrl = &lvts_td->lvts_ctrl[i];

		ret = lvts_ctrl_start(dev, lvts_ctrl);
		if (ret) {
			dev_dbg(dev, "Failed to start controller");
			return ret;
		}
	}

	if (lvts_data->has_soc_max) {
		struct thermal_zone_device *tz;

		tz = devm_thermal_of_zone_register(dev, 0, lvts_td,
						   &lvts_soc_max_ops);
		if (IS_ERR(tz)) {
			if (PTR_ERR(tz) != -ENODEV)
				return PTR_ERR(tz);
		} else {
			lvts_td->soc_max_tz = tz;
		}
	}

	return lvts_debugfs_init(dev, lvts_td);
}

static int lvts_probe(struct platform_device *pdev)
{
	const struct lvts_data *lvts_data;
	struct lvts_domain *lvts_td;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, ret;

	lvts_td = devm_kzalloc(dev, sizeof(*lvts_td), GFP_KERNEL);
	if (!lvts_td)
		return -ENOMEM;

	lvts_data = of_device_get_match_data(dev);
	if (!lvts_data)
		return -ENODEV;

	lvts_td->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(lvts_td->clk))
		return dev_err_probe(dev, PTR_ERR(lvts_td->clk), "Failed to retrieve clock\n");

	lvts_td->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(lvts_td->base))
		return dev_err_probe(dev, PTR_ERR(lvts_td->base), "Failed to map io resource\n");

	lvts_td->reset = devm_reset_control_get_by_index(dev, 0);
	if (IS_ERR(lvts_td->reset))
		return dev_err_probe(dev, PTR_ERR(lvts_td->reset), "Failed to get reset control\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	golden_temp_offset = lvts_data->temp_offset;

	ret = lvts_domain_init(dev, lvts_td, lvts_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize the lvts domain\n");

	ret = devm_request_threaded_irq(dev, irq, NULL, lvts_irq_handler,
					IRQF_ONESHOT, dev_name(dev), lvts_td);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request interrupt\n");

	platform_set_drvdata(pdev, lvts_td);

	return 0;
}

static const struct lvts_ctrl_data mt7988_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT7988_CPU_0,
			  .cal_offsets = { 0x00, 0x01, 0x02 } },
			{ .dt_id = MT7988_CPU_1,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT7988_ETH2P5G_0,
			  .cal_offsets = { 0x08, 0x09, 0x0a } },
			{ .dt_id = MT7988_ETH2P5G_1,
			  .cal_offsets = { 0x0c, 0x0d, 0x0e } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT7988_TOPS_0,
			   .cal_offsets = { 0x14, 0x15, 0x16 } },
			{ .dt_id = MT7988_TOPS_1,
			   .cal_offsets = { 0x18, 0x19, 0x1a } },
			{ .dt_id = MT7988_ETHWARP_0,
			   .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT7988_ETHWARP_1,
			   .cal_offsets = { 0x20, 0x21, 0x22 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x100,
	}
};

static int lvts_suspend(struct device *dev)
{
	struct lvts_domain *lvts_td;
	int i;

	lvts_td = dev_get_drvdata(dev);

	if (lvts_td->lvts_data->manual_rck_v5) {
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
			lvts_ctrl_monitor_enable(dev, &lvts_td->lvts_ctrl[i], false);
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++)
			lvts_ctrl_wait_idle(&lvts_td->lvts_ctrl[i]);
		for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
			lvts_write_device_cmd(&lvts_td->lvts_ctrl[i], LVTS_DEV_RESET_ALL);
			lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);
		}
		clk_disable_unprepare(lvts_td->clk);
		return 0;
	}

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
		lvts_ctrl_monitor_enable(dev, &lvts_td->lvts_ctrl[i], false);
		usleep_range(100, 200);
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], false);
	}

	clk_disable_unprepare(lvts_td->clk);

	return 0;
}

static int lvts_resume(struct device *dev)
{
	struct lvts_domain *lvts_td;
	int i, ret;

	lvts_td = dev_get_drvdata(dev);

	ret = clk_prepare_enable(lvts_td->clk);
	if (ret)
		return ret;

	if (lvts_td->lvts_data->manual_rck_v5) {
		ret = lvts_domain_hw_init(dev, lvts_td, lvts_td->lvts_data);
		if (ret) {
			clk_disable_unprepare(lvts_td->clk);
			return ret;
		}

		for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
			struct lvts_ctrl *ctrl = &lvts_td->lvts_ctrl[i];
			unsigned long flags;

			lvts_ctrl_monitor_enable(dev, ctrl, true);
			if (lvts_td->lvts_data->hw_reboot_trip_point)
				lvts_ctrl_set_hw_reboot(ctrl, ctrl->hw_reboot_trip_point);
			spin_lock_irqsave(&ctrl->lock, flags);
			if (ctrl->high_thresh != INT_MIN) {
				u32 raw_low = lvts_temp_to_raw(
					ctrl->low_thresh != -INT_MAX ? ctrl->low_thresh : LVTS_MINIMUM_THRESHOLD,
					lvts_td->lvts_data->temp_factor);
				u32 raw_high = ctrl->high_thresh != INT_MAX ?
					lvts_temp_to_raw(ctrl->high_thresh, lvts_td->lvts_data->temp_factor) : 0;

				writel(min_t(u32, raw_low, 0x7FFF), LVTS_OFFSETL(ctrl->base));
				writel(min_t(u32, raw_high, 0x7FFF), LVTS_OFFSETH(ctrl->base));
				lvts_update_irq_mask(ctrl);
			}
			spin_unlock_irqrestore(&ctrl->lock, flags);
		}
		return 0;
	}

	for (i = 0; i < lvts_td->num_lvts_ctrl; i++) {
		lvts_ctrl_set_enable(&lvts_td->lvts_ctrl[i], true);
		usleep_range(100, 200);
		lvts_ctrl_monitor_enable(dev, &lvts_td->lvts_ctrl[i], true);
	}

	return 0;
}

static const u32 default_conn_cmds[] = { 0xC103FFFF, 0xC502FF55 };
static const u32 mt7988_conn_cmds[] = { 0xC103FFFF, 0xC502FC55 };
static const u32 mt6789_conn_cmds[] = { 0xC103FFFF, 0xC502FC00 };

static const u32 mt6789_init_cmds[] = {
	0xC1030300, 0xC1030420, 0xC1030500, 0xC10307A6,
	0xC1030CFC, 0xC1030A8C, 0xC103098D, 0xC10308F1,
};

static const struct lvts_ctrl_data mt6789_lvts_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = 1, .cal_offsets = { 4, 5, 0 } },
			{ .dt_id = 2, .cal_offsets = { 6, 7, 0 } },
			{ .dt_id = 3, .cal_offsets = { 8, 9, 0 } },
			{ .dt_id = 4, .cal_offsets = { 10, 11, 0 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
		.mode = LVTS_MSR_FILTERED_MODE,
		.dominator_sensor = 1,
		.rck_word_index = 8,
	},
	{
		.lvts_sensor = {
			{ .dt_id = 5, .cal_offsets = { 12, 13, 0 } },
			{ .dt_id = 6, .cal_offsets = { 14, 15, 0 } },
			{ .dt_id = 7, .cal_offsets = { 16, 17, 0 } },
			{ .dt_id = 8, .cal_offsets = { 18, 19, 0 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x100,
		.mode = LVTS_MSR_FILTERED_MODE,
		.dominator_sensor = 2,
		.rck_word_index = 9,
	},
	{
		.lvts_sensor = {
			{ .dt_id = 9, .cal_offsets = { 20, 21, 0 } },
			{ .dt_id = 10, .cal_offsets = { 22, 23, 0 } },
			{ .dt_id = 11, .cal_offsets = { 24, 25, 0 } },
			{ .dt_id = 12, .cal_offsets = { 26, 27, 0 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x200,
		.mode = LVTS_MSR_FILTERED_MODE,
		.dominator_sensor = 0,
		.rck_word_index = 10,
	},
	{
		.lvts_sensor = {
			{ .dt_id = 13, .cal_offsets = { 28, 29, 0 } },
		},
		VALID_SENSOR_MAP(1, 0, 0, 0),
		.offset = 0x300,
		.mode = LVTS_MSR_FILTERED_MODE,
		.dominator_sensor = 0,
		.rck_word_index = 11,
	},
};

static const u32 default_init_cmds[] = {
	0xC1030E01, 0xC1030CFC, 0xC1030A8C, 0xC103098D, 0xC10308F1,
	0xC10307A6, 0xC10306B8, 0xC1030500, 0xC1030420, 0xC1030300,
	0xC1030030, 0xC10300F6, 0xC1030050, 0xC1030060, 0xC10300AC,
	0xC10300FC, 0xC103009D, 0xC10300F1, 0xC10300E1
};

static const u32 mt7988_init_cmds[] = {
	0xC1030300, 0xC1030420, 0xC1030500, 0xC10307A6, 0xC1030CFC,
	0xC1030A8C, 0xC103098D, 0xC10308F1, 0xC1030B04, 0xC1030E01,
	0xC10306B8
};

static const struct lvts_ctrl_data mt8186_lvts_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_LITTLE_CPU0,
			  .cal_offsets = { 5, 6, 7 } },
			{ .dt_id = MT8186_LITTLE_CPU1,
			  .cal_offsets = { 10, 11, 4 } },
			{ .dt_id = MT8186_LITTLE_CPU2,
			  .cal_offsets = { 15, 8, 9 } },
			{ .dt_id = MT8186_CAM,
			  .cal_offsets = { 12, 13, 14 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_BIG_CPU0,
			  .cal_offsets = { 22, 23, 16 } },
			{ .dt_id = MT8186_BIG_CPU1,
			  .cal_offsets = { 27, 20, 21 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8186_NNA,
			  .cal_offsets = { 29, 30, 31 } },
			{ .dt_id = MT8186_ADSP,
			  .cal_offsets = { 34, 35, 28 } },
			{ .dt_id = MT8186_GPU,
			  .cal_offsets = { 39, 32, 33 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x200,
	}
};

static const struct lvts_ctrl_data mt8188_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_MCU_LITTLE_CPU0,
			  .cal_offsets = { 22, 23, 24 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU1,
			  .cal_offsets = { 25, 26, 27 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU2,
			  .cal_offsets = { 28, 29, 30 } },
			{ .dt_id = MT8188_MCU_LITTLE_CPU3,
			  .cal_offsets = { 31, 32, 33 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_MCU_BIG_CPU0,
			  .cal_offsets = { 34, 35, 36 } },
			{ .dt_id = MT8188_MCU_BIG_CPU1,
			  .cal_offsets = { 37, 38, 39 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	}
};

static const struct lvts_ctrl_data mt8188_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {

			{  },
			{ .dt_id = MT8188_AP_APU,
			  .cal_offsets = { 40, 41, 42 } },
		},
		VALID_SENSOR_MAP(0, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_GPU0,
			  .cal_offsets = { 43, 44, 45 } },
			{ .dt_id = MT8188_AP_GPU1,
			  .cal_offsets = { 46, 47, 48 } },
			{ .dt_id = MT8188_AP_ADSP,
			  .cal_offsets = { 49, 50, 51 } },
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_VDO,
			  .cal_offsets = { 52, 53, 54 } },
			{ .dt_id = MT8188_AP_INFRA,
			  .cal_offsets = { 55, 56, 57 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8188_AP_CAM1,
			  .cal_offsets = { 58, 59, 60 } },
			{ .dt_id = MT8188_AP_CAM2,
			  .cal_offsets = { 61, 62, 63 } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x300,
	}
};

static const struct lvts_ctrl_data mt8192_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_BIG_CPU0,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT8192_MCU_BIG_CPU1,
			  .cal_offsets = { 0x08, 0x09, 0x0a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
		.mode = LVTS_MSR_FILTERED_MODE,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_BIG_CPU2,
			  .cal_offsets = { 0x0c, 0x0d, 0x0e } },
			{ .dt_id = MT8192_MCU_BIG_CPU3,
			  .cal_offsets = { 0x10, 0x11, 0x12 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
		.mode = LVTS_MSR_FILTERED_MODE,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_MCU_LITTLE_CPU0,
			  .cal_offsets = { 0x14, 0x15, 0x16 } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU1,
			  .cal_offsets = { 0x18, 0x19, 0x1a } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU2,
			  .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT8192_MCU_LITTLE_CPU3,
			  .cal_offsets = { 0x20, 0x21, 0x22 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x200,
		.mode = LVTS_MSR_FILTERED_MODE,
	}
};

static const struct lvts_ctrl_data mt8192_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_VPU0,
			  .cal_offsets = { 0x24, 0x25, 0x26 } },
			{ .dt_id = MT8192_AP_VPU1,
			  .cal_offsets = { 0x28, 0x29, 0x2a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_GPU0,
			  .cal_offsets = { 0x2c, 0x2d, 0x2e } },
			{ .dt_id = MT8192_AP_GPU1,
			  .cal_offsets = { 0x30, 0x31, 0x32 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_INFRA,
			  .cal_offsets = { 0x34, 0x35, 0x36 } },
			{ .dt_id = MT8192_AP_CAM,
			  .cal_offsets = { 0x38, 0x39, 0x3a } },
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8192_AP_MD0,
			  .cal_offsets = { 0x3c, 0x3d, 0x3e } },
			{ .dt_id = MT8192_AP_MD1,
			  .cal_offsets = { 0x40, 0x41, 0x42 } },
			{ .dt_id = MT8192_AP_MD2,
			  .cal_offsets = { 0x44, 0x45, 0x46 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x300,
	}
};

static const struct lvts_ctrl_data mt8195_lvts_mcu_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_BIG_CPU0,
			  .cal_offsets = { 0x04, 0x05, 0x06 } },
			{ .dt_id = MT8195_MCU_BIG_CPU1,
			  .cal_offsets = { 0x07, 0x08, 0x09 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_BIG_CPU2,
			  .cal_offsets = { 0x0d, 0x0e, 0x0f } },
			{ .dt_id = MT8195_MCU_BIG_CPU3,
			  .cal_offsets = { 0x10, 0x11, 0x12 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_MCU_LITTLE_CPU0,
			  .cal_offsets = { 0x16, 0x17, 0x18 } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU1,
			  .cal_offsets = { 0x19, 0x1a, 0x1b } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU2,
			  .cal_offsets = { 0x1c, 0x1d, 0x1e } },
			{ .dt_id = MT8195_MCU_LITTLE_CPU3,
			  .cal_offsets = { 0x1f, 0x20, 0x21 } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 1),
		.offset = 0x200,
	}
};

static const struct lvts_ctrl_data mt8195_lvts_ap_data_ctrl[] = {
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_VPU0,
			  .cal_offsets = { 0x25, 0x26, 0x27 } },
			{ .dt_id = MT8195_AP_VPU1,
			  .cal_offsets = { 0x28, 0x29, 0x2a } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x0,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_GPU0,
			  .cal_offsets = { 0x2e, 0x2f, 0x30 } },
			{ .dt_id = MT8195_AP_GPU1,
			  .cal_offsets = { 0x31, 0x32, 0x33 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x100,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_VDEC,
			  .cal_offsets = { 0x37, 0x38, 0x39 } },
			{ .dt_id = MT8195_AP_IMG,
			  .cal_offsets = { 0x3a, 0x3b, 0x3c } },
			{ .dt_id = MT8195_AP_INFRA,
			  .cal_offsets = { 0x3d, 0x3e, 0x3f } }
		},
		VALID_SENSOR_MAP(1, 1, 1, 0),
		.offset = 0x200,
	},
	{
		.lvts_sensor = {
			{ .dt_id = MT8195_AP_CAM0,
			  .cal_offsets = { 0x43, 0x44, 0x45 } },
			{ .dt_id = MT8195_AP_CAM1,
			  .cal_offsets = { 0x46, 0x47, 0x48 } }
		},
		VALID_SENSOR_MAP(1, 1, 0, 0),
		.offset = 0x300,
	}
};

static const struct lvts_data mt7988_lvts_ap_data = {
	.lvts_ctrl	= mt7988_lvts_ap_data_ctrl,
	.conn_cmd	= mt7988_conn_cmds,
	.init_cmd	= mt7988_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt7988_lvts_ap_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(mt7988_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(mt7988_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT7988,
	.temp_offset	= LVTS_COEFF_B_MT7988,
	.gt_calib_bit_offset = 24,
};

static const struct lvts_data mt8186_lvts_data = {
	.lvts_ctrl	= mt8186_lvts_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8186_lvts_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT7988,
	.temp_offset	= LVTS_COEFF_B_MT7988,
	.gt_calib_bit_offset = 24,
	.def_calibration = 19000,
};

static const struct lvts_data mt8188_lvts_mcu_data = {
	.lvts_ctrl	= mt8188_lvts_mcu_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8188_lvts_mcu_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 20,
	.def_calibration = 35000,
};

static const struct lvts_data mt8188_lvts_ap_data = {
	.lvts_ctrl	= mt8188_lvts_ap_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8188_lvts_ap_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 20,
	.def_calibration = 35000,
};

static const struct lvts_data mt8192_lvts_mcu_data = {
	.lvts_ctrl	= mt8192_lvts_mcu_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8192_lvts_mcu_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
	.def_calibration = 35000,
};

static const struct lvts_data mt8192_lvts_ap_data = {
	.lvts_ctrl	= mt8192_lvts_ap_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8192_lvts_ap_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
	.def_calibration = 35000,
};

static const struct lvts_data mt8195_lvts_mcu_data = {
	.lvts_ctrl	= mt8195_lvts_mcu_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8195_lvts_mcu_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
	.def_calibration = 35000,
};

static const struct lvts_data mt8195_lvts_ap_data = {
	.lvts_ctrl	= mt8195_lvts_ap_data_ctrl,
	.conn_cmd	= default_conn_cmds,
	.init_cmd	= default_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt8195_lvts_ap_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(default_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(default_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
	.def_calibration = 35000,
};

static const struct lvts_data mt6789_lvts_data = {
	.lvts_ctrl	= mt6789_lvts_data_ctrl,
	.conn_cmd	= mt6789_conn_cmds,
	.init_cmd	= mt6789_init_cmds,
	.num_lvts_ctrl	= ARRAY_SIZE(mt6789_lvts_data_ctrl),
	.num_conn_cmd	= ARRAY_SIZE(mt6789_conn_cmds),
	.num_init_cmd	= ARRAY_SIZE(mt6789_init_cmds),
	.temp_factor	= LVTS_COEFF_A_MT8195,
	.temp_offset	= LVTS_COEFF_B_MT8195,
	.gt_calib_bit_offset = 24,
	.def_calibration = 35000,
	.def_count_rc	= 2750,
	.monctl1	= LVTS_MT6789_MONCTL1,
	.monctl2	= LVTS_MT6789_MONCTL2,
	.default_golden_temp = 60,
	.hw_reboot_trip_point = LVTS_MT6789_HW_REBOOT_TEMP,
	.manual_rck_v5	= true,
	.has_soc_max	= true,
};

static const struct of_device_id lvts_of_match[] = {
	{ .compatible = "mediatek,mt6789-lvts", .data = &mt6789_lvts_data },
	{ .compatible = "mediatek,mt7988-lvts-ap", .data = &mt7988_lvts_ap_data },
	{ .compatible = "mediatek,mt8186-lvts", .data = &mt8186_lvts_data },
	{ .compatible = "mediatek,mt8188-lvts-mcu", .data = &mt8188_lvts_mcu_data },
	{ .compatible = "mediatek,mt8188-lvts-ap", .data = &mt8188_lvts_ap_data },
	{ .compatible = "mediatek,mt8192-lvts-mcu", .data = &mt8192_lvts_mcu_data },
	{ .compatible = "mediatek,mt8192-lvts-ap", .data = &mt8192_lvts_ap_data },
	{ .compatible = "mediatek,mt8195-lvts-mcu", .data = &mt8195_lvts_mcu_data },
	{ .compatible = "mediatek,mt8195-lvts-ap", .data = &mt8195_lvts_ap_data },
	{},
};
MODULE_DEVICE_TABLE(of, lvts_of_match);

static const struct dev_pm_ops lvts_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(lvts_suspend, lvts_resume)
};

static struct platform_driver lvts_driver = {
	.probe = lvts_probe,
	.driver = {
		.name = "mtk-lvts-thermal",
		.of_match_table = lvts_of_match,
		.pm = &lvts_pm_ops,
	},
};
module_platform_driver(lvts_driver);

MODULE_AUTHOR("Balsam CHIHI <bchihi@baylibre.com>");
MODULE_DESCRIPTION("MediaTek LVTS Thermal Driver");
MODULE_LICENSE("GPL");
