// SPDX-License-Identifier: GPL-2.0
/*
 * eta6965_charger.c -- ETA6965 I2C charger driver (BQ2429x-family) for MindOne.
 * Reconstructed from a disassembly of our eta6965-charger.ko + reg-map; implements the MTK
 * charger_class. Drain fix built in (dump_register is a no-op, see DRAIN FIX below). Value
 * formulas/bitfields re-verified against the actual shipped .ko, fixing 2 register-address
 * bugs (IINLIM/EN_HIZ) and 1 safety bug (CHG_EN toggled the wrong bit); full cross-check in
 * eta6965-formulas-verified. On-device: not yet verified, see unverified markers below.
 */
#include <linux/platform_device.h>
#include <mindone/compat.h>
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

/* ===== Register map -- RE-VERIFIED by disassembly of the ACTUAL shipped .ko
 * (device/extracted/vendor_boot_b/ramdisk/lib/modules/eta6965-charger.ko, BuildID
 * aee1d97000acb5a3dbe9c31d3b52313fa622bdba) -- the real shipped binary, not a rebuild.
 * Full findings table: eta6965-formulas-verified. Found and fixed:
 *  1) IINLIM/EN_HIZ are actually in REG00, not REG0B (bug from an earlier RE session);
 *  2) CHG_EN is actually bit4 of REG01, NOT bit7 (bit7 = PFM enable, unrelated to charging) --
 *     without this fix, eta_op_enable/is_enabled toggled the wrong bit and did not control
 *     charging at all. */
#define ETA6965_REG_00		0x00	/* Input Source Control (was missing from the skeleton) */
#define  REG00_IINLIM_MASK	0x1f	/* bits0-4 (5-bit) -- FIXED: was REG0B_IINLIM_MASK (wrong register) */
#define  REG00_EN_HIZ_MASK	0x80	/* bit7 -- FIXED: was REG0B_EN_HIZ_MASK (wrong register) */
#define ETA6965_REG_01		0x01	/* Power-On Config */
#define  REG01_CHG_EN_MASK	0x10	/* bit4 -- FIXED (was 0x80/bit7 -- safety bug, see above).
					 * Confirmed twice independently in the disassembly:
					 * eta6965_is_enabled reads exactly `ubfx w,#4,#1`,
					 * eta6965_enable_charging writes exactly bit4 on both
					 * branches (en=1 and en=0). */
#define  REG01_PFM_MASK		0x80	/* bit7 -- this is PFM mode (eta6965_set_pfm), NOT chg_en
					 * (old bug). Unused (no direct equivalent in charger_ops). */
#define  REG01_OTG_CONFIG_MASK	0x20	/* bit5 */
#define  REG01_SYS_MIN_MASK	0x0e	/* bits1-3 */
#define  REG01_WDT_RST_MASK	0x40	/* bit6 -- confirmed by eta6965_reset_watch_dog_timer */
#define ETA6965_REG_02		0x02	/* Charge Current */
#define  REG02_ICHG_MASK	0x3f	/* bits0-5. Register is 6-bit, but only codes 0-54 are
					 * actually valid (55 entries in CS_VTH) -- see
					 * ICHG_MAX_CODE below. */
#define  REG02_BOOST_LIM_MASK	0x80	/* bit7 */
#define ETA6965_REG_03		0x03	/* Pre/Term Current */
#define  REG03_ITERM_MASK	0x0f	/* bits0-3 */
#define  REG03_IPRECHG_MASK	0xf0	/* bits4-7. Warning: bitfield confirmed
					 * (eta6965_set_iprechg), but the value formula is NOT
					 * confirmed -- the low-level function is exported but
					 * never called anywhere in the shipped .ko (unlike
					 * iterm/ichg/vreg/iinlim, which have a real caller with
					 * a table). Not guessing -- unused in ops. */
#define ETA6965_REG_04		0x04	/* Charge Voltage */
#define  REG04_VREG_MASK	0xf8	/* bits3-7 (5-bit @sh3) */
#define ETA6965_REG_05		0x05	/* Timer */
#define  REG05_WATCHDOG_MASK	0x30	/* bits4-5 */
#define  REG05_EN_TERM_MASK	0x80	/* bit7 */
#define ETA6965_REG_06		0x06	/* Input/Boost */
#define  REG06_VINDPM_MASK	0x0f	/* bits0-3. Warning: value formula NOT confirmed by
					 * disassembly -- see VINDPM_BASE_UV/STEP_UV below. */
