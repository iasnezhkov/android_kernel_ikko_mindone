// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include "charger_class.h"
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>

#define ETA6965_REG_00		0x00
#define  REG00_IINLIM_MASK	0x1f
#define  REG00_EN_HIZ_MASK	0x80
#define ETA6965_REG_01		0x01
#define  REG01_CHG_EN_MASK	0x10
#define  REG01_PFM_MASK		0x80
#define  REG01_OTG_CONFIG_MASK	0x20
#define  REG01_SYS_MIN_MASK	0x0e
#define  REG01_WDT_RST_MASK	0x40
#define ETA6965_REG_02		0x02
#define  REG02_ICHG_MASK	0x3f
#define  REG02_BOOST_LIM_MASK	0x80
#define ETA6965_REG_03		0x03
#define  REG03_ITERM_MASK	0x0f
#define  REG03_IPRECHG_MASK	0xf0
#define ETA6965_REG_04		0x04
#define  REG04_VREG_MASK	0xf8
#define ETA6965_REG_05		0x05
#define  REG05_WATCHDOG_MASK	0x30
#define  REG05_EN_TERM_MASK	0x80
#define ETA6965_REG_06		0x06
#define  REG06_VINDPM_MASK	0x0f
#define  REG06_BOOSTV_MASK	0x30
#define  REG00_EN_ICHG_MON_MASK	0x60
#define  REG01_MIN_VBAT_SEL_MASK	0x01
#define  REG05_EN_TIMER_MASK	0x08
#define  REG06_OVP_MASK		0xc0
#define  REG06_OVP_10V5		0x80
#define ETA6965_REG_07		0x07
#define  REG07_VDPM_BAT_TRACK_MASK	0x03
#define ETA6965_REG_08		0x08
#define  REG08_VBUS_STAT_MASK	0xe0
#define  REG08_VBUS_STAT_SHIFT	5
#define  REG08_CHRG_STAT_MASK	0x18
#define  REG08_CHRG_STAT_SHIFT	3
#define  REG08_PG_STAT_MASK	0x04
#define  REG08_THERM_STAT_MASK	0x02
#define  REG08_VSYS_STAT_MASK	0x01
#define ETA6965_REG_09		0x09
#define  REG09_WATCHDOG_FAULT_MASK	0x80
#define  REG09_BOOST_FAULT_MASK	0x40
#define  REG09_CHRG_FAULT_MASK	0x30
#define  REG09_CHRG_FAULT_SHIFT	4
#define  REG09_BAT_FAULT_MASK	0x08
#define  REG09_NTC_FAULT_MASK	0x07
#define ETA6965_REG_0A		0x0a
#define  REG0A_VBUS_GD_MASK	0x80
#define  REG0A_VINDPM_STAT_MASK	0x40
#define  REG0A_IINDPM_STAT_MASK	0x20
#define ETA6965_REG_0B		0x0b
#define  REG0B_REG_RST_MASK	0x80

struct eta6965_device {
	struct i2c_client	*client;
	struct device		*dev;
	struct mutex		lock;
	struct power_supply	*psy;
	struct charger_device	*chg_dev;
	bool			otg_active;
	struct regulator_dev	*otg_vbus_rdev;
	bool			suspended;
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*ce_low;
	struct pinctrl_state	*ce_high;
};

static int eta6965_read_byte(struct eta6965_device *eta, u8 reg, u8 *val)
{
	int ret = i2c_smbus_read_byte_data(eta->client, reg);
	if (ret < 0)
		return ret;
	*val = ret & 0xff;
	return 0;
}
static int eta6965_write_byte(struct eta6965_device *eta, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(eta->client, reg, val);
}
static int eta6965_update_bits(struct eta6965_device *eta, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	int ret;
	mutex_lock(&eta->lock);
	ret = eta6965_read_byte(eta, reg, &tmp);
	if (ret)
		goto out;
	tmp = (tmp & ~mask) | (val & mask);
	ret = eta6965_write_byte(eta, reg, tmp);
out:
	mutex_unlock(&eta->lock);
	return ret;
}

