// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>

#include "policy.h"

static int eta_status(int ret)
{
	return ret > 0 ? -EIO : ret;
}

static int eta_read(void *context, u8 reg, u8 *value)
{
	int ret;

	mindone_eta_io_lock(context);
	if (mindone_eta_io_suspended(context))
		ret = -ENODATA;
	else
		ret = eta_status(mindone_eta_io_read(context, reg, value));
	mindone_eta_io_unlock(context);
	return ret;
}

static int eta_update_locked(void *context, u8 reg, u8 mask, u8 value,
			     bool verify)
{
	u8 previous;
	int ret;

	if (mindone_eta_io_suspended(context))
		return -ENODATA;
	ret = eta_status(mindone_eta_io_read(context, reg, &previous));
	if (ret)
		return ret;
	ret = eta_status(mindone_eta_io_write(context, reg,
					    (previous & ~mask) | (value & mask)));
	if (ret || !verify)
		return ret;
	ret = eta_status(mindone_eta_io_read(context, reg, &previous));
	if (ret)
		return ret;
	return (previous & mask) == (value & mask) ? 0 : -EIO;
}

static int eta_update(void *context, u8 reg, u8 mask, u8 value, bool verify)
{
	int ret;

	mindone_eta_io_lock(context);
	ret = eta_update_locked(context, reg, mask, value, verify);
	mindone_eta_io_unlock(context);
	return ret;
}

static int eta_enable(void *context, bool enabled)
{
	u8 value;
	int ret;

	mindone_eta_io_lock(context);
	if (mindone_eta_io_suspended(context)) {
		ret = -ENODATA;
		goto unlock;
	}
	if (enabled) {
		ret = eta_status(mindone_eta_io_read(context, 1, &value));
		if (ret)
			goto unlock;
		if (value & 0x20) {
			ret = -EBUSY;
			goto unlock;
		}
	}
	ret = eta_update_locked(context, 1, 0x10, enabled ? 0x10 : 0, true);
	if (!ret)
		ret = eta_status(mindone_eta_io_pin(context, enabled));
unlock:
	mindone_eta_io_unlock(context);
	return ret;
}

static int eta_otg(void *context, bool enabled)
{
	int ret;

	mindone_eta_io_lock(context);
	if (enabled) {
		ret = eta_update_locked(context, 1, 0x10, 0, true);
		if (ret)
			goto unlock;
	}
	ret = eta_update_locked(context, 1, 0x20, enabled ? 0x20 : 0, true);
	if (!ret && !enabled)
		ret = eta_update_locked(context, 1, 0x10, 0x10, true);
unlock:
	mindone_eta_io_unlock(context);
	return ret;
}

static int eta_pe_step(void *context, u8 ichg, u8 iinlim, u32 milliseconds)
{
	int ret;

	ret = eta_update(context, 2, 0x3f, ichg, false);
	if (ret)
		return ret;
	ret = eta_update(context, 0, 0x1f, iinlim, false);
	if (!ret && milliseconds)
		mindone_eta_io_sleep(context, milliseconds);
	return ret;
}

static int eta_pe_pattern(void *context, bool increase)
{
	static const u32 times[2][6] = {
		{ 281, 281, 281, 85, 85, 485 },
		{ 85, 85, 281, 281, 281, 485 },
	};
	u8 off = increase ? 9 : 1;
	unsigned int i;
	int ret;

	mindone_eta_io_lock(context);
	ret = eta_status(mindone_eta_io_pin(context, true));
	mindone_eta_io_unlock(context);
	if (ret)
		return ret;
	mindone_eta_io_sleep(context, 85);
	for (i = 0; i < ARRAY_SIZE(times[0]); i++) {
		ret = eta_pe_step(context, off, 0, 85);
		if (ret)
			return ret;
		ret = eta_pe_step(context, 9, 4, times[increase][i]);
		if (ret)
			return ret;
	}
	ret = eta_pe_step(context, off, 0, 50);
	if (ret)
		return ret;
	ret = eta_pe_step(context, 9, 4, increase ? 200 : 0);
	if (ret)
		return ret;
	mindone_eta_io_sleep(context, 3000);
	return 0;
}

static u8 eta_encode(u32 value, u32 base, u32 step, u32 limit)
{
	return min((value > base ? value - base : 0) / step, limit);
}

static u32 eta_decode_iinlim(u8 value)
{
	return 100000 + (value & 0x1f) * 100000;
}

static u32 eta_decode_cv(u8 value)
{
	return 3848000 + min_t(u32, (value & 0xf8) >> 3, 24) * 32000;
}

