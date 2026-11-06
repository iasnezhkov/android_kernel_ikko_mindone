// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <linux/errno.h>

#include "policy.h"

struct chip {
	u8 regs[16];
	u8 sticky_clear[16];
	int suspended;
	int fail_read_reg;
	int fail_read_after;
	int fail_write_reg;
	int fail_write_reg_b;
	int fail_status;
	int fail_status_b;
	int pin_status;
	int depth;
	int max_depth;
	int reads;
};

static char trace[16384];
static int failures;

static void record(const char *format, ...)
{
	size_t used = strlen(trace);
	va_list args;

	va_start(args, format);
	vsnprintf(trace + used, sizeof(trace) - used, format, args);
	va_end(args);
}

static void expect(int condition, const char *name)
{
	if (!condition) {
		printf("FAIL %s\ntrace: %s\n", name, trace);
		failures++;
	}
}

static void reset(struct chip *chip)
{
	memset(chip, 0, sizeof(*chip));
	chip->fail_read_reg = -1;
	chip->fail_write_reg = -1;
	chip->fail_write_reg_b = -1;
	trace[0] = 0;
}

void mindone_eta_io_lock(void *context)
{
	struct chip *chip = context;

	chip->depth++;
	if (chip->depth > chip->max_depth)
		chip->max_depth = chip->depth;
}

void mindone_eta_io_unlock(void *context)
{
	((struct chip *)context)->depth--;
}

u8 mindone_eta_io_suspended(void *context)
{
	return ((struct chip *)context)->suspended;
}

int mindone_eta_io_read(void *context, u8 reg, u8 *value)
{
	struct chip *chip = context;

	expect(chip->depth == 1, "register reads happen under the I/O lock");
	chip->reads++;
	if (reg == chip->fail_read_reg && chip->reads > chip->fail_read_after)
		return chip->fail_status;
	*value = chip->regs[reg];
	return 0;
}

int mindone_eta_io_write(void *context, u8 reg, u8 value)
{
	struct chip *chip = context;

	expect(chip->depth == 1, "register writes happen under the I/O lock");
	record("W%u=%02x ", reg, value);
	if (reg == chip->fail_write_reg)
		return chip->fail_status;
	if (reg == chip->fail_write_reg_b)
		return chip->fail_status_b;
	chip->regs[reg] = value & ~chip->sticky_clear[reg];
	return 0;
}

int mindone_eta_io_pin(void *context, u8 enabled)
{
	struct chip *chip = context;

	expect(chip->depth == 1, "CE pin changes happen under the I/O lock");
	record("P%u ", enabled);
	return chip->pin_status;
}

void mindone_eta_io_sleep(void *context, u32 milliseconds)
{
	expect(((struct chip *)context)->depth == 0, "sleeps happen outside the I/O lock");
	record("S%u ", milliseconds);
}

static int call(struct chip *chip, u32 operation, u32 argument, u32 *output)
{
	int ret = mindone_eta_call(chip, operation, argument, output);

	expect(chip->depth == 0, "every operation releases the I/O lock");
	return ret;
}