static void eta6965_dump_register(struct eta6965_device *eta)
{
	u8 reg00 = 0, reg01 = 0, reg02 = 0, reg04 = 0, reg05 = 0, reg06 = 0;
	u8 reg08 = 0, reg09 = 0, reg0a = 0;

	eta6965_read_byte(eta, ETA6965_REG_00, &reg00);
	eta6965_read_byte(eta, ETA6965_REG_01, &reg01);
	eta6965_read_byte(eta, ETA6965_REG_02, &reg02);
	eta6965_read_byte(eta, ETA6965_REG_04, &reg04);
	eta6965_read_byte(eta, ETA6965_REG_05, &reg05);
	eta6965_read_byte(eta, ETA6965_REG_06, &reg06);
	eta6965_read_byte(eta, ETA6965_REG_08, &reg08);
	eta6965_read_byte(eta, ETA6965_REG_09, &reg09);
	eta6965_read_byte(eta, ETA6965_REG_0A, &reg0a);
	dev_dbg(eta->dev,
		"eta6965: REG00=0x%02x REG01=0x%02x REG02=0x%02x REG04=0x%02x REG05=0x%02x REG06=0x%02x REG08=0x%02x REG09=0x%02x REG0A=0x%02x\n",
		reg00, reg01, reg02, reg04, reg05, reg06, reg08, reg09, reg0a);
	dev_dbg(eta->dev,
		"eta6965: vbus_stat=%u chrg_stat=%u pg=%u therm=%u vsys_min=%u chrg_fault=%u bat_fault=%u ntc_fault=%u vbus_gd=%u vindpm=%u iindpm=%u\n",
		(reg08 & REG08_VBUS_STAT_MASK) >> REG08_VBUS_STAT_SHIFT,
		(reg08 & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT,
		!!(reg08 & REG08_PG_STAT_MASK),
		!!(reg08 & REG08_THERM_STAT_MASK),
		!!(reg08 & REG08_VSYS_STAT_MASK),
		(reg09 & REG09_CHRG_FAULT_MASK) >> REG09_CHRG_FAULT_SHIFT,
		!!(reg09 & REG09_BAT_FAULT_MASK),
		reg09 & REG09_NTC_FAULT_MASK,
		!!(reg0a & REG0A_VBUS_GD_MASK),
		!!(reg0a & REG0A_VINDPM_STAT_MASK),
		!!(reg0a & REG0A_IINDPM_STAT_MASK));
}

#define ICHG_BASE_UA	0
#define ICHG_STEP_UA	60000
#define ICHG_MAX_CODE	54

#define CV_BASE_UV	3848000
#define CV_STEP_UV	32000
#define CV_MAX_CODE	24

static const u32 iinlim_table_uA[] = {
	 100000,  200000,  300000,  400000,  500000,  600000,  700000,  800000,
	 900000, 1000000, 1100000, 1200000, 1300000, 1400000, 1500000, 1600000,
	1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000,
	2500000, 2600000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000,
};
#define IINLIM_MAX_CODE	(ARRAY_SIZE(iinlim_table_uA) - 1)

#define ITERM_BASE_UA	60000
#define ITERM_STEP_UA	60000
#define ITERM_MAX_CODE	15

#define VINDPM_BASE_UV	3900000
#define VINDPM_STEP_UV	100000

static u32 eta6965_table_lookup_code(const u32 *tbl, u32 n, u32 target)
{
	u32 i;

	if (target <= tbl[0])
		return 0;
	for (i = 1; i < n; i++) {
		if (tbl[i] > target)
			return i - 1;
	}
	return n - 1;
}

static int eta_op_enable(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	int ret;

	if (en) {
		ret = eta6965_update_bits(eta, ETA6965_REG_00, REG00_EN_HIZ_MASK, 0);
		if (ret)
			return ret;
	}
	ret = eta6965_update_bits(eta, ETA6965_REG_01, REG01_CHG_EN_MASK, en ? REG01_CHG_EN_MASK : 0);
	if (ret || !eta->pinctrl)
		return ret;
	return pinctrl_select_state(eta->pinctrl, en ? eta->ce_low : eta->ce_high);
}

static int eta6965_pe_step(struct eta6965_device *eta, u8 ichg, u8 iinlim, unsigned int ms)
{
	int ret = eta6965_update_bits(eta, ETA6965_REG_02, REG02_ICHG_MASK, ichg);

	if (!ret)
		ret = eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, iinlim);
	if (!ret && ms)
		msleep(ms);
	return ret;
}