static u32 eta_decode_iterm(u8 value)
{
	return 60000 + (value & 0x0f) * 60000;
}

static int eta_aicl(void *context, u32 *output)
{
	u8 original, good = 0, code, value;
	int ret, cleanup;

	ret = eta_read(context, 0, &original);
	if (ret)
		return ret;
	original &= 0x1f;
	ret = eta_update(context, 0, 0x1f, 0, false);
	if (ret)
		return ret;
	mindone_eta_io_sleep(context, 50);
	for (code = 1; code <= original; code++) {
		ret = eta_update(context, 0, 0x1f, code, false);
		if (ret)
			break;
		mindone_eta_io_sleep(context, 50);
		ret = eta_read(context, 8, &value);
		if (ret || !(value & 4))
			break;
		ret = eta_read(context, 10, &value);
		if (ret || (value & 0x40))
			break;
		good = code;
	}
	cleanup = eta_update(context, 0, 0x1f, good, true);
	if (ret)
		return ret;
	if (cleanup)
		return cleanup;
	*output = eta_decode_iinlim(good);
	return 0;
}

static int eta_safety_state(void *context, u32 *output)
{
	u8 timer, regulation;
	int ret;

	mindone_eta_io_lock(context);
	if (mindone_eta_io_suspended(context)) {
		ret = -ENODATA;
		goto unlock;
	}
	ret = eta_status(mindone_eta_io_read(context, 5, &timer));
	if (ret)
		goto unlock;
	ret = eta_status(mindone_eta_io_read(context, 7, &regulation));
	if (!ret)
		*output = timer | ((u32)regulation << 8);
unlock:
	mindone_eta_io_unlock(context);
	return ret;
}

static int eta_initialize(void *context)
{
	static const struct {
		u8 reg;
		u8 mask;
		u8 value;
		bool verify;
	} settings[] = {
		{ 6, 0xc0, 0x80, false },
		{ 0, 0x9f, 0, true },
		{ 0, 0x60, 0x60, false },
		{ 1, 1, 1, false },
		{ 2, 0x80, 0x80, false },
		{ 3, 0xf0, 0x80, false },
		{ 5, 0x0c, 0x0c, true },
		{ 5, 0x30, 0, false },
		{ 7, 3, 3, false },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(settings); i++) {
		ret = eta_update(context, settings[i].reg, settings[i].mask,
				 settings[i].value, settings[i].verify);
		if (ret)
			return ret;
	}
	return 0;
}

static int eta_get_property(void *context, u32 property, u32 *output)
{
	u8 state, vbus, value;
	int ret;

	if (mindone_eta_io_suspended(context))
		return -ENODATA;
	ret = eta_read(context, 8, &state);
	if (ret)
		return ret;
	vbus = (state >> 5) & 7;
	switch (property) {
	case ETA_PROPERTY_ONLINE:
		*output = !!(state & 4);
		return 0;
	case ETA_PROPERTY_STATUS:
		value = (state >> 3) & 3;
		*output = !(state & 4) ? 0 :
			  value == 3 ? 2 : value == 1 || value == 2;
		return 0;
	case ETA_PROPERTY_TYPE:
		*output = vbus == 2 ? 1 : vbus == 3 || vbus == 6 ? 2 : 0;
		return 0;
	case ETA_PROPERTY_USB_TYPE:
		*output = vbus == 1 ? 1 : vbus == 2 ? 2 :
			  vbus == 3 || vbus == 5 || vbus == 6 ? 3 : 0;
		return 0;
	case ETA_PROPERTY_VOLTAGE_MAX:
		*output = 5000000;
		return 0;
	case ETA_PROPERTY_CURRENT_MAX:
		ret = eta_read(context, 0, &value);
		if (!ret)
			*output = eta_decode_iinlim(value);
		return ret;
	case ETA_PROPERTY_CONSTANT_CHARGE_VOLTAGE:
		ret = eta_read(context, 4, &value);
		if (!ret)
			*output = eta_decode_cv(value);
		return ret;
	case ETA_PROPERTY_CHARGE_TERM_CURRENT:
		ret = eta_read(context, 3, &value);
		if (!ret)
			*output = eta_decode_iterm(value);
		return ret;
	default:
		return -EINVAL;
	}
}