static void check_currents(void)
{
	struct chip chip;
	u32 value = 7;

	reset(&chip);
	expect(call(&chip, ETA_OP_SET_ICHG, 3200000, &value) == 0 && (chip.regs[2] & 0x3f) == 50, "ICHG saturates at code 50");
	expect(value == 0, "set operations report zero");
	expect(call(&chip, ETA_OP_GET_ICHG, 0, &value) == 0 && value == 3000000, "ICHG decodes 50 as 3 A");
	chip.regs[2] = 0x3f;
	expect(call(&chip, ETA_OP_GET_ICHG, 0, &value) == 0 && value == 3000000, "ICHG decode clamps codes above 50");
	expect(call(&chip, ETA_OP_SET_CV, 4400000, &value) == 0 && chip.regs[4] == (17 << 3), "CV encodes 4.4 V");
	expect(call(&chip, ETA_OP_GET_CV, 0, &value) == 0 && value == 3848000 + 17 * 32000, "CV decodes");
	expect(call(&chip, ETA_OP_SET_CV, 1000000, &value) == 0 && chip.regs[4] == 0, "CV below base saturates to zero");
	chip.regs[4] = 0xf8;
	expect(call(&chip, ETA_OP_GET_CV, 0, &value) == 0 && value == 3848000 + 24 * 32000, "CV decode clamps at 24");
	expect(call(&chip, ETA_OP_SET_IINLIM, 2000000, &value) == 0 && (chip.regs[0] & 0x1f) == 19, "IINLIM encodes 2 A");
	expect(call(&chip, ETA_OP_GET_IINLIM, 0, &value) == 0 && value == 2000000, "IINLIM decodes");
	expect(call(&chip, ETA_OP_SET_IINLIM, 9000000, &value) == 0 && (chip.regs[0] & 0x1f) == 31, "IINLIM saturates at 31");
	expect(call(&chip, ETA_OP_SET_ITERM, 240000, &value) == 0 && (chip.regs[3] & 0x0f) == 3, "ITERM encodes");
	expect(call(&chip, ETA_OP_GET_ITERM, 0, &value) == 0 && value == 240000, "ITERM decodes");
	expect(call(&chip, ETA_OP_SET_VINDPM, 4600000, &value) == 0 && (chip.regs[6] & 0x0f) == 7, "VINDPM encodes");
	expect(call(&chip, ETA_OP_GET_VINDPM, 0, &value) == 0 && value == 4600000, "VINDPM decodes");
	expect(call(&chip, ETA_OP_SET_BOOST_LIMIT, 1200000, &value) == 0 && (chip.regs[2] & 0x80), "boost limit 1.2 A sets the bit");
	expect(call(&chip, ETA_OP_SET_BOOST_LIMIT, 1199999, &value) == 0 && !(chip.regs[2] & 0x80), "boost limit below 1.2 A clears the bit");
	chip.regs[0] = 0xe0;
	call(&chip, ETA_OP_SET_IINLIM, 100000, &value);
	expect(chip.regs[0] == 0xe0, "IINLIM update keeps unrelated bits");
}

