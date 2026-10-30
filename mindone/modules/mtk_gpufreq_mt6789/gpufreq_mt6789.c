// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 */
#include <linux/module.h>
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

#include <gpufreq_v2_legacy.h>
#include <gpufreq_history_common.h>
#include <gpufreq_history_mt6789.h>
#include <gpufreq_debug_legacy.h>
#include <gpuppm_legacy.h>
#include <gpufreq_common_legacy.h>
#include <gpufreq_mt6789.h>

#include <mtk_gpu_utility.h>

#if IS_ENABLED(CONFIG_MTK_BATTERY_OC_POWER_THROTTLING)
#include <mtk_battery_oc_throttling.h>
#endif
#if IS_ENABLED(CONFIG_MTK_BATTERY_PERCENT_THROTTLING)
#include <mtk_bp_thl.h>
#endif
#if IS_ENABLED(CONFIG_MTK_LOW_BATTERY_POWER_THROTTLING)
#include <mtk_low_battery_throttling.h>
#endif
#if IS_ENABLED(CONFIG_MTK_STATIC_POWER)
#include <leakage_table_v2/mtk_static_power.h>
#endif
#if IS_ENABLED(CONFIG_COMMON_CLK_MTK_FREQ_HOPPING)
#include <clk-mtk.h>
#endif
#include <clk-fmeter.h>
#include <clk-mt6789-fmeter.h>
#if IS_ENABLED(CONFIG_THERMAL)
#include <linux/thermal.h>
#endif

static unsigned int __gpufreq_custom_init_enable(void);
static unsigned int __gpufreq_dvfs_enable(void);
static void __gpufreq_set_dvfs_state(unsigned int set, unsigned int state);
static void __gpufreq_measure_power(void);
static void __gpufreq_set_springboard(void);
static void __iomem *__gpufreq_of_ioremap(const char *node_name, int idx);
static int __maybe_unused __gpufreq_pause_dvfs(void);
static void __maybe_unused __gpufreq_resume_dvfs(void);
static void __gpufreq_interpolate_volt(void);
static void __gpufreq_apply_aging(unsigned int apply_aging);
static void __gpufreq_apply_adjust(struct gpufreq_adj_info *adj_table, int adj_num);

static int __gpufreq_custom_commit_gpu(unsigned int target_freq,
	unsigned int target_volt, enum gpufreq_dvfs_state key);
static int __gpufreq_freq_scale_gpu(unsigned int freq_old, unsigned int freq_new);
static int __gpufreq_volt_scale_gpu(
	unsigned int vgpu_old, unsigned int vgpu_new,
	unsigned int vsram_old, unsigned int vsram_new);
static int __gpufreq_switch_clksrc(enum gpufreq_clk_src clksrc);
static unsigned int __gpufreq_calculate_pcw(unsigned int freq, enum gpufreq_posdiv posdiv_power);
static unsigned int __gpufreq_settle_time_vgpu(unsigned int mode, int deltaV);
static unsigned int __gpufreq_settle_time_vsram(unsigned int mode, int deltaV);

static unsigned int __gpufreq_get_fmeter_fgpu(void);
static unsigned int __gpufreq_get_real_fgpu(void);
static unsigned int __gpufreq_get_real_vgpu(void);
static unsigned int __gpufreq_get_real_vsram(void);
static enum gpufreq_posdiv __gpufreq_get_real_posdiv_gpu(void);
static enum gpufreq_posdiv __gpufreq_get_posdiv_by_fgpu(unsigned int freq);

static void __maybe_unused __gpufreq_asensor_read_register(u32 *a_tn_lvt_cnt, u32 *a_tn_ulvt_cnt);
static unsigned int __maybe_unused __gpufreq_asensor_read_efuse(u32 *a_t0_lvt_rt, u32 *a_t0_ulvt_rt,
	u32 *a_shift_error, u32 *efuse_error);
static unsigned int __maybe_unused __gpufreq_get_aging_table_idx(u32 a_t0_lvt_rt, u32 a_t0_ulvt_rt,
	u32 a_shift_error, u32 efuse_error, u32 a_tn_lvt_cnt, u32 a_tn_ulvt_cnt,
	unsigned int is_efuse_read_success);

#if GPUFREQ_SELF_CTRL_MTCMOS
static void __gpufreq_mfg0_control(enum gpufreq_power_state power);
static void __gpufreq_mfg1_control(enum gpufreq_power_state power);
static void __gpufreq_mfg2_control(enum gpufreq_power_state power);
static void __gpufreq_mfg3_control(enum gpufreq_power_state power);
#endif
static void __gpufreq_external_cg_control(void);
static int __gpufreq_clock_control(enum gpufreq_power_state power);
static int __gpufreq_mtcmos_control(enum gpufreq_power_state power);
static int __gpufreq_buck_control(enum gpufreq_power_state power);

static void __gpufreq_init_shader_present(void);
static void __gpufreq_segment_adjustment(struct platform_device *pdev);
static void __maybe_unused __gpufreq_custom_adjustment(void);
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

static void __mt_update_gpufreqs_power_table(void);
static int (*mt_gpufreq_wrap_fp) (void);

static const struct of_device_id g_gpufreq_of_match[] = {
	{ .compatible = "mediatek,gpufreq" },
	{   }
};
static struct platform_driver g_gpufreq_pdrv = {
	.probe = __gpufreq_pdrv_probe,
	.remove = __gpufreq_pdrv_remove,
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
static void __iomem *g_mfg_cpe_control_base __maybe_unused;
static void __iomem *g_mfg_cpe_sensor_base __maybe_unused;
static void __iomem *g_infra_ao_mem_base __maybe_unused;
static void __iomem *g_topckgen_base;
static void __iomem *g_infra_bcrm_base;
static struct gpufreq_pmic_info *g_pmic;
static struct gpufreq_clk_info *g_clk;
static struct gpufreq_mtcmos_info *g_mtcmos __maybe_unused;
static struct gpufreq_status g_gpu;
static struct gpufreq_asensor_info g_asensor_info __maybe_unused;
static unsigned int g_shader_present;
static unsigned int g_stress_test_enable;
static unsigned int g_gpueb_support;
static unsigned int g_aging_enable;
static unsigned int g_avs_enable;
static unsigned int g_aging_load;
static unsigned int g_mcl50_load;
static unsigned int g_max_freq_off_load;
static enum gpufreq_dvfs_state g_dvfs_state;
static void __gpufreq_set_timestamp(void);
static void __gpufreq_check_bus_idle(void);

static struct mt_gpufreq_power_table_info *g_power_table;
static DEFINE_MUTEX(gpufreq_lock);
static bool g_bus_idle_timeout;

static struct gpufreq_platform_fp platform_ap_fp = {

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

	.dump_infra_status = __gpufreq_dump_infra_status,

	.get_core_mask_table = __gpufreq_get_core_mask_table,
	.get_core_num = __gpufreq_get_core_num,
	.update_power_table = __mt_update_gpufreqs_power_table,
};

static struct gpufreq_platform_fp platform_eb_fp = {

	.dump_infra_status = __gpufreq_dump_infra_status,
	.get_dyn_pgpu = __gpufreq_get_dyn_pgpu,
	.get_core_mask_table = __gpufreq_get_core_mask_table,
	.get_core_num = __gpufreq_get_core_num,
};

unsigned int __gpufreq_bringup(void)
{
	return GPUFREQ_BRINGUP;
}

unsigned int __gpufreq_power_ctrl_enable(void)
{
	return GPUFREQ_POWER_CTRL_ENABLE;
}

unsigned int __gpufreq_get_power_state(void)
{
	if (g_gpu.power_count > 0)
		return GPU_PWR_ON;
	else
		return GPU_PWR_OFF;
}

unsigned int __gpufreq_get_dvfs_state(void)
{
	return g_dvfs_state;
}

unsigned int __gpufreq_get_shader_present(void)
{
	return g_shader_present;
}

unsigned int __gpufreq_get_cur_fgpu(void)
{
	return g_gpu.cur_freq;
}

unsigned int __gpufreq_get_cur_fstack(void)
{
	return 0;
}

unsigned int __gpufreq_get_cur_vgpu(void)
{
	return g_gpu.buck_count ? g_gpu.cur_volt : 0;
}

unsigned int __gpufreq_get_cur_vstack(void)
{
	return 0;
}

unsigned int __gpufreq_get_cur_vsram_gpu(void)
{
	return g_gpu.cur_vsram;
}

unsigned int __gpufreq_get_cur_vsram_stack(void)
{
	return 0;
}

unsigned int __gpufreq_get_cur_pgpu(void)
{
	return g_gpu.working_table[g_gpu.cur_oppidx].power;
}

unsigned int __gpufreq_get_cur_pstack(void)
{
	return 0;
}

unsigned int __gpufreq_get_max_pgpu(void)
{
	return g_gpu.working_table[g_gpu.max_oppidx].power;
}

unsigned int __gpufreq_get_seg_max_opp_index(void)
{
	return g_gpu.segment_upbound;
}
EXPORT_SYMBOL(__gpufreq_get_seg_max_opp_index);

int __gpufreq_get_gpu_temp(void)
{
#if IS_ENABLED(CONFIG_THERMAL)
	struct thermal_zone_device *zone;
	int ret, temp = 0;

	zone = thermal_zone_get_zone_by_name("gpu2");
	if (IS_ERR(zone))
		return 0;

	ret = thermal_zone_get_temp(zone, &temp);
	if (ret != 0)
		return 0;

	temp /= 1000;

	return temp;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(__gpufreq_get_gpu_temp);

unsigned int __gpufreq_get_max_pstack(void)
{
	return 0;
}

unsigned int __gpufreq_get_min_pgpu(void)
{
	return g_gpu.working_table[g_gpu.min_oppidx].power;
}

unsigned int __gpufreq_get_min_pstack(void)
{
	return 0;
}

int __gpufreq_get_cur_idx_gpu(void)
{
	return g_gpu.cur_oppidx;
}

int __gpufreq_get_cur_idx_stack(void)
{
	return -1;
}

int __gpufreq_get_opp_num_gpu(void)
{
	return g_gpu.opp_num;
}

int __gpufreq_get_opp_num_stack(void)
{
	return 0;
}

int __gpufreq_get_signed_opp_num_gpu(void)
{
	return g_gpu.signed_opp_num;
}

int __gpufreq_get_signed_opp_num_stack(void)
{
	return 0;
}

const struct gpufreq_opp_info *__gpufreq_get_working_table_gpu(void)
{
	return g_gpu.working_table;
}

const struct gpufreq_opp_info *__gpufreq_get_working_table_stack(void)
{
	return NULL;
}

const struct gpufreq_opp_info *__gpufreq_get_signed_table_gpu(void)
{
	return g_gpu.signed_table;
}

const struct gpufreq_opp_info *__gpufreq_get_signed_table_stack(void)
{
	return NULL;
}

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

struct gpufreq_debug_opp_info __gpufreq_get_debug_opp_info_stack(void)
{
	struct gpufreq_debug_opp_info opp_info = {};

	return opp_info;
}

struct gpufreq_asensor_info __gpufreq_get_asensor_info(void)
{
	struct gpufreq_asensor_info asensor_info = {};

#if GPUFREQ_ASENSOR_ENABLE
	asensor_info = g_asensor_info;
#endif

	return asensor_info;
}

unsigned int __gpufreq_get_fgpu_by_idx(int oppidx)
{
	if (oppidx >= 0 && oppidx < g_gpu.opp_num)
		return g_gpu.working_table[oppidx].freq;
	else
		return 0;
}

unsigned int __gpufreq_get_fstack_by_idx(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return 0;
}

unsigned int __gpufreq_get_pgpu_by_idx(int oppidx)
{
	if (oppidx >= 0 && oppidx < g_gpu.opp_num)
		return g_gpu.working_table[oppidx].power;
	else
		return 0;
}

unsigned int __gpufreq_get_pstack_by_idx(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return 0;
}

int __gpufreq_get_idx_by_fgpu(unsigned int freq)
{
	int oppidx = -1;
	int i = 0;

	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].freq >= freq)
			break;
	}

	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

