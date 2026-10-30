// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include "charger_class.h"
#include "policy.h"
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
#define  REG05_CHG_TIMER_MASK	0x04
#define  REG05_SAFETY_TIMER_MASK	(REG05_EN_TIMER_MASK | REG05_CHG_TIMER_MASK)
#define  REG07_TMR2X_EN_MASK	0x40
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
	struct charger_device	*chg_dev;
	bool			suspended;
	int			usb_input_current_limit;
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*ce_low;
	struct pinctrl_state	*ce_high;
};

struct eta6965_platform {
	struct eta6965_device *eta;
	struct device *supplier;
	struct pinctrl *pinctrl;
	struct regulator_dev *rdev;
	struct power_supply *psy;
	bool active;
};

static DEFINE_MUTEX(eta6965_binding_lock);
static struct eta6965_device *g_eta6965;
static struct eta6965_platform *eta6965_otg_binding;

void mindone_eta_io_lock(void *context)
{
	struct eta6965_device *eta = context;

	mutex_lock(&eta->lock);
}

void mindone_eta_io_unlock(void *context)
{
	struct eta6965_device *eta = context;

	mutex_unlock(&eta->lock);
}

u8 mindone_eta_io_suspended(void *context)
{
	struct eta6965_device *eta = context;

	return READ_ONCE(eta->suspended);
}

int mindone_eta_io_read(void *context, u8 reg, u8 *value)
{
	struct eta6965_device *eta = context;
	int ret = i2c_smbus_read_byte_data(eta->client, reg);

	if (ret < 0)
		return ret;
	*value = ret & 0xff;
	return 0;
}

int mindone_eta_io_write(void *context, u8 reg, u8 value)
{
	struct eta6965_device *eta = context;

	return i2c_smbus_write_byte_data(eta->client, reg, value);
}

int mindone_eta_io_pin(void *context, u8 enabled)
{
	struct eta6965_device *eta = context;

	return eta->pinctrl ? pinctrl_select_state(eta->pinctrl,
		enabled ? eta->ce_low : eta->ce_high) : 0;
}

void mindone_eta_io_sleep(void *context, u32 milliseconds)
{
	msleep(milliseconds);
}

static int eta6965_logic(struct eta6965_device *eta, u32 operation,
			u32 argument, u32 *output)
{
	u32 value;
	int ret = mindone_eta_call(eta, operation, argument, &value);

	if (!ret && output)
		*output = value;
	return ret;
}

static int eta6965_read_byte(struct eta6965_device *eta, u8 reg, u8 *value)
{
	u32 sample;
	int ret = eta6965_logic(eta, ETA_OP_READ_REGISTER, reg, &sample);

	if (!ret)
		*value = sample;
	return ret;
}

static int eta6965_read_safety_state(struct eta6965_device *eta, u8 *reg05,
				   u8 *reg07)
{
	u32 state;
	int ret = eta6965_logic(eta, ETA_OP_READ_SAFETY_STATE, 0, &state);

	if (!ret) {
		*reg05 = state & 0xff;
		*reg07 = (state >> 8) & 0xff;
	}
	return ret;
}

static ssize_t safety_timer_status_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct eta6965_device *eta = dev_get_drvdata(dev);
	u8 reg05, reg07;
	int ret;

	if (!eta)
		return -ENODEV;
	ret = eta6965_read_safety_state(eta, &reg05, &reg07);
	if (ret)
		return ret;
	return sysfs_emit(buf,
		"reg05=0x%02x reg07=0x%02x enabled=%u duration_hours=%u regulation_half_rate=%u\n",
		reg05, reg07, !!(reg05 & REG05_EN_TIMER_MASK),
		(reg05 & REG05_CHG_TIMER_MASK) ? 10 : 5,
		!!(reg07 & REG07_TMR2X_EN_MASK));
}

static DEVICE_ATTR_RO(safety_timer_status);

static struct attribute *eta6965_attrs[] = {
	&dev_attr_safety_timer_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(eta6965);

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

static int eta_op_enable(struct charger_device *dev, bool en)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_ENABLE, en, NULL);
}


static int eta_op_send_ta_current_pattern(struct charger_device *dev, bool is_increase)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SEND_PE_PATTERN, is_increase, NULL);
}
static int eta_op_is_enabled(struct charger_device *dev, bool *en)
{
	u32 value;
	int ret = eta6965_logic(charger_get_data(dev), ETA_OP_IS_ENABLED, 0, &value);

	if (!ret)
		*en = value != 0;
	return ret;
}
static int eta_op_plug_in(struct charger_device *dev)  { return eta_op_enable(dev, true); }
static int eta_op_plug_out(struct charger_device *dev) { return eta_op_enable(dev, false); }

