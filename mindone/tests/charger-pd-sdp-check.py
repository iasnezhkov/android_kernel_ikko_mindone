# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'charger-pd-sdp'
BASIC = 'mindone/modules/mtk_charger_framework/mtk_basic_charger.c'
PD_HAL = 'mindone/modules/mtk_pd_charging/mtk_pd_hal.c'
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
         '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-unused-but-set-variable',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']

STUBS = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
#define min(a, b) ((a) < (b) ? (a) : (b))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define IS_ERR_OR_NULL(p) ((p) == NULL)
#define IS_ERR(p) (0)
#define IS_ENABLED(x) (0)
#define chr_err(...) ((void)0)
#define chr_info(...) ((void)0)
#define chr_debug(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_name(d) "pdc"

enum {
	POWER_SUPPLY_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_TYPE_USB,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_CDP,
};

enum {
	POWER_SUPPLY_USB_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
};

enum {
	MTK_PD_CONNECT_NONE = 0,
	MTK_PD_CONNECT_TYPEC_ONLY_SNK,
	MTK_PD_CONNECT_PE_READY_SNK,
	MTK_PD_CONNECT_PE_READY_SNK_PD30,
	MTK_PD_CONNECT_PE_READY_SNK_APDO,
};

enum {
	TYPEC_RP_LEVEL = 1,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT = 1,
	POWER_SUPPLY_PROP_TEMP = 2,
};

enum {
	SINGLE_CHARGER = 0,
	DUAL_CHARGERS_IN_SERIES = 1,
};

enum {
	PE_ID = 1,
	PE2_ID = 2,
	PDC_ID = 4,
	MAX_ALG_NO = 2,
};

enum {
	ALG_NOT_READY = 0,
	ALG_READY = 1,
	ALG_RUNNING = 2,
};

enum {
	CHG1_SETTING = 0,
	CHG2_SETTING = 1,
	DVCHG1_SETTING = 2,
	DVCHG2_SETTING = 3,
	TEMP_ABOVE_T4 = 1,
	TEMP_T3_TO_T4 = 2,
	TEMP_T2_TO_T3 = 3,
	TEMP_T1_TO_T2 = 4,
	TEMP_T0_TO_T1 = 5,
	TEMP_BELOW_T0 = 6,
};

struct device { int dummy; };
struct platform_device { struct device dev; };
struct chg_alg_device { struct device dev; int alg_id; };
struct power_supply { int dummy; };
union power_supply_propval { int intval; };
struct chg_limit_setting {
	int cv;
	int input_current_limit1;
	int input_current_limit2;
	int input_current_limit_dvchg1;
	int charging_current_limit1;
	int charging_current_limit2;
};
struct charger_data {
	int input_current_limit;
	int charging_current_limit;
	int input_current_limit_by_aicl;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	int force_charging_current;
	int junction_temp_max;
	bool thermal_throttle_record;
};
struct sw_jeita_data {
	int sm;
	int cv;
	bool charging;
};
struct smart_charging_data {
	int current_limit;
	int sc_ibat;
};
struct charger_custom_data {
	int usb_charger_current;
	int charging_host_charger_current;
	int ac_charger_input_current;
	int ac_charger_current;
	int max_dmivr_charger_current;
	int temp_t4_thres;
	int temp_t3_thres;
	int temp_t2_thres;
	int temp_t1_thres;
	int temp_t0_thres;
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1_to_t2_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_below_t0_cv;
	int jeita_temp_t3_to_t4_cc;
	int jeita_temp_t2_to_t3_cc;
	int jeita_temp_t1_to_t2_cc;
	int jeita_temp_t0_to_t1_cc;
	int min_charger_voltage;
};
struct mtk_charger {
	int chr_type;
	int usb_type;
	int pd_type;
	int config;
	int bootmode;
	int enable_meta_current_limit;
	bool pd_reset;
	bool usb_unlimited;
	bool charger_unlimited;
	bool water_detected;
	bool atm_enabled;
	bool disable_aicl;
	bool enable_dynamic_mivr;
	bool enable_sw_jeita;
	bool enable_fast_charging_indicator;
	int fast_charging_indicator;
	int pre_battery_ntc;
	struct platform_device *pdev;
	void *pd_adapter;
	void *chg1_dev;
	struct power_supply *chg_psy;
	struct power_supply *bat_psy;
	struct chg_alg_device *alg[MAX_ALG_NO];
	struct chg_limit_setting setting;
	struct charger_data chg_data[4];
	struct charger_custom_data data;
	struct sw_jeita_data sw_jeita;
	struct smart_charging_data sc;
};

