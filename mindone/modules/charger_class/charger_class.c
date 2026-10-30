// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "charger_class.h"

static struct class *charger_class;
static struct workqueue_struct *charger_release_queue;

struct charger_registration {
	struct charger_device charger;
	struct work_struct release_work;
	char *alias_name;
};

static ssize_t name_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct charger_device *chg_dev = to_charger_device(dev);

	return snprintf(buf, 20, "%s\n",
		       chg_dev->props.alias_name ?
		       chg_dev->props.alias_name : "anonymous");
}

static void charger_registration_release(struct work_struct *work)
{
	struct charger_registration *registration =
		container_of(work, struct charger_registration, release_work);

	cleanup_srcu_struct(&registration->charger.evt_nh.srcu);
	kfree(registration->alias_name);
	kfree(registration);
	module_put(THIS_MODULE);
}

static void charger_device_release(struct device *dev)
{
	struct charger_device *chg_dev = to_charger_device(dev);
	struct charger_registration *registration =
		container_of(chg_dev, struct charger_registration, charger);

	queue_work(charger_release_queue, &registration->release_work);
}

int charger_dev_enable(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable)
		ret = ops->enable(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable);

int charger_dev_is_enabled(struct charger_device *chg_dev, bool *en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_enabled)
		ret = ops->is_enabled(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_enabled);

int charger_dev_plug_in(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->plug_in)
		ret = ops->plug_in(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_plug_in);

int charger_dev_plug_out(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->plug_out)
		ret = ops->plug_out(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_plug_out);

int charger_dev_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->event)
		ret = ops->event(chg_dev, event, args);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_do_event);

int charger_dev_enable_6pin_battery_charging(struct charger_device *chg_dev,
					     bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_6pin_battery_charging)
		ret = ops->enable_6pin_battery_charging(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_6pin_battery_charging);

int charger_dev_set_charging_current(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_charging_current)
		ret = ops->set_charging_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_charging_current);

int charger_dev_get_charging_current(struct charger_device *chg_dev, u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_charging_current)
		ret = ops->get_charging_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_charging_current);

int charger_dev_get_min_charging_current(struct charger_device *chg_dev,
					 u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_min_charging_current)
		ret = ops->get_min_charging_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_min_charging_current);

int charger_dev_enable_chip(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_chip)
		ret = ops->enable_chip(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_chip);

int charger_dev_is_chip_enabled(struct charger_device *chg_dev, bool *en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_chip_enabled)
		ret = ops->is_chip_enabled(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_chip_enabled);

int charger_dev_enable_direct_charging(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_direct_charging)
		ret = ops->enable_direct_charging(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_direct_charging);

int charger_dev_kick_direct_charging_wdt(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->kick_direct_charging_wdt)
		ret = ops->kick_direct_charging_wdt(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_kick_direct_charging_wdt);

int charger_dev_get_vbus(struct charger_device *chg_dev, u32 *vbus)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_vbus_adc)
		ret = ops->get_vbus_adc(chg_dev, vbus);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_vbus);

int charger_dev_get_ibus(struct charger_device *chg_dev, u32 *ibus)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_ibus_adc)
		ret = ops->get_ibus_adc(chg_dev, ibus);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_ibus);

int charger_dev_get_ibat(struct charger_device *chg_dev, u32 *ibat)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_ibat_adc)
		ret = ops->get_ibat_adc(chg_dev, ibat);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_ibat);

int charger_dev_get_temperature(struct charger_device *chg_dev, int *tchg_min,
		int *tchg_max)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_tchg_adc)
		ret = ops->get_tchg_adc(chg_dev, tchg_min,
						  tchg_max);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_temperature);

int charger_dev_set_input_current(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_input_current)
		ret = ops->set_input_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_input_current);

int charger_dev_get_input_current(struct charger_device *chg_dev, u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_input_current)
		ret = ops->get_input_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_input_current);

int charger_dev_get_min_input_current(struct charger_device *chg_dev, u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_min_input_current)
		ret = ops->get_min_input_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_min_input_current);

int charger_dev_set_eoc_current(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_eoc_current)
		ops->set_eoc_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_eoc_current);

int charger_dev_get_eoc_current(struct charger_device *chg_dev, u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_eoc_current)
		ret = ops->get_eoc_current(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_eoc_current);

int charger_dev_kick_wdt(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->kick_wdt)
		ret = ops->kick_wdt(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_kick_wdt);

int charger_dev_set_constant_voltage(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_constant_voltage)
		ret = ops->set_constant_voltage(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_constant_voltage);

int charger_dev_get_constant_voltage(struct charger_device *chg_dev, u32 *uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_constant_voltage)
		ret = ops->get_constant_voltage(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_constant_voltage);