static int eta_execute(void *context, u32 operation, u32 argument, u32 *output)
{
	u8 value, reg;
	int ret, companion;

	switch (operation) {
	case ETA_OP_READ_REGISTER:
		if (argument > U8_MAX)
			return -EINVAL;
		ret = eta_read(context, argument, &value);
		if (!ret)
			*output = value;
		return ret;
	case ETA_OP_ENABLE:
		return eta_enable(context, argument != 0);
	case ETA_OP_SET_ICHG:
		return eta_update(context, 2, 0x3f,
				  eta_encode(argument, 0, 60000, 50), false);
	case ETA_OP_SET_CV:
		return eta_update(context, 4, 0xf8,
				  eta_encode(argument, 3848000, 32000, 24) << 3,
				  false);
	case ETA_OP_SET_IINLIM:
		return eta_update(context, 0, 0x9f,
				  argument < 100000 ? 0x80 :
				  eta_encode(argument, 100000, 100000, 31), true);
	case ETA_OP_SET_ITERM:
		return eta_update(context, 3, 0x0f,
				  eta_encode(argument, 60000, 60000, 15), false);
	case ETA_OP_SET_VINDPM:
		return eta_update(context, 6, 0x0f,
				  eta_encode(argument, 3900000, 100000, 15), false);
	case ETA_OP_SET_BOOST_LIMIT:
		return eta_update(context, 2, 0x80,
				  argument >= 1200000 ? 0x80 : 0, false);
	case ETA_OP_RUN_AICL:
		return eta_aicl(context, output);
	case ETA_OP_KICK_WATCHDOG:
		ret = eta_update(context, 1, 0x40, 0x40, false);
		companion = eta_update(context, 5, 0x30, 0x30, false);
		return ret ? ret : companion;
	case ETA_OP_ENABLE_TERMINATION:
		return eta_update(context, 5, 0x80, argument ? 0x80 : 0, false);
	case ETA_OP_ENABLE_SAFETY_TIMER:
		return eta_update(context, 5, argument ? 0x0c : 8,
				  argument ? 0x0c : 0, true);
	case ETA_OP_ENABLE_OTG:
		return eta_otg(context, argument != 0);
	case ETA_OP_SEND_PE_PATTERN:
		return eta_pe_pattern(context, argument != 0);
	case ETA_OP_READ_SAFETY_STATE:
		return eta_safety_state(context, output);
	case ETA_OP_GET_PROPERTY:
		return eta_get_property(context, argument, output);
	case ETA_OP_INITIALIZE:
		return eta_initialize(context);
	case ETA_OP_IS_ENABLED:
	case ETA_OP_IS_OTG_ENABLED:
		reg = 1;
		break;
	case ETA_OP_GET_ICHG:
		reg = 2;
		break;
	case ETA_OP_GET_CV:
		reg = 4;
		break;
	case ETA_OP_GET_IINLIM:
		reg = 0;
		break;
	case ETA_OP_GET_ITERM:
		reg = 3;
		break;
	case ETA_OP_GET_VINDPM:
		reg = 6;
		break;
	case ETA_OP_IS_SAFETY_TIMER_ENABLED:
		reg = 5;
		break;
	case ETA_OP_IS_CHARGING_DONE:
		if (mindone_eta_io_suspended(context))
			return -ENODATA;
		reg = 8;
		break;
	default:
		return -EINVAL;
	}
	ret = eta_read(context, reg, &value);
	if (ret)
		return ret;
	switch (operation) {
	case ETA_OP_IS_ENABLED:
		*output = !!(value & 0x10);
		break;
	case ETA_OP_IS_OTG_ENABLED:
		*output = !!(value & 0x20);
		break;
	case ETA_OP_GET_ICHG:
		*output = min_t(u32, value & 0x3f, 50) * 60000;
		break;
	case ETA_OP_GET_CV:
		*output = eta_decode_cv(value);
		break;
	case ETA_OP_GET_IINLIM:
		*output = value & 0x80 ? 0 : eta_decode_iinlim(value);
		break;
	case ETA_OP_GET_ITERM:
		*output = eta_decode_iterm(value);
		break;
	case ETA_OP_GET_VINDPM:
		*output = 3900000 + (value & 0x0f) * 100000;
		break;
	case ETA_OP_IS_SAFETY_TIMER_ENABLED:
		*output = !!(value & 8);
		break;
	case ETA_OP_IS_CHARGING_DONE:
		*output = (value & 4) && ((value >> 3) & 3) == 3;
		break;
	}
	return 0;
}

int mindone_eta_call(void *context, u32 operation, u32 argument, u32 *output)
{
	u32 value = 0;
	int ret;

	if (!context || !output)
		return -EINVAL;
	ret = eta_execute(context, operation, argument, &value);
	if (!ret)
		*output = value;
	return ret;
}