static int g_rp_level = 500;
static int g_usb_psy_icl = 500000;
static int g_bat_temp = 250;
static int g_pdc_ready = ALG_RUNNING;
static struct chg_limit_setting g_last_pdc_setting;
static int g_pdc_calls;

static int adapter_dev_get_property(void *a, int prop) { (void)a; (void)prop; return g_rp_level; }
static int power_supply_get_property(struct power_supply *psy, int prop, union power_supply_propval *val)
{
	if (prop == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT) {
		val->intval = g_usb_psy_icl;
		return 0;
	}
	if (prop == POWER_SUPPLY_PROP_TEMP) {
		val->intval = g_bat_temp;
		return 0;
	}
	return -1;
}
static int charger_dev_get_input_current(void *d, u32 *v) { (void)d; *v = 500000; return 0; }
static int charger_dev_set_input_current(void *d, u32 v) { (void)d; (void)v; return 0; }
static int charger_dev_run_aicl(void *d, int *v) { (void)d; *v = -1; return 0; }
static int charger_dev_get_min_charging_current(void *d, u32 *v) { (void)d; *v = 100000; return 0; }
static int charger_dev_get_min_input_current(void *d, u32 *v) { (void)d; *v = 100000; return 0; }
static int chg_alg_set_current_limit(struct chg_alg_device *alg, struct chg_limit_setting *s)
{
	if (alg->alg_id == PDC_ID) {
		g_last_pdc_setting = *s;
		g_pdc_calls++;
	}
	return 0;
}
static int chg_alg_is_algo_ready(struct chg_alg_device *alg)
{
	return alg->alg_id == PDC_ID ? g_pdc_ready : ALG_NOT_READY;
}
static const char *chg_alg_state_to_str(int s) { (void)s; return "ok"; }
static struct chg_alg_device *get_chg_alg_by_name(const char *n) { (void)n; return NULL; }
static int chg_alg_stop_algo(struct chg_alg_device *a) { (void)a; return 0; }
static void select_cv(struct mtk_charger *info) { info->setting.cv = 4450000; }
static void sc_select_charging_current(struct mtk_charger *info, struct charger_data *p) { (void)info; (void)p; }
static struct power_supply *mindone_devm_power_supply_get_by_reference(struct device *dev, const char *name) { (void)dev; (void)name; return NULL; }
'''

CASES = r'''
static struct platform_device g_pdev;
static struct power_supply g_chg_psy, g_bat_psy;
static struct chg_alg_device g_pdc_alg = { .alg_id = PDC_ID };

static void init_info(struct mtk_charger *info)
{
	memset(info, 0, sizeof(*info));
	info->pdev = &g_pdev;
	info->sc.sc_ibat = -1;
	info->config = DUAL_CHARGERS_IN_SERIES;
	info->chr_type = POWER_SUPPLY_TYPE_USB;
	info->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
	info->pd_type = MTK_PD_CONNECT_NONE;
	info->chg_psy = &g_chg_psy;
	info->bat_psy = &g_bat_psy;
	info->alg[0] = &g_pdc_alg;
	info->data.usb_charger_current = 500000;
	info->data.charging_host_charger_current = 1500000;
	info->data.ac_charger_input_current = 2000000;
	info->data.ac_charger_current = 2000000;
	info->data.max_dmivr_charger_current = 1800000;
	info->data.jeita_temp_t3_to_t4_cc = 1500000;
	info->data.jeita_temp_t2_to_t3_cc = 2000000;
	info->data.jeita_temp_t1_to_t2_cc = 1320000;
	info->data.jeita_temp_t0_to_t1_cc = 500000;
	info->setting.input_current_limit1 = -1;
	info->setting.input_current_limit2 = -1;
	info->setting.charging_current_limit1 = -1;
	info->setting.charging_current_limit2 = -1;
	for (int i = 0; i < 4; i++) {
		info->chg_data[i].input_current_limit_by_aicl = -1;
		info->chg_data[i].thermal_input_current_limit = -1;
		info->chg_data[i].thermal_charging_current_limit = -1;
		info->chg_data[i].force_charging_current = -1;
	}
	g_rp_level = 500;
	g_usb_psy_icl = 500000;
	g_bat_temp = 250;
	g_pdc_ready = ALG_RUNNING;
	g_pdc_calls = 0;
	memset(&g_last_pdc_setting, 0, sizeof(g_last_pdc_setting));
}