#define  REG06_BOOSTV_MASK	0x30	/* bits4-5. Warning: value formula NOT confirmed
					 * (set_boostv is exported but never called, same as
					 * iprechg). */
#define ETA6965_REG_08		0x08	/* System Status (RO) -- VBUS_STAT/CHRG_STAT/PG_STAT.
					 * ETA6965-V1.2.pdf Table 13 (p. 28). MINDONE-ETA6965-PSY
					 * (CHRDET-2908): used for a real power_supply, not
					 * invented -- a direct register read, filled
					 * autonomously by the chip (POR=X, RESET BY=NA -- live
					 * status, not a config field). */
#define  REG08_VBUS_STAT_MASK	0xe0	/* bits 7:5 */
#define  REG08_VBUS_STAT_SHIFT	5
					/* 000 no input · 001 USB Host SDP · 010 USB CDP (1.5A) ·
					 * 011 USB DCP (2.4A) · 101 Unknown Adapter (500mA) ·
					 * 110 Non-Standard Adapter (1/2/2.1/2.4A) · 111 OTG */
#define  REG08_CHRG_STAT_MASK	0x18	/* bits 4:3 */
#define  REG08_CHRG_STAT_SHIFT	3
					/* 00 not charging · 01 pre-charge · 10 fast charge ·
					 * 11 charge termination */
#define  REG08_PG_STAT_MASK	0x04	/* bit 2: 0=Power Not Good, 1=Power Good */
#define ETA6965_REG_0A		0x0a	/* System Status (RO) */
#define ETA6965_REG_0B		0x0b	/* Reg Reset -- FIXED: holds ONLY reg_rst@bit7.
					 * IINLIM/EN_HIZ do NOT belong here (see REG00 above,
					 * old bug from an earlier RE session). */
#define  REG0B_REG_RST_MASK	0x80	/* bit7 */

struct eta6965_device {
	struct i2c_client	*client;
	struct device		*dev;
	struct mutex		lock;
	struct power_supply	*psy;
	struct charger_device	*chg_dev;	/* MTK charger_class */
	bool			otg_active;	/* boost/OTG mode (the device itself drives VBUS) */
	struct regulator_dev	*otg_vbus_rdev;	/* usb-otg-vbus, see USB-OTG-VBUS REGULATOR (F891) */
};

/* ===== I2C access (config_interface(reg,val,mask,shift) from the disassembly) ===== */
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

/* ===== DRAIN FIX: dump_register disarmed (was 12 I2C reads/cycle -> 21% CPU) ===== */
static void eta6965_dump_register(struct eta6965_device *eta)
{
	/* Intentionally empty: the vendor left a debug loop running in production.
	 * Our fix is to not read the registers. (Replaces the fix_charger kprobe
	 * module -- the fix now lives in the source.) */
}

/* ===== Value conversions -- RE-VERIFIED by direct disassembly of the shipped .ko: its
 * rodata tables (CS_VTH/INPUT_CS_VTH/VBAT_CV_VTH/ITERM_CTT) and the algorithms
 * eta6965_set_current/get_current/set_cv_voltage/set_input_current/get_input_current/
 * set_ieoc/get_ieoc were disassembled. Full table with addresses/literals:
 * eta6965-formulas-verified. =====
 *
 * ICHG (REG02[5:0]) -- CS_VTH: strictly linear, CONFIRMED (was already correct before the
 * re-check). Register is 6-bit (0-63), but the table ends at code 54 (55 entries: 0..54);
 * codes 55-63 do not exist in the firmware (the vendor getter BRKs on them). */
#define ICHG_BASE_UA	0
#define ICHG_STEP_UA	60000		/* 60 mA/step -- confirmed (55 entries, all deltas =60000) */
#define ICHG_MAX_CODE	54		/* 55 entries (0..54); max = 3.24 A */

/* CV/VREG (REG04[7:3]) -- VBAT_CV_VTH: strictly linear. FIXED: was base=3856000/
 * step=16000 (step was exactly half the real value!), actually base=3848000/step=32000.
 * Extracted from the decision tree in eta6965_set_cv_voltage: 24 of 25 points are exact
 * comparison literals in the disassembly, every delta between adjacent codes = 32000
 * without a single exception (code0 extrapolated with the same step). The old formula was
 * UNSAFE: a 4.20V request resolved to code 21 -- but code21 actually =
 * 3848000+21*32000=4.52V (Li-ion overcharge, far past any sane limit). */