int __gpufreq_get_idx_by_fstack(unsigned int freq)
{
	GPUFREQ_UNREFERENCED(freq);

	return 0;
}

int __gpufreq_get_idx_by_vgpu(unsigned int volt)
{
	int oppidx = -1;
	int i = 0;

	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].volt >= volt)
			break;
	}

	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

int __gpufreq_get_idx_by_vstack(unsigned int volt)
{
	GPUFREQ_UNREFERENCED(volt);

	return -1;
}

int __gpufreq_get_idx_by_pgpu(unsigned int power)
{
	int oppidx = -1;
	int i = 0;

	for (i = g_gpu.min_oppidx; i >= g_gpu.max_oppidx; i--) {
		if (g_gpu.working_table[i].power >= power)
			break;
	}

	oppidx = (i > g_gpu.max_oppidx) ? i : g_gpu.max_oppidx;

	return oppidx;
}

int __gpufreq_get_idx_by_pstack(unsigned int power)
{
	GPUFREQ_UNREFERENCED(power);

	return -1;
}

unsigned int __gpufreq_get_vsram_by_vgpu(unsigned int vgpu)
{
	unsigned int vsram;

	if (vgpu > VSRAM_FIXED_THRESHOLD)
		vsram = vgpu + VSRAM_FIXED_DIFF;
	else
		vsram = VSRAM_FIXED_VOLT;

	return vsram;
}

unsigned int __gpufreq_get_lkg_pgpu(unsigned int volt, int temper)
{
	GPUFREQ_UNREFERENCED(volt);
	GPUFREQ_UNREFERENCED(temper);

	return GPU_LEAKAGE_POWER;
}

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

unsigned int __gpufreq_get_lkg_pstack(unsigned int volt, int temper)
{
	GPUFREQ_UNREFERENCED(volt);

	return 0;
}

unsigned int __gpufreq_get_dyn_pstack(unsigned int freq, unsigned int volt)
{
	GPUFREQ_UNREFERENCED(freq);
	GPUFREQ_UNREFERENCED(volt);

	return 0;
}

int __gpufreq_power_control(enum gpufreq_power_state power)
{
	int ret = 0;
	u32 val __maybe_unused;

	GPUFREQ_TRACE_START("power=%d", power);

	mutex_lock(&gpufreq_lock);

	GPUFREQ_LOGD("switch power: %s (Power: %d, Buck: %d, MTCMOS: %d, CG: %d)",
			power ? "On" : "Off",
			g_gpu.power_count, g_gpu.buck_count,
			g_gpu.mtcmos_count, g_gpu.cg_count);

	if (power == GPU_PWR_ON) {
		g_gpu.power_count++;
	} else {
		g_gpu.power_count--;

	}
	__gpufreq_footprint_power_count(g_gpu.power_count);

	if (power == GPU_PWR_ON && g_gpu.power_count == 1 && g_bus_idle_timeout) {
		g_bus_idle_timeout = false;
	} else if (power == GPU_PWR_ON && g_gpu.power_count == 1) {
		__gpufreq_footprint_power_step(0x01);

		ret = __gpufreq_buck_control(GPU_PWR_ON);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control Buck: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x02);

		ret = __gpufreq_mtcmos_control(GPU_PWR_ON);
		if (unlikely(ret < 0)) {
			GPUFREQ_LOGE("fail to control MTCMOS: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x03);

		ret = __gpufreq_clock_control(GPU_PWR_ON);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control CLOCK: On (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x04);

		__gpufreq_set_timestamp();

		g_dvfs_state &= ~DVFS_POWEROFF;
	} else if (power == GPU_PWR_OFF && g_gpu.power_count == 0) {

		__gpufreq_check_bus_idle();
		if (g_bus_idle_timeout) {
			GPUFREQ_LOGE("bus not idle after timeout, power-off aborted, GPU stays on");
			ret = -ETIMEDOUT;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x05);

		g_dvfs_state |= DVFS_POWEROFF;
		__gpufreq_footprint_power_step(0x06);

		ret = __gpufreq_clock_control(GPU_PWR_OFF);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control CLOCK: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x07);

		ret = __gpufreq_mtcmos_control(GPU_PWR_OFF);
		if (unlikely(ret < 0)) {
			GPUFREQ_LOGE("fail to control MTCMOS: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}
		__gpufreq_footprint_power_step(0x08);

		ret = __gpufreq_buck_control(GPU_PWR_OFF);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to control Buck: Off (%d)", ret);
			ret = GPUFREQ_EINVAL;
			goto done_unlock;
		}

		__gpufreq_footprint_power_step(0x09);
	}

	ret = g_gpu.power_count;

done_unlock:
	GPUFREQ_LOGD("PWR_STATUS (0x%x): 0x%08x",
		(0x10006000 + 0x16C), readl(g_sleep + 0x16C));

	mutex_unlock(&gpufreq_lock);

	GPUFREQ_TRACE_END();

	return ret;
}

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

	if (target_oppidx < 0 || target_oppidx >= opp_num) {
		GPUFREQ_LOGE("invalid target GPU OPP index: %d (OPP_NUM: %d)",
			target_oppidx, opp_num);
		ret = GPUFREQ_EINVAL;
		goto done;
	}

	mutex_lock(&gpufreq_lock);

	if (g_dvfs_state & ~key) {
		GPUFREQ_LOGD("unavailable dvfs state (0x%x)", g_dvfs_state);
		ret = GPUFREQ_SUCCESS;
		goto done_unlock;
	}

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

	if (target_freq == cur_freq) {

		ret = __gpufreq_volt_scale_gpu(
			cur_volt, target_volt, cur_vsram, target_vsram);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
				cur_volt, target_volt, cur_vsram, target_vsram);
			goto done_unlock;
		}
	} else if (target_freq > cur_freq) {

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

		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
	} else {

		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}

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

		gpufreq_set_history_state(HISTORY_FREE);
		__gpufreq_record_history_entry();
	} else
		g_gpu.cur_oppidx = target_oppidx;
#else
	g_gpu.cur_oppidx = target_oppidx;
#endif
	__gpufreq_footprint_oppidx(target_oppidx);

done_unlock:
	mutex_unlock(&gpufreq_lock);

done:
	GPUFREQ_TRACE_END();

	return ret;
}

int __gpufreq_generic_commit_stack(int target_oppidx, enum gpufreq_dvfs_state key)
{
	GPUFREQ_UNREFERENCED(target_oppidx);
	GPUFREQ_UNREFERENCED(key);

	return GPUFREQ_EINVAL;
}

int __gpufreq_generic_commit_dual(int target_oppidx_gpu, int target_oppidx_stack,
				  enum gpufreq_dvfs_state key)
{
	GPUFREQ_UNREFERENCED(target_oppidx_gpu);
	GPUFREQ_UNREFERENCED(target_oppidx_stack);
	GPUFREQ_UNREFERENCED(key);

	return GPUFREQ_EINVAL;
}

int __gpufreq_fix_target_oppidx_gpu(int oppidx)
{
	int opp_num = g_gpu.opp_num;
	int ret = GPUFREQ_SUCCESS;

	ret = __gpufreq_power_control(GPU_PWR_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", GPU_PWR_ON, ret);
		goto done;
	}

	if (oppidx == -1) {
		__gpufreq_set_dvfs_state(false, DVFS_FIX_OPP);
		ret = GPUFREQ_SUCCESS;
	} else if (oppidx >= 0 && oppidx < opp_num) {
		__gpufreq_set_dvfs_state(true, DVFS_FIX_OPP);

		ret = __gpufreq_generic_commit_gpu(oppidx, DVFS_FIX_OPP);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to commit GPU OPP index: %d (%d)", oppidx, ret);
			__gpufreq_set_dvfs_state(false, DVFS_FIX_OPP);
		}
	} else {
		GPUFREQ_LOGE("invalid fixed OPP index: %d", oppidx);
		ret = GPUFREQ_EINVAL;
	}

	__gpufreq_power_control(GPU_PWR_OFF);

done:
	return ret;
}

int __gpufreq_fix_target_oppidx_stack(int oppidx)
{
	GPUFREQ_UNREFERENCED(oppidx);

	return GPUFREQ_EINVAL;
}