static int eta_op_send_ta_current_pattern(struct charger_device *dev, bool is_increase)
{
	static const u16 inc_on_ms[] = { 85, 85, 281, 281, 281, 485 };
	static const u16 dec_on_ms[] = { 281, 281, 281, 85, 85, 485 };
	struct eta6965_device *eta = charger_get_data(dev);
	const u16 *on_ms = is_increase ? inc_on_ms : dec_on_ms;
	u8 off_ichg = is_increase ? 9 : 1;
	int i, ret;

	if (eta->pinctrl) {
		ret = pinctrl_select_state(eta->pinctrl, eta->ce_low);
		if (ret)
			return ret;
	}
	msleep(85);
	for (i = 0; i < ARRAY_SIZE(inc_on_ms); i++) {
		ret = eta6965_pe_step(eta, off_ichg, 0, 85);
		if (!ret)
			ret = eta6965_pe_step(eta, 9, 4, on_ms[i]);
		if (ret)
			return ret;
	}
	ret = eta6965_pe_step(eta, off_ichg, 0, 50);
	if (!ret)
		ret = eta6965_pe_step(eta, 9, 4, is_increase ? 200 : 0);
	if (ret)
		return ret;
	msleep(3000);
	return 0;
}
static int eta_op_is_enabled(struct charger_device *dev, bool *en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_01, &v);
	if (!ret) *en = !!(v & REG01_CHG_EN_MASK);
	return ret;
}
static int eta_op_plug_in(struct charger_device *dev)  { return eta_op_enable(dev, true); }
static int eta_op_plug_out(struct charger_device *dev) { return eta_op_enable(dev, false); }