static void check_reads(void)
{
	struct chip chip;
	u32 value = 7;

	reset(&chip);
	chip.regs[9] = 0x5a;
	expect(call(&chip, ETA_OP_READ_REGISTER, 9, &value) == 0 && value == 0x5a, "register read returns the value");
	expect(call(&chip, ETA_OP_READ_REGISTER, 256, &value) == -EINVAL, "register numbers above 255 are rejected");
	expect(mindone_eta_call(NULL, ETA_OP_GET_CV, 0, &value) == -EINVAL, "NULL context is rejected");
	expect(mindone_eta_call(&chip, ETA_OP_GET_CV, 0, NULL) == -EINVAL, "NULL output is rejected");
	expect(call(&chip, 99, 0, &value) == -EINVAL, "unknown operation is rejected");
	chip.suspended = 1;
	value = 7;
	expect(call(&chip, ETA_OP_GET_CV, 0, &value) == -ENODATA && value == 7, "suspended reads return ENODATA and keep output");
	expect(call(&chip, ETA_OP_SET_CV, 4400000, &value) == -ENODATA && !trace[0], "suspended writes return ENODATA without I/O");
	expect(call(&chip, ETA_OP_IS_CHARGING_DONE, 0, &value) == -ENODATA, "charging done is ENODATA while suspended");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_ONLINE, &value) == -ENODATA, "properties are ENODATA while suspended");

	reset(&chip);
	chip.fail_read_reg = 4;
	chip.fail_status = 3;
	value = 7;
	expect(call(&chip, ETA_OP_GET_CV, 0, &value) == -EIO && value == 7, "positive read status maps to EIO");
	chip.fail_status = -EREMOTEIO;
	expect(call(&chip, ETA_OP_GET_CV, 0, &value) == -EREMOTEIO, "read errno is returned");

	reset(&chip);
	chip.regs[8] = 0x04 | (3 << 3) | (6 << 5);
	expect(call(&chip, ETA_OP_IS_CHARGING_DONE, 0, &value) == 0 && value == 1, "charging done needs power good and state 3");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_ONLINE, &value) == 0 && value == 1, "online follows power good");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_STATUS, &value) == 0 && value == 2, "status 3 maps to full");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_TYPE, &value) == 0 && value == 2, "VBUS 6 maps to type 2");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_USB_TYPE, &value) == 0 && value == 3, "VBUS 6 maps to USB type 3");
	chip.regs[8] = 0x04 | (1 << 3) | (2 << 5);
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_STATUS, &value) == 0 && value == 1, "status 1 maps to charging");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_TYPE, &value) == 0 && value == 1, "VBUS 2 maps to type 1");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_USB_TYPE, &value) == 0 && value == 2, "VBUS 2 maps to USB type 2");
	chip.regs[8] = 0x04 | (2 << 3);
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_STATUS, &value) == 0 && value == 1, "status 2 maps to charging");
	chip.regs[8] = (3 << 3) | (1 << 5);
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_STATUS, &value) == 0 && value == 0, "no power good means not charging");
	expect(call(&chip, ETA_OP_IS_CHARGING_DONE, 0, &value) == 0 && value == 0, "no power good means not done");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_USB_TYPE, &value) == 0 && value == 1, "VBUS 1 maps to USB type 1");
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_VOLTAGE_MAX, &value) == 0 && value == 5000000, "voltage max is 5 V");
	chip.regs[0] = 4;
	expect(call(&chip, ETA_OP_GET_PROPERTY, ETA_PROPERTY_CURRENT_MAX, &value) == 0 && value == 500000, "current max decodes REG00");
	expect(call(&chip, ETA_OP_GET_PROPERTY, 42, &value) == -EINVAL, "unknown property is rejected");
}

static void check_watchdog_and_timer(void)
{
	struct chip chip;
	u32 value;

	reset(&chip);
	expect(call(&chip, ETA_OP_KICK_WATCHDOG, 0, &value) == 0 && (chip.regs[1] & 0x40) && (chip.regs[5] & 0x30) == 0x30, "watchdog kick writes both registers");
	reset(&chip);
	chip.fail_write_reg = 1;
	chip.fail_status = -EIO;
	expect(call(&chip, ETA_OP_KICK_WATCHDOG, 0, &value) == -EIO, "first watchdog error is returned");
	expect(strstr(trace, "W5=30") != NULL, "companion write still happens after a first failure");
	reset(&chip);
	chip.fail_write_reg = 5;
	chip.fail_status = -ETIMEDOUT;
	expect(call(&chip, ETA_OP_KICK_WATCHDOG, 0, &value) == -ETIMEDOUT, "companion error is returned after a good first write");
	reset(&chip);
	chip.fail_write_reg = 1;
	chip.fail_status = -EIO;
	chip.fail_write_reg_b = 5;
	chip.fail_status_b = -ETIMEDOUT;
	expect(call(&chip, ETA_OP_KICK_WATCHDOG, 0, &value) == -EIO, "first error wins when both watchdog writes fail");

	reset(&chip);
	expect(call(&chip, ETA_OP_ENABLE_SAFETY_TIMER, 1, &value) == 0 && (chip.regs[5] & 0x0c) == 0x0c, "safety timer enable sets both bits");
	expect(call(&chip, ETA_OP_IS_SAFETY_TIMER_ENABLED, 0, &value) == 0 && value == 1, "safety timer reads enabled");
	expect(call(&chip, ETA_OP_ENABLE_SAFETY_TIMER, 0, &value) == 0 && (chip.regs[5] & 0x0c) == 0x04, "safety timer disable clears only bit 3");
	reset(&chip);
	chip.sticky_clear[5] = 0x08;
	expect(call(&chip, ETA_OP_ENABLE_SAFETY_TIMER, 1, &value) == -EIO, "safety timer verify mismatch is EIO");
	reset(&chip);
	expect(call(&chip, ETA_OP_ENABLE_TERMINATION, 1, &value) == 0 && (chip.regs[5] & 0x80), "termination enable");
	expect(call(&chip, ETA_OP_ENABLE_TERMINATION, 0, &value) == 0 && !(chip.regs[5] & 0x80), "termination disable");
	reset(&chip);
	chip.regs[5] = 0x12;
	chip.regs[7] = 0x34;
	expect(call(&chip, ETA_OP_READ_SAFETY_STATE, 0, &value) == 0 && value == 0x3412, "safety state packs REG05 and REG07");
}

