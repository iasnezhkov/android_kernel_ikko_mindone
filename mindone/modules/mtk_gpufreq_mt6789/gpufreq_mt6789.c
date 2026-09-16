// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 */

/**
 * @file    mtk_gpufreq_core.c
 * @brief   GPU-DVFS Driver Platform Implementation
 */

/**
 * ===============================================
 * Include
 * ===============================================
 */
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/moduleparam.h>
/* STEP-BY-STEP PROBE (same technique that found F438). Measured:
 * modpre=mtk_gpufreq_mt6789 -> 0x2, modname -> 0x800: bind never completes.
 * Splitting it into steps. `mtk_gpufreq_mt6789.mindone_gf_stop=N` -- cleanly
 * reboot BEFORE step N: 21 entry · 22 after bringup · 23 after platform info
 * · 24 after gpudfd · 25 after regulator · 26 EB-mode branch · 27 bind end.
 * emergency_restart, NOT machine_restart: the latter isn't exported to
 * modules (F431).
 */
static int mindone_gf_stop;
module_param(mindone_gf_stop, int, 0644);
/* SPLITTING EXPERIMENT (F441). Mark 20 (init entry) gives 0x2, mark 21 (bind
 * entry) gives 0x800, i.e. it hangs INSIDE platform_driver_register. This flag
 * makes init return success WITHOUT registering the driver.
 * Reads as: boot proceeds further => the cause is definitely in
 * registration/binding.
 * Side benefit: the module STAYS loaded, so its symbols stay available to
 * neighbors -- `mtk_gpu_qos.ko` takes two from it (F442), and simply removing
 * the module would break that neighbor.
 */
static int mindone_gf_skip_register;
module_param(mindone_gf_skip_register, int, 0644);
static /* MINDONE: skip the first N matching hits (F667) */
static int mindone_gf_skip;
module_param(mindone_gf_skip, int, 0644);
/* MINDONE: externally tunable springboard-table step (F670/F671 diagnostics).
 * Default 0 means "use the stock MAX_BUCK_DIFF", behavior unchanged.
 * A nonzero value overrides the threshold ONLY in __gpufreq_set_springboard().
 */
static int mindone_max_buck_diff;
module_param(mindone_max_buck_diff, int, 0644);

/* GUARD BEFORE REMOVING GPU POWER.
 * Measured: the bus-idle poll is called EXACTLY 3 times per run, while domain
 * power-off happens 14-24 times (reproduced across six runs). So in the vast
 * majority of cases power is cut WITHOUT verifying the bus went idle.
 * F1158 established the mechanism verbatim: "the poll is a guard BEFORE power
 * off; timing out leads to powering off domains with the bus not stopped" --
 * and a fix along those lines turned out HARMFUL. This does the opposite:
 * the guard runs on EVERY power-off, with no timeout bailout at all.
 * F1999 measured that on a healthy bus the poll completes on the FIRST read
 * (reads=1). */
static int mindone_guard_bus_idle = 1;
module_param(mindone_guard_bus_idle, int, 0644);
MODULE_PARM_DESC(mindone_guard_bus_idle, "1 = poll bus idle before EVERY power-off");

static unsigned long long mindone_guard_calls;
module_param_named(mindone_guard_bus_idle_calls, mindone_guard_calls, ullong, 0444);

/* MINDONE-BUSIDLE-TIMEOUT (F2428/F1157): 1 = the bus-idle poll timed out on
 * the LAST call -- the caller (__gpufreq_power_control) must read this flag
 * and NOT remove power if it's set. */
static int mindone_bi_timed_out;
module_param_named(mindone_bus_idle_timeout, mindone_bi_timed_out, int, 0444);
/* Power/DVFS trace lines (MINDONE-PROT/OFF/COMMIT/VS/GPUFREQ-P*): 15k lines per boot when
 * always on, so they are off by default; echo 1 > /sys/module/mtk_gpufreq_mt6789/parameters/
 * mindone_gf_trace turns them on for a diagnosis session. */
static int mindone_gf_trace;
module_param(mindone_gf_trace, int, 0644);
#define MINDONE_TRACE(fmt, ...) do { if (mindone_gf_trace) pr_notice(fmt, ##__VA_ARGS__); } while (0)

#define MINDONE_MARK_MAX 1024
static unsigned char mindone_gf_seen[MINDONE_MARK_MAX];
void mindone_gf_mark(int step)
{
	if (mindone_gf_stop > 0 && step >= mindone_gf_stop) {
		if (step >= 0 && step < MINDONE_MARK_MAX &&
		    mindone_gf_seen[step]++ < mindone_gf_skip)
			return;
		emergency_restart();
	}
}
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#if IS_ENABLED(CONFIG_MTK_DEVINFO)
#include <linux/nvmem-consumer.h>
#endif

#include <gpufreq_v2.h>
#include <gpufreq_history_common.h>
#include <gpufreq_history_mt6789.h>
#include <gpufreq_debug.h>
#include <gpuppm.h>
#include <gpufreq_common.h>
#include <gpufreq_mt6789.h>
#include <gpudfd_mt6789.h>
#include <mtk_gpu_utility.h>

#if IS_ENABLED(CONFIG_MTK_STATIC_POWER)
#include <leakage_table_v2/mtk_static_power.h>
#endif
#if IS_ENABLED(CONFIG_COMMON_CLK_MTK_FREQ_HOPPING)
#include <clk-mtk.h>
#endif
#if IS_ENABLED(CONFIG_COMMON_CLK_MT6789)
#include <clk-fmeter.h>
#include <clk-mt6789-fmeter.h>
#endif

/**
 * ===============================================
 * Local Function Declaration
 * ===============================================
 */
/* misc function */
static unsigned int __gpufreq_custom_init_enable(void);
static unsigned int __gpufreq_dvfs_enable(void);
static void __gpufreq_set_dvfs_state(unsigned int set, unsigned int state);
static void __gpufreq_dump_bringup_status(struct platform_device *pdev);
static void __gpufreq_measure_power(void);
static void __gpufreq_set_springboard(void);
static void __iomem *__gpufreq_of_ioremap(const char *node_name, int idx);
static int __gpufreq_pause_dvfs(void);
static void __gpufreq_resume_dvfs(void);
static void __gpufreq_interpolate_volt(void);
static void __gpufreq_apply_aging(unsigned int apply_aging);
static void __gpufreq_apply_adjust(struct gpufreq_adj_info *adj_table, int adj_num);
/* dvfs function */
static int __gpufreq_custom_commit_gpu(unsigned int target_freq,
	unsigned int target_volt, enum gpufreq_dvfs_state key);
/* MINDONE-FHDECL: this is a function POINTER (clk-mtk.h:296), not a function.
 * Without this declaration, `-Wno-error=implicit-function-declaration` causes a
 * jump to the variable's address instead of a call through its value -- the CPU
 * ends up executing data.
 */
extern bool (*mtk_fh_set_rate)(const char *name, unsigned long dds, int postdiv);
static int __gpufreq_freq_scale_gpu(unsigned int freq_old, unsigned int freq_new);
static int __gpufreq_volt_scale_gpu(
	unsigned int vgpu_old, unsigned int vgpu_new,
	unsigned int vsram_old, unsigned int vsram_new);
static int __gpufreq_switch_clksrc(enum gpufreq_clk_src clksrc);
static unsigned int __gpufreq_calculate_pcw(unsigned int freq, enum gpufreq_posdiv posdiv_power);
static unsigned int __gpufreq_settle_time_vgpu(unsigned int mode, int deltaV);
static unsigned int __gpufreq_settle_time_vsram(unsigned int mode, int deltaV);
/* get function */
static unsigned int __gpufreq_get_fmeter_fgpu(void);
static unsigned int __gpufreq_get_real_fgpu(void);
static unsigned int __gpufreq_get_real_vgpu(void);
static unsigned int __gpufreq_get_real_vsram(void);
static enum gpufreq_posdiv __gpufreq_get_real_posdiv_gpu(void);
static enum gpufreq_posdiv __gpufreq_get_posdiv_by_fgpu(unsigned int freq);
/* aging sensor function */
static void __gpufreq_asensor_read_register(u32 *a_tn_lvt_cnt, u32 *a_tn_ulvt_cnt);
static unsigned int __gpufreq_asensor_read_efuse(u32 *a_t0_lvt_rt, u32 *a_t0_ulvt_rt,
	u32 *a_shift_error, u32 *efuse_error);
static unsigned int __gpufreq_get_aging_table_idx(u32 a_t0_lvt_rt, u32 a_t0_ulvt_rt,
	u32 a_shift_error, u32 efuse_error, u32 a_tn_lvt_cnt, u32 a_tn_ulvt_cnt,
	unsigned int is_efuse_read_success);
/* power control function */
#if GPUFREQ_SELF_CTRL_MTCMOS
static void __gpufreq_mfg0_control(enum gpufreq_power_state power);
static void __gpufreq_mfg1_control(enum gpufreq_power_state power);
static void __gpufreq_mfg2_control(enum gpufreq_power_state power);
static void __gpufreq_mfg3_control(enum gpufreq_power_state power);
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */
static void __gpufreq_external_cg_control(void);
static int __gpufreq_clock_control(enum gpufreq_power_state power);
static int __gpufreq_mtcmos_control(enum gpufreq_power_state power);
static int __gpufreq_buck_control(enum gpufreq_power_state power);
/* init function */
static void __gpufreq_init_shader_present(void);
static void __gpufreq_segment_adjustment(struct platform_device *pdev);
static void __gpufreq_custom_adjustment(void);
static void __gpufreq_avs_adjustment(void);
static void __gpufreq_aging_adjustment(void);
static int __gpufreq_init_opp_idx(void);
static int __gpufreq_init_opp_table(struct platform_device *pdev);
static int __gpufreq_init_segment_id(struct platform_device *pdev);
static int __gpufreq_init_mtcmos(struct platform_device *pdev);
static int __gpufreq_init_clk(struct platform_device *pdev);
static int __gpufreq_init_pmic(struct platform_device *pdev);
static int __gpufreq_init_platform_info(struct platform_device *pdev);
static int __gpufreq_pdrv_probe(struct platform_device *pdev);
static void __gpufreq_pdrv_remove(struct platform_device *pdev);

/**
 * ===============================================
 * Local Variable Definition
 * ===============================================
 */
static const struct of_device_id g_gpufreq_of_match[] = {
	{ .compatible = "mediatek,gpufreq" },
	{ /* sentinel */ }
};
static struct platform_driver g_gpufreq_pdrv = {
	.probe = __gpufreq_pdrv_probe,
	.remove_new = __gpufreq_pdrv_remove,
	.driver = {
		.name = "gpufreq",
		.owner = THIS_MODULE,
		.of_match_table = g_gpufreq_of_match,
	},
};

static void __iomem *g_apmixed_base;
static void __iomem *g_mfg_top_base;
static void __iomem *g_sleep;
static void __iomem *g_infracfg_base;
static void __iomem *g_infra_peri_debug3;
static void __iomem *g_infra_peri_debug4;
static void __iomem *g_infracfg_ao_base;
static void __iomem *g_infra_ao_debug_ctrl;
static void __iomem *g_infra_ao1_debug_ctrl;
static void __iomem *g_fmem_ao_debug_ctrl;
static void __iomem *g_efuse_base;
static void __iomem *g_mfg_cpe_control_base;
static void __iomem *g_mfg_cpe_sensor_base;
static void __iomem *g_mali_base;
static void __iomem *g_infra_ao_mem_base;
static void __iomem *g_topckgen_base;
static void __iomem *g_infra_bcrm_base;
static struct gpufreq_pmic_info *g_pmic;
static struct gpufreq_clk_info *g_clk;
static struct gpufreq_mtcmos_info *g_mtcmos;
static struct gpufreq_status g_gpu;
static struct gpufreq_asensor_info g_asensor_info;
static unsigned int g_shader_present;
static unsigned int g_stress_test_enable;
static unsigned int g_gpueb_support;
static unsigned int g_aging_enable;
static unsigned int g_avs_enable;
static unsigned int g_aging_load;
static unsigned int g_mcl50_load;
static enum gpufreq_dvfs_state g_dvfs_state;
static DEFINE_MUTEX(gpufreq_lock);

static struct gpufreq_platform_fp platform_ap_fp = {
	.bringup = __gpufreq_bringup,
	.power_ctrl_enable = __gpufreq_power_ctrl_enable,
	.get_power_state = __gpufreq_get_power_state,
	.get_dvfs_state = __gpufreq_get_dvfs_state,
	.get_shader_present = __gpufreq_get_shader_present,
	.get_cur_fgpu = __gpufreq_get_cur_fgpu,
	.get_cur_vgpu = __gpufreq_get_cur_vgpu,
	.get_cur_vsram_gpu = __gpufreq_get_cur_vsram_gpu,
	.get_cur_pgpu = __gpufreq_get_cur_pgpu,
	.get_max_pgpu = __gpufreq_get_max_pgpu,
	.get_min_pgpu = __gpufreq_get_min_pgpu,
	.get_cur_idx_gpu = __gpufreq_get_cur_idx_gpu,
	.get_opp_num_gpu = __gpufreq_get_opp_num_gpu,
	.get_signed_opp_num_gpu = __gpufreq_get_signed_opp_num_gpu,
	.get_working_table_gpu = __gpufreq_get_working_table_gpu,
	.get_signed_table_gpu = __gpufreq_get_signed_table_gpu,
	.get_debug_opp_info_gpu = __gpufreq_get_debug_opp_info_gpu,
	.get_fgpu_by_idx = __gpufreq_get_fgpu_by_idx,
	.get_pgpu_by_idx = __gpufreq_get_pgpu_by_idx,
	.get_idx_by_fgpu = __gpufreq_get_idx_by_fgpu,
	.get_lkg_pgpu = __gpufreq_get_lkg_pgpu,
	.get_dyn_pgpu = __gpufreq_get_dyn_pgpu,
	.power_control = __gpufreq_power_control,
	.generic_commit_gpu = __gpufreq_generic_commit_gpu,
	.fix_target_oppidx_gpu = __gpufreq_fix_target_oppidx_gpu,
	.fix_custom_freq_volt_gpu = __gpufreq_fix_custom_freq_volt_gpu,
	.set_timestamp = __gpufreq_set_timestamp,
	.check_bus_idle = __gpufreq_check_bus_idle,
	.dump_infra_status = __gpufreq_dump_infra_status,
	.set_stress_test = __gpufreq_set_stress_test,
	.set_aging_mode = __gpufreq_set_aging_mode,
	.get_asensor_info = __gpufreq_get_asensor_info,
	.get_core_mask_table = __gpufreq_get_core_mask_table,
	.get_core_num = __gpufreq_get_core_num,
};

static struct gpufreq_platform_fp platform_eb_fp = {
	.bringup = __gpufreq_bringup,
	.set_timestamp = __gpufreq_set_timestamp,
	.check_bus_idle = __gpufreq_check_bus_idle,
	.dump_infra_status = __gpufreq_dump_infra_status,
	.get_dyn_pgpu = __gpufreq_get_dyn_pgpu,
	.get_core_mask_table = __gpufreq_get_core_mask_table,
	.get_core_num = __gpufreq_get_core_num,
};

/**
 * ===============================================
 * External Function Definition
 * ===============================================
 */
/* API: get BRINGUP status */
unsigned int __gpufreq_bringup(void)
{
	return GPUFREQ_BRINGUP;
}

/* API: get POWER_CTRL status */
unsigned int __gpufreq_power_ctrl_enable(void)
{
	return GPUFREQ_POWER_CTRL_ENABLE;
}

/* API: get power state (on/off) */
unsigned int __gpufreq_get_power_state(void)
{
	if (g_gpu.power_count > 0)
		return POWER_ON;
	else
		return POWER_OFF;
}

/* API: get DVFS state (free/disable/keep) */
unsigned int __gpufreq_get_dvfs_state(void)
{
	return g_dvfs_state;
}

/* API: get GPU shader stack */
unsigned int __gpufreq_get_shader_present(void)
{
	return g_shader_present;
}

/* API: get current Freq of GPU */
unsigned int __gpufreq_get_cur_fgpu(void)
{
	return g_gpu.cur_freq;
}

/* API: get current Freq of STACK */
unsigned int __gpufreq_get_cur_fstack(void)
{
	return 0;
}

/* API: get current Volt of GPU */
unsigned int __gpufreq_get_cur_vgpu(void)
{
	return g_gpu.buck_count ? g_gpu.cur_volt : 0;
}

/* API: get current Volt of STACK */
unsigned int __gpufreq_get_cur_vstack(void)
{
	return 0;
}

/* API: get current Vsram of GPU */
unsigned int __gpufreq_get_cur_vsram_gpu(void)
{
	return g_gpu.cur_vsram;
}

/* API: get current Vsram of STACK */
unsigned int __gpufreq_get_cur_vsram_stack(void)
{
	return 0;
}

/* API: get current Power of GPU */
unsigned int __gpufreq_get_cur_pgpu(void)
{
	return g_gpu.working_table[g_gpu.cur_oppidx].power;
}

/* API: get current Power of STACK */
unsigned int __gpufreq_get_cur_pstack(void)
{
	return 0;
}

/* API: get max Power of GPU */
unsigned int __gpufreq_get_max_pgpu(void)
{
	return g_gpu.working_table[g_gpu.max_oppidx].power;
}

/* API: get max Power of STACK */
unsigned int __gpufreq_get_max_pstack(void)
{
	return 0;
}

/* API: get min Power of GPU */
unsigned int __gpufreq_get_min_pgpu(void)
{
	return g_gpu.working_table[g_gpu.min_oppidx].power;
}

/* API: get min Power of STACK */
unsigned int __gpufreq_get_min_pstack(void)
{
	return 0;
}

/* API: get current working OPP index of GPU */
int __gpufreq_get_cur_idx_gpu(void)
{
	return g_gpu.cur_oppidx;
}

/* API: get current working OPP index of STACK */
int __gpufreq_get_cur_idx_stack(void)
{
	return -1;
}

/* API: get number of working OPP of GPU */
int __gpufreq_get_opp_num_gpu(void)
{
	return g_gpu.opp_num;
}

/*
 * Legacy v1-API compat wrappers: mtk_gpu_qos.ko (and other satellite consumers)
 * call the old flat mt_gpufreq_* names. Other v2 chip files (mt6765/mt6768/...)
 * define these natively; the mt6789 file never grew them. Thin wrappers over the
 * already-implemented internal accessors above -- no new logic, no stub.
 */
unsigned int mt_gpufreq_get_cur_freq_index(void)
{
	return (unsigned int)__gpufreq_get_cur_idx_gpu();
}
EXPORT_SYMBOL(mt_gpufreq_get_cur_freq_index);

unsigned int mt_gpufreq_get_dvfs_table_num(void)
{
	return (unsigned int)__gpufreq_get_opp_num_gpu();
}
EXPORT_SYMBOL(mt_gpufreq_get_dvfs_table_num);

/* API: get number of working OPP of STACK */
int __gpufreq_get_opp_num_stack(void)
{
	return 0;
}

/* API: get number of signed OPP of GPU */
int __gpufreq_get_signed_opp_num_gpu(void)
{
	return g_gpu.signed_opp_num;
}

/* API: get number of signed OPP of STACK */
int __gpufreq_get_signed_opp_num_stack(void)
{
	return 0;
}