int charger_dev_dump_registers(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->dump_registers)
		ret = ops->dump_registers(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_dump_registers);

int charger_dev_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_charging_done)
		ret = ops->is_charging_done(chg_dev, done);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_charging_done);

int charger_dev_enable_vbus_ovp(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_vbus_ovp)
		ret = ops->enable_vbus_ovp(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_vbus_ovp);

int charger_dev_set_mivr(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_mivr)
		ret = ops->set_mivr(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_mivr);

int charger_dev_get_mivr(struct charger_device *chg_dev, u32 *uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_mivr)
		ret = ops->get_mivr(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_mivr);

int charger_dev_enable_powerpath(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_powerpath)
		ret = ops->enable_powerpath(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_powerpath);

int charger_dev_is_powerpath_enabled(struct charger_device *chg_dev, bool *en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_powerpath_enabled)
		ret = ops->is_powerpath_enabled(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_powerpath_enabled);

int charger_dev_enable_safety_timer(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_safety_timer)
		ret = ops->enable_safety_timer(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_safety_timer);

int charger_dev_enable_hz(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_hz)
		ret = ops->enable_hz(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_hz);

int charger_dev_get_adc(struct charger_device *charger_dev,
	enum adc_channel chan, int *min, int *max)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->get_adc)
		ret = ops->get_adc(charger_dev, chan, min, max);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_adc);

int charger_dev_get_adc_accuracy(struct charger_device *charger_dev,
	enum adc_channel chan, int *min, int *max)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->get_adc_accuracy)
		ret = ops->get_adc_accuracy(charger_dev, chan,
							  min, max);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_adc_accuracy);

int charger_dev_is_safety_timer_enabled(struct charger_device *chg_dev,
					bool *en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_safety_timer_enabled)
		ret = ops->is_safety_timer_enabled(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_safety_timer_enabled);

int charger_dev_enable_termination(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_termination)
		ret = ops->enable_termination(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_termination);

int charger_dev_get_mivr_state(struct charger_device *chg_dev, bool *in_loop)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_mivr_state)
		ret = ops->get_mivr_state(chg_dev, in_loop);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_mivr_state);

int charger_dev_send_ta_current_pattern(struct charger_device *chg_dev,
					bool is_increase)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->send_ta_current_pattern)
		ret = ops->send_ta_current_pattern(chg_dev,
							     is_increase);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_send_ta_current_pattern);

int charger_dev_send_ta20_current_pattern(struct charger_device *chg_dev,
					  u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->send_ta20_current_pattern)
		ret = ops->send_ta20_current_pattern(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_send_ta20_current_pattern);

int charger_dev_reset_ta(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->reset_ta)
		ret = ops->reset_ta(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_reset_ta);

int charger_dev_set_pe20_efficiency_table(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_pe20_efficiency_table)
		ret = ops->set_pe20_efficiency_table(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_pe20_efficiency_table);

int charger_dev_enable_cable_drop_comp(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_cable_drop_comp)
		ret = ops->enable_cable_drop_comp(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_cable_drop_comp);

int charger_dev_set_direct_charging_ibusoc(struct charger_device *chg_dev,
					   u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_direct_charging_ibusoc)
		ret = ops->set_direct_charging_ibusoc(chg_dev,
								uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_direct_charging_ibusoc);

int charger_dev_set_direct_charging_vbusov(struct charger_device *chg_dev,
					   u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_direct_charging_vbusov)
		ret = ops->set_direct_charging_vbusov(chg_dev,
								uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_direct_charging_vbusov);

int charger_dev_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_ibusocp)
		ret = ops->set_ibusocp(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_ibusocp);

int charger_dev_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_vbusovp)
		ret = ops->set_vbusovp(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_vbusovp);

int charger_dev_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_ibatocp)
		ret = ops->set_ibatocp(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_ibatocp);

int charger_dev_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_vbatovp)
		ret = ops->set_vbatovp(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_vbatovp);

int charger_dev_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_vbatovp_alarm)
		ret = ops->set_vbatovp_alarm(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_vbatovp_alarm);

int charger_dev_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->reset_vbatovp_alarm)
		ret = ops->reset_vbatovp_alarm(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_reset_vbatovp_alarm);

int charger_dev_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_vbusovp_alarm)
		ret = ops->set_vbusovp_alarm(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_vbusovp_alarm);

int charger_dev_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->reset_vbusovp_alarm)
		ret = ops->reset_vbusovp_alarm(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_reset_vbusovp_alarm);

int charger_dev_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->is_vbuslowerr)
		ret = ops->is_vbuslowerr(chg_dev, err);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_is_vbuslowerr);