static int eta_op_set_ichg(struct charger_device *dev, u32 uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_ICHG, uA, NULL);
}
static int eta_op_get_ichg(struct charger_device *dev, u32 *uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_GET_ICHG, 0, uA);
}
static int eta_op_set_cv(struct charger_device *dev, u32 uV)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_CV, uV, NULL);
}
static int eta6965_get_cv_uV(struct eta6965_device *eta, u32 *uV)
{
	return eta6965_logic(eta, ETA_OP_GET_CV, 0, uV);
}
static int eta_op_get_cv(struct charger_device *dev, u32 *uV)
{
	return eta6965_get_cv_uV(charger_get_data(dev), uV);
}
static int eta_op_set_iinlim(struct charger_device *dev, u32 uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_IINLIM, uA, NULL);
}
static int eta6965_get_iinlim_uA(struct eta6965_device *eta, u32 *uA)
{
	return eta6965_logic(eta, ETA_OP_GET_IINLIM, 0, uA);
}
static int eta_op_get_iinlim(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);

	return eta6965_get_iinlim_uA(eta, uA);
}
static int eta_op_set_eoc_current(struct charger_device *dev, u32 uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_ITERM, uA, NULL);
}
static int eta_op_get_eoc_current(struct charger_device *dev, u32 *uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_GET_ITERM, 0, uA);
}
static int eta_op_set_mivr(struct charger_device *dev, u32 uV)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_VINDPM, uV, NULL);
}
static int eta_op_get_mivr(struct charger_device *dev, u32 *uV)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_GET_VINDPM, 0, uV);
}
static int eta_op_run_aicl(struct charger_device *dev, u32 *uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_RUN_AICL, 0, uA);
}
static int eta_op_kick_wdt(struct charger_device *dev)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_KICK_WATCHDOG, 0, NULL);
}
static int eta_op_enable_term(struct charger_device *dev, bool en)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_ENABLE_TERMINATION, en, NULL);
}

static int eta_op_enable_safety_timer(struct charger_device *dev, bool en)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_ENABLE_SAFETY_TIMER, en, NULL);
}

static int eta_op_is_safety_timer_enabled(struct charger_device *dev, bool *en)
{
	u32 value;
	int ret = eta6965_logic(charger_get_data(dev), ETA_OP_IS_SAFETY_TIMER_ENABLED, 0, &value);

	if (!ret)
		*en = value != 0;
	return ret;
}
static int eta6965_set_otg(struct eta6965_device *eta, bool en)
{
	return eta6965_logic(eta, ETA_OP_ENABLE_OTG, en, NULL);
}
static int eta_op_enable_otg(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	return eta6965_set_otg(eta, en);
}
static int eta_op_set_boost_ilim(struct charger_device *dev, u32 uA)
{
	return eta6965_logic(charger_get_data(dev), ETA_OP_SET_BOOST_LIMIT, uA, NULL);
}
static int eta_op_dump_registers(struct charger_device *dev)
{
	struct eta6965_device *eta = charger_get_data(dev);
	eta6965_dump_register(eta);
	return 0;
}

static int eta_regulator_call(struct regulator_dev *rdev, int operation)
{
	struct eta6965_platform *binding;
	u32 value;
	int ret = -ENODEV;

	mutex_lock(&eta6965_binding_lock);
	binding = eta6965_otg_binding;
	if (!binding || binding->rdev != rdev)
		goto unlock;
	if (operation == 0) {
		ret = eta6965_logic(binding->eta, ETA_OP_IS_OTG_ENABLED, 0, &value);
		if (!ret)
			ret = value;
	} else {
		ret = eta6965_set_otg(binding->eta, operation > 0);
	}

unlock:
	mutex_unlock(&eta6965_binding_lock);
	return ret;
}

static int eta_regulator_enable(struct regulator_dev *rdev)
{
	return eta_regulator_call(rdev, 1);
}

static int eta_regulator_disable(struct regulator_dev *rdev)
{
	return eta_regulator_call(rdev, -1);
}