/* API: get poiner of working OPP table of GPU */
const struct gpufreq_opp_info *__gpufreq_get_working_table_gpu(void)
{
	return g_gpu.working_table;
}

/* API: get poiner of working OPP table of STACK */
const struct gpufreq_opp_info *__gpufreq_get_working_table_stack(void)
{
	return NULL;
}

/* API: get poiner of signed OPP table of GPU */
const struct gpufreq_opp_info *__gpufreq_get_signed_table_gpu(void)
{
	return g_gpu.signed_table;
}

/* API: get poiner of signed OPP table of STACK */
const struct gpufreq_opp_info *__gpufreq_get_signed_table_stack(void)
{
	return NULL;
}

/* API: get debug info of GPU for Proc show */
struct gpufreq_debug_opp_info __gpufreq_get_debug_opp_info_gpu(void)
{
	struct gpufreq_debug_opp_info opp_info = {};

	mutex_lock(&gpufreq_lock);
	opp_info.cur_oppidx = g_gpu.cur_oppidx;
	opp_info.cur_freq = g_gpu.cur_freq;
	opp_info.cur_volt = g_gpu.cur_volt;
	opp_info.cur_vsram = g_gpu.cur_vsram;
	opp_info.power_count = g_gpu.power_count;
	opp_info.cg_count = g_gpu.cg_count;
	opp_info.mtcmos_count = g_gpu.mtcmos_count;
	opp_info.buck_count = g_gpu.buck_count;
	opp_info.segment_id = g_gpu.segment_id;
	opp_info.opp_num = g_gpu.opp_num;
	opp_info.signed_opp_num = g_gpu.signed_opp_num;
	opp_info.dvfs_state = g_dvfs_state;
	opp_info.shader_present = g_shader_present;
	opp_info.aging_enable = g_aging_enable;
	opp_info.avs_enable = g_avs_enable;
	if (__gpufreq_get_power_state()) {
		opp_info.fmeter_freq = __gpufreq_get_fmeter_fgpu();
		opp_info.con1_freq = __gpufreq_get_real_fgpu();
		opp_info.regulator_volt = __gpufreq_get_real_vgpu();
		opp_info.regulator_vsram = __gpufreq_get_real_vsram();
	} else {
		opp_info.fmeter_freq = 0;
		opp_info.con1_freq = 0;
		opp_info.regulator_volt = 0;
		opp_info.regulator_vsram = 0;
	}
	mutex_unlock(&gpufreq_lock);

	return opp_info;
}

/* API: get debug info of STACK for Proc show */
struct gpufreq_debug_opp_info __gpufreq_get_debug_opp_info_stack(void)
{
	struct gpufreq_debug_opp_info opp_info = {};

	return opp_info;
}

/* API: get asensor info for Proc show */
struct gpufreq_asensor_info __gpufreq_get_asensor_info(void)
{
	struct gpufreq_asensor_info asensor_info = {};

#if GPUFREQ_ASENSOR_ENABLE
	asensor_info = g_asensor_info;
#endif /* GPUFREQ_ASENSOR_ENABLE */

	return asensor_info;
}

/* API: get Freq of GPU via OPP index */
unsigned int __gpufreq_get_fgpu_by_idx(int oppidx)
{
	if (oppidx >= 0 && oppidx < g_gpu.opp_num)
		return g_gpu.working_table[oppidx].freq;
	else
		return 0;
}

/* API: get Freq of STACK via OPP index */
unsigned int __gpufreq_get_fstack_by_idx(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return 0;
}

/* API: get Power of GPU via OPP index */
unsigned int __gpufreq_get_pgpu_by_idx(int oppidx)
{
	if (oppidx >= 0 && oppidx < g_gpu.opp_num)
		return g_gpu.working_table[oppidx].power;
	else
		return 0;
}

/* API: get Power of STACK via OPP index */
unsigned int __gpufreq_get_pstack_by_idx(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return 0;
}

/* API: get working OPP index of GPU via Freq */
int __gpufreq_get_idx_by_fgpu(unsigned int freq)
{
	int oppidx = -1;
	int i = 0;

	/* find the smallest index that satisfy given freq */
	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].freq >= freq)
			break;
	}
	/* use max_oppidx if not found */
	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

/* API: get working OPP index of STACK via Freq */
int __gpufreq_get_idx_by_fstack(unsigned int freq)
{
	GPUFREQ_UNREFERENCED(freq);

	return 0;
}

/* API: get working OPP index of GPU via Volt */
int __gpufreq_get_idx_by_vgpu(unsigned int volt)
{
	int oppidx = -1;
	int i = 0;

	/* find the smallest index that satisfy given volt */
	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].volt >= volt)
			break;
	}
	/* use max_oppidx if not found */
	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

/* API: get working OPP index of STACK via Volt */
int __gpufreq_get_idx_by_vstack(unsigned int volt)
{
	GPUFREQ_UNREFERENCED(volt);

	return -1;
}

/* API: get working OPP index of GPU via Power */
int __gpufreq_get_idx_by_pgpu(unsigned int power)
{
	int oppidx = -1;
	int i = 0;

	/* find the smallest index that satisfy given power */
	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].power >= power)
			break;
	}
	/* use max_oppidx if not found */
	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

/* API: get working OPP index of STACK via Power */
int __gpufreq_get_idx_by_pstack(unsigned int power)
{
	GPUFREQ_UNREFERENCED(power);

	return -1;
}

/* API: get Volt of SRAM via volt of GPU */
unsigned int __gpufreq_get_vsram_by_vgpu(unsigned int vgpu)
{
	unsigned int vsram;

	if (vgpu > VSRAM_FIXED_THRESHOLD)
		vsram = vgpu + VSRAM_FIXED_DIFF;
	else
		vsram = VSRAM_FIXED_VOLT;

	return vsram;
}

/* API: get leakage Power of GPU */
unsigned int __gpufreq_get_lkg_pgpu(unsigned int volt)
{
	GPUFREQ_UNREFERENCED(volt);

	return GPU_LEAKAGE_POWER;
}

/* API: get dynamic Power of GPU */
unsigned int __gpufreq_get_dyn_pgpu(unsigned int freq, unsigned int volt)
{
	unsigned int p_dynamic = GPU_ACT_REF_POWER;
	unsigned int ref_freq = GPU_ACT_REF_FREQ;
	unsigned int ref_volt = GPU_ACT_REF_VOLT;

	p_dynamic = p_dynamic *
		((freq * 100) / ref_freq) *
		((volt * 100) / ref_volt) *
		((volt * 100) / ref_volt) /
		(100 * 100 * 100);

	return p_dynamic;
}

/* API: get leakage Power of STACK */
unsigned int __gpufreq_get_lkg_pstack(unsigned int volt)
{
	GPUFREQ_UNREFERENCED(volt);

	return 0;
}

/* API: get dynamic Power of STACK */
unsigned int __gpufreq_get_dyn_pstack(unsigned int freq, unsigned int volt)
{
	GPUFREQ_UNREFERENCED(freq);
	GPUFREQ_UNREFERENCED(volt);

	return 0;
}

/*
 * API: control power state of whole MFG system
 * return power_count if success
 * return GPUFREQ_EINVAL if failure
 */
int __gpufreq_power_control(enum gpufreq_power_state power)
{
	int ret = 0;
	u32 val;

	GPUFREQ_TRACE_START("power=%d", power);

	mutex_lock(&gpufreq_lock);

	GPUFREQ_LOGD("switch power: %s (Power: %d, Buck: %d, MTCMOS: %d, CG: %d)",
			power ? "On" : "Off",
			g_gpu.power_count, g_gpu.buck_count,
			g_gpu.mtcmos_count, g_gpu.cg_count);

	if (power == POWER_ON) {
		g_gpu.power_count++;
	} else {
		g_gpu.power_count--;
		__gpufreq_check_pending_exception();
	}
	__gpufreq_footprint_power_count(g_gpu.power_count);

	if (power == POWER_ON && g_gpu.power_count == 1) {
		mindone_gf_mark(400);
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_01);

		/* control Buck */
		MINDONE_TRACE("MINDONE-GPUFREQ-P2: enter __gpufreq_power_control POWER_ON, buck about to start\n");
		/* MINDONE INSTRUMENTATION: bus and protection state BEFORE every power-on.
		 * Two hypotheses remain -- a bus stall and a trip to EL3 (F1940). This
		 * print shows the EMI and INFRA protection registers; the last logged
		 * state before the stall will show whether the bus was already unusual. */
		if (g_infracfg_ao_base)
			MINDONE_TRACE("MINDONE-PROT[on]: STA1=0x%08x STA1_1=0x%08x STA1_2=0x%08x\n",
				readl(g_infracfg_ao_base + 0x228),
				readl(g_infracfg_ao_base + 0x258),
				readl(g_infracfg_ao_base + 0x724));
		ret = __gpufreq_buck_control(POWER_ON);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control Buck: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_02);

		mindone_gf_mark(401);
		/* control MTCMOS */
		MINDONE_TRACE("MINDONE-GPUFREQ-P3: buck control done (VGPU+VSRAM_GPU regulators on)\n");
		ret = __gpufreq_mtcmos_control(POWER_ON);
		if (unlikely(ret < 0)) {
			GPUFREQ_LOGE("fail to control MTCMOS: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_03);

		mindone_gf_mark(402);
		/* control clock */
		ret = __gpufreq_clock_control(POWER_ON);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control CLOCK: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}

		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_04);

		mindone_gf_mark(403);
		/* free DVFS when power on */
		g_dvfs_state &= ~DVFS_POWEROFF;
	} else if (power == POWER_OFF && g_gpu.power_count == 0) {
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_05);

		/* freeze DVFS when power off */
		g_dvfs_state |= DVFS_POWEROFF;
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_06);

		/* GUARD: make sure the bus went idle BEFORE removing power.
		 * Without it, domains get cut under an unfinished transaction (see F1158). */
		if (mindone_guard_bus_idle) {
			mindone_guard_calls++;
			if (mindone_guard_calls <= 3)
				MINDONE_TRACE("MINDONE-GUARD: bus-idle guard call %llu before power off\n",
					  mindone_guard_calls);
			__gpufreq_check_bus_idle();
			if (mindone_bi_timed_out) {
				/* MINDONE-OFF-ABORT (F2428, implementing the F1157 plan):
				 * the bus did not report idle within 1s -- do NOT remove
				 * power under an unfinished transaction. Domains stay as-is
				 * (usually on); the next POWER_OFF will try again. */
				GPUFREQ_LOGE("bus not idle after timeout, aborting power-off (MTCMOS stays as-is)");
				pr_err("MINDONE-OFF-ABORT: bus-idle timeout, keeping MTCMOS domains as-is\n");
				ret = GPUFREQ_EINVAL;
				goto done_unlock;
			}
		}

		/* control clock */
		MINDONE_TRACE("MINDONE-OFF-1: entering clock power-off\n");
		ret = __gpufreq_clock_control(POWER_OFF);
		MINDONE_TRACE("MINDONE-OFF-2: clocks off, ret=%d\n", ret);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control CLOCK: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_07);

		/* control MTCMOS */
		if (g_infracfg_ao_base)
			MINDONE_TRACE("MINDONE-PROT[before-mtcmos]: STA1=0x%08x STA1_1=0x%08x STA1_2=0x%08x\n",
				readl(g_infracfg_ao_base + 0x228),
				readl(g_infracfg_ao_base + 0x258),
				readl(g_infracfg_ao_base + 0x724));
		MINDONE_TRACE("MINDONE-OFF-3: entering MTCMOS domain power-off\n");
		ret = __gpufreq_mtcmos_control(POWER_OFF);
		MINDONE_TRACE("MINDONE-OFF-4: MTCMOS domains off, ret=%d\n", ret);
		if (g_infracfg_ao_base)
			MINDONE_TRACE("MINDONE-PROT[after-mtcmos]: STA1=0x%08x STA1_1=0x%08x STA1_2=0x%08x\n",
				readl(g_infracfg_ao_base + 0x228),
				readl(g_infracfg_ao_base + 0x258),
				readl(g_infracfg_ao_base + 0x724));
		if (unlikely(ret < 0)) {
			GPUFREQ_LOGE("fail to control MTCMOS: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_08);

		/* control Buck */
		MINDONE_TRACE("MINDONE-OFF-5: entering regulator power-off\n");
		ret = __gpufreq_buck_control(POWER_OFF);
		MINDONE_TRACE("MINDONE-OFF-6: regulators off, ret=%d\n", ret);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control Buck: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}

		__gpufreq_footprint_power_step(GPUFREQ_POWER_STEP_09);
	}

	/* return power count if successfully control power */
	ret = g_gpu.power_count;

done_unlock:
	GPUFREQ_LOGD("PWR_STATUS (0x%x): 0x%08x",
		(0x10006000 + 0x16C), readl(g_sleep + 0x16C));

	mutex_unlock(&gpufreq_lock);
	MINDONE_TRACE("MINDONE-GPUFREQ-P13: __gpufreq_power_control about to return, ret=%d\n", ret);

	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * API: commit DVFS to GPU by given OPP index
 * this is the main entrance of generic DVFS
 */
int __gpufreq_generic_commit_gpu(int target_oppidx, enum gpufreq_dvfs_state key)
{
	struct gpufreq_opp_info *opp_table = g_gpu.working_table;
	struct gpufreq_sb_info *sb_table = g_gpu.sb_table;
	int opp_num = g_gpu.opp_num;
	int cur_oppidx = 0;
	unsigned int cur_freq = 0, target_freq = 0;
	unsigned int cur_volt = 0, target_volt = 0;
	unsigned int cur_vsram = 0, target_vsram = 0;
	int sb_idx = 0;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("target_oppidx=%d, key=%d",
		target_oppidx, key);

	/* validate 0 <= target_oppidx < opp_num */
	if (target_oppidx < 0 || target_oppidx >= opp_num) {
		GPUFREQ_LOGE("invalid target GPU OPP index: %d (OPP_NUM: %d)",
			target_oppidx, opp_num);
		ret = GPUFREQ_EINVAL;
		goto done;
	}


	mutex_lock(&gpufreq_lock);

	/* check dvfs state */
	if (g_dvfs_state & ~key) {
		GPUFREQ_LOGD("unavailable dvfs state (0x%x)", g_dvfs_state);
		ret = GPUFREQ_SUCCESS;
		goto done_unlock;
	}

	/* randomly replace target index */
	if (g_stress_test_enable) {
		get_random_bytes(&target_oppidx, sizeof(target_oppidx));
		target_oppidx = target_oppidx < 0 ?
			(target_oppidx*-1) % opp_num : target_oppidx % opp_num;
	}

	cur_oppidx = g_gpu.cur_oppidx;
	cur_freq = g_gpu.cur_freq;
	cur_volt = g_gpu.cur_volt;
	cur_vsram = g_gpu.cur_vsram;

	target_freq = opp_table[target_oppidx].freq;
	target_volt = opp_table[target_oppidx].volt;
	target_vsram = opp_table[target_oppidx].vsram;

	GPUFREQ_LOGD("begin to commit GPU OPP index: (%d->%d)",
		cur_oppidx, target_oppidx);

	/* MINDONE: capture ALL the numbers before switching the operating point,
	 * in one measurement. The two loops below iterate sb_table; if it's not
	 * populated, they never terminate.
	 */
	MINDONE_TRACE("MINDONE-COMMIT: target_oppidx=%d opp_num=%d dvfs_state=0x%x\n",
		target_oppidx, opp_num, g_dvfs_state);
	MINDONE_TRACE("MINDONE-COMMIT: cur  idx=%d freq=%u volt=%u vsram=%u\n",
		cur_oppidx, cur_freq, cur_volt, cur_vsram);
	MINDONE_TRACE("MINDONE-COMMIT: targ idx=%d freq=%u volt=%u vsram=%u\n",
		target_oppidx, target_freq, target_volt, target_vsram);
	MINDONE_TRACE("MINDONE-COMMIT: sb_table=%p opp_table=%p\n", sb_table, opp_table);
	if (sb_table && cur_oppidx >= 0 && cur_oppidx < opp_num)
		MINDONE_TRACE("MINDONE-COMMIT: sb[cur].up=%d sb[cur].down=%d\n",
			sb_table[cur_oppidx].up, sb_table[cur_oppidx].down);
	else
		MINDONE_TRACE("MINDONE-COMMIT: sb_table UNAVAILABLE or cur_oppidx out of table\n");
	MINDONE_TRACE("MINDONE-COMMIT: branch=%s\n",
		target_freq == cur_freq ? "voltage only" :
		(target_freq > cur_freq ? "up" : "down"));
	mindone_gf_mark(600);

	/* todo: GED log buffer (gpufreq_pr_logbuf) */

	if (target_freq == cur_freq) {
		/* voltage scaling */
		ret = __gpufreq_volt_scale_gpu(
			cur_volt, target_volt, cur_vsram, target_vsram);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
				cur_volt, target_volt, cur_vsram, target_vsram);
			goto done_unlock;
		}
		mindone_gf_mark(620);
	} else if (target_freq > cur_freq) {
		mindone_gf_mark(630);
		/* voltage scaling */
		while (target_volt != cur_volt) {
			sb_idx = target_oppidx > sb_table[cur_oppidx].up ?
				target_oppidx : sb_table[cur_oppidx].up;

			ret = __gpufreq_volt_scale_gpu(
				cur_volt, opp_table[sb_idx].volt,
				cur_vsram, opp_table[sb_idx].vsram);
			if (unlikely(ret)) {
				GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
					cur_volt, opp_table[sb_idx].volt,
					cur_vsram, opp_table[sb_idx].vsram);
				goto done_unlock;
			}

			cur_oppidx = sb_idx;
			cur_volt = opp_table[sb_idx].volt;
			cur_vsram = opp_table[sb_idx].vsram;
		}
		mindone_gf_mark(634);
		/* frequency scaling */
		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
	} else {
		/* frequency scaling */
		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
		/* voltage scaling */
		while (target_volt != cur_volt) {
			sb_idx = target_oppidx < sb_table[cur_oppidx].down ?
				target_oppidx : sb_table[cur_oppidx].down;

			ret = __gpufreq_volt_scale_gpu(
				cur_volt, opp_table[sb_idx].volt,
				cur_vsram, opp_table[sb_idx].vsram);
			if (unlikely(ret)) {
				GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
					cur_volt, opp_table[sb_idx].volt,
					cur_vsram, opp_table[sb_idx].vsram);
				goto done_unlock;
			}

			cur_oppidx = sb_idx;
			cur_volt = opp_table[sb_idx].volt;
			cur_vsram = opp_table[sb_idx].vsram;
		}
	}
#if GPUFREQ_HISTORY_ENABLE
	if (g_gpu.cur_oppidx != target_oppidx) {
		g_gpu.cur_oppidx = target_oppidx;
		/* update history record to shared memory */
		gpufreq_set_history_state(HISTORY_FREE);
		__gpufreq_record_history_entry();
	} else
		g_gpu.cur_oppidx = target_oppidx;
#else
	g_gpu.cur_oppidx = target_oppidx;
#endif /* GPUFREQ_HISTORY_ENABLE */
	__gpufreq_footprint_oppidx(target_oppidx);
	mindone_gf_mark(621);