#define CV_BASE_UV	3848000		/* code0 = 3.848V -- FIXED (was 3856000) */
#define CV_STEP_UV	32000		/* 32 mV/step -- FIXED (was 16000, half the real value) */
#define CV_MAX_CODE	24		/* 25 entries (0..24); max = 4.616V */

/* IINLIM (REG00[4:0]) -- FIXED: register was wrongly REG0B, actually REG00 (see #define
 * above). INPUT_CS_VTH is NOT strictly linear: the one gap is between code22(2300mA) and
 * code23(2500mA) -- 2400mA is skipped; before and after it's exactly 100mA/step. The
 * base+step*code formula gives the wrong code (+-100mA shift) for uA>=2400000, so it was
 * replaced with an exact table (31 entries, taken 1:1 from the disassembly; the x10 scale
 * is confirmed by add+lsl (*5,<<1) instructions in the getter). */
static const u32 iinlim_table_uA[] = {
	 100000,  200000,  300000,  400000,  500000,  600000,  700000,  800000,
	 900000, 1000000, 1100000, 1200000, 1300000, 1400000, 1500000, 1600000,
	1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000,	/* codes 0-22: 100mA/step */
	2500000, 2600000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000, /* 23-30: gap! */
};
#define IINLIM_MAX_CODE	(ARRAY_SIZE(iinlim_table_uA) - 1)	/* 30 */

/* ITERM/EOC (REG03[3:0], termination current) -- ADDED, was missing from the skeleton.
 * ITERM_CTT: strictly linear, base=60000 step=60000, WITHOUT the x10 scale (unlike
 * ICHG/IINLIM) -- confirmed: eta6965_set_ieoc passes uA DIRECTLY into the table search
 * with no multiplication, eta6965_get_ieoc returns table[code] with no multiplication
 * either. */
#define ITERM_BASE_UA	60000
#define ITERM_STEP_UA	60000
#define ITERM_MAX_CODE	15		/* 16 entries (0..15); max = 960 mA */

/* VINDPM (REG06[3:0]) -- warning: NOT confirmed by disassembly (unlike everything above).
 * In the vendor code, eta6965_set_vindpm_voltage is an empty stub (`mov w0,#0; ret`, 16
 * bytes) -- the physical value is NEVER written dynamically in this .ko. The VINDPM_REG
 * table exists in .rodata (26 entries, 3900-6400mV, 100mV step), but has NOT A SINGLE
 * reference (relocation) anywhere in the file -- dead data. The formula below matches the
 * first 16/16 needed entries of this dead table (weak indirect confirmation, NOT
 * execution-verified) -- left in as best-effort rather than removed. If a device becomes
 * available, cross-check with a real REG06 write. */
#define VINDPM_BASE_UV	3900000	/* REG06: VINDPM base -- unverified, see comment above */
#define VINDPM_STEP_UV	100000	/* 100 mV/step -- unverified */

/* Vendor's algorithm for looking up a code from a value (eta6965_set_current/
 * set_input_current): find the LARGEST table[i] <= target; below table[0], clamp to 0;
 * above table[N-1], clamp to N-1. The table must be monotonically increasing (all our
 * tables are). */
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