static int eta_regulator_is_enabled(struct regulator_dev *rdev)
{
	return eta_regulator_call(rdev, 0);
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

static int eta_op_is_charging_done(struct charger_device *dev, bool *done)
{
	u32 value;
	int ret = eta6965_logic(charger_get_data(dev), ETA_OP_IS_CHARGING_DONE, 0, &value);

	if (!ret)
		*done = value != 0;
	return ret;
}

static int eta_op_event(struct charger_device *dev, u32 event, u32 args)
{
	struct eta6965_device *eta = charger_get_data(dev);
	struct eta6965_platform *binding;
	int ret = -ENODEV;

	mutex_lock(&eta6965_binding_lock);
	binding = eta6965_otg_binding;
	if (binding && binding->active && binding->eta == eta && binding->psy) {
		power_supply_changed(binding->psy);
		ret = 0;
	}
	mutex_unlock(&eta6965_binding_lock);
	return ret;
}

static const struct charger_ops eta6965_chg_ops = {
	.enable			= eta_op_enable,
	.send_ta_current_pattern = eta_op_send_ta_current_pattern,
	.is_enabled		= eta_op_is_enabled,
	.is_charging_done	= eta_op_is_charging_done,
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
	.enable_safety_timer	= eta_op_enable_safety_timer,
	.is_safety_timer_enabled = eta_op_is_safety_timer_enabled,
	.enable_otg		= eta_op_enable_otg,
	.set_boost_current_limit = eta_op_set_boost_ilim,
	.dump_registers		= eta_op_dump_registers,
	.event			= eta_op_event,
};

static const struct charger_properties eta6965_chg_props = {
	.alias_name = "eta6965",
};


static int eta6965_psy_read_property(void *context, int psp, int *value)
{
	struct eta6965_device *eta = context;
	u32 property, sample;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		property = ETA_PROPERTY_ONLINE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		property = ETA_PROPERTY_STATUS;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		property = ETA_PROPERTY_TYPE;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		property = ETA_PROPERTY_USB_TYPE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		property = ETA_PROPERTY_VOLTAGE_MAX;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		property = ETA_PROPERTY_CURRENT_MAX;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		property = ETA_PROPERTY_CONSTANT_CHARGE_VOLTAGE;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		property = ETA_PROPERTY_CHARGE_TERM_CURRENT;
		break;
	default:
		property = 0;
		break;
	}
	ret = eta6965_logic(eta, ETA_OP_GET_PROPERTY, property, &sample);
	if (ret)
		return ret;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		switch (sample) {
		case 0: sample = POWER_SUPPLY_STATUS_NOT_CHARGING; break;
		case 1: sample = POWER_SUPPLY_STATUS_CHARGING; break;
		case 2: sample = POWER_SUPPLY_STATUS_FULL; break;
		default: return -EINVAL;
		}
		break;
	case POWER_SUPPLY_PROP_TYPE:
		switch (sample) {
		case 0: sample = POWER_SUPPLY_TYPE_USB; break;
		case 1: sample = POWER_SUPPLY_TYPE_USB_CDP; break;
		case 2: sample = POWER_SUPPLY_TYPE_USB_DCP; break;
		default: return -EINVAL;
		}
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		switch (sample) {
		case 0: sample = POWER_SUPPLY_USB_TYPE_UNKNOWN; break;
		case 1: sample = POWER_SUPPLY_USB_TYPE_SDP; break;
		case 2: sample = POWER_SUPPLY_USB_TYPE_CDP; break;
		case 3: sample = POWER_SUPPLY_USB_TYPE_DCP; break;
		default: return -EINVAL;
		}
		break;
	default:
		break;
	}
	*value = sample;
	return 0;
}

static int eta6965_psy_get_property(struct power_supply *psy,
				    enum power_supply_property property,
				    union power_supply_propval *value)
{
	struct eta6965_platform *binding = power_supply_get_drvdata(psy);
	int ret = -ENODEV;

	mutex_lock(&eta6965_binding_lock);
	if (eta6965_otg_binding == binding && binding->active &&
	    binding->psy == psy) {
		if (property == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT) {
			value->intval = binding->eta->usb_input_current_limit;
			ret = 0;
		} else {
			ret = eta6965_psy_read_property(binding->eta, property,
						       &value->intval);
		}
	}
	mutex_unlock(&eta6965_binding_lock);
	return ret;
}

static int eta6965_psy_set_property(struct power_supply *psy,
				    enum power_supply_property property,
				    const union power_supply_propval *value)
{
	struct eta6965_platform *binding = power_supply_get_drvdata(psy);
	int ret = -ENODEV;

	if (property != POWER_SUPPLY_PROP_ONLINE &&
	    property != POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT)
		return -EINVAL;
	if (property == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT &&
	    (value->intval < 0 || value->intval > 500000))
		return -EINVAL;
	mutex_lock(&eta6965_binding_lock);
	if (eta6965_otg_binding == binding && binding->active &&
	    binding->psy == psy) {
		if (property == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT)
			binding->eta->usb_input_current_limit = value->intval;
		power_supply_changed(psy);
		ret = 0;
	}
	mutex_unlock(&eta6965_binding_lock);
	return ret;
}