static int eta_op_set_ichg(struct charger_device *dev, u32 uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u32 code = uA / ICHG_STEP_UA;
	if (code > ICHG_MAX_CODE)
		code = ICHG_MAX_CODE;
	return eta6965_update_bits(eta, ETA6965_REG_02, REG02_ICHG_MASK, (u8)code);
}
static int eta_op_get_ichg(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_02, &v);
	if (!ret) {
		u8 code = v & REG02_ICHG_MASK;
		if (code > ICHG_MAX_CODE)
			code = ICHG_MAX_CODE;
		*uA = ICHG_BASE_UA + code * ICHG_STEP_UA;
	}
	return ret;
}
static int eta_op_set_cv(struct charger_device *dev, u32 uV)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u32 code = (uV > CV_BASE_UV) ? (uV - CV_BASE_UV) / CV_STEP_UV : 0;
	if (code > CV_MAX_CODE)
		code = CV_MAX_CODE;
	return eta6965_update_bits(eta, ETA6965_REG_04, REG04_VREG_MASK, (u8)(code << 3));
}
static int eta6965_get_cv_uV(struct eta6965_device *eta, u32 *uV)
{
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_04, &v);
	if (!ret) {
		u8 code = (v & REG04_VREG_MASK) >> 3;
		if (code > CV_MAX_CODE)
			code = CV_MAX_CODE;
		*uV = CV_BASE_UV + code * CV_STEP_UV;
	}
	return ret;
}
static int eta_op_get_cv(struct charger_device *dev, u32 *uV)
{
	return eta6965_get_cv_uV(charger_get_data(dev), uV);
}
static int eta_op_set_iinlim(struct charger_device *dev, u32 uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 code = (u8)eta6965_table_lookup_code(iinlim_table_uA, ARRAY_SIZE(iinlim_table_uA), uA);
	return eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, code);
}
static int eta6965_get_iinlim_uA(struct eta6965_device *eta, u32 *uA)
{
	u8 v;
	int ret = eta6965_read_byte(eta, ETA6965_REG_00, &v);

	if (!ret) {
		u8 code = v & REG00_IINLIM_MASK;

		if (code > IINLIM_MAX_CODE)
			code = IINLIM_MAX_CODE;
		*uA = iinlim_table_uA[code];
	}
	return ret;
}
static int eta_op_get_iinlim(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);

	return eta6965_get_iinlim_uA(eta, uA);
}
static int eta_op_set_eoc_current(struct charger_device *dev, u32 uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u32 code = (uA > ITERM_BASE_UA) ? (uA - ITERM_BASE_UA) / ITERM_STEP_UA : 0;
	if (code > ITERM_MAX_CODE)
		code = ITERM_MAX_CODE;
	return eta6965_update_bits(eta, ETA6965_REG_03, REG03_ITERM_MASK, (u8)code);
}
static int eta_op_get_eoc_current(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_03, &v);
	if (!ret) *uA = ITERM_BASE_UA + (v & REG03_ITERM_MASK) * ITERM_STEP_UA;
	return ret;
}
static int eta_op_set_mivr(struct charger_device *dev, u32 uV)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u32 code;

	if (uV <= VINDPM_BASE_UV)
		code = 0;
	else {
		code = (uV - VINDPM_BASE_UV) / VINDPM_STEP_UV;
		if (code > REG06_VINDPM_MASK)
			code = REG06_VINDPM_MASK;
	}
	return eta6965_update_bits(eta, ETA6965_REG_06, REG06_VINDPM_MASK, (u8)code);
}
static int eta_op_get_mivr(struct charger_device *dev, u32 *uV)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v;
	int ret = eta6965_read_byte(eta, ETA6965_REG_06, &v);

	if (!ret)
		*uV = VINDPM_BASE_UV + (v & REG06_VINDPM_MASK) * VINDPM_STEP_UV;
	return ret;
}
static int eta_op_run_aicl(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 orig_code, code, good_code;
	u8 v;
	int ret;

	ret = eta6965_read_byte(eta, ETA6965_REG_00, &v);
	if (ret)
		return ret;
	orig_code = v & REG00_IINLIM_MASK;
	if (orig_code > IINLIM_MAX_CODE)
		orig_code = IINLIM_MAX_CODE;

	ret = eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, 0);
	if (ret)
		return ret;
	msleep(50);

	good_code = 0;
	for (code = 1; code <= orig_code; code++) {
		ret = eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, code);
		if (ret)
			break;
		msleep(50);
		ret = eta6965_read_byte(eta, ETA6965_REG_08, &v);
		if (ret)
			break;
		if (!(v & REG08_PG_STAT_MASK))
			break;
		ret = eta6965_read_byte(eta, ETA6965_REG_0A, &v);
		if (ret)
			break;
		if (v & REG0A_VINDPM_STAT_MASK)
			break;
		good_code = code;
	}

	eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);
	*uA = iinlim_table_uA[good_code];
	return 0;
}
static int eta_op_kick_wdt(struct charger_device *dev)
{
	struct eta6965_device *eta = charger_get_data(dev);
	int ret = eta6965_update_bits(eta, ETA6965_REG_01, REG01_WDT_RST_MASK, REG01_WDT_RST_MASK);
	eta6965_update_bits(eta, ETA6965_REG_05, REG05_WATCHDOG_MASK, REG05_WATCHDOG_MASK);
	return ret;
}
static int eta_op_enable_term(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	return eta6965_update_bits(eta, ETA6965_REG_05, REG05_EN_TERM_MASK, en ? REG05_EN_TERM_MASK : 0);
}
static int eta6965_set_otg(struct eta6965_device *eta, bool en)
{
	int ret;

	if (en)
		eta6965_update_bits(eta, ETA6965_REG_01, REG01_CHG_EN_MASK, 0);

	ret = eta6965_update_bits(eta, ETA6965_REG_01, REG01_OTG_CONFIG_MASK,
				  en ? REG01_OTG_CONFIG_MASK : 0);

	if (!en)
		eta6965_update_bits(eta, ETA6965_REG_01, REG01_CHG_EN_MASK, REG01_CHG_EN_MASK);

	if (!ret)
		eta->otg_active = en;
	return ret;
}
static int eta_op_enable_otg(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	return eta6965_set_otg(eta, en);
}
static int eta_op_set_boost_ilim(struct charger_device *dev, u32 uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v = (uA >= 1200000) ? REG02_BOOST_LIM_MASK : 0;
	return eta6965_update_bits(eta, ETA6965_REG_02, REG02_BOOST_LIM_MASK, v);
}
static int eta_op_dump_registers(struct charger_device *dev)
{
	struct eta6965_device *eta = charger_get_data(dev);
	eta6965_dump_register(eta);
	return 0;
}

static int eta_regulator_enable(struct regulator_dev *rdev)
{
	struct eta6965_device *eta = rdev_get_drvdata(rdev);
	return eta6965_set_otg(eta, true);
}
static int eta_regulator_disable(struct regulator_dev *rdev)
{
	struct eta6965_device *eta = rdev_get_drvdata(rdev);
	return eta6965_set_otg(eta, false);
}
static int eta_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct eta6965_device *eta = rdev_get_drvdata(rdev);
	u8 v;
	int ret = eta6965_read_byte(eta, ETA6965_REG_01, &v);
	if (ret)
		return ret;
	return !!(v & REG01_OTG_CONFIG_MASK);
}