int __gpufreq_fix_custom_freq_volt_gpu(unsigned int freq, unsigned int volt)
{
	int ret = GPUFREQ_SUCCESS;

	ret = __gpufreq_power_control(GPU_PWR_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", GPU_PWR_ON, ret);
		goto done;
	}

	if (freq == 0 && volt == 0) {
		mutex_lock(&gpufreq_lock);
		g_dvfs_state &= ~DVFS_FIX_OPP;
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
		g_dvfs_state |= DVFS_FIX_OPP;
		mutex_unlock(&gpufreq_lock);

		ret = __gpufreq_custom_commit_gpu(freq, volt, DVFS_FIX_OPP);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to custom commit GPU freq: %d, volt: %d (%d)",
				freq, volt, ret);
			mutex_lock(&gpufreq_lock);
			g_dvfs_state &= ~DVFS_FIX_OPP;
			mutex_unlock(&gpufreq_lock);
		}
	}

	__gpufreq_power_control(GPU_PWR_OFF);

done:
	return ret;
}

int __gpufreq_fix_custom_freq_volt_stack(unsigned int freq, unsigned int volt)
{
	GPUFREQ_UNREFERENCED(freq);
	GPUFREQ_UNREFERENCED(volt);

	return GPUFREQ_EINVAL;
}

void __gpufreq_set_timestamp(void)
{

	writel(0x00000003, g_mfg_top_base + 0x130);
}

static void __gpufreq_check_bus_idle(void)
{
	u32 val __maybe_unused;
	unsigned long long reads = 0;

	writel(0x00000001, g_mfg_top_base + 0xB4);

	writel(0x00000003, g_mfg_top_base + 0x170);

	g_bus_idle_timeout = false;
	do {
		val = readl(g_mfg_top_base + 0x178);
		reads++;
		if ((val & 0x4) == 0x4)
			break;
		udelay(10);
	} while (reads < 100000);

	if ((val & 0x4) != 0x4) {
		g_bus_idle_timeout = true;
		GPUFREQ_LOGE("bus not idle after 1s, reads=%llu val=0x%08x", reads, val);
	}
}

void __gpufreq_dump_infra_status(char *log_buf, int *log_len, int log_size)
{
	u32 val __maybe_unused = 0;

	GPUFREQ_LOGI("== [GPUFREQ INFRA STATUS] ==");

	if (g_infracfg_base) {

		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[EMI]",
			(0x1020E000 + 0x810), readl(g_infracfg_base + 0x810),
			(0x1020E000 + 0x814), readl(g_infracfg_base + 0x814));
	}

	if (g_infracfg_ao_base && g_infra_ao_debug_ctrl) {

		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x10001000 + 0xCA0), readl(g_infracfg_ao_base + 0xCA0),
			(0x10001000 + 0xCAC), readl(g_infracfg_ao_base + 0xCAC),
			(0x10023000 + 0x000), readl(g_infra_ao_debug_ctrl + 0x000));
	}

	if (g_infra_ao1_debug_ctrl && g_infra_peri_debug3) {

		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x1002B000 + 0x000), readl(g_infra_ao1_debug_ctrl + 0x000),
			(0x1002E000 + 0x000), readl(g_infra_peri_debug3 + 0x000));
	}

	if (g_infra_peri_debug4 && g_fmem_ao_debug_ctrl) {

		GPUFREQ_LOGI("%-7s (0x%x): 0x%08x, (0x%x): 0x%08x",
			"[INFRA]",
			(0x10040000 + 0x000), readl(g_infra_peri_debug4 + 0x000),
			(0x10042000 + 0x000), readl(g_fmem_ao_debug_ctrl + 0x000));
	}

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

int __gpufreq_get_batt_oc_idx(int batt_oc_level)
{
#if 0
	if (batt_oc_level == BATTERY_OC_LEVEL_1)
		return __gpufreq_get_idx_by_fgpu(GPUFREQ_BATT_OC_FREQ);
	else
		return GPUPPM_RESET_IDX;
#else
	GPUFREQ_UNREFERENCED(batt_oc_level);

	return GPUPPM_KEEP_IDX;
#endif
}

int __gpufreq_get_batt_percent_idx(int batt_percent_level)
{
#if 0
	if (batt_percent_level == BATTERY_PERCENT_LEVEL_1)
		return GPUFREQ_BATT_PERCENT_IDX - g_gpu.segment_upbound;
	else
		return GPUPPM_RESET_IDX;
#else
	GPUFREQ_UNREFERENCED(batt_percent_level);

	return GPUPPM_KEEP_IDX;
#endif
}

int __gpufreq_get_low_batt_idx(int low_batt_level)
{
#if 0
	if (low_batt_level == LOW_BATTERY_LEVEL_2)
		return __gpufreq_get_idx_by_fgpu(GPUFREQ_LOW_BATT_FREQ);
	else
		return GPUPPM_RESET_IDX;
#else
	GPUFREQ_UNREFERENCED(low_batt_level);

	return GPUPPM_KEEP_IDX;
#endif
}

void __gpufreq_set_stress_test(unsigned int mode)
{
	g_stress_test_enable = mode;
}

int __gpufreq_set_aging_mode(unsigned int mode)
{

	if (g_aging_enable ^ mode) {
		__gpufreq_apply_aging(mode);
		g_aging_enable = mode;

		__gpufreq_measure_power();

		return GPUFREQ_SUCCESS;
	} else {
		return GPUFREQ_EINVAL;
	}
}

struct gpufreq_core_mask_info *__gpufreq_get_core_mask_table(void)
{
	return g_core_mask_table;
}

unsigned int __gpufreq_get_core_num(void)
{
	return SHADER_CORE_NUM;
}

static unsigned int __gpufreq_custom_init_enable(void)
{
	return GPUFREQ_CUST_INIT_ENABLE;
}

static unsigned int __gpufreq_dvfs_enable(void)
{
	return GPUFREQ_DVFS_ENABLE;
}