done_unlock:
	mutex_unlock(&gpufreq_lock);
	mindone_gf_mark(622);

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * API: commit DVFS to STACK by given OPP index
 * this is the main entrance of generic DVFS
 */
int __gpufreq_generic_commit_stack(int target_oppidx, enum gpufreq_dvfs_state key)
{
	GPUFREQ_UNREFERENCED(target_oppidx);
	GPUFREQ_UNREFERENCED(key);

	return GPUFREQ_EINVAL;
}

/* API: fix OPP of GPU via given OPP index */
int __gpufreq_fix_target_oppidx_gpu(int oppidx)
{
	int opp_num = g_gpu.opp_num;
	int ret = GPUFREQ_SUCCESS;

	mindone_gf_mark(30);
	ret = __gpufreq_power_control(POWER_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", POWER_ON, ret);
		goto done;
	}

	if (oppidx == -1) {
		__gpufreq_set_dvfs_state(false, DVFS_DEBUG_KEEP);
		ret = GPUFREQ_SUCCESS;
	} else if (oppidx >= 0 && oppidx < opp_num) {
		__gpufreq_set_dvfs_state(true, DVFS_DEBUG_KEEP);

		ret = __gpufreq_generic_commit_gpu(oppidx, DVFS_DEBUG_KEEP);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to commit GPU OPP index: %d (%d)", oppidx, ret);
			__gpufreq_set_dvfs_state(false, DVFS_DEBUG_KEEP);
		}
	} else {
		GPUFREQ_LOGE("invalid fixed OPP index: %d", oppidx);
		ret = GPUFREQ_EINVAL;
	}

	__gpufreq_power_control(POWER_OFF);

done:
	return ret;
}

/* API: fix OPP of STACK via given OPP index */
int __gpufreq_fix_target_oppidx_stack(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return GPUFREQ_EINVAL;
}

/* API: fix Freq and Volt of GPU via given Freq and Volt */
int __gpufreq_fix_custom_freq_volt_gpu(unsigned int freq, unsigned int volt)
{
	int ret = GPUFREQ_SUCCESS;

	ret = __gpufreq_power_control(POWER_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", POWER_ON, ret);
		goto done;
	}

	if (freq == 0 && volt == 0) {
		mutex_lock(&gpufreq_lock);
		g_dvfs_state &= ~DVFS_DEBUG_KEEP;
		mutex_unlock(&gpufreq_lock);
		ret = GPUFREQ_SUCCESS;
	} else if (freq > POSDIV_2_MAX_FREQ || freq < POSDIV_8_MIN_FREQ) {
		GPUFREQ_LOGE("invalid fixed freq: %d\n", freq);
		ret = GPUFREQ_EINVAL;
	} else if (volt > VGPU_MAX_VOLT || volt < VGPU_MIN_VOLT) {
		GPUFREQ_LOGE("invalid fixed volt: %d\n", volt);
		ret = GPUFREQ_EINVAL;
	} else {
		mutex_lock(&gpufreq_lock);
		g_dvfs_state |= DVFS_DEBUG_KEEP;
		mutex_unlock(&gpufreq_lock);

		ret = __gpufreq_custom_commit_gpu(freq, volt, DVFS_DEBUG_KEEP);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to custom commit GPU freq: %d, volt: %d (%d)",
				freq, volt, ret);
			mutex_lock(&gpufreq_lock);
			g_dvfs_state &= ~DVFS_DEBUG_KEEP;
			mutex_unlock(&gpufreq_lock);
		}
	}

	__gpufreq_power_control(POWER_OFF);

done:
	return ret;
}

/* API: fix Freq and Volt of STACK via given Freq and Volt */
int __gpufreq_fix_custom_freq_volt_stack(unsigned int freq, unsigned int volt)
{
	GPUFREQ_UNREFERENCED(freq);
	GPUFREQ_UNREFERENCED(volt);

	return GPUFREQ_EINVAL;
}

void __gpufreq_set_timestamp(void)
{
	/* MFG_TIMESTAMP 0x13FBF130 [0] enable timestamp = 1'b1 */
	/* MFG_TIMESTAMP 0x13FBF130 [1] timer from internal module = 1'b0 */
	/* MFG_TIMESTAMP 0x13FBF130 [1] timer from soc = 1'b0 */
	writel(0x00000003, g_mfg_top_base + 0x130);
}

/* PROBE FOR F1995. The bus-idle poll is the only place the CPU waits on
 * hardware with no limit or failure report. An infinite LOOP can't explain
 * our symptom (F845's watchdog would catch a spinning CPU); the first read
 * never returning CAN. Two marks around that read discriminate it (declared
 * before measuring): "BEFORE-READ" with no "DONE" => stuck on the bus access;
 * "DONE" => poll innocent. Printed only for the first calls to avoid ring
 * buffer flooding. mindone_skip_bus_idle=1 skips the poll entirely -- the
 * switch F1157 assumed existed, which F1993 showed never did. */
static int mindone_skip_bus_idle;
module_param(mindone_skip_bus_idle, int, 0644);
MODULE_PARM_DESC(mindone_skip_bus_idle, "1 -- do not poll bus idle at all");

static unsigned long long mindone_bi_calls;
module_param_named(mindone_bus_idle_calls, mindone_bi_calls, ullong, 0444);
static unsigned long long mindone_bi_done;
module_param_named(mindone_bus_idle_done, mindone_bi_done, ullong, 0444);

#define MINDONE_BI_LOUD 3	/* how many of the first calls to print */

void __gpufreq_check_bus_idle(void)
{
	u32 val;
	unsigned long long reads = 0;
	unsigned long long n = ++mindone_bi_calls;

	if (mindone_skip_bus_idle) {
		if (n <= MINDONE_BI_LOUD)
			MINDONE_TRACE("MINDONE-BUSIDLE: call %llu SKIPPED-BY-PARAM\n", n);
		return;
	}

	/* MFG_QCHANNEL_CON (0x13fb_f0b4) bit [1:0] = 0x1 */
	writel(0x00000001, g_mfg_top_base + 0xB4);

	/* set register MFG_DEBUG_SEL (0x13fb_f170) bit [7:0] = 0x03 */
	writel(0x00000003, g_mfg_top_base + 0x170);

	if (n <= MINDONE_BI_LOUD)
		MINDONE_TRACE("MINDONE-BUSIDLE: call %llu BEFORE-READ-0x178\n", n);

	/*
	 * polling register MFG_DEBUG_TOP (0x13fb_f178) bit 2 = 0x1
	 * 1 for bus idle
	 * 0 for bus non-idle
	 */
	/* MINDONE-BUSIDLE-TIMEOUT (F2428): the only poll with no time limit in the
	 * entire GPU power path -- the other 20 loops are already bounded (F1138).
	 * Limit 1s = 100000 x 10us -- the same number mainline uses to bound THIS
	 * SAME handshake (F1989, drivers/soc/mediatek/mtk-pm-domains.c,
	 * regmap_read_poll_timeout). On timeout we do NOT exit silently (a
	 * retracted F1155/F1158 fix did that and it turned out harmful) -- we set
	 * a flag for the caller, which must leave power as-is. */
	mindone_bi_timed_out = 0;
	do {
		val = readl(g_mfg_top_base + 0x178);
		reads++;
		if ((val & 0x4) == 0x4)
			break;
		udelay(10);
	} while (reads < 100000);

	if ((val & 0x4) != 0x4) {
		mindone_bi_timed_out = 1;
		pr_err("MINDONE-BUSIDLE-TIMEOUT: bus not idle after 1s, reads=%llu val=0x%08x\n",
			reads, val);
	}

	mindone_bi_done++;
	if (n <= MINDONE_BI_LOUD)
		MINDONE_TRACE("MINDONE-BUSIDLE: call %llu DONE reads=%llu val=0x%08x\n",
			  n, reads, val);
}

void __gpufreq_dump_infra_status(void)
{
	u32 val = 0;

	GPUFREQ_LOGI("== [GPUFREQ INFRA STATUS] ==");
	GPUFREQ_LOGI("GPU[%d] Freq: %d, Vgpu: %d, Vsram: %d",
		g_gpu.cur_oppidx, g_gpu.cur_freq,
		g_gpu.cur_volt, g_gpu.cur_vsram);

	/*0x1020E000 */
	if (g_infracfg_base) {
		/* g_infracfg_base */
		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[EMI]",
			(0x1020E000 + 0x810), readl(g_infracfg_base + 0x810),
			(0x1020E000 + 0x814), readl(g_infracfg_base + 0x814));
	}

	/* 0x10001000, 0x10023000 */
	if (g_infracfg_ao_base && g_infra_ao_debug_ctrl) {
		/* MD_MFGSYS_PROTECT_EN_STA_0 */
		/* MD_MFGSYS_PROTECT_RDY_STA_0 */
		/* INFRA_AO_BUS_U_DEBUG_CTRL_AO_INFRA_AO_CTRL0 */
		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x10001000 + 0xCA0), readl(g_infracfg_ao_base + 0xCA0),
			(0x10001000 + 0xCAC), readl(g_infracfg_ao_base + 0xCAC),
			(0x10023000 + 0x000), readl(g_infra_ao_debug_ctrl + 0x000));
	}

	/* 0x1002B000, 0x1002E000 */
	if (g_infra_ao1_debug_ctrl && g_infra_peri_debug3) {
		/* INFRA_QAXI_AO_BUS_SUB1_U_DEBUG_CTRL_AO_INFRA_AO1_CTRL0 */
		/*GPU_DFD*/
		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x1002B000 + 0x000), readl(g_infra_ao1_debug_ctrl + 0x000),
			(0x1002E000 + 0x000), readl(g_infra_peri_debug3 + 0x000));
	}

	/* 0x10040000, 0x10042000 */
	if (g_infra_peri_debug4 && g_fmem_ao_debug_ctrl) {
		/* GPU_DFD */
		/* NTH_EMI_AO_DEBUG_CTRL_EMI_AO_BUS_U_DEBUG_CTRL_AO_EMI_AO_CTRL0 */
		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x10040000 + 0x000), readl(g_infra_peri_debug4 + 0x000),
			(0x10042000 + 0x000), readl(g_fmem_ao_debug_ctrl + 0x000));
	}

	/* 0x10006000 */
	if (g_sleep) {
		GPUFREQ_LOGI("[GPU_DFD] pwr info 0x%x:0x%08x %08x %08x %08x\n",
				(0x10006000 + 0x308),
				readl(g_sleep + 0x308),
				readl(g_sleep + 0x30C),
				readl(g_sleep + 0x310),
				readl(g_sleep + 0x314));

		GPUFREQ_LOGI("[GPU_DFD] pwr info 0x%x:0x%08x %08x %08x\n",
				(0x10006000 + 0x318),
				readl(g_sleep + 0x318),
				readl(g_sleep + 0x31C),
				readl(g_sleep + 0x320));

		GPUFREQ_LOGI("[GPU_DFD] pwr info 0x%x:0x%08x\n",
				(0x10006000 + 0x16C),
				readl(g_sleep + 0x16C));

		GPUFREQ_LOGI("[GPU_DFD] pwr info 0x%x:0x%08x\n",
				(0x10006000 + 0x170),
				readl(g_sleep + 0x170));
	}
}

/* API: enable/disable random OPP index substitution to do stress test */
void __gpufreq_set_stress_test(unsigned int mode)
{
	g_stress_test_enable = mode;
}

/* API: apply/restore Vaging to working table of GPU */
int __gpufreq_set_aging_mode(unsigned int mode)
{
	/* prevent from repeatedly applying aging */
	if (g_aging_enable ^ mode) {
		__gpufreq_apply_aging(mode);
		g_aging_enable = mode;

		/* set power info to working table */
		__gpufreq_measure_power();

		return GPUFREQ_SUCCESS;
	} else {
		return GPUFREQ_EINVAL;
	}
}

/* API: get core_mask table */
struct gpufreq_core_mask_info *__gpufreq_get_core_mask_table(void)
{
	return g_core_mask_table;
}

/* API: get max number of shader cores */
unsigned int __gpufreq_get_core_num(void)
{
	return SHADER_CORE_NUM;
}

/**
 * ===============================================
 * Internal Function Definition
 * ===============================================
 */
static unsigned int __gpufreq_custom_init_enable(void)
{
	return GPUFREQ_CUST_INIT_ENABLE;
}

static unsigned int __gpufreq_dvfs_enable(void)
{
	return GPUFREQ_DVFS_ENABLE;
}

/* API: set/reset DVFS state with lock */
static void __gpufreq_set_dvfs_state(unsigned int set, unsigned int state)
{
	mutex_lock(&gpufreq_lock);
	if (set)
		g_dvfs_state |= state;
	else
		g_dvfs_state &= ~state;
	mutex_unlock(&gpufreq_lock);
}

/*
 * API: commit DVFS to GPU by given freq and volt
 * this is debug function and use it with caution
 */
static int __gpufreq_custom_commit_gpu(unsigned int target_freq,
	unsigned int target_volt, enum gpufreq_dvfs_state key)
{
	unsigned int cur_freq = 0, cur_volt = 0;
	unsigned int cur_vsram = 0, target_vsram = 0;
	unsigned int sb_volt = 0, sb_vsram = 0;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("target_freq=%d, target_volt=%d, key=%d",
		target_freq, target_volt, key);

	mutex_lock(&gpufreq_lock);

	/* check dvfs state */
	if (g_dvfs_state & ~key) {
		GPUFREQ_LOGI("unavailable dvfs state (0x%x)", g_dvfs_state);
		ret = GPUFREQ_SUCCESS;
		goto done_unlock;
	}

	cur_freq = g_gpu.cur_freq;
	cur_volt = g_gpu.cur_volt;
	cur_vsram = g_gpu.cur_vsram;

	target_vsram = __gpufreq_get_vsram_by_vgpu(target_volt);

	GPUFREQ_LOGD("begin to custom commit freq: (%d->%d), volt: (%d->%d)",
		cur_freq, target_freq, cur_volt, target_volt);

	if (target_freq == cur_freq) {
		/* voltage scaling */
		ret = __gpufreq_volt_scale_gpu(
			cur_volt, target_volt, cur_vsram, target_vsram);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
				cur_volt, target_volt, cur_vsram, target_vsram);
			goto done_unlock;
		}
	} else if (target_freq > cur_freq) {
		/* voltage scaling */
		while (target_volt != cur_volt) {
			if ((target_vsram - cur_volt) > MAX_BUCK_DIFF) {
				sb_volt = cur_volt + MAX_BUCK_DIFF;
				sb_vsram = __gpufreq_get_vsram_by_vgpu(sb_volt);
			} else {
				sb_volt = target_volt;
				sb_vsram = target_vsram;
			}

			ret = __gpufreq_volt_scale_gpu(
				cur_volt, sb_volt, cur_vsram, sb_vsram);
			if (unlikely(ret)) {
				GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
					cur_volt, sb_volt, cur_vsram, sb_vsram);
				goto done_unlock;
			}

			cur_volt = sb_volt;
			cur_vsram = sb_vsram;
		}
		/* frequency scaling */
		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
	} else {
		/* frequency scaling */
		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
		/* voltage scaling */
		while (target_volt != cur_volt) {
			if ((cur_vsram - target_volt) > MAX_BUCK_DIFF) {
				sb_volt = cur_volt - MAX_BUCK_DIFF;
				sb_vsram = __gpufreq_get_vsram_by_vgpu(sb_volt);
			} else {
				sb_volt = target_volt;
				sb_vsram = target_vsram;
			}

			ret = __gpufreq_volt_scale_gpu(
				cur_volt, sb_volt, cur_vsram, sb_vsram);
			if (unlikely(ret)) {
				GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
					cur_volt, sb_volt, cur_vsram, sb_vsram);
				goto done_unlock;
			}

			cur_volt = sb_volt;
			cur_vsram = sb_vsram;
		}
	}

	g_gpu.cur_oppidx = __gpufreq_get_idx_by_fgpu(target_freq);

	__gpufreq_footprint_oppidx(g_gpu.cur_oppidx);

done_unlock:
	mutex_unlock(&gpufreq_lock);

	GPUFREQ_TRACE_END();

	return ret;
}

static int __gpufreq_switch_clksrc(enum gpufreq_clk_src clksrc)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("clksrc=%d", clksrc);

	if (clksrc == CLOCK_MAIN) {
		ret = clk_set_parent(g_clk->clk_mux, g_clk->clk_main_parent);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to switch GPU to CLOCK_MAIN (%d)", ret);
		goto done;
		}
	} else if (clksrc == CLOCK_SUB) {
		ret = clk_set_parent(g_clk->clk_mux, g_clk->clk_sub_parent);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to switch GPU to CLOCK_SUB (%d)", ret);
			goto done;
		}
	} else {
		GPUFREQ_LOGE("invalid clock source: %d (EINVAL)", clksrc);
		goto done;
	}

done:
	GPUFREQ_TRACE_END();
	return ret;
}

/*
 * API: calculate pcw for setting CON1
 * Fin is 26 MHz
 * VCO Frequency = Fin * N_INFO
 * MFGPLL output Frequency = VCO Frequency / POSDIV
 * N_INFO = MFGPLL output Frequency * POSDIV / FIN
 * N_INFO[21:14] = FLOOR(N_INFO, 8)
 */
static unsigned int __gpufreq_calculate_pcw(unsigned int freq, enum gpufreq_posdiv posdiv)
{
	/*
	 * MFGPLL VCO range: 1.5GHz - 3.8GHz by divider 1/2/4/8/16,
	 * MFGPLL range: 125MHz - 3.8GHz,
	 * | VCO MAX | VCO MIN | POSDIV | PLL OUT MAX | PLL OUT MIN |
	 * |  3800   |  1500   |    1   |   3800MHz   |   1500MHz   |
	 * |  3800   |  1500   |    2   |   1900MHz   |    750MHz   |
	 * |  3800   |  1500   |    4   |    950MHz   |    375MHz   |
	 * |  3800   |  1500   |    8   |    475MHz   |  187.5MHz   |
	 * |  3800   |  2000   |   16   |  237.5MHz   |    125MHz   |
	 */
	unsigned int pcw = 0;

	/* only use posdiv 2 or 4 */
	if ((freq >= POSDIV_4_MIN_FREQ) && (freq <= POSDIV_2_MAX_FREQ)) {
		pcw = (((freq / TO_MHZ_HEAD * (1 << posdiv)) << DDS_SHIFT)
			/ MFGPLL_FIN + ROUNDING_VALUE) / TO_MHZ_TAIL;
	} else {
		GPUFREQ_LOGE("out of range Freq: %d (EINVAL)", freq);
	}

	return pcw;
}

static enum gpufreq_posdiv __gpufreq_get_real_posdiv_gpu(void)
{
	unsigned int mfgpll = 0;
	enum gpufreq_posdiv posdiv = POSDIV_POWER_1;

	mfgpll = readl(MFGPLL_CON1);

	posdiv = (mfgpll & (0x7 << POSDIV_SHIFT)) >> POSDIV_SHIFT;

	return posdiv;
}

static enum gpufreq_posdiv __gpufreq_get_posdiv_by_fgpu(unsigned int freq)
{
	struct gpufreq_opp_info *signed_table = g_gpu.signed_table;
	int i = 0;

	for (i = 0; i < g_gpu.signed_opp_num; i++) {
		if (signed_table[i].freq <= freq)
			return signed_table[i].posdiv;
	}