/* ===== charger_class ops (dev → eta via charger_get_data) ===== */
static int eta_op_enable(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	return eta6965_update_bits(eta, ETA6965_REG_01, REG01_CHG_EN_MASK, en ? REG01_CHG_EN_MASK : 0);
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
		code = ICHG_MAX_CODE;	/* FIXED: a bare `& mask` used to be able to wrap the
					 * code (e.g. 66 & 0x3f = 2 -> silently under-set the
					 * current instead of clamping) */
	return eta6965_update_bits(eta, ETA6965_REG_02, REG02_ICHG_MASK, (u8)code);
}
static int eta_op_get_ichg(struct charger_device *dev, u32 *uA)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_02, &v);
	if (!ret) {
		u8 code = v & REG02_ICHG_MASK;
		if (code > ICHG_MAX_CODE)
			code = ICHG_MAX_CODE;	/* codes 55-63 are not valid (vendor code BRKs here) */
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
static int eta_op_get_cv(struct charger_device *dev, u32 *uV)
{
	struct eta6965_device *eta = charger_get_data(dev);
	u8 v; int ret = eta6965_read_byte(eta, ETA6965_REG_04, &v);
	if (!ret) {
		u8 code = (v & REG04_VREG_MASK) >> 3;
		if (code > CV_MAX_CODE)
			code = CV_MAX_CODE;	/* guard: codes 25-31 are outside VBAT_CV_VTH (25 entries) */
		*uV = CV_BASE_UV + code * CV_STEP_UV;
	}
	return ret;
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
	u8 code = (uV - VINDPM_BASE_UV) / VINDPM_STEP_UV;
	return eta6965_update_bits(eta, ETA6965_REG_06, REG06_VINDPM_MASK, code & REG06_VINDPM_MASK);
}
static int eta_op_kick_wdt(struct charger_device *dev)
{
	struct eta6965_device *eta = charger_get_data(dev);
	int ret = eta6965_update_bits(eta, ETA6965_REG_01, REG01_WDT_RST_MASK, REG01_WDT_RST_MASK);
	/* The vendor also re-arms the watchdog timeout (REG05[5:4]=3, max) on every kick --
	 * confirmed in eta6965_reset_watch_dog_timer (2 back-to-back config_interface calls).
	 * Not critical to the counter reset itself, but we mirror it for identical behavior. */
	eta6965_update_bits(eta, ETA6965_REG_05, REG05_WATCHDOG_MASK, REG05_WATCHDOG_MASK);
	return ret;
}
static int eta_op_enable_term(struct charger_device *dev, bool en)
{
	struct eta6965_device *eta = charger_get_data(dev);
	return eta6965_update_bits(eta, ETA6965_REG_05, REG05_EN_TERM_MASK, en ? REG05_EN_TERM_MASK : 0);
}
/* ===== OTG / boost (REG01 OTG_CONFIG bit5, REG02 BOOST_LIM bit7) =====
 * enable_otg switches the chip into boost mode: the chip itself drives VBUS onto USB
 * (host power). otg_active is needed by the upper layer (mtk_charger) so it does NOT
 * count this VBUS as charging (otherwise online=1 falsely). online itself is reported
 * by the framework, not here -- see the OTG-ONLINE TODO.
 *
 * The shared switching core is factored into eta6965_set_otg() -- used by BOTH
 * charger_ops.enable_otg (eta_op_enable_otg, right below) AND the regulator_ops of the
 * "usb-otg-vbus" node (see USB-OTG-VBUS REGULATOR below, F891 in the fact log): this is
 * the SAME physical switch (REG01 bit5), now with two consumers instead of two places
 * toggling the same bit independently. */
static int eta6965_set_otg(struct eta6965_device *eta, bool en)
{
	int ret;

	/* The vendor (eta6965_enable_otg) NEVER enables OTG_CONFIG and CHG_EN at the same
	 * time: entering OTG first clears CHG_EN (REG01 bit4), leaving OTG restores it.
	 * Confirmed by disassembly (2 config_interface calls on each branch). We mirror
	 * this for safety (charge+boost at once is a mode the hardware does not support). */
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
	/* REG02 bit7: 0=low / 1=high boost limit. CONFIRMED by disassembly of
	 * eta6965_set_boost_current_limit: `cmp w1,#0x124f7f (1199999); cset w1,hi` --
	 * the threshold is exactly >=1200000uA (1.2A), bit7. Formula was correct before
	 * the re-check too. */
	u8 v = (uA >= 1200000) ? REG02_BOOST_LIM_MASK : 0;
	return eta6965_update_bits(eta, ETA6965_REG_02, REG02_BOOST_LIM_MASK, v);
}
/* DRAIN FIX: dump_registers = no-op (was 12 I2C reads/cycle -> 21% CPU). Replaces the kprobe hack. */
static int eta_op_dump_registers(struct charger_device *dev)
{
	struct eta6965_device *eta = charger_get_data(dev);
	eta6965_dump_register(eta); /* empty */
	return 0;
}

/* ===== USB-OTG-VBUS REGULATOR (F890/F891, usb-role-switch-track) =====
 * eta6965_otg_vbus never becomes a struct device via OF unless this driver calls
 * devm_regulator_register() itself -- otherwise fw_devlink holds extcon_usb's
 * mandatory vbus-supply reference unresolved forever and its probe never runs, so
 * the USB role switch never fires. Full analysis and the stock-driver byte-level
 * comparison: usb-role-switch-track. Unlike the vendor driver, a failed
 * register here is non-fatal -- charging matters more than USB-role diagnostics.
 */
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
	.of_match	= "usb-otg-vbus",	/* = regulator-compatible in the DTB,
						 * device/dts/vendor_boot_b-platform.dts:5969 */
	.ops		= &eta6965_otg_vbus_ops,
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.n_voltages	= 1,
	.fixed_uV	= 5000000,	/* 5V, see the disassembly analysis above */
};