static void __gpufreq_set_dvfs_state(unsigned int set, unsigned int state)
{
	mutex_lock(&gpufreq_lock);
	if (set)
		g_dvfs_state |= state;
	else
		g_dvfs_state &= ~state;
	mutex_unlock(&gpufreq_lock);
}

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

		ret = __gpufreq_volt_scale_gpu(
			cur_volt, target_volt, cur_vsram, target_vsram);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Vgpu: (%d->%d), Vsram_gpu: (%d->%d)",
				cur_volt, target_volt, cur_vsram, target_vsram);
			goto done_unlock;
		}
	} else if (target_freq > cur_freq) {

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

		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}
	} else {

		ret = __gpufreq_freq_scale_gpu(cur_freq, target_freq);
		if (unlikely(ret)) {
			GPUFREQ_LOGE("fail to scale Fgpu: (%d->%d)",
				cur_freq, target_freq);
			goto done_unlock;
		}

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
			__gpufreq_abort("fail to switch GPU to CLOCK_MAIN (%d)", ret);
		goto done;
		}
	} else if (clksrc == CLOCK_SUB) {
		ret = clk_set_parent(g_clk->clk_mux, g_clk->clk_sub_parent);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to switch GPU to CLOCK_SUB (%d)", ret);
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

static unsigned int __gpufreq_calculate_pcw(unsigned int freq, enum gpufreq_posdiv posdiv)
{

	unsigned int pcw = 0;

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

static int __gpufreq_freq_scale_gpu(unsigned int freq_old, unsigned int freq_new)
{
	enum gpufreq_posdiv cur_posdiv __maybe_unused = POSDIV_POWER_1;
	enum gpufreq_posdiv target_posdiv = POSDIV_POWER_1;
	unsigned int pcw = 0;
	unsigned int pll = 0;
	unsigned int parking = false;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START("freq_old=%d, freq_new=%d", freq_old, freq_new);

	GPUFREQ_LOGD("begin to scale Fgpu: (%d->%d)", freq_old, freq_new);

	cur_posdiv = __gpufreq_get_real_posdiv_gpu();
	target_posdiv = __gpufreq_get_posdiv_by_fgpu(freq_new);

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

	parking = true;
#endif

	if (!g_clk->clk_fhctl) {
		ret = __gpufreq_switch_clksrc(CLOCK_SUB);
		if (unlikely(ret))
			goto done;
		writel(pll, MFGPLL_CON1);
		udelay(20);
		ret = __gpufreq_switch_clksrc(CLOCK_MAIN);
		if (unlikely(ret))
			goto done;
	} else if (parking) {

		if (freq_new > freq_old) {

			ret = clk_prepare_enable(g_clk->clk_fhctl);
			if (unlikely(ret)) {
				__gpufreq_abort("fail to hopping PCW: 0x%x (%d)", pcw, ret);
				goto done;
			}
			ret = clk_set_rate(g_clk->clk_fhctl, freq_new*1000);
			clk_disable_unprepare(g_clk->clk_fhctl);

			pll = (readl(MFGPLL_CON1) & 0xF8FFFFFF) | (target_posdiv << POSDIV_SHIFT);

			writel(pll, MFGPLL_CON1);

			udelay(20);
		} else {

			pll = (readl(MFGPLL_CON1) & 0xF8FFFFFF) | (target_posdiv << POSDIV_SHIFT);

			writel(pll, MFGPLL_CON1);

			udelay(20);

			ret = clk_prepare_enable(g_clk->clk_fhctl);
			if (unlikely(ret)) {
				__gpufreq_abort("fail to enable clk_fhctl: 0x%x (%d)", pcw, ret);
				goto done;
			}
			ret = clk_set_rate(g_clk->clk_fhctl, freq_new*1000);
			clk_disable_unprepare(g_clk->clk_fhctl);
		}
	} else {
#if (GPUFREQ_FHCTL_ENABLE && IS_ENABLED(CONFIG_COMMON_CLK_MTK_FREQ_HOPPING))
		ret = clk_prepare_enable(g_clk->clk_fhctl);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable clk_fhctl (%d)", ret);
			goto done;
		}

		ret = clk_set_rate(g_clk->clk_fhctl, freq_new*1000);
		clk_disable_unprepare(g_clk->clk_fhctl);
#endif
	}

	g_gpu.cur_freq = __gpufreq_get_real_fgpu();
	if (unlikely(g_gpu.cur_freq != freq_new))
		__gpufreq_abort("inconsistent scaled Fgpu, cur_freq: %d, target_freq: %d",
			g_gpu.cur_freq, freq_new);

	GPUFREQ_LOGD("Fgpu: %d, PCW: 0x%x, CON1: 0x%08x", g_gpu.cur_freq, pcw, pll);

	ret = GPUFREQ_SUCCESS;

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

	unsigned int t_settle = 0;

	if (mode) {

		t_settle = deltaV / (8 * 100) + 9;
	} else {

		t_settle = deltaV / (4 * 100) + 9;
	}

	return t_settle;
}

static unsigned int __gpufreq_settle_time_vsram(unsigned int mode, int deltaV)
{

	unsigned int t_settle = 0;

	if (mode) {

		t_settle = deltaV / (8 * 100) + 8;
	} else {

		t_settle = deltaV / (4 * 100) + 8;
	}

	return t_settle;
}

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

	if (vgpu_new > vgpu_old) {

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
			__gpufreq_abort("fail to set VSRAM_G (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE

		if (vsram_new != vsram_old) {
			gpufreq_set_history_state(HISTORY_VSRAM_PARK);
			gpufreq_set_history_park_volt(vsram_new);
			__gpufreq_record_history_entry();
		}
#endif

		ret = regulator_set_voltage(
				g_pmic->reg_vgpu,
				vgpu_new * 10,
				VGPU_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			__gpufreq_abort( "fail to set VGPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE

		gpufreq_set_history_state(HISTORY_CHANGE_VOLT_TOP);
		gpufreq_set_history_park_volt(vgpu_new);
		__gpufreq_record_history_entry();
#endif

	} else if (vgpu_new < vgpu_old) {

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
			__gpufreq_abort("fail to set VGPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE

		gpufreq_set_history_state(HISTORY_CHANGE_VOLT_TOP);
		gpufreq_set_history_park_volt(vgpu_new);
		__gpufreq_record_history_entry();
#endif

		ret = regulator_set_voltage(
				g_pmic->reg_vsram_gpu,
				vsram_new * 10,
				VSRAM_MAX_VOLT * 10 + 125);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to set VSRAM_GPU (%d)", ret);
			goto done;
		}
#if GPUFREQ_HISTORY_ENABLE

		if (vsram_new != vsram_old)	{
			gpufreq_set_history_state(HISTORY_VSRAM_PARK);
			gpufreq_set_history_park_volt(vsram_new);
			__gpufreq_record_history_entry();
		}
#endif
	} else {

		ret = GPUFREQ_SUCCESS;
	}

	t_settle = (t_settle_vgpu > t_settle_vsram) ?
		t_settle_vgpu : t_settle_vsram;
	udelay(t_settle);

	g_gpu.cur_volt = __gpufreq_get_real_vgpu();
	if (unlikely(g_gpu.cur_volt != vgpu_new))
		__gpufreq_abort("inconsistent scaled Vgpu, cur_volt: %d, target_volt: %d",
			g_gpu.cur_volt, vgpu_new);

	g_gpu.cur_vsram = __gpufreq_get_real_vsram();
	if (unlikely(g_gpu.cur_vsram != vsram_new))
		__gpufreq_abort("inconsistent scaled Vsram, cur_vsram: %d, target_vsram: %d",
			g_gpu.cur_vsram, vsram_new);

	GPUFREQ_LOGD("Vgpu: %d, Vsram: %d, udelay: %d",
		g_gpu.cur_volt, g_gpu.cur_vsram, t_settle);

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static unsigned int __gpufreq_get_fmeter_fgpu(void)
{
	return mt_get_abist_freq(FM_MFGPLL_CK);
}

static unsigned int __gpufreq_get_real_fgpu(void)
{
	unsigned int mfgpll = 0;
	unsigned int posdiv_power = 0;
	unsigned int freq = 0;
	unsigned int pcw = 0;

	mfgpll = readl(MFGPLL_CON1);

	pcw = mfgpll & (0x3FFFFF);

	posdiv_power = (mfgpll & (0x7 << POSDIV_SHIFT)) >> POSDIV_SHIFT;

	freq = (((pcw * TO_MHZ_TAIL + ROUNDING_VALUE) * MFGPLL_FIN) >> DDS_SHIFT) /
		(1 << posdiv_power) * TO_MHZ_HEAD;

	return freq;
}

static unsigned int __gpufreq_get_real_vgpu(void)
{
	unsigned int volt = 0;

	if (regulator_is_enabled(g_pmic->reg_vgpu))

		volt = regulator_get_voltage(g_pmic->reg_vgpu) / 10;

	return volt;
}

static unsigned int __gpufreq_get_real_vsram(void)
{
	unsigned int volt = 0;

	if (regulator_is_enabled(g_pmic->reg_vsram_gpu))

		volt = regulator_get_voltage(g_pmic->reg_vsram_gpu) / 10;

	return volt;
}

static void __gpufreq_external_cg_control(void)
{
	u32 val __maybe_unused;

	val = readl(g_mfg_top_base + 0x20);
	val |= (1UL << 22);
	val |= (1UL << 23);
	val |= (1UL << 24);
	val |= (1UL << 25);
	writel(val, g_mfg_top_base + 0x20);

	val = readl(g_mfg_top_base + 0xB0);
	val &= ~(1UL << 10);
	val &= ~(1UL << 9);
	writel(val, g_mfg_top_base + 0xB0);

	val = readl(g_mfg_top_base + 0xB4);
	val |= (1UL << 4);
	writel(val, g_mfg_top_base + 0xB4);

	val = readl(g_mfg_top_base + 0xB0);
	val |= (1UL << 19);
	val &= ~(1UL << 8);
	writel(val, g_mfg_top_base + 0xB0);

	val = readl(g_mfg_top_base + 0x24);
	val |= (1UL << 0);
	writel(val, g_mfg_top_base + 0x24);

	val = readl(g_mfg_top_base + 0xF60);
	val |= (1UL << 0);
	writel(val, g_mfg_top_base + 0xF60);
}

static int __gpufreq_clock_control(enum gpufreq_power_state power)
{
	int ret = GPUFREQ_SUCCESS;
	int i = 0;

	GPUFREQ_TRACE_START("power=%d", power);

	if (power == GPU_PWR_ON) {

		ret = clk_prepare_enable(g_clk->clk_mux);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable clk_mux (%d)", ret);
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
					__gpufreq_abort("fail to switch clock_main (%d)", i);
					goto done;
				}
			}
		}

		ret = clk_prepare_enable(g_clk->subsys_bg3d);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable subsys_bg3d (%d)", ret);
			goto done;
		}

		__gpufreq_external_cg_control();

		g_gpu.cg_count++;
	} else {
		clk_disable_unprepare(g_clk->subsys_bg3d);
		__gpufreq_switch_clksrc(CLOCK_SUB);
		clk_disable_unprepare(g_clk->clk_mux);
		g_gpu.cg_count--;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

#if GPUFREQ_SELF_CTRL_MTCMOS
static void __gpufreq_mfg0_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == GPU_PWR_OFF) {

		writel((readl(MFG0_PWR_CON) | SRAM_PDN), MFG0_PWR_CON);

		while ((readl(MFG0_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE0);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE0, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel((readl(MFG0_PWR_CON) | PWR_ISO), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) | PWR_CLK_DIS), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) & ~PWR_RST_B), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) & ~PWR_ON), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) & ~PWR_ON_2ND), MFG0_PWR_CON);

		while ((readl(PWR_STATUS) & MFG0_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG0_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE1);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE1, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {

		writel((readl(MFG0_PWR_CON) | PWR_ON), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) | PWR_ON_2ND), MFG0_PWR_CON);

		while (((readl(PWR_STATUS) & MFG0_PWR_STA_MASK) != MFG0_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG0_PWR_STA_MASK) != MFG0_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE2);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE2, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		writel((readl(MFG0_PWR_CON) & ~PWR_CLK_DIS), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) & ~PWR_ISO), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) | PWR_RST_B), MFG0_PWR_CON);

		writel((readl(MFG0_PWR_CON) & ~SRAM_PDN), MFG0_PWR_CON);

		while (readl(MFG0_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE3);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE3, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}

static void __gpufreq_mfg1_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == GPU_PWR_OFF) {

		writel(MFG1_PROT_STEP1_0_MASK, INFRA_TOPAXI_PROTECTEN_1_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_1) & MFG1_PROT_STEP1_0_ACK_MASK) !=
			MFG1_PROT_STEP1_0_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE4);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE4, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel(MFG1_PROT_STEP1_1_MASK, INFRA_TOPAXI_PROTECTEN_2_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_2) & MFG1_PROT_STEP1_1_ACK_MASK) !=
			MFG1_PROT_STEP1_1_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE5);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE5, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel(MFG1_PROT_STEP2_0_MASK, INFRA_TOPAXI_PROTECTEN_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1) & MFG1_PROT_STEP2_0_ACK_MASK) !=
			MFG1_PROT_STEP2_0_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE6);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE6, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel(MFG1_PROT_STEP2_1_MASK, INFRA_TOPAXI_PROTECTEN_2_SET);
		while ((readl(INFRA_TOPAXI_PROTECTSTA1_2) & MFG1_PROT_STEP2_1_ACK_MASK) !=
			MFG1_PROT_STEP2_1_ACK_MASK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE7);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE7, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel((readl(MFG1_PWR_CON) | SRAM_PDN), MFG1_PWR_CON);

		while ((readl(MFG1_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE8);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE8, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel((readl(MFG1_PWR_CON) | PWR_ISO), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) | PWR_CLK_DIS), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) & ~PWR_RST_B), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) & ~PWR_ON), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) & ~PWR_ON_2ND), MFG1_PWR_CON);

		while ((readl(PWR_STATUS) & MFG1_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG1_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xE9);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xE9, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {

		writel((readl(MFG1_PWR_CON) | PWR_ON), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) | PWR_ON_2ND), MFG1_PWR_CON);

		while (((readl(PWR_STATUS) & MFG1_PWR_STA_MASK) != MFG1_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG1_PWR_STA_MASK) != MFG1_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xEA);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xEA, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		writel((readl(MFG1_PWR_CON) & ~PWR_CLK_DIS), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) & ~PWR_ISO), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) | PWR_RST_B), MFG1_PWR_CON);

		writel((readl(MFG1_PWR_CON) & ~SRAM_PDN), MFG1_PWR_CON);

		while (readl(MFG1_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xEB);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xEB, %s) -- timing out",
					__func__);
				return;
			}
		}

		__gpufreq_footprint_power_step(0xEC);

		writel(MFG1_PROT_STEP2_1_MASK, INFRA_TOPAXI_PROTECTEN_2_CLR);

		__gpufreq_footprint_power_step(0xEC);

		writel(MFG1_PROT_STEP2_0_MASK, INFRA_TOPAXI_PROTECTEN_CLR);

		__gpufreq_footprint_power_step(0xEE);

		writel(MFG1_PROT_STEP1_1_MASK, INFRA_TOPAXI_PROTECTEN_2_CLR);

		__gpufreq_footprint_power_step(0xEF);

		writel(MFG1_PROT_STEP1_0_MASK, INFRA_TOPAXI_PROTECTEN_1_CLR);
	}
}