	GPUFREQ_LOGE("fail to find post-divider of Freq: %d", freq);

	if (freq > POSDIV_2_MAX_FREQ)
		return POSDIV_POWER_1;
	else if (freq > POSDIV_4_MAX_FREQ)
		return POSDIV_POWER_2;
	else if (freq > POSDIV_8_MAX_FREQ)
		return POSDIV_POWER_4;
	else if (freq > POSDIV_16_MAX_FREQ)
		return POSDIV_POWER_8;
	else
		return POSDIV_POWER_16;
}

/* API: scale Freq of GPU via CON1 Reg or FHCTL */
static int __gpufreq_freq_scale_gpu(unsigned int freq_old, unsigned int freq_new)
{
	enum gpufreq_posdiv cur_posdiv = POSDIV_POWER_1;
	enum gpufreq_posdiv target_posdiv = POSDIV_POWER_1;
	unsigned int pcw = 0;
	unsigned int pll = 0;
	unsigned int parking = false;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("freq_old=%d, freq_new=%d", freq_old, freq_new);

	GPUFREQ_LOGD("begin to scale Fgpu: (%d->%d)", freq_old, freq_new);

	/*
	 * MFGPLL_CON1[31:31]: MFGPLL_SDM_PCW_CHG
	 * MFGPLL_CON1[26:24]: MFGPLL_POSDIV
	 * MFGPLL_CON1[21:0] : MFGPLL_SDM_PCW (DDS)
	 */
	cur_posdiv = __gpufreq_get_real_posdiv_gpu();
	target_posdiv = __gpufreq_get_posdiv_by_fgpu(freq_new);
	/* compute PCW based on target Freq */
	pcw = __gpufreq_calculate_pcw(freq_new, target_posdiv);
	if (!pcw) {
		GPUFREQ_LOGE("invalid PCW: 0x%x", pcw);
		goto done;
	}
	pll = (0x80000000) | (target_posdiv << POSDIV_SHIFT) | pcw;

#if (GPUFREQ_FHCTL_ENABLE && IS_ENABLED(CONFIG_COMMON_CLK_MTK_FREQ_HOPPING))
	if (target_posdiv != cur_posdiv)
		parking = true;
	else
		parking = false;
#else
	/* force parking if FHCTL isn't ready */
	parking = true;
#endif

	if (parking) {
		MINDONE_TRACE("MINDONE-FH: pointer=%p parking=%d %u->%u posdiv %d->%d pcw=0x%x\n",
			mtk_fh_set_rate, parking, freq_old, freq_new,
			cur_posdiv, target_posdiv, pcw);
		mindone_gf_mark(700);
		/* freq scale up */
		if (freq_new > freq_old) {
			/* 1. change PCW by hopping */
			/* MINDONE-FHNULL: the pointer is filled in by the `fhctl` module,
			 * which is not in this image, so it is NULL. The stock check is
			 * dead at the preprocessor level (`GPUFREQ_FHCTL_ENABLE` is never
			 * defined anywhere), and the call used to jump through null.
			 */
			if (unlikely(!mtk_fh_set_rate)) {
				/* MINDONE: there is no frequency-hopping driver. Write the
				 * CONSISTENT value as a whole -- multiplier, divider, and
				 * apply bit in one write. Otherwise the divider would change
				 * without the multiplier, the readback would not match the
				 * target, and the driver would stall the GPU. */
				pll = (0x80000000) | (target_posdiv << POSDIV_SHIFT) | pcw;
				MINDONE_TRACE("MINDONE-FHDIRECT: direct write CON1=0x%08x (pcw=0x%x posdiv=%d)\n",
					pll, pcw, target_posdiv);
				writel(pll, MFGPLL_CON1);
				udelay(20);
				ret = true;
			} else
			ret = mtk_fh_set_rate(MFG_PLL_NAME, pcw, target_posdiv);
			if (unlikely(!ret)) {
				__gpufreq_abort(GPUFREQ_FHCTL_EXCEPTION,
					"fail to hopping PCW: 0x%x (%d)", pcw, ret);
				ret = GPUFREQ_EINVAL;
				goto done;
			}
			/* 2. compute CON1 with target POSDIV */
			pll = (readl(MFGPLL_CON1) & 0xF8FFFFFF) | (target_posdiv << POSDIV_SHIFT);
			/* 3. change POSDIV by writing CON1 */
			writel(pll, MFGPLL_CON1);
			/* PLL spec */
			udelay(20);
		} else {
			/* 1. change POSDIV (by MFGPLL_CON1) */
			pll = (readl(MFGPLL_CON1) & 0xF8FFFFFF) | (target_posdiv << POSDIV_SHIFT);
			/* 2. change POSDIV by writing CON1 */
			writel(pll, MFGPLL_CON1);
			/* 3. wait until PLL stable */
			udelay(20);
			/* 4. change PCW by hopping */
			/* MINDONE-FHNULL: the pointer is filled in by the `fhctl` module,
			 * which is not in this image, so it is NULL. The stock check is
			 * dead at the preprocessor level (`GPUFREQ_FHCTL_ENABLE` is never
			 * defined anywhere), and the call used to jump through null.
			 */
			if (unlikely(!mtk_fh_set_rate)) {
				/* MINDONE: there is no frequency-hopping driver. Write the
				 * CONSISTENT value as a whole -- multiplier, divider, and
				 * apply bit in one write. Otherwise the divider would change
				 * without the multiplier, the readback would not match the
				 * target, and the driver would stall the GPU. */
				pll = (0x80000000) | (target_posdiv << POSDIV_SHIFT) | pcw;
				MINDONE_TRACE("MINDONE-FHDIRECT: direct write CON1=0x%08x (pcw=0x%x posdiv=%d)\n",
					pll, pcw, target_posdiv);
				writel(pll, MFGPLL_CON1);
				udelay(20);
				ret = true;
			} else
			ret = mtk_fh_set_rate(MFG_PLL_NAME, pcw, target_posdiv);
			if (unlikely(!ret)) {
				__gpufreq_abort(GPUFREQ_FHCTL_EXCEPTION,
					"fail to hopping PCW: 0x%x (%d)", pcw, ret);
				ret = GPUFREQ_EINVAL;
				goto done;
			}
		}
	} else {
#if (GPUFREQ_FHCTL_ENABLE && IS_ENABLED(CONFIG_COMMON_CLK_MTK_FREQ_HOPPING))
		if (unlikely(!mtk_fh_set_rate)) {
			__gpufreq_abort(GPUFREQ_FHCTL_EXCEPTION, "null hopping fp");
			ret = GPUFREQ_ENOENT;
			goto done;
		}

		ret = mtk_fh_set_rate(MFG_PLL_NAME, pcw, target_posdiv);
		if (unlikely(!ret)) {
			__gpufreq_abort(GPUFREQ_FHCTL_EXCEPTION,
				"fail to hopping pcw: 0x%x (%d)", pcw, ret);
			goto done;
		}
#endif
	}

	g_gpu.cur_freq = __gpufreq_get_real_fgpu();
	if (unlikely(g_gpu.cur_freq != freq_new))
		__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
			"inconsistent scaled Fgpu, cur_freq: %d, target_freq: %d",
			g_gpu.cur_freq, freq_new);

	GPUFREQ_LOGD("Fgpu: %d, PCW: 0x%x, CON1: 0x%08x", g_gpu.cur_freq, pcw, pll);

	/* because return value is different across the APIs */
	ret = GPUFREQ_SUCCESS;

	/* notify gpu freq change to DDK */
	mtk_notify_gpu_freq_change(0, freq_new);
#if GPUFREQ_HISTORY_ENABLE
	if (freq_old != freq_new) {
		gpufreq_set_history_state(HISTORY_CHANGE_FREQ_TOP);
		__gpufreq_record_history_entry();
	}
#endif

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static unsigned int __gpufreq_settle_time_vgpu(unsigned int mode, int deltaV)
{
	/* [MT6358][VGPU]
	 * DVS Rising : delta(mV) / 8(mV/us) + 4(us) + 5(us)
	 * DVS Falling: delta(mV) / 4(mV/us) + 4(us) + 5(us)
	 */
	unsigned int t_settle = 0;

	if (mode) {
		/*  rising 8 mV/us */
		t_settle = deltaV / (8 * 100) + 9;
	} else {
		/* falling 4 mV/us*/
		t_settle = deltaV / (4 * 100) + 9;
	}

	return t_settle; /* us */
}

static unsigned int __gpufreq_settle_time_vsram(unsigned int mode, int deltaV)
{
	/* [MT6358][VSRAM_GPU]
	 * DVS Rising : delta(mV) / 8(mV/us) + 3(us) + 5(us)
	 * DVS Falling: delta(mV) /  4(mV/us) + 3(us) + 5(us)
	 */
	unsigned int t_settle = 0;

	if (mode) {
		/* rising 8 mv/us*/
		t_settle = deltaV / (8 * 100) + 8;
	} else {
		/* falling 4 mv/us*/
		t_settle = deltaV / (4 * 100) + 8;
	}

	return t_settle; /* us */
}