static const struct charger_ops eta6965_chg_ops = {
	.enable			= eta_op_enable,
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
	.kick_wdt		= eta_op_kick_wdt,
	.enable_termination	= eta_op_enable_term,
	.enable_otg		= eta_op_enable_otg,
	.set_boost_current_limit = eta_op_set_boost_ilim,
	.dump_registers		= eta_op_dump_registers,
};

static const struct charger_properties eta6965_chg_props = {
	.alias_name = "eta6965",
};

/* ===== MINDONE-ETA6965-PSY (CHRDET-2908, F3014) =====
 * Registers power_supply "eta6965_chg" at of_node=/eta6965_chg, the phandle two existing
 * consumers reference (mtk_chg_det.c "bc12" F3013, extcon-mtk-usb.ko "charger" F1645-F1647)
 * but that nothing provided before this fix. REG08 is read synchronously on demand; no new
 * polling. Registration is deliberately silent -- NO power_supply_changed() in probe():
 * CHARGER-ROOTCAUSE §3 documents a prior incident where calling it there caused a
 * reboot. The only changed() call is in set_property(ONLINE), driven by a real CHRDET IRQ.
 */
static void eta6965_reg08_to_usb_type(u8 vbus_stat, enum power_supply_type *type,
				       enum power_supply_usb_type *usb_type)
{
	switch (vbus_stat) {
	case 0x1:	/* 001 USB Host SDP */
		*type = POWER_SUPPLY_TYPE_USB;
		*usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case 0x2:	/* 010 USB CDP (1.5A) */
		*type = POWER_SUPPLY_TYPE_USB_CDP;
		*usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case 0x3:	/* 011 USB DCP (2.4A) */
	case 0x5:	/* 101 Unknown Adapter (500mA) -- closest to the DCP family */
	case 0x6:	/* 110 Non-Standard Adapter (1/2/2.1/2.4A) -- also DCP family */
		*type = POWER_SUPPLY_TYPE_USB_DCP;
		*usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case 0x0:	/* 000 no input */
	case 0x7:	/* 111 OTG -- we are the source, not a consumer; input type undefined */
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
	u32 uA;
	int ret;

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
		case 0x1:	/* pre-charge */
		case 0x2:	/* fast charge */
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 0x3:	/* termination */
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:	/* 0x0 not charging, but power is present */
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
		/* ETA6965 has no VBUS ADC channel (no .get_vbus_adc in charger_ops, and the
		 * datasheet documents no such register -- F3011/CHARGER-ROOTCAUSE.md) --
		 * a live measurement does not physically exist here. The 5V nominal is the
		 * same constant already used in this file for the usb-otg-vbus regulator
		 * (eta6965_otg_vbus_desc .fixed_uV = 5000000, see above). The real VBUS
		 * measurement comes from a DIFFERENT psy ("mtk_charger_type"/VOLTAGE_NOW,
		 * mtk_chg_det.c, via IIO pmic_vbus). */
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = eta6965_get_iinlim_uA(eta, &uA);
		if (ret)
			return ret;
		val->intval = uA;
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
	/* The switch itself is not needed -- ONLINE is already read autonomously from
	 * PG_STAT (see get_property). This is the ENTRY POINT for an external kick from
	 * mtk_chg_det.c (do_charger_detect(), F3013) after a REAL CHRDET IRQ -- the only
	 * place in this file that calls power_supply_changed(), and it is NOT
	 * probe()/plat_probe() (see the warning in the block header above). */
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
	MINDONE_PSY_USB_TYPES(eta6965_psy_usb_types,
			      BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) | BIT(POWER_SUPPLY_USB_TYPE_SDP) |
			      BIT(POWER_SUPPLY_USB_TYPE_CDP) | BIT(POWER_SUPPLY_USB_TYPE_DCP)),
};

/* ===== Probe: i2c + charger_class registration ===== */
/* Pointer for the platform launch: the regulator ops go through the same
 * chip as charging. */