static void __gpufreq_mfg2_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == GPU_PWR_OFF) {

		writel((readl(MFG2_PWR_CON) | SRAM_PDN), MFG2_PWR_CON);

		while ((readl(MFG2_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF0);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF0, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel((readl(MFG2_PWR_CON) | PWR_ISO), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) | PWR_CLK_DIS), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) & ~PWR_RST_B), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) & ~PWR_ON), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) & ~PWR_ON_2ND), MFG2_PWR_CON);

		while ((readl(PWR_STATUS) & MFG2_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG2_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF1);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF1, %s) -- timing out",
					__func__);
				return;
			}
		}
	} else {

		writel((readl(MFG2_PWR_CON) | PWR_ON), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) | PWR_ON_2ND), MFG2_PWR_CON);

		while (((readl(PWR_STATUS) & MFG2_PWR_STA_MASK) != MFG2_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG2_PWR_STA_MASK) != MFG2_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF2);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF2, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		writel((readl(MFG2_PWR_CON) & ~PWR_CLK_DIS), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) & ~PWR_ISO), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) | PWR_RST_B), MFG2_PWR_CON);

		writel((readl(MFG2_PWR_CON) & ~SRAM_PDN), MFG2_PWR_CON);

		while (readl(MFG2_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF3);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF3, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}

static void __gpufreq_mfg3_control(enum gpufreq_power_state power)
{
	int i = 0;

	if (power == GPU_PWR_OFF) {

		writel((readl(MFG3_PWR_CON) | SRAM_PDN), MFG3_PWR_CON);

		while ((readl(MFG3_PWR_CON) & SRAM_PDN_ACK) != SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF4);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF4, %s) -- timing out",
					__func__);
				return;
			}
		}

		writel((readl(MFG3_PWR_CON) | PWR_ISO), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) | PWR_CLK_DIS), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) & ~PWR_RST_B), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) & ~PWR_ON), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) & ~PWR_ON_2ND), MFG3_PWR_CON);

		while ((readl(PWR_STATUS) & MFG3_PWR_STA_MASK) ||
			(readl(PWR_STATUS_2ND) & MFG3_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF5);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF5, %s) -- timing out",
					__func__);
				return;
			}
		}

	} else {

		writel((readl(MFG3_PWR_CON) | PWR_ON), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) | PWR_ON_2ND), MFG3_PWR_CON);

		while (((readl(PWR_STATUS) & MFG3_PWR_STA_MASK) != MFG3_PWR_STA_MASK) ||
			((readl(PWR_STATUS_2ND) & MFG3_PWR_STA_MASK) != MFG3_PWR_STA_MASK)) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF6);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF6, %s) -- timing out",
					__func__);
				return;
			}
		}

		udelay(100);

		writel((readl(MFG3_PWR_CON) & ~PWR_CLK_DIS), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) & ~PWR_ISO), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) | PWR_RST_B), MFG3_PWR_CON);

		writel((readl(MFG3_PWR_CON) & ~SRAM_PDN), MFG3_PWR_CON);

		while (readl(MFG3_PWR_CON) & SRAM_PDN_ACK) {
			udelay(10);
			if (++i > 500) {
				__gpufreq_footprint_power_step(0xF7);
				__gpufreq_abort(
					"domain did not confirm ready (step 0xF7, %s) -- timing out",
					__func__);
				return;
			}
		}
	}
}
#endif