static const struct regulator_ops eta6965_otg_vbus_ops = {
	.enable		= eta_regulator_enable,
	.disable	= eta_regulator_disable,
	.is_enabled	= eta_regulator_is_enabled,
};

static const struct regulator_desc eta6965_otg_vbus_desc = {
	.name		= "usb-otg-vbus",
	.of_match	= "usb-otg-vbus",
	.ops		= &eta6965_otg_vbus_ops,
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.n_voltages	= 1,
	.fixed_uV	= 5000000,
};

static const struct charger_ops eta6965_chg_ops = {
	.enable			= eta_op_enable,
	.send_ta_current_pattern = eta_op_send_ta_current_pattern,
	.is_enabled		= eta_op_is_enabled,
	.plug_in		= eta_op_plug_in,
	.plug_out		= eta_op_plug_out,
	.get_charging_current	= eta_op_get_ichg,
	.set_charging_current	= eta_op_set_ichg,
	.get_constant_voltage	= eta_op_get_cv,
	.set_constant_voltage	= eta_op_set_cv,
	.get_input_current	= eta_op_get_iinlim,
	.set_input_current	= eta_op_set_iinlim,
	.get_eoc_current	= eta_op_get_eoc_current,
	.set_eoc_current	= eta_op_set_eoc_current,
	.set_mivr		= eta_op_set_mivr,
	.get_mivr		= eta_op_get_mivr,
	.run_aicl		= eta_op_run_aicl,
	.kick_wdt		= eta_op_kick_wdt,
	.enable_termination	= eta_op_enable_term,
	.enable_otg		= eta_op_enable_otg,
	.set_boost_current_limit = eta_op_set_boost_ilim,
	.dump_registers		= eta_op_dump_registers,
};

static const struct charger_properties eta6965_chg_props = {
	.alias_name = "eta6965",
};

static void eta6965_reg08_to_usb_type(u8 vbus_stat, enum power_supply_type *type,
				       enum power_supply_usb_type *usb_type)
{
	switch (vbus_stat) {
	case 0x1:
		*type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case 0x2:
		*type = POWER_SUPPLY_TYPE_USB_CDP;
		*usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case 0x3:
	case 0x6:
		*type = POWER_SUPPLY_TYPE_USB_DCP;
		*usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case 0x5:
		*type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case 0x0:
	case 0x7:
	default:
		*type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	}
}

static int eta6965_psy_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct eta6965_device *eta = power_supply_get_drvdata(psy);
	enum power_supply_type type;
	enum power_supply_usb_type usb_type;
	u8 reg08, vbus_stat, chrg_stat;
	u32 uA, uV;
	int ret;

	if (READ_ONCE(eta->suspended))
		return -ENODATA;

	ret = eta6965_read_byte(eta, ETA6965_REG_08, &reg08);
	if (ret)
		return ret;

	vbus_stat = (reg08 & REG08_VBUS_STAT_MASK) >> REG08_VBUS_STAT_SHIFT;
	chrg_stat = (reg08 & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(reg08 & REG08_PG_STAT_MASK);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!(reg08 & REG08_PG_STAT_MASK)) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		}
		switch (chrg_stat) {
		case 0x1:
		case 0x2:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 0x3:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_TYPE:
		eta6965_reg08_to_usb_type(vbus_stat, &type, &usb_type);
		val->intval = type;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		eta6965_reg08_to_usb_type(vbus_stat, &type, &usb_type);
		val->intval = usb_type;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = eta6965_get_iinlim_uA(eta, &uA);
		if (ret)
			return ret;
		val->intval = uA;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = eta6965_get_cv_uV(eta, &uV);
		if (ret)
			return ret;
		val->intval = uV;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int eta6965_psy_set_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     const union power_supply_propval *val)
{
	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	power_supply_changed(psy);
	return 0;
}

static int eta6965_psy_property_is_writeable(struct power_supply *psy,
					      enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_ONLINE;
}

static enum power_supply_property eta6965_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
};