int charger_dev_init_chip(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->init_chip)
		ret = ops->init_chip(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_init_chip);

int charger_dev_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_chg_type_det)
		ret = ops->enable_chg_type_det(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_chg_type_det);

int charger_dev_enable_otg(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_otg)
		ret = ops->enable_otg(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_otg);

int charger_dev_enable_discharge(struct charger_device *chg_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->enable_discharge)
		ret = ops->enable_discharge(chg_dev, en);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_discharge);

int charger_dev_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->set_boost_current_limit)
		ret = ops->set_boost_current_limit(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_boost_current_limit);

int charger_dev_get_zcv(struct charger_device *chg_dev, u32 *uV)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->get_zcv)
		ret = ops->get_zcv(chg_dev, uV);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_zcv);

int charger_dev_run_aicl(struct charger_device *chg_dev, u32 *uA)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->run_aicl)
		ret = ops->run_aicl(chg_dev, uA);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_run_aicl);

int charger_dev_reset_eoc_state(struct charger_device *chg_dev)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->reset_eoc_state)
		ret = ops->reset_eoc_state(chg_dev);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_reset_eoc_state);

int charger_dev_safety_check(struct charger_device *chg_dev, u32 polling_ieoc)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!chg_dev)
		return ret;

	index = srcu_read_lock(&chg_dev->evt_nh.srcu);
	ops = smp_load_acquire(&chg_dev->ops);
	if (ops && ops->safety_check)
		ret = ops->safety_check(chg_dev, polling_ieoc);
	srcu_read_unlock(&chg_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_safety_check);

int charger_dev_notify(struct charger_device *chg_dev, int event)
{
	return srcu_notifier_call_chain(
		&chg_dev->evt_nh, event, &chg_dev->noti);
}
EXPORT_SYMBOL(charger_dev_notify);

int charger_dev_enable_usbid(struct charger_device *charger_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->enable_usbid)
		ret = ops->enable_usbid(charger_dev, en);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_usbid);

int charger_dev_set_usbid_rup(struct charger_device *charger_dev, u32 rup)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->set_usbid_rup)
		ret = ops->set_usbid_rup(charger_dev, rup);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_usbid_rup);

int charger_dev_set_usbid_src_ton(struct charger_device *charger_dev,
				  u32 src_ton)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->set_usbid_src_ton)
		ret = ops->set_usbid_src_ton(charger_dev,
							   src_ton);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_usbid_src_ton);

int charger_dev_enable_usbid_floating(struct charger_device *charger_dev,
				      bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->enable_usbid_floating)
		ret = ops->enable_usbid_floating(charger_dev, en);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_usbid_floating);

int charger_dev_enable_force_typec_otp(struct charger_device *charger_dev,
				       bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->enable_force_typec_otp)
		ret = ops->enable_force_typec_otp(charger_dev,
								en);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_force_typec_otp);

int charger_dev_get_ctd_dischg_status(struct charger_device *charger_dev,
				      u8 *status)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->get_ctd_dischg_status)
		ret = ops->get_ctd_dischg_status(charger_dev,
							       status);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_ctd_dischg_status);

int charger_dev_enable_hidden_mode(struct charger_device *charger_dev, bool en)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->enable_hidden_mode)
		ret = ops->enable_hidden_mode(charger_dev, en);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_enable_hidden_mode);

int charger_dev_set_property(struct charger_device *charger_dev,
			     enum charger_property prop,
			     union charger_propval *val)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->set_property)
		ret = ops->set_property(charger_dev, prop, val);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_set_property);

int charger_dev_get_property(struct charger_device *charger_dev,
			     enum charger_property prop,
			     union charger_propval *val)
{
	const struct charger_ops *ops;
	int index, ret = -EOPNOTSUPP;

	if (!charger_dev)
		return ret;

	index = srcu_read_lock(&charger_dev->evt_nh.srcu);
	ops = smp_load_acquire(&charger_dev->ops);
	if (ops && ops->get_property)
		ret = ops->get_property(charger_dev, prop, val);
	srcu_read_unlock(&charger_dev->evt_nh.srcu, index);
	return ret;
}
EXPORT_SYMBOL(charger_dev_get_property);

static DEVICE_ATTR_RO(name);

static struct attribute *charger_class_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group charger_group = {
	.attrs = charger_class_attrs,
};

static const struct attribute_group *charger_groups[] = {
	&charger_group,
	NULL,
};

int register_charger_device_notifier(struct charger_device *chg_dev,
				struct notifier_block *nb)
{
	int ret;

	ret = srcu_notifier_chain_register(&chg_dev->evt_nh, nb);
	return ret;
}
EXPORT_SYMBOL(register_charger_device_notifier);