static struct eta6965_device *g_eta6965;

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

	/* F976: the board has TWO eta6965 chips (primary and secondary, both at address
	 * 0x6b on different buses). Take the name FROM the DT, not hardcoded: otherwise
	 * the second instance would register as "primary_chg" and collide with the
	 * first, while the charging driver kept looking for "secondary_chg" and never
	 * finding it. */
	if (of_property_read_string(eta->dev->of_node, "charger_name", &chg_name))
		chg_name = "primary_chg";	/* DT is silent -- previous behavior */

	eta->chg_dev = charger_device_register(chg_name, eta->dev, eta,
					       &eta6965_chg_ops, &eta6965_chg_props);
	if (IS_ERR(eta->chg_dev))
		return PTR_ERR(eta->chg_dev);
	dev_info(eta->dev, "eta6965: registered as \"%s\"\n", chg_name);

	/* USB-OTG-VBUS REGULATOR -- registration MOVED to the platform launch.
	 * Reason (F940, measured on device): regulator_of_get_init_node() looks for the
	 * subnode among the descendants of config->dev's node. Our dev here is the I2C
	 * client with node /soc/i2c@11017000/eta6965@6b, while the usb-otg-vbus subnode
	 * lives under the ROOT node /eta6965_chg. No match -> of_node is empty ->
	 * fw_devlink does not recognize the supplier. Passing config.of_node is useless:
	 * on this path the kernel does not look at it at all
	 * (drivers/regulator/of_regulator.c:520 -- only config->dev is used).
	 * So we register from the PLATFORM device eta6965_chg -- exactly like stock. */
	/* The pointer for the usb-otg-vbus regulator is set ONLY by the primary chip.
	 * The secondary one would overwrite it, and the regulator would go through the
	 * wrong chip. */
	if (!strcmp(chg_name, "primary_chg"))
		g_eta6965 = eta;

	eta6965_dump_register(eta); /* no-op: drain fix */
	return 0;
}

/* On kernel 6.1 the I2C remove handler returns void, not int (a kernel interface
 * change). The module had still been built against the old tree, so this
 * mismatch only surfaced now. */
static void eta6965_remove(struct i2c_client *client)
{
	struct eta6965_device *eta = i2c_get_clientdata(client);

	if (eta && !IS_ERR(eta->chg_dev))
		charger_device_unregister(eta->chg_dev);
}

static const struct of_device_id eta6965_of_match[] = {
	{ .compatible = "mediatek,eta6965_chg_driver" },
	{ .compatible = "mediatek,eta6965_chg_sec" },
	/* F976: the SECOND chip's I2C node is declared in the overlay exactly this way
	 * (dtbo_a-overlay0.dts:1099). Without this line it did not bind, and
	 * charger_init_algo() kept looping forever, re-registering notifiers. */
	{ .compatible = "mediatek,eta6965_sec" },
	/* the primary chip is declared with a short compatible (dtbo_a-overlay0.dts:1034) */
	{ .compatible = "mediatek,eta6965" },
	{ }
};
MODULE_DEVICE_TABLE(of, eta6965_of_match);

static const struct i2c_device_id eta6965_i2c_id[] = { { "eta6965", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, eta6965_i2c_id);

static struct i2c_driver eta6965_driver = {
	.driver = { .name = "eta6965_charger", .of_match_table = eta6965_of_match },
	MINDONE_I2C_PROBE(eta6965_probe),
	.remove = eta6965_remove,
	.id_table = eta6965_i2c_id,
};
/* --- platform part: usb-otg-vbus regulator only (F940) ------------ */
static int eta6965_plat_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct power_supply_config psy_cfg = { };

	/* The chip must already be up over I2C: the regulator ops go through it.
	 * If not yet -- ask the kernel to retry later, this is the normal path. */
	if (!g_eta6965)
		return -EPROBE_DEFER;

	/* MINDONE-ETA6965-PSY (CHRDET-2908, F3014) -- registration is SILENT, no
	 * power_supply_changed() (see the block comment on the psy code above). */
	if (!g_eta6965->psy) {
		psy_cfg.drv_data = g_eta6965;
		psy_cfg.of_node = pdev->dev.of_node;
		g_eta6965->psy = power_supply_register(&pdev->dev, &eta6965_psy_desc,
							&psy_cfg);
		if (IS_ERR(g_eta6965->psy)) {
			dev_err(&pdev->dev,
				"eta6965: MINDONE-ETA6965-PSY registration failed (%ld)\n",
				PTR_ERR(g_eta6965->psy));
			g_eta6965->psy = NULL;
			/* Non-critical -- same as the regulator below: charging matters
			 * more than diagnostics. */
		} else {
			dev_info(&pdev->dev,
				 "eta6965: MINDONE-ETA6965-PSY power supply registered, of_node=%pOF\n",
				 pdev->dev.of_node);
		}
	}

	config.dev = &pdev->dev;	/* node /eta6965_chg -- usb-otg-vbus is its own subnode */
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
