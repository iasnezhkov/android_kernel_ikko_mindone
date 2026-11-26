// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

struct charger_device { int id; };
struct chg_limit_setting { int cv; };
struct mtk_charger {
	struct charger_device *chg1_dev;
	struct chg_limit_setting setting;
	bool is_chg_done;
	bool term_en;
	u8 term_ready_cnt;
	u8 false_eoc_retry;
};

#define chr_err(fmt, ...) do { } while (0)
#define chr_debug(fmt, ...) do { } while (0)

static struct {
	u32 hw_cv_uV;
	u32 iterm_uA;
	int vbat_mV;
	int ibat_mA;
	bool hw_term_en;
	bool hw_chg_en;
	int term_calls;
	int enable_calls;
	int disable_calls;
	int msleep_calls;
} sim;

static void sim_reset(void)
{
	memset(&sim, 0, sizeof(sim));
	sim.hw_cv_uV = 4424000;
	sim.iterm_uA = 120000;
	sim.hw_chg_en = true;
}

static int charger_dev_get_constant_voltage(struct charger_device *d, u32 *uV)
{
	(void)d;
	*uV = sim.hw_cv_uV;
	return 0;
}

static int charger_dev_get_eoc_current(struct charger_device *d, u32 *uA)
{
	(void)d;
	*uA = sim.iterm_uA;
	return 0;
}

static int charger_dev_enable_termination(struct charger_device *d, bool en)
{
	(void)d;
	sim.hw_term_en = en;
	sim.term_calls++;
	return 0;
}

static int charger_dev_enable(struct charger_device *d, bool en)
{
	(void)d;
	sim.hw_chg_en = en;
	if (en)
		sim.enable_calls++;
	else
		sim.disable_calls++;
	return 0;
}

static int get_battery_voltage(struct mtk_charger *info) { (void)info; return sim.vbat_mV; }
static int get_battery_current(struct mtk_charger *info) { (void)info; return sim.ibat_mA; }
static void msleep(unsigned int ms) { (void)ms; sim.msleep_calls++; }