static void check_enable_and_otg(void)
{
	struct chip chip;
	u32 value;

	reset(&chip);
	chip.regs[0] = 0x80;
	expect(call(&chip, ETA_OP_ENABLE, 1, &value) == 0, "charging enable succeeds");
	expect(!(chip.regs[0] & 0x80) && (chip.regs[1] & 0x10), "enable clears HIZ and sets CHG_CONFIG");
	expect(strstr(trace, "P1") != NULL, "enable drives the CE pin");
	reset(&chip);
	chip.regs[1] = 0x20;
	expect(call(&chip, ETA_OP_ENABLE, 1, &value) == -EBUSY && !trace[0], "enable refuses while OTG is on");
	reset(&chip);
	chip.regs[1] = 0x30;
	expect(call(&chip, ETA_OP_ENABLE, 0, &value) == 0 && !(chip.regs[1] & 0x10) && strstr(trace, "P0"), "disable while OTG is on is allowed");
	reset(&chip);
	chip.pin_status = 2;
	expect(call(&chip, ETA_OP_ENABLE, 1, &value) == -EIO, "positive CE pin status maps to EIO");
	reset(&chip);
	chip.sticky_clear[1] = 0x10;
	expect(call(&chip, ETA_OP_ENABLE, 1, &value) == -EIO && !strstr(trace, "P1"), "CHG_CONFIG verify failure skips the pin");

	reset(&chip);
	chip.regs[1] = 0x10;
	expect(call(&chip, ETA_OP_ENABLE_OTG, 1, &value) == 0 && (chip.regs[1] & 0x30) == 0x20, "OTG on clears charging first");
	expect(call(&chip, ETA_OP_IS_OTG_ENABLED, 0, &value) == 0 && value == 1, "OTG reads enabled");
	expect(call(&chip, ETA_OP_ENABLE_OTG, 0, &value) == 0 && (chip.regs[1] & 0x30) == 0x10, "OTG off restores charging");
	expect(call(&chip, ETA_OP_IS_ENABLED, 0, &value) == 0 && value == 1, "charging reads enabled");
	reset(&chip);
	chip.regs[1] = 0x10;
	chip.sticky_clear[1] = 0x20;
	expect(call(&chip, ETA_OP_ENABLE_OTG, 1, &value) == -EIO, "OTG verify failure is EIO");
}

static void check_aicl(void)
{
	struct chip chip;
	u32 value = 7;

	reset(&chip);
	chip.regs[0] = 0xe0 | 5;
	chip.regs[8] = 0x04;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == 0 && value == 600000, "AICL settles on the highest good code");
	expect((chip.regs[0] & 0x1f) == 5 && (chip.regs[0] & 0xe0) == 0xe0, "AICL leaves the good code and other bits");
	expect(!strncmp(trace, "W0=e0 S50 W0=e1 S50 ", 20), "AICL starts from code zero and steps up");

	reset(&chip);
	chip.regs[0] = 6;
	chip.regs[8] = 0x04;
	chip.regs[10] = 0x40;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == 0 && value == 100000 && (chip.regs[0] & 0x1f) == 0, "DPM on the first step keeps code zero");

	reset(&chip);
	chip.regs[0] = 6;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == 0 && value == 100000, "missing power good stops at code zero");

	reset(&chip);
	chip.regs[0] = 6;
	chip.regs[8] = 0x04;
	chip.fail_read_reg = 10;
	chip.fail_read_after = 6;
	chip.fail_status = -EIO;
	value = 7;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == -EIO && value == 7, "acquisition error is returned");
	expect((chip.regs[0] & 0x1f) >= 1, "cleanup restores the last good code after an acquisition error");

	reset(&chip);
	chip.regs[0] = 2;
	chip.regs[8] = 0x04;
	chip.sticky_clear[0] = 0x02;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == -EIO, "cleanup verify failure is returned");

	reset(&chip);
	chip.fail_read_reg = 0;
	chip.fail_status = -ETIMEDOUT;
	expect(call(&chip, ETA_OP_RUN_AICL, 0, &value) == -ETIMEDOUT && !trace[0], "initial read failure writes nothing");
}