static int eta6965_psy_is_writeable(struct power_supply *psy,
				    enum power_supply_property property)
{
	return property == POWER_SUPPLY_PROP_ONLINE ||
	       property == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT;
}

static const enum power_supply_property eta6965_psy_properties[] = {
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
};

static const struct power_supply_desc eta6965_psy_desc = {
	.name = "eta6965_chg",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = eta6965_psy_properties,
	.num_properties = ARRAY_SIZE(eta6965_psy_properties),
	.get_property = eta6965_psy_get_property,
	.set_property = eta6965_psy_set_property,
	.property_is_writeable = eta6965_psy_is_writeable,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP),
};

static int eta6965_hw_init(struct eta6965_device *eta)
{
	return eta6965_logic(eta, ETA_OP_INITIALIZE, 0, NULL);
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
	eta->usb_input_current_limit = 100000;
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

	eta6965_dump_register(eta);
	if (!strcmp(chg_name, "primary_chg")) {
		mutex_lock(&eta6965_binding_lock);
		if (g_eta6965) {
			mutex_unlock(&eta6965_binding_lock);
			charger_device_unregister(eta->chg_dev);
			return -EBUSY;
		}
		g_eta6965 = eta;
		mutex_unlock(&eta6965_binding_lock);
	}
	return 0;
}

static void eta6965_remove(struct i2c_client *client)
{
	struct eta6965_device *eta = i2c_get_clientdata(client);

	mutex_lock(&eta6965_binding_lock);
	if (g_eta6965 == eta)
		g_eta6965 = NULL;
	mutex_unlock(&eta6965_binding_lock);
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

static int eta6965_suspend(struct device *dev)
{
	struct eta6965_device *eta = i2c_get_clientdata(to_i2c_client(dev));

	if (eta) {
		mutex_lock(&eta->lock);
		WRITE_ONCE(eta->suspended, true);
		mutex_unlock(&eta->lock);
	}
	return 0;
}

static int eta6965_resume(struct device *dev)
{
	struct eta6965_device *eta = i2c_get_clientdata(to_i2c_client(dev));

	if (eta) {
		mutex_lock(&eta->lock);
		WRITE_ONCE(eta->suspended, false);
		mutex_unlock(&eta->lock);
	}
	return 0;
}

static const struct dev_pm_ops eta6965_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(eta6965_suspend, eta6965_resume)
};

static struct i2c_driver eta6965_driver = {
	.driver = { .name = "eta6965_charger", .of_match_table = eta6965_of_match,
		    .pm = pm_sleep_ptr(&eta6965_pm_ops),
		    .dev_groups = eta6965_groups },
	.probe = eta6965_probe,
	.remove = eta6965_remove,
	.id_table = eta6965_i2c_id,
};
static char *eta6965_supplied_to[] = {
	"mtk-master-charger",
};

static void eta6965_platform_release(struct eta6965_platform *binding)
{
	struct eta6965_device *eta = binding->eta;

	mutex_lock(&eta6965_binding_lock);
	binding->active = false;
	if (eta6965_otg_binding == binding)
		eta6965_otg_binding = NULL;
	mutex_unlock(&eta6965_binding_lock);
	mutex_lock(&eta->lock);
	if (eta->pinctrl == binding->pinctrl) {
		eta->pinctrl = NULL;
		eta->ce_low = NULL;
		eta->ce_high = NULL;
	}
	mutex_unlock(&eta->lock);
	if (binding->rdev)
		regulator_unregister(binding->rdev);
	if (binding->psy)
		power_supply_unregister(binding->psy);
	put_device(binding->supplier);
	kfree(binding);
}