int unregister_charger_device_notifier(struct charger_device *chg_dev,
				struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&chg_dev->evt_nh, nb);
}
EXPORT_SYMBOL(unregister_charger_device_notifier);

/**
 * charger_device_register - create and register a new object of
 *   charger_device class.
 * @name: the name of the new object
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use.
 * The methods may retrieve it by using charger_get_data(charger_dev).
 * @ops: the charger operations structure.
 *
 * Creates and registers new charger device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct charger_device *charger_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct charger_ops *ops,
		const struct charger_properties *props)
{
	struct charger_registration *registration;
	struct charger_device *chg_dev;
	static struct lock_class_key key;
	struct srcu_notifier_head *head;
	int rc;

	if (!name || !name[0])
		return ERR_PTR(-EINVAL);
	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(-ENODEV);
	registration = kzalloc(sizeof(*registration), GFP_KERNEL);
	if (!registration) {
		module_put(THIS_MODULE);
		return ERR_PTR(-ENOMEM);
	}
	chg_dev = &registration->charger;
	if (props && props->alias_name) {
		registration->alias_name = kstrdup(props->alias_name, GFP_KERNEL);
		if (!registration->alias_name) {
			rc = -ENOMEM;
			goto free_registration;
		}
		chg_dev->props.alias_name = registration->alias_name;
	}

	head = &chg_dev->evt_nh;
	mutex_init(&head->mutex);
	rc = init_srcu_struct(&head->srcu);
	if (rc)
		goto free_alias;
	head->head = NULL;
	lockdep_init_map(&(&head->srcu)->dep_map, "charger_callbacks", &key, 0);
	mutex_init(&chg_dev->ops_lock);
	INIT_WORK(&registration->release_work, charger_registration_release);
	device_initialize(&chg_dev->dev);
	chg_dev->dev.class = charger_class;
	chg_dev->dev.parent = parent;
	chg_dev->dev.release = charger_device_release;
	rc = dev_set_name(&chg_dev->dev, "%s", name);
	if (rc)
		goto put_registration;
	dev_set_drvdata(&chg_dev->dev, devdata);
	rc = device_add(&chg_dev->dev);
	if (rc)
		goto put_registration;
	smp_store_release(&chg_dev->ops, ops);
	return chg_dev;

put_registration:
	put_device(&chg_dev->dev);
	return ERR_PTR(rc);
free_alias:
	kfree(registration->alias_name);
free_registration:
	kfree(registration);
	module_put(THIS_MODULE);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL(charger_device_register);

/**
 * charger_device_unregister - unregisters a switching charger device
 * object.
 * @charger_dev: the switching charger device object to be unregistered
 * and freed.
 *
 * Unregisters a previously registered via charger_device_register object.
 */
void charger_device_unregister(struct charger_device *chg_dev)
{
	if (!chg_dev)
		return;

	mutex_lock(&chg_dev->ops_lock);
	smp_store_release(&chg_dev->ops, NULL);
	synchronize_srcu(&chg_dev->evt_nh.srcu);
	mutex_unlock(&chg_dev->ops_lock);
	device_unregister(&chg_dev->dev);
}
EXPORT_SYMBOL(charger_device_unregister);


static int charger_match_device_by_name(struct device *dev,
	const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct charger_device *get_charger_by_name(const char *name)
{
	struct device *dev;

	if (!name)
		return (struct charger_device *)NULL;
	dev = class_find_device(charger_class, NULL, name,
				charger_match_device_by_name);

	return dev ? to_charger_device(dev) : NULL;

}
EXPORT_SYMBOL(get_charger_by_name);

static void __exit charger_class_exit(void)
{
	class_destroy(charger_class);
	destroy_workqueue(charger_release_queue);
}

static int __init charger_class_init(void)
{
	charger_release_queue = alloc_workqueue("charger_release", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!charger_release_queue)
		return -ENOMEM;
	charger_class = class_create("switching_charger");
	if (IS_ERR(charger_class)) {
		pr_notice("Unable to create charger class; errno = %ld\n",
			PTR_ERR(charger_class));
		destroy_workqueue(charger_release_queue);
		return PTR_ERR(charger_class);
	}
	charger_class->dev_groups = charger_groups;
	return 0;
}

#if IS_BUILTIN(CONFIG_MTK_CHARGER)
subsys_initcall(charger_class_init);
#else
module_init(charger_class_init);
#endif
module_exit(charger_class_exit);

MODULE_DESCRIPTION("Switching Charger Class Device");
MODULE_AUTHOR("Patrick Chang <patrick_chang@richtek.com>");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