/* API: scale vgpu and vsram via PMIC */
static int __gpufreq_volt_scale_gpu(
	unsigned int vgpu_old, unsigned int vgpu_new,
	unsigned int vsram_old, unsigned int vsram_new)
{
	unsigned int t_settle_vgpu = 0;
	unsigned int t_settle_vsram = 0;
	unsigned int t_settle = 0;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("vgpu_old=%d, vgpu_new=%d, vsram_old=%d, vsram_new=%d",
		vgpu_old, vgpu_new, vsram_old, vsram_new);

	GPUFREQ_LOGD("begin to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
		vgpu_old, vgpu_new, vsram_old, vsram_new);

	/* volt scaling up */
	if (vgpu_new > vgpu_old) {
		/* scale-up volt */
		MINDONE_TRACE("MINDONE-VU: RAISING vgpu %u->%u vsram %u->%u\n",
			vgpu_old, vgpu_new, vsram_old, vsram_new);
		mindone_gf_mark(631);
		t_settle_vgpu =
			__gpufreq_settle_time_vgpu(
				true, (vgpu_new - vgpu_old));
		t_settle_vsram =
			__gpufreq_settle_time_vsram(
				true, (vsram_new - vsram_old));

		ret = regulator_set_voltage(
				g_pmic->reg_vsram_gpu,
				vsram_new * 10,
				VSRAM_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			MINDONE_TRACE("MINDONE-VU-ERR: regulator_set_voltage(vsram) returned FAST, ret=%d\n", ret);
			mindone_gf_mark(640);
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to set VSRAM_G (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE
		/* update history record to shared memory */
		if (vsram_new != vsram_old) {
			gpufreq_set_history_state(HISTORY_VSRAM_PARK);
			gpufreq_set_history_park_volt(vsram_new);
			__gpufreq_record_history_entry();
		}
#endif

		MINDONE_TRACE("MINDONE-VU: vsram set, ret=%d\n", ret);
		mindone_gf_mark(632);
		ret = regulator_set_voltage(
				g_pmic->reg_vgpu,
				vgpu_new * 10,
				VGPU_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			MINDONE_TRACE("MINDONE-VU-ERR: regulator_set_voltage(vgpu) returned FAST, ret=%d\n", ret);
			mindone_gf_mark(641);
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to set VGPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE
		/* update history record to shared memory */
		gpufreq_set_history_state(HISTORY_CHANGE_VOLT_TOP);
		gpufreq_set_history_park_volt(vgpu_new);
		__gpufreq_record_history_entry();
#endif

		MINDONE_TRACE("MINDONE-VU: vgpu set, ret=%d\n", ret);
		mindone_gf_mark(633);
	} else if (vgpu_new < vgpu_old) {
		/* scale-down volt */
		MINDONE_TRACE("MINDONE-VS: LOWERING branch vgpu %u->%u vsram %u->%u\n",
			vgpu_old, vgpu_new, vsram_old, vsram_new);
		mindone_gf_mark(610);
		t_settle_vgpu =
			__gpufreq_settle_time_vgpu(
				false, (vgpu_old - vgpu_new));
		t_settle_vsram =
			__gpufreq_settle_time_vsram(
				false, (vsram_old - vsram_new));

		ret = regulator_set_voltage(
				g_pmic->reg_vgpu,
				vgpu_new * 10,
				VGPU_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to set VGPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE
		/* update history record to shared memory */
		gpufreq_set_history_state(HISTORY_CHANGE_VOLT_TOP);
		gpufreq_set_history_park_volt(vgpu_new);
		__gpufreq_record_history_entry();
#endif

		MINDONE_TRACE("MINDONE-VS: vgpu set, ret=%d\n", ret);
		mindone_gf_mark(611);
		ret = regulator_set_voltage(
				g_pmic->reg_vsram_gpu,
				vsram_new * 10,
				VSRAM_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to set VSRAM_GPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE
		/* update history record to shared memory */
		if (vsram_new != vsram_old)	{
			gpufreq_set_history_state(HISTORY_VSRAM_PARK);
			gpufreq_set_history_park_volt(vsram_new);
			__gpufreq_record_history_entry();
		}
#endif
		MINDONE_TRACE("MINDONE-VS: vsram set, ret=%d\n", ret);
		mindone_gf_mark(612);
	} else {
		/* keep volt */
		ret = GPUFREQ_SUCCESS;
	}

	t_settle = (t_settle_vgpu > t_settle_vsram) ?
		t_settle_vgpu : t_settle_vsram;
	udelay(t_settle);
	MINDONE_TRACE("MINDONE-VS: settle time %u us elapsed\n", t_settle);
	mindone_gf_mark(613);

	g_gpu.cur_volt = __gpufreq_get_real_vgpu();
	MINDONE_TRACE("MINDONE-VS: readback vgpu=%u, expected %u\n",
		g_gpu.cur_volt, vgpu_new);
	mindone_gf_mark(614);
	if (unlikely(g_gpu.cur_volt != vgpu_new))
		__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
			"inconsistent scaled Vgpu, cur_volt: %d, target_volt: %d",
			g_gpu.cur_volt, vgpu_new);

	g_gpu.cur_vsram = __gpufreq_get_real_vsram();
	MINDONE_TRACE("MINDONE-VS: readback vsram=%u, expected %u\n",
		g_gpu.cur_vsram, vsram_new);
	mindone_gf_mark(615);
	if (unlikely(g_gpu.cur_vsram != vsram_new))
		__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
			"inconsistent scaled Vsram, cur_vsram: %d, target_vsram: %d",
			g_gpu.cur_vsram, vsram_new);

	/* todo: GED log buffer (gpufreq_pr_logbuf) */

	GPUFREQ_LOGD("Vgpu: %d, Vsram: %d, udelay: %d",
		g_gpu.cur_volt, g_gpu.cur_vsram, t_settle);

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * API: dump power/clk status when bring-up
 */
static void __gpufreq_dump_bringup_status(struct platform_device *pdev)
{
	struct device *gpufreq_dev = &pdev->dev;
	struct resource *res = NULL;

	if (unlikely(!gpufreq_dev)) {
		GPUFREQ_LOGE("fail to find gpufreq device (ENOENT)");
		goto done;
	}

	/* 0x1000C000 */
	g_apmixed_base = __gpufreq_of_ioremap("mediatek,mt6789-apmixedsys", 0);
	if (unlikely(!g_apmixed_base)) {
		GPUFREQ_LOGE("fail to ioremap APMIXED");
		goto done;
	}

	/* 0x13FBF000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mfg_top_config");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource MFG_TOP_CONFIG");
		goto done;
	}
	g_mfg_top_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_mfg_top_base)) {
		GPUFREQ_LOGE("fail to ioremap MFG_TOP_CONFIG: 0x%llx", res->start);
		goto done;
	}

	/* 0x10006000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sleep");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource SLEEP");
		goto done;
	}
	g_sleep = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_sleep)) {
		GPUFREQ_LOGE("fail to ioremap SLEEP: 0x%llx", res->start);
		goto done;
	}

	/* 0x13000000 */
	g_mali_base = __gpufreq_of_ioremap("mediatek,mali", 0);
	if (unlikely(!g_mali_base)) {
		GPUFREQ_LOGE("fail to ioremap MALI");
		goto done;
	}

	/*
	 * [SPM] pwr_status: pwr_ack (@0x1000_616C)
	 * [SPM] pwr_status_2nd: pwr_ack_2nd (@x1000_6170)
	 * [2]: MFG0, [3]: MFG1, [4]: MFG2, [5]: MFG3
	 */
	GPUFREQ_LOGI("[GPU] MALI: 0x%08x, MFG_TOP_CONFIG: 0x%08x",
		readl(g_mali_base), readl(g_mfg_top_base));
	GPUFREQ_LOGI("[TOP] FMETER: %d, CON1: %d",
		__gpufreq_get_fmeter_fgpu(), __gpufreq_get_real_fgpu());
	GPUFREQ_LOGI("@%s: [PWR_ACK] MFG0~MFG3=0x%08X(0x%08X)\n",
		__func__,
		readl(g_sleep + 0x16C) & 0x0000003C,
		readl(g_sleep + 0x170) & 0x0000003C);

done:
	return;
}

static unsigned int __gpufreq_get_fmeter_fgpu(void)
{
#if IS_ENABLED(CONFIG_COMMON_CLK_MT6789)
	return mt_get_abist_freq(FM_MFGPLL_CK);
#else
	return 0;
#endif
}

/*
 * API: get real current frequency from CON1 (khz)
 * Freq = ((PLL_CON1[21:0] * 26M) / 2^14) / 2^PLL_CON1[26:24]
 */
static unsigned int __gpufreq_get_real_fgpu(void)
{
	unsigned int mfgpll = 0;
	unsigned int posdiv_power = 0;
	unsigned int freq = 0;
	unsigned int pcw = 0;

	GPUFREQ_LOGD("%s: MFGPLL_CON1 = 0x%x", __func__, (g_apmixed_base + 0x026C));

	mfgpll = readl(MFGPLL_CON1);

	pcw = mfgpll & (0x3FFFFF);

	posdiv_power = (mfgpll & (0x7 << POSDIV_SHIFT)) >> POSDIV_SHIFT;

	freq = (((pcw * TO_MHZ_TAIL + ROUNDING_VALUE) * MFGPLL_FIN) >> DDS_SHIFT) /
		(1 << posdiv_power) * TO_MHZ_HEAD;

	return freq;
}

/* API: get real current Vgpu from regulator (mV * 100) */
static unsigned int __gpufreq_get_real_vgpu(void)
{
	unsigned int volt = 0;

	if (regulator_is_enabled(g_pmic->reg_vgpu))
		/* regulator_get_voltage return volt with uV */
		volt = regulator_get_voltage(g_pmic->reg_vgpu) / 10;

	return volt;
}

/* API: get real current Vsram from regulator (mV * 100) */
static unsigned int __gpufreq_get_real_vsram(void)
{
	unsigned int volt = 0;

	if (regulator_is_enabled(g_pmic->reg_vsram_gpu))
		/* regulator_get_voltage return volt with uV */
		volt = regulator_get_voltage(g_pmic->reg_vsram_gpu) / 10;

	return volt;
}

static void __gpufreq_external_cg_control(void)
{
	u32 val;

	/* [F] MFG_ASYNC_CON 0x13FB_F020 [22] MEM0_MST_CG_ENABLE = 0x1 */
	/* [J] MFG_ASYNC_CON 0x13FB_F020 [23] MEM0_SLV_CG_ENABLE = 0x1 */
	/* [H] MFG_ASYNC_CON 0x13FB_F020 [24] MEM1_MST_CG_ENABLE = 0x1 */
	/* [L] MFG_ASYNC_CON 0x13FB_F020 [25] MEM1_SLV_CG_ENABLE = 0x1 */
	val = readl(g_mfg_top_base + 0x20);
	val |= (1UL << 22);
	val |= (1UL << 23);
	val |= (1UL << 24);
	val |= (1UL << 25);
	writel(val, g_mfg_top_base + 0x20);

	/* [D] MFG_GLOBAL_CON 0x13FB_F0B0 [10] GPU_CLK_FREE_RUN = 0x0 */
	/* [D] MFG_GLOBAL_CON 0x13FB_F0B0 [9] MFG_SOC_OUT_AXI_FREE_RUN = 0x0 */
	val = readl(g_mfg_top_base + 0xB0);
	val &= ~(1UL << 10);
	val &= ~(1UL << 9);
	writel(val, g_mfg_top_base + 0xB0);

	/* [D] MFG_QCHANNEL_CON 0x13FB_F0B4 [4] QCHANNEL_ENABLE = 0x1 */
	val = readl(g_mfg_top_base + 0xB4);
	val |= (1UL << 4);
	writel(val, g_mfg_top_base + 0xB4);

	/* [E] MFG_GLOBAL_CON 0x13FB_F0B0 [19] PWR_CG_FREE_RUN = 0x1 */
	/* [P] MFG_GLOBAL_CON 0x13FB_F0B0 [8] MFG_SOC_IN_AXI_FREE_RUN = 0x0 */
	val = readl(g_mfg_top_base + 0xB0);
	val |= (1UL << 19);
	val &= ~(1UL << 8);
	writel(val, g_mfg_top_base + 0xB0);

	/*[O] MFG_ASYNC_CON_1 0x13FB_F024 [0] FAXI_CK_SOC_IN_EN_ENABLE = 0x1*/
	val = readl(g_mfg_top_base + 0x24);
	val |= (1UL << 0);
	writel(val, g_mfg_top_base + 0x24);

	/* [M] MFG_I2M_PROTECTOR_CFG_00 0x13FB_FF60 [0] GPM_ENABLE = 0x1 */
	val = readl(g_mfg_top_base + 0xF60);
	val |= (1UL << 0);
	writel(val, g_mfg_top_base + 0xF60);
}

static int __gpufreq_clock_control(enum gpufreq_power_state power)
{
	int ret = GPUFREQ_SUCCESS;
	int i = 0;

	GPUFREQ_TRACE_START("power=%d", power);

	if (power == POWER_ON) {

		ret = clk_prepare_enable(g_clk->clk_mux);
		MINDONE_TRACE("MINDONE-GPUFREQ-P10: clock control entry, about to enable clk_mux\n");
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable clk_mux (%d)", ret);
			goto done;
		}

		__gpufreq_switch_clksrc(CLOCK_MAIN);
		if (readl(g_topckgen_base + 0x50) & 0x40000) {
			udelay(10);
		} else {
			GPUFREQ_LOGI("switch clock_main fail,switch again");

			while ((~readl(g_topckgen_base + 0x50)) & 0x40000) {

				__gpufreq_switch_clksrc(CLOCK_MAIN);
				udelay(10);

				if (++i > 5) {
					__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
					"fail to switch clock_main (%d)", i);
					goto done;
				}
			}
		}

		ret = clk_prepare_enable(g_clk->subsys_bg3d);
		MINDONE_TRACE("MINDONE-GPUFREQ-P11: clock source switched to MAIN confirmed\n");
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable subsys_bg3d (%d)", ret);
			goto done;
		}

		__gpufreq_external_cg_control();

		g_gpu.cg_count++;
	} else {
		clk_disable_unprepare(g_clk->subsys_bg3d);
		MINDONE_TRACE("MINDONE-GPUFREQ-P12: clock control complete (subsys_bg3d + external CG on)\n");
		__gpufreq_switch_clksrc(CLOCK_SUB);
		clk_disable_unprepare(g_clk->clk_mux);
		g_gpu.cg_count--;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * when call Linux standard genpd API(ex:pm_runtime_put_sync())
 * to operate mtcmos on/off on kernel-5.10, there maybe occur
 * unexpected behavior, hence mtcmos on/off fail
 * so we change to self control mtcmos by mfgsys driver
 * ex: __gpufreq_mfg0/1/2/3_control
 */
#if GPUFREQ_SELF_CTRL_MTCMOS
static void __gpufreq_mfg0_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == POWER_OFF) {
		/* TINFO="Set SRAM_PDN = 1" */
		writel((readl(MFG0_PWR_CON) | SRAM_PDN), MFG0_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 1" */
		while ((readl(MFG0_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE0);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE0, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set PWR_ISO = 1" */
		writel((readl(MFG0_PWR_CON) | PWR_ISO), MFG0_PWR_CON);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		writel((readl(MFG0_PWR_CON) | PWR_CLK_DIS), MFG0_PWR_CON);
		/* TINFO="Set PWR_RST_B = 0" */
		writel((readl(MFG0_PWR_CON) & ~PWR_RST_B), MFG0_PWR_CON);
		/* TINFO="Set PWR_ON = 0" */
		writel((readl(MFG0_PWR_CON) & ~PWR_ON), MFG0_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		writel((readl(MFG0_PWR_CON) & ~PWR_ON_2ND), MFG0_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((readl(PWR_STATUS) & MFG0_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG0_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE1);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE1, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {
		/* TINFO="Set PWR_ON = 1" */
		writel((readl(MFG0_PWR_CON) | PWR_ON), MFG0_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		writel((readl(MFG0_PWR_CON) | PWR_ON_2ND), MFG0_PWR_CON);
		MINDONE_TRACE("MINDONE-GPUFREQ-P4: mtcmos control entry, first register write (MFG0_PWR_CON)\n");
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((readl(PWR_STATUS) & MFG0_PWR_STA_MASK) != MFG0_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG0_PWR_STA_MASK) != MFG0_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE2);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE2, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		/* TINFO="Set PWR_CLK_DIS = 0" */
		writel((readl(MFG0_PWR_CON) & ~PWR_CLK_DIS), MFG0_PWR_CON);
		/* TINFO="Set PWR_ISO = 0" */
		writel((readl(MFG0_PWR_CON) & ~PWR_ISO), MFG0_PWR_CON);
		/* TINFO="Set PWR_RST_B = 1" */
		writel((readl(MFG0_PWR_CON) | PWR_RST_B), MFG0_PWR_CON);
		/* TINFO="Set SRAM_PDN = 0" */
		writel((readl(MFG0_PWR_CON) & ~SRAM_PDN), MFG0_PWR_CON);
		MINDONE_TRACE("MINDONE-GPUFREQ-P5: mfg0 up PWR_STATUS=0x%08x\n", g_sleep ? readl(g_sleep + 0x16C) : 0);
		/* TINFO="Wait until SRAM_PDN_ACK = 0" */
		while (readl(MFG0_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE3);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE3, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}

static void __gpufreq_mfg1_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == POWER_OFF) {
		/* TINFO="Set bus protect" */
		writel(MFG1_PROT_STEP1_0_MASK, INFRA_TOPAXI_PROTECTEN_1_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_1) & MFG1_PROT_STEP1_0_ACK_MASK) !=
			MFG1_PROT_STEP1_0_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE4);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE4, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set bus protect" */
		writel(MFG1_PROT_STEP1_1_MASK, INFRA_TOPAXI_PROTECTEN_2_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_2) & MFG1_PROT_STEP1_1_ACK_MASK) !=
			MFG1_PROT_STEP1_1_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE5);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE5, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set bus protect" */
		writel(MFG1_PROT_STEP2_0_MASK, INFRA_TOPAXI_PROTECTEN_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1) & MFG1_PROT_STEP2_0_ACK_MASK) !=
			MFG1_PROT_STEP2_0_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE6);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE6, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set bus protect" */
		writel(MFG1_PROT_STEP2_1_MASK, INFRA_TOPAXI_PROTECTEN_2_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_2) & MFG1_PROT_STEP2_1_ACK_MASK) !=
			MFG1_PROT_STEP2_1_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE7);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE7, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set SRAM_PDN = 1" */
		writel((readl(MFG1_PWR_CON) | SRAM_PDN), MFG1_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 1" */
		while ((readl(MFG1_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE8);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE8, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set PWR_ISO = 1" */
		writel((readl(MFG1_PWR_CON) | PWR_ISO), MFG1_PWR_CON);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		writel((readl(MFG1_PWR_CON) | PWR_CLK_DIS), MFG1_PWR_CON);
		/* TINFO="Set PWR_RST_B = 0" */
		writel((readl(MFG1_PWR_CON) & ~PWR_RST_B), MFG1_PWR_CON);
		/* TINFO="Set PWR_ON = 0" */
		writel((readl(MFG1_PWR_CON) & ~PWR_ON), MFG1_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		writel((readl(MFG1_PWR_CON) & ~PWR_ON_2ND), MFG1_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((readl(PWR_STATUS) & MFG1_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG1_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE9);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xE9, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {
		/* TINFO="Set PWR_ON = 1" */
		writel((readl(MFG1_PWR_CON) | PWR_ON), MFG1_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		writel((readl(MFG1_PWR_CON) | PWR_ON_2ND), MFG1_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((readl(PWR_STATUS) & MFG1_PWR_STA_MASK) != MFG1_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG1_PWR_STA_MASK) != MFG1_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xEA);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xEA, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		/* TINFO="Set PWR_CLK_DIS = 0" */
		writel((readl(MFG1_PWR_CON) & ~PWR_CLK_DIS), MFG1_PWR_CON);
		/* TINFO="Set PWR_ISO = 0" */
		writel((readl(MFG1_PWR_CON) & ~PWR_ISO), MFG1_PWR_CON);
		/* TINFO="Set PWR_RST_B = 1" */
		writel((readl(MFG1_PWR_CON) | PWR_RST_B), MFG1_PWR_CON);
		/* TINFO="Set SRAM_PDN = 0" */
		writel((readl(MFG1_PWR_CON) & ~SRAM_PDN), MFG1_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 0" */
		while (readl(MFG1_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xEB);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xEB, %s) -- timing out",
					__func__);
				return;
			}
		}

		__gpufreq_footprint_power_step(0xEC);
		/* TINFO="Release bus protect" */
		writel(MFG1_PROT_STEP2_1_MASK, INFRA_TOPAXI_PROTECTEN_2_CLR);

		__gpufreq_footprint_power_step(0xEC);
		/* TINFO="Release bus protect" */
		writel(MFG1_PROT_STEP2_0_MASK, INFRA_TOPAXI_PROTECTEN_CLR);

		__gpufreq_footprint_power_step(0xEE);
		/* TINFO="Release bus protect" */
		writel(MFG1_PROT_STEP1_1_MASK, INFRA_TOPAXI_PROTECTEN_2_CLR);

		MINDONE_TRACE("MINDONE-GPUFREQ-P6: mfg1 up PWR_STATUS=0x%08x\n", g_sleep ? readl(g_sleep + 0x16C) : 0);
		__gpufreq_footprint_power_step(0xEF);
		/* TINFO="Release bus protect" */
		writel(MFG1_PROT_STEP1_0_MASK, INFRA_TOPAXI_PROTECTEN_1_CLR);
	}
}

static void __gpufreq_mfg2_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == POWER_OFF) {
		/* TINFO="Set SRAM_PDN = 1" */
		writel((readl(MFG2_PWR_CON) | SRAM_PDN), MFG2_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 1" */
		while ((readl(MFG2_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF0);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF0, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set PWR_ISO = 1" */
		writel((readl(MFG2_PWR_CON) | PWR_ISO), MFG2_PWR_CON);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		writel((readl(MFG2_PWR_CON) | PWR_CLK_DIS), MFG2_PWR_CON);
		/* TINFO="Set PWR_RST_B = 0" */
		writel((readl(MFG2_PWR_CON) & ~PWR_RST_B), MFG2_PWR_CON);
		/* TINFO="Set PWR_ON = 0" */
		writel((readl(MFG2_PWR_CON) & ~PWR_ON), MFG2_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		writel((readl(MFG2_PWR_CON) & ~PWR_ON_2ND), MFG2_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((readl(PWR_STATUS) & MFG2_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG2_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF1);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF1, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {
		/* TINFO="Set PWR_ON = 1" */
		writel((readl(MFG2_PWR_CON) | PWR_ON), MFG2_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		writel((readl(MFG2_PWR_CON) | PWR_ON_2ND), MFG2_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((readl(PWR_STATUS) & MFG2_PWR_STA_MASK) != MFG2_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG2_PWR_STA_MASK) != MFG2_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF2);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF2, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		/* TINFO="Set PWR_CLK_DIS = 0" */
		writel((readl(MFG2_PWR_CON) & ~PWR_CLK_DIS), MFG2_PWR_CON);
		/* TINFO="Set PWR_ISO = 0" */
		writel((readl(MFG2_PWR_CON) & ~PWR_ISO), MFG2_PWR_CON);
		/* TINFO="Set PWR_RST_B = 1" */
		writel((readl(MFG2_PWR_CON) | PWR_RST_B), MFG2_PWR_CON);
		/* TINFO="Set SRAM_PDN = 0" */
		writel((readl(MFG2_PWR_CON) & ~SRAM_PDN), MFG2_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 0" */
		MINDONE_TRACE("MINDONE-GPUFREQ-P8: mfg2 up PWR_STATUS=0x%08x\n", g_sleep ? readl(g_sleep + 0x16C) : 0);
		while (readl(MFG2_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF3);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF3, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}

static void __gpufreq_mfg3_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == POWER_OFF) {
		/* TINFO="Set SRAM_PDN = 1" */
		writel((readl(MFG3_PWR_CON) | SRAM_PDN), MFG3_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 1" */
		while ((readl(MFG3_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF4);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF4, %s) -- timing out",
					__func__);
				return;
			}
		}

		/* TINFO="Set PWR_ISO = 1" */
		writel((readl(MFG3_PWR_CON) | PWR_ISO), MFG3_PWR_CON);
		/* TINFO="Set PWR_CLK_DIS = 1" */
		writel((readl(MFG3_PWR_CON) | PWR_CLK_DIS), MFG3_PWR_CON);
		/* TINFO="Set PWR_RST_B = 0" */
		writel((readl(MFG3_PWR_CON) & ~PWR_RST_B), MFG3_PWR_CON);
		/* TINFO="Set PWR_ON = 0" */
		writel((readl(MFG3_PWR_CON) & ~PWR_ON), MFG3_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 0" */
		writel((readl(MFG3_PWR_CON) & ~PWR_ON_2ND), MFG3_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 0 and PWR_STATUS_2ND = 0" */
		while ((readl(PWR_STATUS) & MFG3_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG3_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF5);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF5, %s) -- timing out",
					__func__);
				return;
			}
		}

	} else {
		/* TINFO="Set PWR_ON = 1" */
		writel((readl(MFG3_PWR_CON) | PWR_ON), MFG3_PWR_CON);
		/* TINFO="Set PWR_ON_2ND = 1" */
		writel((readl(MFG3_PWR_CON) | PWR_ON_2ND), MFG3_PWR_CON);
		/* TINFO="Wait until PWR_STATUS = 1 and PWR_STATUS_2ND = 1" */
		while (((readl(PWR_STATUS) & MFG3_PWR_STA_MASK) != MFG3_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG3_PWR_STA_MASK) != MFG3_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF6);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF6, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		/* TINFO="Set PWR_CLK_DIS = 0" */
		writel((readl(MFG3_PWR_CON) & ~PWR_CLK_DIS), MFG3_PWR_CON);
		/* TINFO="Set PWR_ISO = 0" */
		writel((readl(MFG3_PWR_CON) & ~PWR_ISO), MFG3_PWR_CON);
		/* TINFO="Set PWR_RST_B = 1" */
		writel((readl(MFG3_PWR_CON) | PWR_RST_B), MFG3_PWR_CON);
		/* TINFO="Set SRAM_PDN = 0" */
		writel((readl(MFG3_PWR_CON) & ~SRAM_PDN), MFG3_PWR_CON);
		/* TINFO="Wait until SRAM_PDN_ACK = 0" */
		MINDONE_TRACE("MINDONE-GPUFREQ-P9: mfg3 up PWR_STATUS=0x%08x\n", g_sleep ? readl(g_sleep + 0x16C) : 0);
		while (readl(MFG3_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF7);
				GPUFREQ_LOGE(
					"MINDONE: domain did not confirm ready (step 0xF7, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */

static int __gpufreq_mtcmos_control(enum gpufreq_power_state power)
{
	int ret = GPUFREQ_SUCCESS;
	u32 val = 0;

	GPUFREQ_TRACE_START("power=%d", power);

	if (power == POWER_ON) {
#if GPUFREQ_SELF_CTRL_MTCMOS
		__gpufreq_mfg0_control(POWER_ON);
		__gpufreq_mfg1_control(POWER_ON);
		ret = clk_prepare_enable(g_clk->clk_ref_mux);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable clk_ref_mux (%d)", ret);
			goto done;
		}
		__gpufreq_mfg2_control(POWER_ON);
		MINDONE_TRACE("MINDONE-GPUFREQ-P7: clk_ref_mux enabled, about to power mfg2\n");
		__gpufreq_mfg3_control(POWER_ON);
#else
		/* MFG1 on by CCF */
		ret = pm_runtime_get_sync(g_mtcmos->mfg0_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable mfg0_dev (%d)", ret);
			goto done;
		}
		ret = pm_runtime_get_sync(g_mtcmos->mfg1_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable mfg1_dev (%d)", ret);
			goto done;
		}
		if (g_shader_present & MFG2_SHADER_STACK0) {
			ret = pm_runtime_get_sync(g_mtcmos->mfg2_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
					"fail to enable mfg2_dev (%d)", ret);
				goto done;
			}
		}
		if (g_shader_present & MFG3_SHADER_STACK2) {
			ret = pm_runtime_get_sync(g_mtcmos->mfg3_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
					"fail to enable mfg3_dev (%d)", ret);
				goto done;
			}
		}
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */
#if GPUFREQ_CHECK_MTCMOS_PWR_STATUS
		/* CCF contorl, check MFG0-3 power status */
		if (g_sleep) {
			if (g_shader_present == GPU_SHADER_PRESENT_3) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_5_PWR_MASK;
				if (unlikely(val != MFG_0_5_PWR_MASK)) {
					__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
						"incorrect MFG0-5 power on status: 0x%08x", val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			} else if (g_shader_present == GPU_SHADER_PRESENT_2) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_4_PWR_MASK;
				if (unlikely(val != MFG_0_4_PWR_MASK)) {
					__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
						"incorrect MFG0-4 power on status: 0x%08x", val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			} else if (g_shader_present == GPU_SHADER_PRESENT_1) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_3_PWR_MASK;
				if (unlikely(val != MFG_0_3_PWR_MASK)) {
					__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
						"incorrect MFG0-3 power on status: 0x%08x", val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			}
		}
#endif /* GPUFREQ_CHECK_MTCMOS_PWR_STATUS */

		g_gpu.mtcmos_count++;
	} else {
#if GPUFREQ_SELF_CTRL_MTCMOS
		__gpufreq_mfg3_control(POWER_OFF);
		__gpufreq_mfg2_control(POWER_OFF);
		clk_disable_unprepare(g_clk->clk_ref_mux);
		__gpufreq_mfg1_control(POWER_OFF);
		__gpufreq_mfg0_control(POWER_OFF);
#else
		/* manually control MFG2-5 if PDC is disabled */
		if (g_shader_present & MFG3_SHADER_STACK2) {
			ret = pm_runtime_put_sync(g_mtcmos->mfg3_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
					"fail to disable mfg3_dev (%d)", ret);
				goto done;
			}
		}
		if (g_shader_present & MFG2_SHADER_STACK0) {
			ret = pm_runtime_put_sync(g_mtcmos->mfg2_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
					"fail to disable mfg2_dev (%d)", ret);
				goto done;
			}
		}
		ret = pm_runtime_put_sync(g_mtcmos->mfg1_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to enable mfg1_dev (%d)", ret);
			goto done;
		}
		/* MFG1 off by CCF */
		ret = pm_runtime_put_sync(g_mtcmos->mfg0_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
				"fail to disable mfg0_dev (%d)", ret);
			goto done;
		}
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */
#if GPUFREQ_CHECK_MTCMOS_PWR_STATUS
		/* no matter who control, check MFG1-5 power status, MFG0 is off in ATF */
		if (g_sleep && !g_gpu.mtcmos_count) {
			val = readl(g_sleep + PWR_STATUS_OFS) & MFG_1_5_PWR_MASK;
			if (unlikely(val))
				/* only print error if pwr is incorrect when mtcmos off */
				GPUFREQ_LOGE("incorrect MFG power off status: 0x%08x", val);
		}
#endif /* GPUFREQ_CHECK_MTCMOS_PWR_STATUS */

		g_gpu.mtcmos_count--;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static int __gpufreq_buck_control(enum gpufreq_power_state power)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("power=%d", power);

	/* power on */
	if (power == POWER_ON) {
		ret = regulator_enable(g_pmic->reg_vgpu);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to enable VGPU (%d)", ret);
			goto done;
		}
		ret = regulator_enable(g_pmic->reg_vsram_gpu);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to enable VSRAM_GPU (%d)",
			ret);
			goto done;
		}
		g_gpu.buck_count++;
	/* power off */
	} else {
		ret = regulator_disable(g_pmic->reg_vsram_gpu);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to disable VSRAM_GPU (%d)",
			ret);
			goto done;
		}
		ret = regulator_disable(g_pmic->reg_vgpu);
		if (unlikely(ret)) {
			__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to disable VGPU (%d)", ret);
			goto done;
		}
		g_gpu.buck_count--;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * API: find the longest and valid opp idx can be reached
 * use springboard opp index to avoid buck variation,
 * as the diff between Vgpu and Vsram must be in valid range
 * that is, MIN_BUCK_DIFF <= Vsram - Vgpu <= MAX_BUCK_DIFF
 * and Vsram always >= Vgpu, so we only focus on "MAX_BUCK_DIFF"
 */
static void __gpufreq_set_springboard(void)
{
	struct gpufreq_opp_info *opp_table = g_gpu.working_table;
	int src_idx = 0, dst_idx = 0;
	unsigned int src_vgpu = 0, src_vsram = 0;
	unsigned int dst_vgpu = 0, dst_vsram = 0;

	/* build volt scale-up springboad table */
	/* when volt scale-up: Vsram -> Vgpu */
	for (src_idx = 0; src_idx < g_gpu.opp_num; src_idx++) {
		src_vgpu = opp_table[src_idx].volt;
		/* search from the beginning of opp table */
		for (dst_idx = 0; dst_idx < g_gpu.opp_num; dst_idx++) {
			dst_vsram = opp_table[dst_idx].vsram;
			/* the smallest valid opp idx can be reached */
			if (dst_vsram - src_vgpu <=
				(mindone_max_buck_diff > 0 ? mindone_max_buck_diff : MAX_BUCK_DIFF)) {
				g_gpu.sb_table[src_idx].up = dst_idx;
				break;
			}
		}
		GPUFREQ_LOGD("springboard_up[%02d]: %d",
			src_idx, g_gpu.sb_table[src_idx].up);
	}

	/* build volt scale-down springboad table */
	/* when volt scale-down: Vgpu -> Vsram */
	for (src_idx = 0; src_idx < g_gpu.opp_num; src_idx++) {
		src_vsram = opp_table[src_idx].vsram;
		/* search from the end of opp table */
		for (dst_idx = g_gpu.min_oppidx; dst_idx >= 0 ; dst_idx--) {
			dst_vgpu = opp_table[dst_idx].volt;
			/* the largest valid opp idx can be reached */
			if (src_vsram - dst_vgpu <=
				(mindone_max_buck_diff > 0 ? mindone_max_buck_diff : MAX_BUCK_DIFF)) {
				g_gpu.sb_table[src_idx].down = dst_idx;
				break;
			}
		}
		GPUFREQ_LOGD("springboard_down[%02d]: %d",
			src_idx, g_gpu.sb_table[src_idx].down);
	}
}

/* API: init first OPP idx by init freq set in preloader */
static int __gpufreq_init_opp_idx(void)
{
	struct gpufreq_opp_info *working_table = g_gpu.working_table;
	unsigned int cur_freq = 0;
	int oppidx = 0;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START();

	/* get current GPU OPP idx by freq set in preloader */
	cur_freq = __gpufreq_get_real_fgpu();
	GPUFREQ_LOGI("preloader init freq: %d", cur_freq);

	/* decide first OPP idx by custom setting */
	if (__gpufreq_custom_init_enable()) {
		oppidx = GPUFREQ_CUST_INIT_OPPIDX;
		GPUFREQ_LOGI("custom init GPU OPP index: %d, Freq: %d",
			oppidx, working_table[oppidx].freq);
	/* decide first OPP idx by SRAMRC setting */
	} else {
		/* Restrict freq to legal opp idx */
		if (cur_freq >= working_table[0].freq) {
			oppidx = 0;
		} else if (cur_freq <= working_table[g_gpu.min_oppidx].freq) {
			oppidx = g_gpu.min_oppidx;
		/* Mapping freq to the first smaller opp idx */
		} else {
			for (oppidx = 1; oppidx < g_gpu.opp_num; oppidx++) {
				if (cur_freq >= working_table[oppidx].freq)
					break;
			}
		}
	}

	g_gpu.cur_oppidx = oppidx;
	g_gpu.cur_freq = cur_freq;
	g_gpu.cur_volt = __gpufreq_get_real_vgpu();
	g_gpu.cur_vsram = __gpufreq_get_real_vsram();

	/* init first OPP index */
	if (!__gpufreq_dvfs_enable()) {
		g_dvfs_state = DVFS_DISABLE;
		GPUFREQ_LOGI("DVFS state: 0x%x, disable DVFS", g_dvfs_state);

		/* set OPP once if DVFS is disabled but custom init is enabled */
		if (__gpufreq_custom_init_enable()) {
			ret = __gpufreq_generic_commit_gpu(oppidx, DVFS_DISABLE);
			if (unlikely(ret)) {
				GPUFREQ_LOGE("fail to commit GPU OPP index: %d (%d)",
					oppidx, ret);
				goto done;
			}
		}
	} else {
		g_dvfs_state = DVFS_FREE;
		GPUFREQ_LOGI("DVFS state: 0x%x, enable DVFS", g_dvfs_state);

		ret = __gpufreq_generic_commit_gpu(oppidx, DVFS_FREE);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to commit GPU OPP index: %d (%d)",
				oppidx, ret);
			goto done;
		}
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/* API: calculate power of every OPP in working table */
static void __gpufreq_measure_power(void)
{
	unsigned int freq = 0, volt = 0;
	unsigned int p_dynamic = 0;
	int i = 0;
	struct gpufreq_opp_info *working_table = g_gpu.working_table;
	int opp_num = g_gpu.opp_num;

	for (i = 0; i < opp_num; i++) {
		freq = working_table[i].freq;
		volt = working_table[i].volt;

		p_dynamic = __gpufreq_get_dyn_pgpu(freq, volt);

		working_table[i].power = p_dynamic;

		GPUFREQ_LOGD("GPU[%02d] power: %d ", i, p_dynamic);
	}
}

/* API: resume dvfs to free run */
static void __gpufreq_resume_dvfs(void)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START();

	__gpufreq_power_control(POWER_OFF);

if (unlikely(ret < 0))
	GPUFREQ_LOGE("fail to control power state: %d (%d)", POWER_OFF, ret);


	__gpufreq_set_dvfs_state(false, DVFS_AGING_KEEP);

	GPUFREQ_LOGI("resume DVFS, state: 0x%x", g_dvfs_state);

	GPUFREQ_TRACE_END();
}

/* API: pause dvfs to given freq and volt */
static int __gpufreq_pause_dvfs(void)
{
	int ret = GPUFREQ_SUCCESS;
	/* GPU */
	unsigned int cur_fgpu = 0, cur_vgpu = 0, cur_vsram_gpu = 0;
	unsigned int target_fgpu = 0, target_vgpu = 0, target_vsram_gpu = 0;

	GPUFREQ_TRACE_START();

	__gpufreq_set_dvfs_state(true, DVFS_AGING_KEEP);

	ret = __gpufreq_power_control(POWER_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", POWER_ON, ret);
		__gpufreq_set_dvfs_state(false, DVFS_AGING_KEEP);
		goto done;
	}

	mutex_lock(&gpufreq_lock);

	/* prepare GPU setting */
	cur_fgpu = g_gpu.cur_freq;
	cur_vgpu = g_gpu.cur_volt;
	cur_vsram_gpu = g_gpu.cur_vsram;
	target_fgpu = GPUFREQ_AGING_KEEP_FGPU;
	target_vgpu = GPUFREQ_AGING_KEEP_VGPU;
	target_vsram_gpu = VSRAM_LEVEL_0;

	GPUFREQ_LOGD("begin to commit GPU Freq: (%d->%d), Volt: (%d->%d)",
		cur_fgpu, target_fgpu, cur_vgpu, target_vgpu);

	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to scale GPU: Freq(%d->%d), Volt(%d->%d), Vsram(%d->%d)",
			cur_fgpu, target_fgpu, cur_vgpu, target_vgpu,
			cur_vsram_gpu, target_vsram_gpu);
		goto done_unlock;
	}

	GPUFREQ_LOGI("pause DVFS at GPU(%d, %d), state: 0x%x",
		target_fgpu, target_vgpu, g_dvfs_state);

done_unlock:
	mutex_unlock(&gpufreq_lock);
done:
	GPUFREQ_TRACE_END();

	return ret;
}

/*
 * API: interpolate OPP of none AVS idx.
 * step = (large - small) / range
 * vnew = large - step * j
 */
static void __gpufreq_interpolate_volt(void)
{
	int avs_num = 0;
	int front_idx = 0, rear_idx = 0, inner_idx = 0;
	unsigned int large_volt = 0, small_volt = 0;
	unsigned int large_freq = 0, small_freq = 0;
	unsigned int inner_volt = 0, inner_freq = 0;
	unsigned int previous_volt = 0;
	int range = 0;
	int slope = 0;
	int i = 0, j = 0;
	struct gpufreq_opp_info *signed_table = g_gpu.signed_table;

	avs_num = AVS_ADJ_NUM;

	mutex_lock(&gpufreq_lock);

	for (i = 1; i < avs_num; i++) {
		front_idx = g_avs_adj[i - 1].oppidx;
		rear_idx = g_avs_adj[i].oppidx;
		range = rear_idx - front_idx;

		/* freq division to amplify slope */
		large_volt = signed_table[front_idx].volt * 100;
		large_freq = signed_table[front_idx].freq / 1000;

		small_volt = signed_table[rear_idx].volt * 100;
		small_freq = signed_table[rear_idx].freq / 1000;

		/* slope = volt / freq */
		slope = (large_volt - small_volt) / (large_freq - small_freq);

		if (unlikely(slope < 0))
			__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
				"invalid slope when interpolate OPP Volt: %d", slope);

		GPUFREQ_LOGD("GPU[%02d*] Freq: %d, Volt: %d, slope: %d",
			rear_idx, small_freq*1000, small_volt, slope);

		/* start from small v and f, and use (+) instead of (-) */
		for (j = 1; j < range; j++) {
			inner_idx = rear_idx - j;
			inner_freq = signed_table[inner_idx].freq / 1000;
			inner_volt = (small_volt + slope * (inner_freq - small_freq)) / 100;
			inner_volt = VOLT_NORMALIZATION(inner_volt);

			/* compare interpolated volt with volt of previous OPP idx */
			previous_volt = signed_table[inner_idx + 1].volt;
			if (inner_volt < previous_volt)
				__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
					"invalid interpolated [%02d*] Volt: %d < [%02d*] Volt: %d",
					inner_idx, inner_volt, inner_idx + 1, previous_volt);

			signed_table[inner_idx].volt = inner_volt;
			signed_table[inner_idx].vsram = __gpufreq_get_vsram_by_vgpu(inner_volt);

			GPUFREQ_LOGD("GPU[%02d*] Freq: %d, Volt: %d, vsram: %d,",
				inner_idx, inner_freq*1000, inner_volt,
				signed_table[inner_idx].vsram);
		}
		GPUFREQ_LOGD("GPU[%02d*] Freq: %d, Volt: %d",
			front_idx, large_freq*1000, large_volt);
	}

	mutex_unlock(&gpufreq_lock);
}

/* API: apply aging volt diff to working table */
static void __gpufreq_apply_aging(unsigned int apply_aging)
{
	int i = 0;
	int signed_opp_num = g_gpu.signed_opp_num;
	GPUFREQ_TRACE_START("apply_aging=%d", apply_aging);

	mutex_lock(&gpufreq_lock);

	for (i = 0; i < g_gpu.opp_num; i++) {
		if (apply_aging)
			g_gpu.working_table[i].volt -= g_gpu.working_table[i].vaging;
		else
			g_gpu.working_table[i].volt += g_gpu.working_table[i].vaging;

		g_gpu.working_table[i].vsram =
			__gpufreq_get_vsram_by_vgpu(g_gpu.working_table[i].volt);

		GPUFREQ_LOGD("apply Vaging: %d, GPU[%02d] vgpu: %d, Vsram: %d",
			apply_aging, i, g_gpu.working_table[i].volt, g_gpu.working_table[i].vsram);
	}

	for (i = 0; i < signed_opp_num; i++) {
		if (apply_aging)
			g_gpu.signed_table[i].volt -= g_gpu.signed_table[i].vaging;
		else
			g_gpu.signed_table[i].volt += g_gpu.signed_table[i].vaging;

		g_gpu.signed_table[i].vsram =
			__gpufreq_get_vsram_by_vgpu(g_gpu.signed_table[i].volt);
	}


	//__gpufreq_set_springboard();

	mutex_unlock(&gpufreq_lock);

	GPUFREQ_TRACE_END();
}

/* API: apply given adjustment table to signed table */
static void __gpufreq_apply_adjust(struct gpufreq_adj_info *adj_table, int adj_num)
{
	int i = 0;
	int oppidx = 0;
	struct gpufreq_opp_info *signed_table = g_gpu.signed_table;
	int opp_num = g_gpu.signed_opp_num;

	GPUFREQ_TRACE_START("adj_table=0x%x, adj_num=%d",
		adj_table, adj_num);

	if (!adj_table) {
		GPUFREQ_LOGE("null adjustment table (EINVAL)");
		goto done;
	}

	mutex_lock(&gpufreq_lock);

	for (i = 0; i < adj_num; i++) {
		oppidx = adj_table[i].oppidx;
		if (oppidx >= 0 && oppidx < opp_num) {
			signed_table[oppidx].freq = adj_table[i].freq ?
				adj_table[i].freq : signed_table[oppidx].freq;
			signed_table[oppidx].volt = adj_table[i].volt ?
				adj_table[i].volt : signed_table[oppidx].volt;
			signed_table[oppidx].vsram = adj_table[i].vsram ?
				adj_table[i].vsram : signed_table[oppidx].vsram;
			signed_table[oppidx].vaging = adj_table[i].vaging ?
				adj_table[i].vaging : signed_table[oppidx].vaging;
		} else {
			GPUFREQ_LOGE("invalid adj_table[%d].oppidx: %d", i, adj_table[i].oppidx);
		}

		GPUFREQ_LOGD("%s[%02d*] Freq: %d, Volt: %d, Vsram: %d, Vaging: %d",
			oppidx, signed_table[oppidx].freq,
			signed_table[oppidx].volt,
			signed_table[oppidx].vsram,
			signed_table[oppidx].vaging);
	}

	mutex_unlock(&gpufreq_lock);

done:
	GPUFREQ_TRACE_END();
}

/* API: get Aging sensor data from EFUSE, return if success*/
static unsigned int __gpufreq_asensor_read_efuse(u32 *a_t0_lvt_rt, u32 *a_t0_ulvt_rt,
	u32 *a_shift_error, u32 *efuse_error)
{
#if GPUFREQ_ASENSOR_ENABLE
	u32 efuse_val1 = 0, efuse_val2 = 0;

	/*
	 * A_T0_LVT_RT   : 0x11F10AB8 [9:0]
	 * A_T0_ULVT_RT  : 0x11F10AB8 [19:10]
	 * A_shift_error : 0x11F10AB8 [31]
	 * efuse_error   : 0x11F10ABC [31]
	 */
	efuse_val1 = readl(g_efuse_base + 0xAB8);
	efuse_val2 = readl(g_efuse_base + 0xABC);
	if (efuse_val1 == 0 || efuse_val2 == 0)
		return false;

	GPUFREQ_LOGD("efuse_val1=0x%08x, efuse_val2=0x%08x", efuse_val1, efuse_val2);

	// todo: Check shift value, DE is SIMing
	*a_t0_lvt_rt = (efuse_val1 & 0x3FF) + 800;
	*a_t0_ulvt_rt = ((efuse_val1 & 0xFFC00) >> 10) + 1300;
	*a_shift_error = efuse_val1 >> 31;
	*efuse_error = efuse_val2 >> 31;

	g_asensor_info.efuse_val1 = efuse_val1;
	g_asensor_info.efuse_val2 = efuse_val2;
	g_asensor_info.efuse_val1_addr = 0x11F10AB8;
	g_asensor_info.efuse_val2_addr = 0x11F10ABC;
	g_asensor_info.a_t0_lvt_rt = *a_t0_lvt_rt;
	g_asensor_info.a_t0_ulvt_rt = *a_t0_ulvt_rt;

	GPUFREQ_LOGD("a_t0_lvt_rt=%08x, a_t0_ulvt_rt=%08x, a_shift_error=%08x, efuse_error=%08x",
		*a_t0_lvt_rt, *a_t0_ulvt_rt, *a_shift_error, *efuse_error);

	return true;
#else
	GPUFREQ_UNREFERENCED(a_t0_lvt_rt);
	GPUFREQ_UNREFERENCED(a_t0_ulvt_rt);
	GPUFREQ_UNREFERENCED(a_shift_error);
	GPUFREQ_UNREFERENCED(efuse_error);
	return false;
#endif /* GPUFREQ_ASENSOR_ENABLE */
}

static void __gpufreq_asensor_read_register(u32 *a_tn_lvt_cnt, u32 *a_tn_ulvt_cnt)
{
#if GPUFREQ_ASENSOR_ENABLE
	u32 aging_data0 = 0, aging_data1 = 0;

	/*
	 * Enable sensor bclk cg
	 * MFG_SENSOR_BCLK_CG 0x13fb_ff98 = 0x0000_0001
	 */
	writel(0x1, g_mfg_top_base + 0xf98);

	/*
	 * Config window setting
	 * CPE_CTRL_MCU_REG_CPEMONCTL 0x13fb_9c00 = 0x0901_031f
	 */
	writel(0x0901031f, g_mfg_cpe_control_base + 0x0);

	/*
	 * Enable CPE
	 * CPE_CTRL_MCU_REG_CEPEN  0x13fb9c04 = 0x0000_ffff
	 */
	writel(0x0000ffff, g_mfg_cpe_control_base + 0x4);

	/* wait 50us */
	udelay(50);

	/*
	 * Readout the data
	 * MFG_CPE_CTRL_SENSOR_REG_C0ASENSORDATA0 0x13FB6000 [31:16]
	 * MFG_CPE_CTRL_SENSOR_REG_C0ASENSORDATA1 0x13FB6004 [15:0]
	 */
	aging_data0 = readl(g_mfg_cpe_sensor_base + 0x0);
	aging_data1 = readl(g_mfg_cpe_sensor_base + 0x4);

	GPUFREQ_LOGD("aging_data0=0x%08x, aging_data1=0x%08x", aging_data0, aging_data1);

	*a_tn_lvt_cnt = (aging_data0 & 0xFFFF0000) >> 16;
	*a_tn_ulvt_cnt = (aging_data1 & 0xFFFF);

	g_asensor_info.a_tn_lvt_cnt = *a_tn_lvt_cnt;
	g_asensor_info.a_tn_ulvt_cnt = *a_tn_ulvt_cnt;

	GPUFREQ_LOGD("a_tn_lvt_cnt=0x%08x, a_tn_ulvt_cnt=0x%08x", *a_tn_lvt_cnt, *a_tn_ulvt_cnt);
#else
	GPUFREQ_UNREFERENCED(a_tn_lvt_cnt);
	GPUFREQ_UNREFERENCED(a_tn_ulvt_cnt);
#endif /* GPUFREQ_ASENSOR_ENABLE */
}

static unsigned int __gpufreq_get_aging_table_idx(u32 a_t0_lvt_rt, u32 a_t0_ulvt_rt,
	u32 a_shift_error, u32 efuse_error, u32 a_tn_lvt_cnt, u32 a_tn_ulvt_cnt,
	unsigned int is_efuse_read_success)
{
	unsigned int aging_table_idx = GPUFREQ_AGING_MOST_AGRRESIVE;

#if GPUFREQ_ASENSOR_ENABLE
	int tj = 0, tj1 = 0, tj2 = 0;
	int adiff = 0, adiff1 = 0, adiff2 = 0;
	unsigned int leakage_power = 0;

	/*
	 * todo: Need to check the API for 2 tj sensor belong to GPU.
	 * Ex:
	 * tj1 = get_immediate_tslvts3_0_wrap() / 1000;
	 * tj2 = get_immediate_tslvts3_1_wrap() / 1000;
	 */

	tj1 = 40;
	tj2 = 40;

	/* todo: Need to check correct fomula */
	tj = (((MAX(tj1, tj2) - 25) * 5) / 40) + 1;

	adiff1 = a_t0_lvt_rt + tj - a_tn_lvt_cnt;
	adiff2 = a_t0_ulvt_rt + tj - a_tn_ulvt_cnt;
	adiff = MAX(adiff1, adiff2);

	/*
	 * todo: Check how to get leackage power
	 * apply volt=0.8V, temp=30C to get leakage information.
	 * For leakage < 16 (mW), DISABLE aging reduction.
	 */
	leakage_power = __gpufreq_get_lkg_pgpu(0);

	g_asensor_info.tj1 = tj1;
	g_asensor_info.tj2 = tj2;
	g_asensor_info.adiff1 = adiff1;
	g_asensor_info.adiff2 = adiff2;
	g_asensor_info.leakage_power = leakage_power;

	GPUFREQ_LOGD("tj1=%d, tj2=%d, tj=%d, adiff1=%d, adiff2=%d",
		tj1, tj2, tj, adiff1, adiff2);
	GPUFREQ_LOGD("adiff=%d, leakage_power=%d, is_efuse_read_success=%d",
		adiff, leakage_power, is_efuse_read_success);

	if (g_aging_load)
		aging_table_idx = GPUFREQ_AGING_MOST_AGRRESIVE;
	else if (!is_efuse_read_success)
		aging_table_idx = 3;
	else if (MIN(tj1, tj2) < 25)
		aging_table_idx = 3;
	else if (a_shift_error || efuse_error)
		aging_table_idx = 3;
	else if (leakage_power < 16)
		aging_table_idx = 3;
	else if (adiff < GPUFREQ_AGING_GAP_MIN)
		aging_table_idx = 3;
	else if ((adiff >= GPUFREQ_AGING_GAP_MIN)
		&& (adiff < GPUFREQ_AGING_GAP_1))
		aging_table_idx = 0;
	else if ((adiff >= GPUFREQ_AGING_GAP_1)
		&& (adiff < GPUFREQ_AGING_GAP_2))
		aging_table_idx = 1;
	else if ((adiff >= GPUFREQ_AGING_GAP_2)
		&& (adiff < GPUFREQ_AGING_GAP_3))
		aging_table_idx = 2;
	else if (adiff >= GPUFREQ_AGING_GAP_3)
		aging_table_idx = 3;
	else {
		GPUFREQ_LOGW("non of the condition is true for aging_table_idx");
		aging_table_idx = 3;
	}
#else
	GPUFREQ_UNREFERENCED(a_t0_lvt_rt);
	GPUFREQ_UNREFERENCED(a_t0_ulvt_rt);
	GPUFREQ_UNREFERENCED(a_shift_error);
	GPUFREQ_UNREFERENCED(efuse_error);
	GPUFREQ_UNREFERENCED(a_tn_lvt_cnt);
	GPUFREQ_UNREFERENCED(a_tn_ulvt_cnt);
	GPUFREQ_UNREFERENCED(is_efuse_read_success);
#endif /* GPUFREQ_ASENSOR_ENABLE */

	/* check the MostAgrresive setting */
	aging_table_idx = MAX(aging_table_idx, GPUFREQ_AGING_MOST_AGRRESIVE);

	return aging_table_idx;
}

static void __gpufreq_aging_adjustment(void)
{
	struct gpufreq_adj_info *aging_adj = NULL;
	int adj_num = 0;
	int i;
	unsigned int aging_table_idx = GPUFREQ_AGING_MOST_AGRRESIVE;

#if GPUFREQ_ASENSOR_ENABLE
	u32 a_tn_lvt_cnt = 0, a_tn_ulvt_cnt = 0;
	u32 a_t0_lvt_rt = 0, a_t0_ulvt_rt = 0;
	u32 a_shift_error = 0, efuse_error = 0;
	unsigned int is_efuse_read_success = false;

	if (__gpufreq_dvfs_enable()) {
		/* keep volt for A sensor working */
		if (__gpufreq_pause_dvfs()) {
			GPUFREQ_LOGE("fail to pause DVFS for Aging Sensor");
			return;
		}

		/* get aging efuse data */
		is_efuse_read_success = __gpufreq_asensor_read_efuse(
			&a_t0_lvt_rt, &a_t0_ulvt_rt, &a_shift_error, &efuse_error);

		/* get aging sensor data */
		__gpufreq_asensor_read_register(&a_tn_lvt_cnt, &a_tn_ulvt_cnt);

		/* resume DVFS */
		__gpufreq_resume_dvfs();

		/* compute aging_table_idx with aging efuse data and aging sensor data */
		aging_table_idx = __gpufreq_get_aging_table_idx(
			a_t0_lvt_rt, a_t0_ulvt_rt, a_shift_error, efuse_error,
			a_tn_lvt_cnt, a_tn_ulvt_cnt, is_efuse_read_success);
	}

	g_asensor_info.aging_table_idx_most_agrresive = GPUFREQ_AGING_MOST_AGRRESIVE;
	g_asensor_info.aging_table_idx_choosed = aging_table_idx;

	GPUFREQ_LOGI("Aging Sensor choose aging table id: %d", aging_table_idx);
#endif /* GPUFREQ_ASENSOR_ENABLE */

	adj_num = g_gpu.signed_opp_num;

	/* prepare aging adj */
	aging_adj = kcalloc(adj_num, sizeof(struct gpufreq_adj_info), GFP_KERNEL);
	if (!aging_adj) {
		GPUFREQ_LOGE("fail to alloc gpufreq_adj_info (ENOMEM)");
		return;
	}

	for (i = 0; i < adj_num; i++) {
		aging_adj[i].oppidx = i;
		aging_adj[i].vaging = g_aging_table[aging_table_idx][i];
	}

	/* apply aging to signed table */
	__gpufreq_apply_adjust(aging_adj, adj_num);

	kfree(aging_adj);
}

static void __gpufreq_avs_adjustment(void)
{
#if GPUFREQ_AVS_ENABLE
	u32 val = 0;
	unsigned int temp_volt = 0, temp_freq = 0, volt_offset = 0;
	int i = 0, oppidx = 0;
	int adj_num = AVS_ADJ_NUM;

	/*
	 * Read AVS efuse
	 *
	 * Freq (MHz) | Signedoff Volt (V) | Efuse name | Efuse address
	 * ============================================================
	 * 1100       | 0.85               | PTPOD16    | 0x11C1_05C0
	 * 700        | 0.7                | PTPOD17    | 0x11C1_05C4
	 * 390        | 0.625              | PTPOD18    | 0x11C1_05C8
	 *
	 * 0.625 will not be lowered, but will increase according to
	 * the binning result for rescue yeild.
	 */

	/* read AVS efuse and compute Freq and Volt */
	for (i = 0; i < adj_num; i++) {
		oppidx = g_avs_adj[i].oppidx;
		val = readl(g_efuse_base + 0x5C0 + (i * 0x4));

	if (g_mcl50_load) {
		if (i == 0)
			val = 0x001C4C79;
		else if (i == 1)
			val = 0x0012BC62;
		else if (i == 2)
			val = 0x0019865C;
	}

		/* if efuse value is not set */
		if (!val)
			continue;

		/* compute Freq from efuse */
		temp_freq = 0;
		temp_freq |= (val & 0x00070000) >> 8;  // Get freq[10:8] from efuse[18:16]
		temp_freq |= (val & 0x0000FF00) >> 8;  // Get freq[7:0] from efuse[15:8]
		/* Freq is stored in efuse with MHz unit */
		temp_freq *= 1000;
		/* verify with signoff Freq */
		if (temp_freq != g_gpu.signed_table[oppidx].freq) {
			__gpufreq_abort(GPUFREQ_GPU_EXCEPTION,
				"OPP[%02d*]: AVS efuse[%d].freq(%d) != signed-off.freq(%d)",
				oppidx, i, temp_freq, g_gpu.signed_table[oppidx].freq);
			return;
		}
		g_avs_adj[i].freq = temp_freq;

		/* compute Volt from efuse */
		temp_volt = 0;
		temp_volt |= (val & 0x000000FF);       // Get volt[7:0] from efuse[7:0]
		/* Volt is stored in efuse with 6.25mV unit */
		temp_volt *= 625;

		/* clamp to signoff Volt */
		if (temp_volt > g_gpu.signed_table[oppidx].volt) {
			GPUFREQ_LOGW("OPP[%02d*]: AVS efuse[%d].volt(%d) > signed-off.volt(%d)",
				oppidx, i, temp_volt, g_gpu.signed_table[oppidx].volt);
			g_avs_adj[i].volt = g_gpu.signed_table[oppidx].volt;
		} else
			g_avs_adj[i].volt = temp_volt;

		GPUFREQ_LOGD("OPP[%02d*]: AVS efuse[%d] freq(%d), volt(%d)",
			oppidx, i, temp_freq, temp_volt);

		/* AVS is enabled if any OPP is adjusted by AVS */
		g_avs_enable = true;
	}

	/* check AVS Volt and update Vsram */
	for (i = adj_num - 1; i >= 0; i--) {
		oppidx = g_avs_adj[i].oppidx;
		/* mV * 100 */
		if (i == 0)
			volt_offset = 1250;
		else if (i == 1)
			volt_offset = 1250;
		/* if AVS Volt is not set */
		if (!g_avs_adj[i].volt)
			continue;

		/*
		 * AVS Volt reverse check, start from adj_num -2
		 * Volt of sign-off[i] should always be larger than sign-off[i+1]
		 * if not, add Volt offset to sign-off[i]
		 */
		if (i != (adj_num - 1)) {
			if (g_avs_adj[i].volt < g_avs_adj[i+1].volt) {
				GPUFREQ_LOGW("efuse[%d].volt(%d) < efuse[%d].volt(%d)",
					i, g_avs_adj[i].volt, i+1, g_avs_adj[i+1].volt);
				g_avs_adj[i].volt = g_avs_adj[i+1].volt + volt_offset;
			}
		}

		/* clamp to signoff Volt */
		if (g_avs_adj[i].volt > g_gpu.signed_table[oppidx].volt) {
			GPUFREQ_LOGW("OPP[%d*]: efuse[%d].volt(%d) > signed-off.volt(%d)",
				oppidx, i, g_avs_adj[i].volt, g_gpu.signed_table[oppidx].volt);
			g_avs_adj[i].volt = g_gpu.signed_table[oppidx].volt;
		}

		/* update Vsram */
		g_avs_adj[i].vsram = __gpufreq_get_vsram_by_vgpu(g_avs_adj[i].volt);
	}

	for (i = 0; i < adj_num; i++)
		GPUFREQ_LOGI("OPP[%d*]: efuse[%d]: freq(%d), volt(%d)",
			g_avs_adj[i].oppidx, i, g_avs_adj[i].freq, g_avs_adj[i].volt);

	/* apply AVS to signed table */
	__gpufreq_apply_adjust(g_avs_adj, adj_num);

	/* interpolate volt of non-sign-off OPP */
	__gpufreq_interpolate_volt();
#endif /* GPUFREQ_AVS_ENABLE */
}

static void __gpufreq_custom_adjustment(void)
{
	struct gpufreq_adj_info *custom_adj;
	int adj_num = 0;

	if (g_mcl50_load) {
		custom_adj = g_mcl50_adj;
		adj_num = MCL50_ADJ_NUM;
		__gpufreq_apply_adjust(custom_adj, adj_num);
		GPUFREQ_LOGI("MCL50 flavor load");
	}
}

static void __gpufreq_segment_adjustment(struct platform_device *pdev)
{
	struct gpufreq_adj_info *segment_adj;
	int adj_num = 0;
	unsigned int efuse_id = 0x0;

	switch (efuse_id) {
	case 0x2:
		segment_adj = g_segment_adj;
		adj_num = SEGMENT_ADJ_NUM;
		__gpufreq_apply_adjust(segment_adj, adj_num);
		break;
	default:
		GPUFREQ_LOGW("unknown efuse id: 0x%x", efuse_id);
		break;
	}

	GPUFREQ_LOGI("efuse_id: 0x%x, adj_num: %d", efuse_id, adj_num);
}

static void __gpufreq_init_shader_present(void)
{
	unsigned int segment_id = 0;

	segment_id = g_gpu.segment_id;

	switch (segment_id) {
	case MT6789_SEGMENT:
		g_shader_present = GPU_SHADER_PRESENT_2;
		break;
	case MT6789T_SEGMENT:
		g_shader_present = GPU_SHADER_PRESENT_2;
		break;
	default:
		g_shader_present = GPU_SHADER_PRESENT_2;
		GPUFREQ_LOGI("invalid segment id: %d", segment_id);
	}
	GPUFREQ_LOGD("segment_id: %d, shader_present: %d", segment_id, g_shader_present);
}

/*
 * 1. init working OPP range
 * 2. init working OPP table = default + adjustment
 * 3. init springboard table
 */
static int __gpufreq_init_opp_table(struct platform_device *pdev)
{
	unsigned int segment_id = 0;
	int i = 0, j = 0;
	int ret = GPUFREQ_SUCCESS;

	/* init working OPP range */
	segment_id = g_gpu.segment_id;
	if (segment_id == MT6789_SEGMENT)
		g_gpu.segment_upbound = 7;
	else if (segment_id == MT6789T_SEGMENT)
		g_gpu.segment_upbound = 0;
	else
		g_gpu.segment_upbound = 0;
	g_gpu.segment_lowbound = SIGNED_OPP_GPU_NUM - 1;
	g_gpu.signed_opp_num = SIGNED_OPP_GPU_NUM;

	g_gpu.max_oppidx = 0;
	g_gpu.min_oppidx = g_gpu.segment_lowbound - g_gpu.segment_upbound;
	g_gpu.opp_num = g_gpu.min_oppidx + 1;

	GPUFREQ_LOGD("number of signed GPU OPP: %d, upper and lower bound: [%d, %d]",
		g_gpu.signed_opp_num, g_gpu.segment_upbound, g_gpu.segment_lowbound);
	GPUFREQ_LOGI("number of working GPU OPP: %d, max and min OPP index: [%d, %d]",
		g_gpu.opp_num, g_gpu.max_oppidx, g_gpu.min_oppidx);

	g_gpu.signed_table = g_default_gpu;
	/* apply segment adjustment to GPU signed table */
	__gpufreq_segment_adjustment(pdev);
	/* apply aging adjustment to GPU signed table */
	__gpufreq_aging_adjustment();
	/* apply AVS adjustment to GPU signed table */
	__gpufreq_avs_adjustment();

	/* after these, signed table is settled down */
	g_gpu.working_table = kcalloc(g_gpu.opp_num, sizeof(struct gpufreq_opp_info), GFP_KERNEL);
	if (!g_gpu.working_table) {
		GPUFREQ_LOGE("fail to alloc gpufreq_opp_info (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	for (i = 0; i < g_gpu.opp_num; i++) {
		j = i + g_gpu.segment_upbound;
		g_gpu.working_table[i].freq = g_gpu.signed_table[j].freq;
		g_gpu.working_table[i].volt = g_gpu.signed_table[j].volt;
		g_gpu.working_table[i].vsram = g_gpu.signed_table[j].vsram;
		g_gpu.working_table[i].posdiv = g_gpu.signed_table[j].posdiv;
		g_gpu.working_table[i].vaging = g_gpu.signed_table[j].vaging;
		g_gpu.working_table[i].power = g_gpu.signed_table[j].power;

		GPUFREQ_LOGD("GPU[%02d] Freq: %d, Volt: %d, Vsram: %d, Vaging: %d",
			i, g_gpu.working_table[i].freq, g_gpu.working_table[i].volt,
			g_gpu.working_table[i].vsram, g_gpu.working_table[i].vaging);
	}

	if (g_aging_enable)
		__gpufreq_apply_aging(true);

	/* set power info to working table */
	__gpufreq_measure_power();

	/* init springboard table */
	g_gpu.sb_table = kcalloc(g_gpu.opp_num,
		sizeof(struct gpufreq_sb_info), GFP_KERNEL);
	if (!g_gpu.sb_table) {
		GPUFREQ_LOGE("fail to alloc springboard table (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	__gpufreq_set_springboard();

done:
	return ret;
}

static void __iomem *__gpufreq_of_ioremap(const char *node_name, int idx)
{
	struct device_node *node;
	void __iomem *base;

	node = of_find_compatible_node(NULL, NULL, node_name);

	if (node)
		base = of_iomap(node, idx);
	else
		base = NULL;

	return base;
}

static int __gpufreq_init_segment_id(struct platform_device *pdev)
{
	unsigned int efuse_id = 0x0;
	unsigned int segment_id = ENG_SEGMENT;
	int ret = GPUFREQ_SUCCESS;

#if IS_ENABLED(CONFIG_MTK_DEVINFO)
	struct nvmem_cell *efuse_cell;
	unsigned int *efuse_buf;
	size_t efuse_len;

	efuse_cell = nvmem_cell_get(&pdev->dev, "efuse_segment_cell");
	if (IS_ERR(efuse_cell)) {
		GPUFREQ_LOGE("fail to get efuse_segment_cell (%ld)", PTR_ERR(efuse_cell));
		ret = PTR_ERR(efuse_cell);
		goto done;
	}

	efuse_buf = (unsigned int *)nvmem_cell_read(efuse_cell, &efuse_len);
	nvmem_cell_put(efuse_cell);
	if (IS_ERR(efuse_buf)) {
		GPUFREQ_LOGE("fail to get efuse_buf (%ld)", PTR_ERR(efuse_buf));
		ret = PTR_ERR(efuse_buf);
		goto done;
	}

	efuse_id = (*efuse_buf & 0xFF);
	kfree(efuse_buf);
#else
	/* MINDONE FIX (F1934/F1935). This used to be a hardcoded 0x0, which per the
	 * lookup table gave ENG_SEGMENT -- an engineering sample, i.e. the WHOLE
	 * unbinned table of 45 states with a 1100MHz ceiling at 0.775V. Our chip is
	 * binned differently: the working 5.10 kernel, where the vendor module is
	 * built with CONFIG_MTK_DEVINFO and READS the binning, reports segment 1 --
	 * 38 states with a 1003MHz ceiling. Substituting the real value. */
	efuse_id = 0x0;   /* temporarily reverted: 0x1 breaks GED (F1937), interferes with observation */
#endif /* CONFIG_MTK_DEVINFO */

	switch (efuse_id) {
	case 0x1:
		segment_id = MT6789_SEGMENT;
		break;
	case 0x2:
		segment_id = MT6789T_SEGMENT;
		break;
	default:
		segment_id = ENG_SEGMENT;
		break;
	}

	GPUFREQ_LOGI("efuse_id: 0x%x, segment_id: %d", efuse_id, segment_id);

done:
	g_gpu.segment_id = segment_id;

	return ret;
}

static int __gpufreq_init_mtcmos(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = GPUFREQ_SUCCESS;

#if !GPUFREQ_SELF_CTRL_MTCMOS
	GPUFREQ_TRACE_START("pdev=0x%x", pdev);

	g_mtcmos = kzalloc(sizeof(struct gpufreq_mtcmos_info), GFP_KERNEL);
	if (!g_mtcmos) {
		GPUFREQ_LOGE("fail to alloc gpufreq_mtcmos_info (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	g_mtcmos->mfg0_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg0");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg0_dev)) {
		ret = g_mtcmos->mfg0_dev ? PTR_ERR(g_mtcmos->mfg0_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION, "fail to get mfg0_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg0_dev, true);

	g_mtcmos->mfg1_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg1");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg1_dev)) {
		ret = g_mtcmos->mfg1_dev ? PTR_ERR(g_mtcmos->mfg1_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION, "fail to get mfg1_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg1_dev, true);

	g_mtcmos->mfg2_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg2");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg2_dev)) {
		ret = g_mtcmos->mfg2_dev ? PTR_ERR(g_mtcmos->mfg2_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION, "fail to get mfg2_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg2_dev, true);

	g_mtcmos->mfg3_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg3");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg3_dev)) {
		ret = g_mtcmos->mfg3_dev ? PTR_ERR(g_mtcmos->mfg3_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION, "fail to get mfg3_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg3_dev, true);

done:
	GPUFREQ_TRACE_END();
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */

	return ret;
}

static int __gpufreq_init_clk(struct platform_device *pdev)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("pdev=0x%x", pdev);

	g_clk = kzalloc(sizeof(struct gpufreq_clk_info), GFP_KERNEL);
	if (!g_clk) {
		GPUFREQ_LOGE("fail to alloc gpufreq_clk_info (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	g_clk->clk_mux = devm_clk_get(&pdev->dev, "clk_mux");
	if (IS_ERR(g_clk->clk_mux)) {
		ret = PTR_ERR(g_clk->clk_mux);
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
			"fail to get clk_mux (%ld)", ret);
		goto done;
	}

	g_clk->clk_ref_mux = devm_clk_get(&pdev->dev, "clk_ref_mux");
	if (IS_ERR(g_clk->clk_ref_mux)) {
		ret = PTR_ERR(g_clk->clk_ref_mux);
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
			"fail to get clk_ref_mux (%ld)", ret);
		goto done;
	}

	g_clk->clk_main_parent = devm_clk_get(&pdev->dev, "clk_main_parent");
	if (IS_ERR(g_clk->clk_main_parent)) {
		ret = PTR_ERR(g_clk->clk_main_parent);
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
			"fail to get clk_main_parent (%ld)", ret);
		goto done;
	}

	g_clk->clk_sub_parent = devm_clk_get(&pdev->dev, "clk_sub_parent");
	if (IS_ERR(g_clk->clk_sub_parent)) {
		ret = PTR_ERR(g_clk->clk_sub_parent);
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
			"fail to get clk_sub_parent (%ld)", ret);
		goto done;
	}

	g_clk->subsys_bg3d = devm_clk_get(&pdev->dev, "subsys_bg3d");
	if (IS_ERR(g_clk->subsys_bg3d)) {
		ret = PTR_ERR(g_clk->subsys_bg3d);
		__gpufreq_abort(GPUFREQ_CCF_EXCEPTION,
			"fail to get subsys_bg3d (%ld)", ret);
		goto done;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static int __gpufreq_init_pmic(struct platform_device *pdev)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("pdev=0x%x", pdev);

	g_pmic = kzalloc(sizeof(struct gpufreq_pmic_info), GFP_KERNEL);
	if (!g_pmic) {
		GPUFREQ_LOGE("fail to alloc gpufreq_pmic_info (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	g_pmic->reg_vgpu = regulator_get_optional(&pdev->dev, "_vgpu");
	if (IS_ERR(g_pmic->reg_vgpu)) {
		ret = PTR_ERR(g_pmic->reg_vgpu);
		__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to get VGPU (%ld)", ret);
		goto done;
	}

	/* VSRAM is co-buck and controlled by SRAMRC, but use regulator to get Volt */
	g_pmic->reg_vsram_gpu = regulator_get_optional(&pdev->dev, "_vsram_gpu");
	if (IS_ERR(g_pmic->reg_vsram_gpu)) {
		ret = PTR_ERR(g_pmic->reg_vsram_gpu);
		__gpufreq_abort(GPUFREQ_PMIC_EXCEPTION, "fail to get VSRAM_GPU (%ld)", ret);
		goto done;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

/* API: init reg base address and flavor config of the platform */
static int __gpufreq_init_platform_info(struct platform_device *pdev)
{
	struct device *gpufreq_dev = &pdev->dev;
	struct device_node *of_wrapper = NULL;
	struct resource *res = NULL;
	int ret = GPUFREQ_ENOENT;

	GPUFREQ_TRACE_START("pdev=0x%x", pdev);

	if (unlikely(!gpufreq_dev)) {
		GPUFREQ_LOGE("fail to find gpufreq device (ENOENT)");
		goto done;
	}

	of_wrapper = of_find_compatible_node(NULL, NULL, "mediatek,gpufreq_wrapper");
	if (unlikely(!of_wrapper)) {
		GPUFREQ_LOGE("fail to find gpufreq_wrapper of_node");
		goto done;
	}

	/* ignore return error and use default value if property doesn't exist */
	of_property_read_u32(gpufreq_dev->of_node, "aging-load", &g_aging_load);
	of_property_read_u32(gpufreq_dev->of_node, "mcl50-load", &g_mcl50_load);
	of_property_read_u32(of_wrapper, "gpueb-support", &g_gpueb_support);

	/* 0x1000C000 */
	g_apmixed_base = __gpufreq_of_ioremap("mediatek,mt6789-apmixedsys", 0);
	if (unlikely(!g_apmixed_base)) {
		GPUFREQ_LOGE("fail to ioremap APMIXED");
		goto done;
	}

	/* 0x13FBF000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mfg_top_config");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource MFG_TOP_CONFIG");
		goto done;
	}
	g_mfg_top_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_mfg_top_base)) {
		GPUFREQ_LOGE("fail to ioremap MFG_TOP_CONFIG: 0x%llx", res->start);
		goto done;
	}

	/* 0x10006000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sleep");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource SLEEP");
		goto done;
	}
	g_sleep = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_sleep)) {
		GPUFREQ_LOGE("fail to ioremap SLEEP: 0x%llx", res->start);
		goto done;
	}

	/* 0x1020E000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "infracfg");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource infracfg");
		goto done;
	}
	g_infracfg_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_infracfg_base)) {
		GPUFREQ_LOGE("fail to ioremap infracfg: 0x%llx", res->start);
		goto done;
	}

	/* 0x10001000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "infracfg_ao");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource INFRACFG_AO");
		goto done;
	}
	g_infracfg_ao_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_infracfg_ao_base)) {
		GPUFREQ_LOGE("fail to ioremap INFRACFG_AO: 0x%llx", res->start);
		goto done;
	}

	/* 0x10023000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "infra_ao_debug_ctrl");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource INFRA_AO_DEBUG_CTRL");
		goto done;
	}
	g_infra_ao_debug_ctrl = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_infra_ao_debug_ctrl)) {
		GPUFREQ_LOGE("fail to ioremap INFRA_AO_DEBUG_CTRL: 0x%llx", res->start);
		goto done;
	}

	/* 0x1002B000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "infra_ao1_debug_ctrl");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource INFRA_AO1_DEBUG_CTRL");
		goto done;
	}
	g_infra_ao1_debug_ctrl = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_infra_ao1_debug_ctrl)) {
		GPUFREQ_LOGE("fail to ioremap INFRA_AO1_DEBUG_CTRL: 0x%llx", res->start);
		goto done;
	}

	/* 0x10042000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fmem_ao_debug_ctrl");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource fmem_ao_debug_ctrl");
		goto done;
	}
	g_fmem_ao_debug_ctrl = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_fmem_ao_debug_ctrl)) {
		GPUFREQ_LOGE("fail to ioremap fmem_ao_debug_ctrl: 0x%llx", res->start);
		goto done;
	}

	/* 0x10000000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "topckgen");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource topckgen");
		goto done;
	}
	g_topckgen_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_topckgen_base)) {
		GPUFREQ_LOGE("fail to ioremap topckgen: 0x%llx", res->start);
		goto done;
	}

	/* 0x10215000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "infra_bcrm");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource infra_bcrm");
		goto done;
	}
	g_infra_bcrm_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_infra_bcrm_base)) {
		GPUFREQ_LOGE("fail to ioremap infra_bcrm: 0x%llx", res->start);
		goto done;
	}

#if GPUFREQ_AVS_ENABLE || GPUFREQ_ASENSOR_ENABLE
	/* 0x11C10000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "efuse");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource EFUSE");
		goto done;
	}
	g_efuse_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_efuse_base)) {
		GPUFREQ_LOGE("fail to ioremap EFUSE: 0x%llx", res->start);
		goto done;
	}
#endif /* GPUFREQ_AVS_ENABLE || GPUFREQ_ASENSOR_ENABLE*/

#if GPUFREQ_ASENSOR_ENABLE
	/* 0x13FB9C00 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mfg_cpe_control");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource MFG_CPE_CONTROL");
		goto done;
	}
	g_mfg_cpe_control_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_mfg_cpe_control_base)) {
		GPUFREQ_LOGE("fail to ioremap MFG_CPE_CONTROL: 0x%llx", res->start);
		goto done;
	}

	/* 0x13FB6000 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mfg_cpe_sensor");
	if (unlikely(!res)) {
		GPUFREQ_LOGE("fail to get resource MFG_CPE_SENSOR");
		goto done;
	}
	g_mfg_cpe_sensor_base = devm_ioremap(gpufreq_dev, res->start, resource_size(res));
	if (unlikely(!g_mfg_cpe_sensor_base)) {
		GPUFREQ_LOGE("fail to ioremap MFG_CPE_SENSOR: 0x%llx", res->start);
		goto done;
	}
#endif /* GPUFREQ_ASENSOR_ENABLE */

	ret = GPUFREQ_SUCCESS;

done:
	GPUFREQ_TRACE_END();

	return ret;
}


static void __gpufreq_init_acp(void)
{
	unsigned int val;

	/* disable acp */
	val = readl(g_infracfg_ao_base + 0x290) | (0x1 << 9);
	writel(val, g_infracfg_ao_base + 0x290);
}

/* API: gpufreq driver probe */
static int __gpufreq_pdrv_probe(struct platform_device *pdev)
{
	int ret = GPUFREQ_SUCCESS;

	mindone_gf_mark(21);

	GPUFREQ_LOGI("start to probe gpufreq platform driver");

	/* keep probe successful but do nothing when bringup */
	if (__gpufreq_bringup()) {
		GPUFREQ_LOGI("skip gpufreq platform driver probe when bringup");
		__gpufreq_dump_bringup_status(pdev);
		goto done;
	}

	mindone_gf_mark(22);
	/* init reg base address and flavor config of the platform in both AP and EB mode */
	ret = __gpufreq_init_platform_info(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init platform info (%d)", ret);
		goto done;
	}

	mindone_gf_mark(23);
	/* init gpu dfd */
	ret = gpudfd_init(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init gpudfd (%d)", ret);
		goto done;
	}

	mindone_gf_mark(24);
	/* init pmic regulator */
	ret = __gpufreq_init_pmic(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init pmic (%d)", ret);
		goto done;
	}

	mindone_gf_mark(25);
	/* skip most of probe in EB mode */
	if (g_gpueb_support) {
		GPUFREQ_LOGI("gpufreq platform probe only init reg_base/dfd/pmic/fp in EB mode");
		goto register_fp;
	}

	/* init clock source */
	mindone_gf_mark(26);
	ret = __gpufreq_init_clk(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init clk (%d)", ret);
		goto done;
	}

	/* init mtcmos power domain */
	mindone_gf_mark(27);
	ret = __gpufreq_init_mtcmos(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init mtcmos (%d)", ret);
		goto done;
	}

	/* init segment id */
	mindone_gf_mark(28);
	ret = __gpufreq_init_segment_id(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init segment id (%d)", ret);
		goto done;
	}

	/* init shader present */
	mindone_gf_mark(29);
	__gpufreq_init_shader_present();

	/* power on to init first OPP index */
	ret = __gpufreq_power_control(POWER_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", POWER_ON, ret);
		goto done;
	}

	/* apply aging volt to working table volt depending on Aging load */
	g_aging_enable = g_aging_load;

#if GPUFREQ_HISTORY_ENABLE
	__gpufreq_history_memory_init();
	gpufreq_set_history_state(HISTORY_FREE);
	gpufreq_set_history_park_volt(0);
#endif

	/* init ACP */
	mindone_gf_mark(31);
	__gpufreq_init_acp();

	/* init OPP table */
	mindone_gf_mark(32);
	ret = __gpufreq_init_opp_table(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init OPP table (%d)", ret);
		goto done;
	}

	/* init first OPP index by current freq and volt */
	mindone_gf_mark(33);
	ret = __gpufreq_init_opp_idx();
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init OPP index (%d)", ret);
		goto done;
	}

	/* power off after init first OPP index */
	mindone_gf_mark(34);
	if (__gpufreq_power_ctrl_enable())
		__gpufreq_power_control(POWER_OFF);
	else
		/* never power off if power control is disabled */
		GPUFREQ_LOGI("power control always on");

	/* init AEE debug */
	mindone_gf_mark(35);
	__gpufreq_footprint_power_step_reset();
	__gpufreq_footprint_oppidx_reset();
	__gpufreq_footprint_power_count_reset();
	mindone_gf_mark(36);
#if GPUFREQ_HISTORY_ENABLE
	__gpufreq_history_memory_reset();
#endif

register_fp:
	/*
	 * GPUFREQ PLATFORM INIT DONE
	 * register differnet platform fp to wrapper depending on AP or EB mode
	 */
	if (g_gpueb_support)
		gpufreq_register_gpufreq_fp(&platform_eb_fp);
	else
		gpufreq_register_gpufreq_fp(&platform_ap_fp);

	/* init gpu ppm */
	ret = gpuppm_init(TARGET_GPU, g_gpueb_support, 0);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init gpuppm (%d)", ret);
		goto done;
	}

	GPUFREQ_LOGI("gpufreq platform driver probe done");

done:
	GPUFREQ_LOGI("gpufreq platform driver probe done");
	return ret;
}

/* API: gpufreq driver remove */
static void __gpufreq_pdrv_remove(struct platform_device *pdev)
{
#if !GPUFREQ_SELF_CTRL_MTCMOS
	dev_pm_domain_detach(g_mtcmos->mfg3_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg2_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg1_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg0_dev, true);
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */

	kfree(g_gpu.working_table);
	kfree(g_gpu.sb_table);
	kfree(g_clk);
	kfree(g_pmic);
#if !GPUFREQ_SELF_CTRL_MTCMOS
	kfree(g_mtcmos);
#endif /* GPUFREQ_SELF_CTRL_MTCMOS */

}

/* API: register gpufreq platform driver */
static int __init __gpufreq_init(void)
{
	int ret = GPUFREQ_SUCCESS;

	/* CONTROL. Mark 21 at bind entry did not fire. Until it's proven the module
	 * parameter arrives and init actually starts, this negative result means
	 * nothing. Mark 20 is the first line of init -- it runs unconditionally.
	 */
	mindone_gf_mark(20);

	if (mindone_gf_skip_register) {
		GPUFREQ_LOGI("mindone: driver registration skipped intentionally");
		return 0;
	}

	GPUFREQ_LOGI("start to init gpufreq platform driver");

	/* register gpufreq platform driver */
	ret = platform_driver_register(&g_gpufreq_pdrv);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to register gpufreq platform driver (%d)", ret);
		goto done;
	}

	GPUFREQ_LOGI("gpufreq platform driver init done");

done:
	return ret;
}

/* API: unregister gpufreq driver */
static void __exit __gpufreq_exit(void)
{
	platform_driver_unregister(&g_gpufreq_pdrv);
#if GPUFREQ_HISTORY_ENABLE
	__gpufreq_history_memory_uninit();
#endif
}

module_init(__gpufreq_init);
module_exit(__gpufreq_exit);

MODULE_DEVICE_TABLE(of, g_gpufreq_of_match);
MODULE_DESCRIPTION("MediaTek GPU-DVFS platform driver");
MODULE_LICENSE("GPL");