int main(void)
{
	struct mtk_charger info;
	bool is_basic;

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(!is_basic);
	assert(g_pdc_calls == 1);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 2000000);
	assert(info.chg_data[CHG1_SETTING].charging_current_limit == 2000000);
	assert(info.chg_data[CHG2_SETTING].input_current_limit == 2000000);
	assert(info.chg_data[CHG2_SETTING].charging_current_limit == 2000000);
	assert(g_last_pdc_setting.input_current_limit1 == 2000000);
	assert(g_last_pdc_setting.charging_current_limit1 == 2000000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(!is_basic && g_last_pdc_setting.input_current_limit1 == 2000000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(!is_basic && g_last_pdc_setting.input_current_limit1 == 2000000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_NONE;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(is_basic);
	assert(g_pdc_calls == 0);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 500000);
	assert(info.chg_data[CHG1_SETTING].charging_current_limit == 500000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK;
	info.water_detected = true;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(is_basic);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 500000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK;
	info.atm_enabled = true;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(is_basic);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 100000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK;
	info.chg_data[CHG1_SETTING].thermal_input_current_limit = 1000000;
	info.chg_data[CHG1_SETTING].thermal_charging_current_limit = 800000;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(!is_basic);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 1000000);
	assert(info.chg_data[CHG1_SETTING].charging_current_limit == 800000);

	init_info(&info);
	info.pd_type = MTK_PD_CONNECT_PE_READY_SNK;
	g_pdc_ready = ALG_NOT_READY;
	is_basic = select_charging_current_limit(&info, &info.setting);
	assert(is_basic);
	assert(info.chg_data[CHG1_SETTING].input_current_limit == 2000000);
	assert(info.chg_data[CHG1_SETTING].charging_current_limit == 2000000);

	puts("PASS charger-pd-sdp 8 cases");
	return 0;
}
'''


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def extract_unit(basic):
    start = basic.index('static bool is_typec_adapter(')
    end = basic.index('static int do_algorithm(')
    body = basic[start:end]
    cut = body.index('static void mtk_basic_update_termination(')
    return '\n'.join([STUBS, body[:cut], CASES]) + '\n'


def build_and_run(directory, name, source):
    cfile = directory / f'{name}.c'
    binary = directory / name
    cfile.write_text(source)
    res = subprocess.run(FLAGS + [str(cfile), '-o', str(binary)], capture_output=True, text=True, timeout=120)
    if res.returncode:
        fail(f'{name} compile failed:\n{res.stdout}{res.stderr}')
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)


def check_pd_hal(pd_hal):
    for fn, call in (
        ('pd_hal_get_mivr', 'charger_dev_get_mivr(hal->chg1_dev'),
        ('pd_hal_set_input_current', 'charger_dev_set_input_current(hal->chg1_dev'),
        ('pd_hal_set_charging_current', 'charger_dev_set_charging_current(hal->chg1_dev'),
    ):
        m = re.search(rf'int\s+{fn}\([^)]*\)\s*\{{(.*?)\n\}}', pd_hal, re.S)
        if not m:
            fail(f'{fn} not found in {PD_HAL}')
        if m.group(1).count(call) != 1:
            fail(f'{fn} still calls {call} more than once (stray unconditional CHG1 write)')


def main():
    parser = argparse.ArgumentParser(description='Verify PD-ready chargers on SDP are not clamped to 500 mA and CHG2 HAL writes do not clobber CHG1')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    basic = (tree / BASIC).read_text()
    pd_hal = (tree / PD_HAL).read_text()
    check_pd_hal(pd_hal)

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        d = Path(tmp)
        res = build_and_run(d, 'candidate', extract_unit(basic))
        if res.returncode:
            fail(f'candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('drop-pd-branch', 'if (is_pd_ready(info)) {', 'if (0 && is_pd_ready(info)) {'),
            ('force-pd-on-water', 'if (info->water_detected) {', 'if (0 && info->water_detected) {'),
            ('skip-pd30', 'case MTK_PD_CONNECT_PE_READY_SNK_PD30:', 'case -999:'),
        )
        for name, old, new in mutants:
            assert basic.count(old) == 1, name
            mres = build_and_run(d, 'mutant-' + name, extract_unit(basic.replace(old, new, 1)))
            if mres.returncode == 0:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name} caught')
    print(f'{CHECK}: PASS')


if __name__ == '__main__':
    sys.exit(main())