static enum power_supply_usb_type eta6965_psy_usb_types[] __maybe_unused = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_DCP,
};

static const struct power_supply_desc eta6965_psy_desc = {
	.name			= "eta6965_chg",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= eta6965_psy_properties,
	.num_properties		= ARRAY_SIZE(eta6965_psy_properties),
	.get_property		= eta6965_psy_get_property,
	.set_property		= eta6965_psy_set_property,
	.property_is_writeable	= eta6965_psy_property_is_writeable,
	.usb_types = (BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) | BIT(POWER_SUPPLY_USB_TYPE_SDP) |
			      BIT(POWER_SUPPLY_USB_TYPE_CDP) | BIT(POWER_SUPPLY_USB_TYPE_DCP)),
};

static struct eta6965_device *g_eta6965;

static int eta6965_hw_init(struct eta6965_device *eta)
{
	static const struct {
		u8 reg, mask, val;
	} init[] = {
		{ ETA6965_REG_06, REG06_OVP_MASK, REG06_OVP_10V5 },
		{ ETA6965_REG_00, REG00_EN_HIZ_MASK, 0 },
		{ ETA6965_REG_00, REG00_EN_ICHG_MON_MASK, REG00_EN_ICHG_MON_MASK },
		{ ETA6965_REG_01, REG01_MIN_VBAT_SEL_MASK, REG01_MIN_VBAT_SEL_MASK },
		{ ETA6965_REG_02, REG02_BOOST_LIM_MASK, REG02_BOOST_LIM_MASK },
		{ ETA6965_REG_03, REG03_IPRECHG_MASK, 8 << 4 },
		{ ETA6965_REG_05, REG05_EN_TIMER_MASK, 0 },
		{ ETA6965_REG_05, REG05_WATCHDOG_MASK, 0 },
		{ ETA6965_REG_07, REG07_VDPM_BAT_TRACK_MASK, REG07_VDPM_BAT_TRACK_MASK },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(init); i++) {
		ret = eta6965_update_bits(eta, init[i].reg, init[i].mask, init[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static int eta6965_probe(struct i2c_client *client)
{
	struct eta6965_device *eta;
	const char *chg_name;
	u8 status;
	int ret;

	eta = devm_kzalloc(&client->dev, sizeof(*eta), GFP_KERNEL);
	if (!eta)
		return -ENOMEM;
	eta->client = client;
	eta->dev = &client->dev;
	mutex_init(&eta->lock);
	i2c_set_clientdata(client, eta);

	ret = eta6965_read_byte(eta, ETA6965_REG_0A, &status);
	if (ret) {
		dev_err(eta->dev, "eta6965: i2c read failed: %d\n", ret);
		return ret;
	}
	dev_info(eta->dev, "eta6965: probed, status(REG0A)=0x%02x\n", status);

	ret = eta6965_hw_init(eta);
	if (ret) {
		dev_err(eta->dev, "eta6965: register init failed: %d\n", ret);
		return ret;
	}

	if (of_property_read_string(eta->dev->of_node, "charger_name", &chg_name))
		chg_name = "primary_chg";

	eta->chg_dev = charger_device_register(chg_name, eta->dev, eta,
					       &eta6965_chg_ops, &eta6965_chg_props);
	if (IS_ERR(eta->chg_dev))
		return PTR_ERR(eta->chg_dev);
	dev_info(eta->dev, "eta6965: registered as \"%s\"\n", chg_name);

	if (!strcmp(chg_name, "primary_chg"))
		g_eta6965 = eta;

	eta6965_dump_register(eta);
	return 0;
}

static void eta6965_remove(struct i2c_client *client)
{
	struct eta6965_device *eta = i2c_get_clientdata(client);

	if (eta && !IS_ERR(eta->chg_dev))
		charger_device_unregister(eta->chg_dev);
}

static const struct of_device_id eta6965_of_match[] = {
	{ .compatible = "mediatek,eta6965_chg_driver" },
	{ .compatible = "mediatek,eta6965_chg_sec" },
	{ .compatible = "mediatek,eta6965_sec" },
	{ .compatible = "mediatek,eta6965" },
	{ }
};
MODULE_DEVICE_TABLE(of, eta6965_of_match);

static const struct i2c_device_id eta6965_i2c_id[] = { { "eta6965", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, eta6965_i2c_id);

static int eta6965_suspend_noirq(struct device *dev)
{
	struct eta6965_device *eta = i2c_get_clientdata(to_i2c_client(dev));

	if (eta)
		WRITE_ONCE(eta->suspended, true);
	return 0;
}

static int eta6965_resume_noirq(struct device *dev)
{
	struct eta6965_device *eta = i2c_get_clientdata(to_i2c_client(dev));

	if (eta)
		WRITE_ONCE(eta->suspended, false);
	return 0;
}

static const struct dev_pm_ops eta6965_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(eta6965_suspend_noirq, eta6965_resume_noirq)
};

static struct i2c_driver eta6965_driver = {
	.driver = { .name = "eta6965_charger", .of_match_table = eta6965_of_match,
		    .pm = pm_sleep_ptr(&eta6965_pm_ops) },
	.probe = eta6965_probe,
	.remove = eta6965_remove,
	.id_table = eta6965_i2c_id,
};
static char *eta6965_supplied_to[] = {
	"mtk-master-charger",
};

static int eta6965_plat_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct power_supply_config psy_cfg = { };

	if (!g_eta6965)
		return -EPROBE_DEFER;

	if (!g_eta6965->psy) {
		psy_cfg.drv_data = g_eta6965;
		psy_cfg.of_node = pdev->dev.of_node;
		psy_cfg.supplied_to = eta6965_supplied_to;
		psy_cfg.num_supplicants = ARRAY_SIZE(eta6965_supplied_to);
		g_eta6965->psy = power_supply_register(&pdev->dev, &eta6965_psy_desc,
							&psy_cfg);
		if (IS_ERR(g_eta6965->psy)) {
			dev_err(&pdev->dev,
				"power supply registration failed (%ld)\n",
				PTR_ERR(g_eta6965->psy));
			g_eta6965->psy = NULL;
		} else {
			dev_dbg(&pdev->dev,
				"power supply registered, of_node=%pOF\n",
				pdev->dev.of_node);
		}
	}

	if (!g_eta6965->pinctrl) {
		struct pinctrl *pinctrl = devm_pinctrl_get(&pdev->dev);
		struct pinctrl_state *low, *high;

		if (!IS_ERR(pinctrl)) {
			low = pinctrl_lookup_state(pinctrl, "psc_chg_en_low");
			high = pinctrl_lookup_state(pinctrl, "psc_chg_en_high");
			if (!IS_ERR(low) && !IS_ERR(high)) {
				g_eta6965->ce_low = low;
				g_eta6965->ce_high = high;
				g_eta6965->pinctrl = pinctrl;
			}
		}
		if (!g_eta6965->pinctrl)
			dev_warn(&pdev->dev, "eta6965: no psc_chg_en pin states, CE left as is\n");
	}

	config.dev = &pdev->dev;
	config.driver_data = g_eta6965;

	rdev = devm_regulator_register(&pdev->dev, &eta6965_otg_vbus_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "eta6965: usb-otg-vbus registration failed (%ld)\n",
			PTR_ERR(rdev));
		return PTR_ERR(rdev);
	}
	g_eta6965->otg_vbus_rdev = rdev;
	dev_info(&pdev->dev, "eta6965: usb-otg-vbus regulator registered, of_node=%pOF\n",
		 rdev->dev.of_node);
	return 0;
}

static const struct of_device_id eta6965_plat_of_match[] = {
	{ .compatible = "mediatek,eta6965_chg_driver" },
	{ }
};
MODULE_DEVICE_TABLE(of, eta6965_plat_of_match);

static struct platform_driver eta6965_plat_driver = {
	.probe = eta6965_plat_probe,
	.driver = {
		.name = "eta6965_chg_plat",
		.of_match_table = eta6965_plat_of_match,
	},
};

static int __init eta6965_init(void)
{
	int ret = i2c_add_driver(&eta6965_driver);

	if (ret)
		return ret;
	ret = platform_driver_register(&eta6965_plat_driver);
	if (ret)
		i2c_del_driver(&eta6965_driver);
	return ret;
}
module_init(eta6965_init);

static void __exit eta6965_exit(void)
{
	platform_driver_unregister(&eta6965_plat_driver);
	i2c_del_driver(&eta6965_driver);
}
module_exit(eta6965_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ETA6965 charger (reconstructed, drain-fixed) for MindOne");
MODULE_AUTHOR("mind_one project");