static int eta6965_plat_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_config = {
		.of_node = pdev->dev.of_node,
		.supplied_to = eta6965_supplied_to,
		.num_supplicants = ARRAY_SIZE(eta6965_supplied_to),
	};
	struct regulator_config config = { .dev = &pdev->dev };
	struct eta6965_platform *binding;
	struct eta6965_device *eta;
	struct device *supplier = NULL;
	struct pinctrl *pinctrl;
	struct pinctrl_state *low, *high;
	struct regulator_dev *rdev;
	int ret;

	mutex_lock(&eta6965_binding_lock);
	if (g_eta6965)
		supplier = get_device(g_eta6965->dev);
	mutex_unlock(&eta6965_binding_lock);
	if (!supplier)
		return -EPROBE_DEFER;
	if (!device_trylock(supplier)) {
		ret = -EPROBE_DEFER;
		goto put;
	}
	if (!device_is_bound(supplier) ||
	    READ_ONCE(supplier->links.status) != DL_DEV_DRIVER_BOUND) {
		ret = -EPROBE_DEFER;
		goto unlock;
	}
	eta = dev_get_drvdata(supplier);
	mutex_lock(&eta6965_binding_lock);
	ret = !eta || g_eta6965 != eta ? -EPROBE_DEFER :
		eta6965_otg_binding ? -EBUSY : 0;
	mutex_unlock(&eta6965_binding_lock);
	if (ret)
		goto unlock;
	if (!device_link_add(&pdev->dev, supplier, DL_FLAG_AUTOREMOVE_CONSUMER)) {
		ret = -ENOMEM;
		goto unlock;
	}
	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding) {
		ret = -ENOMEM;
		goto unlock;
	}
	binding->eta = eta;
	binding->supplier = supplier;
	device_unlock(supplier);
	mutex_lock(&eta6965_binding_lock);
	if (eta6965_otg_binding) {
		mutex_unlock(&eta6965_binding_lock);
		ret = -EBUSY;
		goto release;
	}
	eta6965_otg_binding = binding;
	mutex_unlock(&eta6965_binding_lock);

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(pinctrl)) {
		low = pinctrl_lookup_state(pinctrl, "psc_chg_en_low");
		high = pinctrl_lookup_state(pinctrl, "psc_chg_en_high");
		if (!IS_ERR(low) && !IS_ERR(high)) {
			binding->pinctrl = pinctrl;
			mutex_lock(&eta->lock);
			eta->ce_low = low;
			eta->ce_high = high;
			eta->pinctrl = pinctrl;
			mutex_unlock(&eta->lock);
		}
	}
	if (!binding->pinctrl)
		dev_warn(&pdev->dev, "eta6965: no psc_chg_en pin states, CE left as is\n");
	psy_config.drv_data = binding;
	binding->psy = power_supply_register(&pdev->dev, &eta6965_psy_desc,
					     &psy_config);
	if (IS_ERR(binding->psy)) {
		ret = PTR_ERR(binding->psy);
		binding->psy = NULL;
		goto release;
	}
	rdev = regulator_register(&pdev->dev, &eta6965_otg_vbus_desc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		goto release;
	}
	mutex_lock(&eta6965_binding_lock);
	binding->rdev = rdev;
	binding->active = true;
	power_supply_changed(binding->psy);
	mutex_unlock(&eta6965_binding_lock);
	platform_set_drvdata(pdev, binding);
	return 0;

release:
	eta6965_platform_release(binding);
	return ret;
unlock:
	device_unlock(supplier);
put:
	put_device(supplier);
	return ret;
}

static void eta6965_plat_remove(struct platform_device *pdev)
{
	struct eta6965_platform *binding = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (binding)
		eta6965_platform_release(binding);
}

static const struct of_device_id eta6965_plat_of_match[] = {
	{ .compatible = "mediatek,eta6965_chg_driver" },
	{ }
};
MODULE_DEVICE_TABLE(of, eta6965_plat_of_match);

static struct platform_driver eta6965_plat_driver = {
	.probe = eta6965_plat_probe,
	.remove_new = eta6965_plat_remove,
	.driver = {
		.name = "eta6965_chg_plat",
		.of_match_table = eta6965_plat_of_match,
	},
};

static int __init mindone_eta_driver_register(void)
{
	int ret = i2c_add_driver(&eta6965_driver);

	if (ret)
		return ret < 0 ? ret : -EIO;
	ret = platform_driver_register(&eta6965_plat_driver);
	if (ret)
		i2c_del_driver(&eta6965_driver);
	return ret > 0 ? -EIO : ret;
}

static void __exit mindone_eta_driver_unregister(void)
{
	platform_driver_unregister(&eta6965_plat_driver);
	i2c_del_driver(&eta6965_driver);
}

module_init(mindone_eta_driver_register);
module_exit(mindone_eta_driver_unregister);

MODULE_AUTHOR("MindOne Contributors");
MODULE_DESCRIPTION("ETA6965 charger");
MODULE_LICENSE("GPL");