static int __gpufreq_mtcmos_control(enum gpufreq_power_state power)
{
	int ret = GPUFREQ_SUCCESS;
	u32 val __maybe_unused = 0;

	GPUFREQ_TRACE_START("power=%d", power);

	if (power == GPU_PWR_ON) {
#if GPUFREQ_SELF_CTRL_MTCMOS
		__gpufreq_mfg0_control(GPU_PWR_ON);
		__gpufreq_mfg1_control(GPU_PWR_ON);
		ret = clk_prepare_enable(g_clk->clk_ref_mux);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable clk_ref_mux (%d)", ret);
			goto done;
		}
		__gpufreq_mfg2_control(GPU_PWR_ON);
		__gpufreq_mfg3_control(GPU_PWR_ON);
#else

		ret = pm_runtime_get_sync(g_mtcmos->mfg0_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort("fail to enable mfg0_dev (%d)", ret);
			goto done;
		}
		ret = pm_runtime_get_sync(g_mtcmos->mfg1_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort("fail to enable mfg1_dev (%d)", ret);
			goto done;
		}
		if (g_shader_present & MFG2_SHADER_STACK0) {
			ret = pm_runtime_get_sync(g_mtcmos->mfg2_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort("fail to enable mfg2_dev (%d)", ret);
				goto done;
			}
		}
		if (g_shader_present & MFG3_SHADER_STACK2) {
			ret = pm_runtime_get_sync(g_mtcmos->mfg3_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort("fail to enable mfg3_dev (%d)", ret);
				goto done;
			}
		}
#endif
#if GPUFREQ_CHECK_MTCMOS_PWR_STATUS

		if (g_sleep) {
			if (g_shader_present == GPU_SHADER_PRESENT_3) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_5_PWR_MASK;
				if (unlikely(val != MFG_0_5_PWR_MASK)) {
					__gpufreq_abort("incorrect MFG0-5 power on status: 0x%08x",
							val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			} else if (g_shader_present == GPU_SHADER_PRESENT_2) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_4_PWR_MASK;
				if (unlikely(val != MFG_0_4_PWR_MASK)) {
					__gpufreq_abort("incorrect MFG0-4 power on status: 0x%08x",
							val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			} else if (g_shader_present == GPU_SHADER_PRESENT_1) {
				val = readl(g_sleep + PWR_STATUS_OFS) & MFG_0_3_PWR_MASK;
				if (unlikely(val != MFG_0_3_PWR_MASK)) {
					__gpufreq_abort("incorrect MFG0-3 power on status: 0x%08x",
							val);
					ret = GPUFREQ_EINVAL;
					goto done;
				}
			}
		}
#endif

		g_gpu.mtcmos_count++;
	} else {
#if GPUFREQ_SELF_CTRL_MTCMOS
		__gpufreq_mfg3_control(GPU_PWR_OFF);
		__gpufreq_mfg2_control(GPU_PWR_OFF);
		clk_disable_unprepare(g_clk->clk_ref_mux);
		__gpufreq_mfg1_control(GPU_PWR_OFF);
		__gpufreq_mfg0_control(GPU_PWR_OFF);
#else

		if (g_shader_present & MFG3_SHADER_STACK2) {
			ret = pm_runtime_put_sync(g_mtcmos->mfg3_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort("fail to disable mfg3_dev (%d)", ret);
				goto done;
			}
		}
		if (g_shader_present & MFG2_SHADER_STACK0) {
			ret = pm_runtime_put_sync(g_mtcmos->mfg2_dev);
			if (unlikely(ret < 0)) {
				__gpufreq_abort("fail to disable mfg2_dev (%d)", ret);
				goto done;
			}
		}

		ret = pm_runtime_put_sync(g_mtcmos->mfg1_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort("fail to enable mfg1_dev (%d)", ret);
			goto done;
		}

		ret = pm_runtime_put_sync(g_mtcmos->mfg0_dev);
		if (unlikely(ret < 0)) {
			__gpufreq_abort("fail to disable mfg0_dev (%d)", ret);
			goto done;
		}
#endif
#if GPUFREQ_CHECK_MTCMOS_PWR_STATUS

		if (g_sleep && !g_gpu.mtcmos_count) {
			val = readl(g_sleep + PWR_STATUS_OFS) & MFG_1_5_PWR_MASK;
			if (unlikely(val))

				GPUFREQ_LOGE("incorrect MFG power off status: 0x%08x", val);
		}
#endif

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

	if (power == GPU_PWR_ON) {
		ret = regulator_enable(g_pmic->reg_vgpu);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable VGPU (%d)", ret);
			goto done;
		}
		ret = regulator_enable(g_pmic->reg_vsram_gpu);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to enable VSRAM_GPU (%d)", ret);
			goto done;
		}
		g_gpu.buck_count++;

	} else {
		ret = regulator_disable(g_pmic->reg_vsram_gpu);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to disable VSRAM_GPU (%d)", ret);
			goto done;
		}
		ret = regulator_disable(g_pmic->reg_vgpu);
		if (unlikely(ret)) {
			__gpufreq_abort("fail to disable VGPU (%d)", ret);
			goto done;
		}
		g_gpu.buck_count--;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static void __gpufreq_set_springboard(void)
{
	struct gpufreq_opp_info *opp_table = g_gpu.working_table;
	int src_idx = 0, dst_idx = 0;
	unsigned int src_vgpu = 0, src_vsram = 0;
	unsigned int dst_vgpu = 0, dst_vsram = 0;

	for (src_idx = 0; src_idx < g_gpu.opp_num; src_idx++) {
		src_vgpu = opp_table[src_idx].volt;

		for (dst_idx = 0; dst_idx < g_gpu.opp_num; dst_idx++) {
			dst_vsram = opp_table[dst_idx].vsram;

			if (dst_vsram - src_vgpu <= MAX_BUCK_DIFF) {
				g_gpu.sb_table[src_idx].up = dst_idx;
				break;
			}
		}
		GPUFREQ_LOGD("springboard_up[%02d]: %d",
			src_idx, g_gpu.sb_table[src_idx].up);
	}

	for (src_idx = 0; src_idx < g_gpu.opp_num; src_idx++) {
		src_vsram = opp_table[src_idx].vsram;

		for (dst_idx = g_gpu.min_oppidx; dst_idx >= 0 ; dst_idx--) {
			dst_vgpu = opp_table[dst_idx].volt;

			if (src_vsram - dst_vgpu <= MAX_BUCK_DIFF) {
				g_gpu.sb_table[src_idx].down = dst_idx;
				break;
			}
		}
		GPUFREQ_LOGD("springboard_down[%02d]: %d",
			src_idx, g_gpu.sb_table[src_idx].down);
	}
}

static int __gpufreq_init_opp_idx(void)
{
	struct gpufreq_opp_info *working_table = g_gpu.working_table;
	unsigned int cur_freq = 0;
	int oppidx = 0;
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START();

	cur_freq = __gpufreq_get_real_fgpu();
	GPUFREQ_LOGI("preloader init freq: %d", cur_freq);

	if (__gpufreq_custom_init_enable()) {
		oppidx = GPUFREQ_CUST_INIT_OPPIDX;
		GPUFREQ_LOGI("custom init GPU OPP index: %d, Freq: %d",
			oppidx, working_table[oppidx].freq);

	} else {

		if (cur_freq >= working_table[0].freq) {
			oppidx = 0;
		} else if (cur_freq <= working_table[g_gpu.min_oppidx].freq) {
			oppidx = g_gpu.min_oppidx;

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

	if (!__gpufreq_dvfs_enable()) {
		g_dvfs_state = DVFS_DISABLE;
		GPUFREQ_LOGI("DVFS state: 0x%x, disable DVFS", g_dvfs_state);

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

static void __maybe_unused __gpufreq_resume_dvfs(void)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_TRACE_START();

	__gpufreq_power_control(GPU_PWR_OFF);

if (unlikely(ret < 0))
	GPUFREQ_LOGE("fail to control power state: %d (%d)", GPU_PWR_OFF, ret);

	__gpufreq_set_dvfs_state(false, DVFS_AGING_KEEP);

	GPUFREQ_LOGI("resume DVFS, state: 0x%x", g_dvfs_state);

	GPUFREQ_TRACE_END();
}

static int __maybe_unused __gpufreq_pause_dvfs(void)
{
	int ret = GPUFREQ_SUCCESS;

	unsigned int cur_fgpu = 0, cur_vgpu = 0, cur_vsram_gpu = 0;
	unsigned int target_fgpu = 0, target_vgpu = 0, target_vsram_gpu = 0;

	GPUFREQ_TRACE_START();

	__gpufreq_set_dvfs_state(true, DVFS_AGING_KEEP);

	ret = __gpufreq_power_control(GPU_PWR_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", GPU_PWR_ON, ret);
		__gpufreq_set_dvfs_state(false, DVFS_AGING_KEEP);
		goto done;
	}

	mutex_lock(&gpufreq_lock);

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

		large_volt = signed_table[front_idx].volt * 100;
		large_freq = signed_table[front_idx].freq / 1000;

		small_volt = signed_table[rear_idx].volt * 100;
		small_freq = signed_table[rear_idx].freq / 1000;

		slope = (large_volt - small_volt) / (large_freq - small_freq);

		if (unlikely(slope < 0))
			__gpufreq_abort("invalid slope when interpolate OPP Volt: %d", slope);

		GPUFREQ_LOGD("GPU[%02d*] Freq: %d, Volt: %d, slope: %d",
			rear_idx, small_freq*1000, small_volt, slope);

		for (j = 1; j < range; j++) {
			inner_idx = rear_idx - j;
			inner_freq = signed_table[inner_idx].freq / 1000;
			inner_volt = (small_volt + slope * (inner_freq - small_freq)) / 100;
			inner_volt = VOLT_NORMALIZATION(inner_volt);

			previous_volt = signed_table[inner_idx + 1].volt;
			if (inner_volt < previous_volt)
				__gpufreq_abort
				    ("invalid interpolated [%02d*] Volt: %d < [%02d*] Volt: %d",
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

static void __gpufreq_apply_aging(unsigned int apply_aging)
{
	int i = 0;
	struct gpufreq_opp_info *working_table = g_gpu.working_table;
	int opp_num = g_gpu.opp_num;

	mutex_lock(&gpufreq_lock);

	for (i = 0; i < opp_num; i++) {
		if (apply_aging)
			working_table[i].volt -= working_table[i].margin;
		else
			working_table[i].volt += working_table[i].margin;

		working_table[i].vsram = __gpufreq_get_vsram_by_vgpu(working_table[i].volt);

		GPUFREQ_LOGD("apply Vaging: %d, GPU[%02d] Volt: %d, Vsram: %d",
			     apply_aging, i, working_table[i].volt, working_table[i].vsram);
	}

	__gpufreq_set_springboard();

	mutex_unlock(&gpufreq_lock);
}

static void __gpufreq_apply_adjust(struct gpufreq_adj_info *adj_table, int adj_num)
{
	int i = 0;
	int oppidx = 0;
	struct gpufreq_opp_info *signed_table = g_gpu.signed_table;
	int opp_num = g_gpu.signed_opp_num;

	GPUFREQ_TRACE_START("adj_table=0x%x, adj_num=%d", adj_table, adj_num, target);

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
		} else {
			GPUFREQ_LOGE("invalid adj_table[%d].oppidx: %d", i, adj_table[i].oppidx);
		}

		GPUFREQ_LOGD("[%02d*] Freq: %d, Volt: %d, Vsram: %d",
			oppidx, signed_table[oppidx].freq,
			     signed_table[oppidx].volt, signed_table[oppidx].vsram);
	}

	mutex_unlock(&gpufreq_lock);

done:
	GPUFREQ_TRACE_END();
}

static unsigned int __maybe_unused __gpufreq_asensor_read_efuse(u32 *a_t0_lvt_rt, u32 *a_t0_ulvt_rt,
	u32 *a_shift_error, u32 *efuse_error)
{
#if GPUFREQ_ASENSOR_ENABLE
	u32 efuse_val1 = 0, efuse_val2 = 0;

	efuse_val1 = readl(g_efuse_base + 0xAB8);
	efuse_val2 = readl(g_efuse_base + 0xABC);
	if (efuse_val1 == 0 || efuse_val2 == 0)
		return false;

	GPUFREQ_LOGD("efuse_val1=0x%08x, efuse_val2=0x%08x", efuse_val1, efuse_val2);

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
#endif
}

static void __maybe_unused __gpufreq_asensor_read_register(u32 *a_tn_lvt_cnt, u32 *a_tn_ulvt_cnt)
{
#if GPUFREQ_ASENSOR_ENABLE
	u32 aging_data0 = 0, aging_data1 = 0;

	writel(0x1, g_mfg_top_base + 0xf98);

	writel(0x0901031f, g_mfg_cpe_control_base + 0x0);

	writel(0x0000ffff, g_mfg_cpe_control_base + 0x4);

	udelay(50);

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
#endif
}

static unsigned int __maybe_unused __gpufreq_get_aging_table_idx(u32 a_t0_lvt_rt, u32 a_t0_ulvt_rt,
	u32 a_shift_error, u32 efuse_error, u32 a_tn_lvt_cnt, u32 a_tn_ulvt_cnt,
	unsigned int is_efuse_read_success)
{
	unsigned int aging_table_idx = GPUFREQ_AGING_MOST_AGRRESIVE;

#if GPUFREQ_ASENSOR_ENABLE
	int tj = 0, tj1 = 0, tj2 = 0;
	int adiff = 0, adiff1 = 0, adiff2 = 0;
	unsigned int leakage_power = 0;

	tj1 = 40;
	tj2 = 40;

	tj = (((MAX(tj1, tj2) - 25) * 5) / 40) + 1;

	adiff1 = a_t0_lvt_rt + tj - a_tn_lvt_cnt;
	adiff2 = a_t0_ulvt_rt + tj - a_tn_ulvt_cnt;
	adiff = MAX(adiff1, adiff2);

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
#endif

	aging_table_idx = MAX(aging_table_idx, GPUFREQ_AGING_MOST_AGRRESIVE);

	return aging_table_idx;
}

static void __gpufreq_aging_adjustment(void)
{
	struct gpufreq_adj_info *aging_adj = NULL;
	int adj_num = 0;
	int i;

#if GPUFREQ_ASENSOR_ENABLE
	unsigned int aging_table_idx = GPUFREQ_AGING_MOST_AGRRESIVE;
	u32 a_tn_lvt_cnt = 0, a_tn_ulvt_cnt = 0;
	u32 a_t0_lvt_rt = 0, a_t0_ulvt_rt = 0;
	u32 a_shift_error = 0, efuse_error = 0;
	unsigned int is_efuse_read_success = false;

	if (__gpufreq_dvfs_enable()) {

		if (__gpufreq_pause_dvfs()) {
			GPUFREQ_LOGE("fail to pause DVFS for Aging Sensor");
			return;
		}

		is_efuse_read_success = __gpufreq_asensor_read_efuse(
			&a_t0_lvt_rt, &a_t0_ulvt_rt, &a_shift_error, &efuse_error);

		__gpufreq_asensor_read_register(&a_tn_lvt_cnt, &a_tn_ulvt_cnt);

		__gpufreq_resume_dvfs();

		aging_table_idx = __gpufreq_get_aging_table_idx(
			a_t0_lvt_rt, a_t0_ulvt_rt, a_shift_error, efuse_error,
			a_tn_lvt_cnt, a_tn_ulvt_cnt, is_efuse_read_success);
	}

	g_asensor_info.aging_table_idx_most_agrresive = GPUFREQ_AGING_MOST_AGRRESIVE;
	g_asensor_info.aging_table_idx_choosed = aging_table_idx;

	GPUFREQ_LOGI("Aging Sensor choose aging table id: %d", aging_table_idx);
#endif

	adj_num = g_gpu.signed_opp_num;

	aging_adj = kcalloc(adj_num, sizeof(struct gpufreq_adj_info), GFP_KERNEL);
	if (!aging_adj) {
		GPUFREQ_LOGE("fail to alloc gpufreq_adj_info (ENOMEM)");
		return;
	}

	for (i = 0; i < adj_num; i++) {
		aging_adj[i].oppidx = i;

	}

	__gpufreq_apply_adjust(aging_adj, adj_num);

	kfree(aging_adj);
}

static void __gpufreq_avs_adjustment(void)
{
#if GPUFREQ_AVS_ENABLE
	u32 val __maybe_unused = 0;
	unsigned int temp_volt = 0, temp_freq = 0, volt_offset = 0;
	int i = 0, oppidx = 0;
	int adj_num = AVS_ADJ_NUM;

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

		if (!val)
			continue;

		temp_freq = 0;
		temp_freq |= (val & 0x00070000) >> 8;
		temp_freq |= (val & 0x0000FF00) >> 8;

		temp_freq *= 1000;

		if (temp_freq != g_gpu.signed_table[oppidx].freq) {
			__gpufreq_abort("OPP[%02d*]: AVS efuse[%d].freq(%d) != signed-off.freq(%d)",
				oppidx, i, temp_freq, g_gpu.signed_table[oppidx].freq);
			return;
		}
		g_avs_adj[i].freq = temp_freq;

		temp_volt = 0;
		temp_volt |= (val & 0x000000FF);

		temp_volt *= 625;

		if (temp_volt > g_gpu.signed_table[oppidx].volt) {
			GPUFREQ_LOGW("OPP[%02d*]: AVS efuse[%d].volt(%d) > signed-off.volt(%d)",
				oppidx, i, temp_volt, g_gpu.signed_table[oppidx].volt);
			g_avs_adj[i].volt = g_gpu.signed_table[oppidx].volt;
		} else
			g_avs_adj[i].volt = temp_volt;

		GPUFREQ_LOGD("OPP[%02d*]: AVS efuse[%d] freq(%d), volt(%d)",
			oppidx, i, temp_freq, temp_volt);

		g_avs_enable = true;
	}

	for (i = adj_num - 1; i >= 0; i--) {
		oppidx = g_avs_adj[i].oppidx;

		if (i == 0)
			volt_offset = 1250;
		else if (i == 1)
			volt_offset = 1250;

		if (!g_avs_adj[i].volt)
			continue;

		if (i != (adj_num - 1)) {
			if (g_avs_adj[i].volt < g_avs_adj[i+1].volt) {
				GPUFREQ_LOGW("efuse[%d].volt(%d) < efuse[%d].volt(%d)",
					i, g_avs_adj[i].volt, i+1, g_avs_adj[i+1].volt);
				g_avs_adj[i].volt = g_avs_adj[i+1].volt + volt_offset;
			}
		}

		if (g_avs_adj[i].volt > g_gpu.signed_table[oppidx].volt) {
			GPUFREQ_LOGW("OPP[%d*]: efuse[%d].volt(%d) > signed-off.volt(%d)",
				oppidx, i, g_avs_adj[i].volt, g_gpu.signed_table[oppidx].volt);
			g_avs_adj[i].volt = g_gpu.signed_table[oppidx].volt;
		}

		g_avs_adj[i].vsram = __gpufreq_get_vsram_by_vgpu(g_avs_adj[i].volt);
	}

	for (i = 0; i < adj_num; i++)
		GPUFREQ_LOGI("OPP[%d*]: efuse[%d]: freq(%d), volt(%d)",
			g_avs_adj[i].oppidx, i, g_avs_adj[i].freq, g_avs_adj[i].volt);

	__gpufreq_apply_adjust(g_avs_adj, adj_num);

	__gpufreq_interpolate_volt();
#endif
}

static void __maybe_unused __gpufreq_custom_adjustment(void)
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
void mt_gpufreq_set_gpu_wrap_fp(int (*gpu_wrap_fp) (void))
{
	mt_gpufreq_wrap_fp = gpu_wrap_fp;
}
EXPORT_SYMBOL(mt_gpufreq_set_gpu_wrap_fp);

unsigned int mt_gpufreq_get_seg_max_opp_index(void)
{
	return g_gpu.segment_upbound;
}
EXPORT_SYMBOL(mt_gpufreq_get_seg_max_opp_index);

unsigned int mt_gpufreq_get_dvfs_table_num(void)
{
	return g_gpu.segment_lowbound - g_gpu.segment_upbound + 1;
}
EXPORT_SYMBOL(mt_gpufreq_get_dvfs_table_num);

static void __mt_gpufreq_calculate_power(unsigned int idx, unsigned int freq,
					 unsigned int volt, unsigned int temp)
{
	unsigned int p_total = 0;
	unsigned int p_dynamic = 0;
	int p_leakage = 0;

	p_dynamic = __gpufreq_get_dyn_pgpu(freq, volt);

#if IS_ENABLED(CONFIG_MTK_STATIC_POWER_LEGACY)
	p_leakage = mt_spower_get_leakage(MTK_SPOWER_GPU, (volt / 100), temp);
	if (p_leakage < 0)
		p_leakage = 0;
#else
	p_leakage = 71;
#endif

	p_total = p_dynamic + p_leakage;

	GPUFREQ_LOGD("idx = %d, p_dynamic = %d, p_leakage = %d, p_total = %d, temp = %d\n",
		     idx, p_dynamic, p_leakage, p_total, temp);

	g_power_table[idx].gpufreq_power = p_total;
}

static void __mt_gpufreq_setup_opp_power_table(int num)
{
	int i = 0;
	int temp = 0;

	g_power_table = kzalloc((num) * sizeof(struct mt_gpufreq_power_table_info), GFP_KERNEL);

	if (g_power_table == NULL)
		return;

#ifdef CONFIG_THERMAL
	if (mt_gpufreq_wrap_fp)
		temp = mt_gpufreq_wrap_fp() / 1000;
	else
		temp = 40;
#else
	temp = 40;
#endif

	GPUFREQ_LOGD("temp = %d\n", temp);

	if ((temp < -20) || (temp > 125)) {
		GPUFREQ_LOGD("temp < -20 or temp > 125!\n");
		temp = 65;
	}

	for (i = 0; i < num; i++) {
		g_power_table[i].gpufreq_khz = g_gpu.signed_table[i].freq;
		g_power_table[i].gpufreq_volt = g_gpu.signed_table[i].volt;

		__mt_gpufreq_calculate_power(i, g_power_table[i].gpufreq_khz,
					     g_power_table[i].gpufreq_volt, temp);

		GPUFREQ_LOGD("[%d], freq_khz = %u, volt = %u, power = %u\n",
			     i, g_power_table[i].gpufreq_khz,
			     g_power_table[i].gpufreq_volt, g_power_table[i].gpufreq_power);
	}
}

static void __mt_update_gpufreqs_power_table(void)
{
	int i;
	int temp = 0;
	unsigned int freq = 0;
	unsigned int volt = 0;

#ifdef CONFIG_THERMAL
	if (mt_gpufreq_wrap_fp)
		temp = mt_gpufreq_wrap_fp() / 1000;
	else
		temp = 40;

#else
	temp = 40;
#endif

	GPUFREQ_LOGD("temp = %d\n", temp);

	if (!g_power_table)
		return;

	mutex_lock(&gpufreq_lock);

	if ((temp >= -20) && (temp <= 125)) {
		for (i = 0; i < g_gpu.signed_opp_num; i++) {
			freq = g_power_table[i].gpufreq_khz;
			volt = g_power_table[i].gpufreq_volt;

			__mt_gpufreq_calculate_power(i, freq, volt, temp);

			GPUFREQ_LOGD("[%d] freq_khz = %d, volt = %d, power = %d\n",
				     i, g_power_table[i].gpufreq_khz,
				     g_power_table[i].gpufreq_volt, g_power_table[i].gpufreq_power);
		}
	} else {
		GPUFREQ_LOGE("temp < -20 or temp > 125, NOT update power table!\n");
	}

	mutex_unlock(&gpufreq_lock);
}

struct mt_gpufreq_power_table_info *mt_gpufreq_get_power_table(void)
{
	return g_power_table;
}
EXPORT_SYMBOL(mt_gpufreq_get_power_table);

unsigned int mt_gpufreq_get_power_table_num(void)
{
	return g_gpu.signed_opp_num;
}
EXPORT_SYMBOL(mt_gpufreq_get_power_table_num);

static int __gpufreq_init_opp_table(struct platform_device *pdev)
{
	unsigned int segment_id = 0;
	int i = 0, j = 0;
	int ret = GPUFREQ_SUCCESS;

	segment_id = g_gpu.segment_id;
	if ((segment_id == MT6789_SEGMENT) || g_max_freq_off_load)
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

	__gpufreq_segment_adjustment(pdev);

	__gpufreq_aging_adjustment();

	__gpufreq_avs_adjustment();

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
		g_gpu.working_table[i].margin = g_gpu.signed_table[j].margin;
		g_gpu.working_table[i].power = g_gpu.signed_table[j].power;

		GPUFREQ_LOGD("GPU[%02d] Freq: %d, Volt: %d, Vsram: %d, Vaging: %d",
			i, g_gpu.working_table[i].freq, g_gpu.working_table[i].volt,
			g_gpu.working_table[i].vsram, g_gpu.working_table[i].margin);
	}

	if (g_aging_enable)
		__gpufreq_apply_aging(true);

	__gpufreq_measure_power();

	g_gpu.sb_table = kcalloc(g_gpu.opp_num,
		sizeof(struct gpufreq_sb_info), GFP_KERNEL);
	if (!g_gpu.sb_table) {
		GPUFREQ_LOGE("fail to alloc springboard table (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

	__gpufreq_set_springboard();

	__mt_gpufreq_setup_opp_power_table(g_gpu.signed_opp_num);
	if (!g_power_table) {
		GPUFREQ_LOGE("fail to alloc power table (ENOMEM)");
		ret = GPUFREQ_ENOMEM;
		goto done;
	}

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
	unsigned int segment_id = 0;
	int ret = GPUFREQ_SUCCESS;

#if IS_ENABLED(CONFIG_MTK_DEVINFO)
	struct nvmem_cell *efuse_cell;
	unsigned int *efuse_buf;
	size_t efuse_len;

	efuse_cell = nvmem_cell_get(&pdev->dev, "efuse_segment_cell");
	if (IS_ERR(efuse_cell)) {
		ret = PTR_ERR(efuse_cell);
		if (ret == -ENOENT &&
		    !of_find_property(pdev->dev.of_node, "nvmem-cells", NULL)) {
			GPUFREQ_LOGW("efuse_segment_cell missing, using engineering defaults");
			segment_id = ENG_SEGMENT;
			ret = GPUFREQ_SUCCESS;
		} else {
			GPUFREQ_LOGE("fail to get efuse_segment_cell (%d)", ret);
		}
		goto done;
	}

	efuse_buf = (unsigned int *)nvmem_cell_read(efuse_cell, &efuse_len);
	nvmem_cell_put(efuse_cell);
	if (IS_ERR(efuse_buf)) {
		GPUFREQ_LOGE("fail to get efuse_buf (%ld)", PTR_ERR(efuse_buf));
		ret = PTR_ERR(efuse_buf);
		goto done;
	}

	if (efuse_len < sizeof(*efuse_buf)) {
		GPUFREQ_LOGE("invalid efuse_segment_cell size (%zu)", efuse_len);
		kfree(efuse_buf);
		ret = -EINVAL;
		goto done;
	}

	efuse_id = (*efuse_buf & 0xFF);
	kfree(efuse_buf);
#else
	efuse_id = 0x0;
#endif

	switch (efuse_id) {
	case 0x1:
		segment_id = MT6789_SEGMENT;
		break;
	case 0x2:
		segment_id = MT6789T_SEGMENT;
		break;
	default:
		segment_id = MT6789_SEGMENT;
		break;
	}

	GPUFREQ_LOGI("efuse_id: 0x%x, segment_id: %d", efuse_id, segment_id);

#if IS_ENABLED(CONFIG_MTK_DEVINFO)
done:
#endif
	g_gpu.segment_id = segment_id;

	return ret;
}

static int __gpufreq_init_mtcmos(struct platform_device *pdev)
{
	struct device *dev __maybe_unused = &pdev->dev;
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
		__gpufreq_abort("fail to get mfg0_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg0_dev, true);

	g_mtcmos->mfg1_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg1");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg1_dev)) {
		ret = g_mtcmos->mfg1_dev ? PTR_ERR(g_mtcmos->mfg1_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort("fail to get mfg1_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg1_dev, true);

	g_mtcmos->mfg2_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg2");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg2_dev)) {
		ret = g_mtcmos->mfg2_dev ? PTR_ERR(g_mtcmos->mfg2_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort("fail to get mfg2_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg2_dev, true);

	g_mtcmos->mfg3_dev = dev_pm_domain_attach_by_name(dev, "pd_mfg3");
	if (IS_ERR_OR_NULL(g_mtcmos->mfg3_dev)) {
		ret = g_mtcmos->mfg3_dev ? PTR_ERR(g_mtcmos->mfg3_dev) : GPUFREQ_ENODEV;
		__gpufreq_abort("fail to get mfg3_dev (%ld)", ret);
		goto done;
	}
	dev_pm_syscore_device(g_mtcmos->mfg3_dev, true);

done:
	GPUFREQ_TRACE_END();
#endif

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
		__gpufreq_abort("fail to get clk_mux (%ld)", ret);
		goto done;
	}

	g_clk->clk_ref_mux = devm_clk_get(&pdev->dev, "clk_ref_mux");
	if (IS_ERR(g_clk->clk_ref_mux)) {
		ret = PTR_ERR(g_clk->clk_ref_mux);
		__gpufreq_abort("fail to get clk_ref_mux (%ld)", ret);
		goto done;
	}

	g_clk->clk_main_parent = devm_clk_get(&pdev->dev, "clk_main_parent");
	if (IS_ERR(g_clk->clk_main_parent)) {
		ret = PTR_ERR(g_clk->clk_main_parent);
		__gpufreq_abort("fail to get clk_main_parent (%ld)", ret);
		goto done;
	}

	g_clk->clk_sub_parent = devm_clk_get(&pdev->dev, "clk_sub_parent");
	if (IS_ERR(g_clk->clk_sub_parent)) {
		ret = PTR_ERR(g_clk->clk_sub_parent);
		__gpufreq_abort("fail to get clk_sub_parent (%ld)", ret);
		goto done;
	}

	g_clk->subsys_bg3d = devm_clk_get(&pdev->dev, "subsys_bg3d");
	if (IS_ERR(g_clk->subsys_bg3d)) {
		ret = PTR_ERR(g_clk->subsys_bg3d);
		__gpufreq_abort("fail to get subsys_bg3d (%d)", ret);
		goto done;
	}

	g_clk->clk_fhctl = devm_clk_get_optional(&pdev->dev, "clk_fhctl");
	if (IS_ERR(g_clk->clk_fhctl)) {
		ret = PTR_ERR(g_clk->clk_fhctl);
		__gpufreq_abort("fail to get clk_fhctl (%ld)", ret);
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
		__gpufreq_abort("fail to get VGPU (%ld)", ret);
		goto done;
	}

	g_pmic->reg_vsram_gpu = regulator_get_optional(&pdev->dev, "_vsram_gpu");
	if (IS_ERR(g_pmic->reg_vsram_gpu)) {
		ret = PTR_ERR(g_pmic->reg_vsram_gpu);
		__gpufreq_abort("fail to get VSRAM_GPU (%ld)", ret);
		goto done;
	}

done:
	GPUFREQ_TRACE_END();

	return ret;
}

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

	of_property_read_u32(gpufreq_dev->of_node, "aging-load", &g_aging_load);
	of_property_read_u32(gpufreq_dev->of_node, "mcl50-load", &g_mcl50_load);
	of_property_read_u32(gpufreq_dev->of_node, "max_gpufreq_support_off", &g_max_freq_off_load);
	GPUFREQ_LOGD("max_gpufreq_support_off = %u", g_max_freq_off_load);
	of_property_read_u32(of_wrapper, "gpueb-support", &g_gpueb_support);

	g_apmixed_base = __gpufreq_of_ioremap("mediatek,mt6789-apmixedsys", 0);
	if (unlikely(!g_apmixed_base)) {
		GPUFREQ_LOGE("fail to ioremap APMIXED");
		goto done;
	}

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
#endif

#if GPUFREQ_ASENSOR_ENABLE

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
#endif

	ret = GPUFREQ_SUCCESS;

done:
	GPUFREQ_TRACE_END();

	return ret;
}

static void __gpufreq_init_acp(void)
{
	unsigned int val;

	val = readl(g_infracfg_ao_base + 0x290) | (0x1 << 9);
	writel(val, g_infracfg_ao_base + 0x290);
}

static int __gpufreq_pdrv_probe(struct platform_device *pdev)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_LOGI("start to probe gpufreq platform driver");

	if (__gpufreq_bringup()) {
		GPUFREQ_LOGI("skip gpufreq platform driver probe when bringup");

		goto done;
	}

	ret = __gpufreq_init_platform_info(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init platform info (%d)", ret);
		goto done;
	}

	ret = __gpufreq_init_pmic(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init pmic (%d)", ret);
		goto done;
	}

	if (g_gpueb_support) {
		GPUFREQ_LOGI("gpufreq platform probe only init reg_base/dfd/pmic/fp in EB mode");
		goto register_fp;
	}

	ret = __gpufreq_init_clk(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init clk (%d)", ret);
		goto done;
	}

	ret = __gpufreq_init_mtcmos(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init mtcmos (%d)", ret);
		goto done;
	}

	ret = __gpufreq_init_segment_id(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init segment id (%d)", ret);
#if !GPUFREQ_SELF_CTRL_MTCMOS
		dev_pm_domain_detach(g_mtcmos->mfg3_dev, true);
		dev_pm_domain_detach(g_mtcmos->mfg2_dev, true);
		dev_pm_domain_detach(g_mtcmos->mfg1_dev, true);
		dev_pm_domain_detach(g_mtcmos->mfg0_dev, true);
		kfree(g_mtcmos);
#endif
		regulator_put(g_pmic->reg_vsram_gpu);
		regulator_put(g_pmic->reg_vgpu);
		kfree(g_pmic);
		kfree(g_clk);
		iounmap(g_apmixed_base);
		goto done;
	}

	__gpufreq_init_shader_present();

	ret = __gpufreq_power_control(GPU_PWR_ON);
	if (unlikely(ret < 0)) {
		GPUFREQ_LOGE("fail to control power state: %d (%d)", GPU_PWR_ON, ret);
		goto done;
	}

	g_aging_enable = g_aging_load;

#if GPUFREQ_HISTORY_ENABLE
	__gpufreq_history_memory_init();
	gpufreq_set_history_state(HISTORY_FREE);
	gpufreq_set_history_park_volt(0);
#endif

	__gpufreq_init_acp();

	ret = __gpufreq_init_opp_table(pdev);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init OPP table (%d)", ret);
		goto done;
	}

	ret = __gpufreq_init_opp_idx();
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init OPP index (%d)", ret);
		goto done;
	}

	if (__gpufreq_power_ctrl_enable())
		__gpufreq_power_control(GPU_PWR_OFF);
	else

		GPUFREQ_LOGI("power control always on");

#if GPUFREQ_HISTORY_ENABLE
	__gpufreq_history_memory_reset();
#endif

register_fp:

	if (g_gpueb_support)
		gpufreq_register_gpufreq_fp(&platform_eb_fp);
	else
		gpufreq_register_gpufreq_fp(&platform_ap_fp);

	ret = gpuppm_init(TARGET_GPU, g_gpueb_support);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to init gpuppm (%d)", ret);
		goto done;
	}

	GPUFREQ_LOGI("gpufreq platform driver probe done");

done:
	GPUFREQ_LOGI("gpufreq platform driver probe done");
	return ret;
}

static void __gpufreq_pdrv_remove(struct platform_device *pdev)
{
#if !GPUFREQ_SELF_CTRL_MTCMOS
	dev_pm_domain_detach(g_mtcmos->mfg3_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg2_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg1_dev, true);
	dev_pm_domain_detach(g_mtcmos->mfg0_dev, true);
#endif

	kfree(g_gpu.working_table);
	kfree(g_gpu.sb_table);
	kfree(g_clk);
	kfree(g_pmic);
	kfree(g_power_table);
#if !GPUFREQ_SELF_CTRL_MTCMOS
	kfree(g_mtcmos);
#endif

}

static int __init __gpufreq_init(void)
{
	int ret = GPUFREQ_SUCCESS;

	GPUFREQ_LOGI("start to init gpufreq platform driver");

	ret = platform_driver_register(&g_gpufreq_pdrv);
	if (unlikely(ret)) {
		GPUFREQ_LOGE("fail to register gpufreq platform driver (%d)", ret);
		goto done;
	}

	GPUFREQ_LOGI("gpufreq platform driver init done");

done:
	return ret;
}

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