static const char *pe_trace(int increase)
{
	static const unsigned int times[2][6] = {
		{ 281, 281, 281, 85, 85, 485 },
		{ 85, 85, 281, 281, 281, 485 },
	};
	static char expected[1024];
	unsigned int off = increase ? 9 : 1;
	size_t used;
	int i;

	used = snprintf(expected, sizeof(expected), "P1 S85 ");
	for (i = 0; i < 6; i++)
		used += snprintf(expected + used, sizeof(expected) - used, "W2=%02x W0=00 S85 W2=09 W0=04 S%u ", off, times[increase][i]);
	used += snprintf(expected + used, sizeof(expected) - used, "W2=%02x W0=00 S50 W2=09 W0=04 ", off);
	snprintf(expected + used, sizeof(expected) - used, "%sS3000 ", increase ? "S200 " : "");
	return expected;
}

static void check_pe_and_init(void)
{
	struct chip chip;
	u32 value;
	int writes = 0;
	const char *p;

	reset(&chip);
	expect(call(&chip, ETA_OP_SEND_PE_PATTERN, 1, &value) == 0, "PE+ pattern succeeds");
	expect(!strncmp(trace, "P1 S85 ", 7), "PE starts by driving CE and waiting");
	expect(!strcmp(trace, pe_trace(1)), "PE+ follows the exact increase sequence");
	reset(&chip);
	expect(call(&chip, ETA_OP_SEND_PE_PATTERN, 0, &value) == 0, "PE- pattern succeeds");
	expect(!strcmp(trace, pe_trace(0)), "PE- follows the exact decrease sequence");
	reset(&chip);
	chip.pin_status = -EIO;
	expect(call(&chip, ETA_OP_SEND_PE_PATTERN, 1, &value) == -EIO && !strstr(trace, "W"), "PE stops when the pin fails");
	reset(&chip);
	chip.fail_write_reg = 0;
	chip.fail_status = -EIO;
	expect(call(&chip, ETA_OP_SEND_PE_PATTERN, 1, &value) == -EIO && !strstr(trace, "S3000"), "PE stops on a register error");

	reset(&chip);
	expect(call(&chip, ETA_OP_INITIALIZE, 0, &value) == 0, "initialize succeeds");
	for (p = trace; (p = strchr(p, 'W')); p++)
		writes++;
	expect(writes == 9, "initialize writes nine settings");
	expect(chip.regs[6] == 0x80 && chip.regs[0] == 0x60 && chip.regs[1] == 1 && chip.regs[2] == 0x80, "initialize sets REG06/00/01/02");
	expect(chip.regs[3] == 0x80 && chip.regs[5] == 0x0c && chip.regs[7] == 3, "initialize sets REG03/05/07");
	reset(&chip);
	chip.sticky_clear[5] = 0x04;
	expect(call(&chip, ETA_OP_INITIALIZE, 0, &value) == -EIO && !strstr(trace, "W7="), "initialize stops on the safety timer verify");
}

int main(void)
{
	check_currents();
	check_reads();
	check_watchdog_and_timer();
	check_enable_and_otg();
	check_aicl();
	check_pe_and_init();
	printf("%s %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
