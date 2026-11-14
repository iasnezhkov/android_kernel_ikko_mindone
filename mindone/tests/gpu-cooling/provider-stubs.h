/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static struct gpufreq_platform_fp platform_ap_fp;
static struct gpufreq_platform_fp platform_eb_fp;
static unsigned int knob_bringup;
static unsigned int knob_segment;
static unsigned int knob_gpueb;
static int knob_commits;

unsigned int __gpufreq_bringup(void) { return knob_bringup; }
unsigned int __gpufreq_power_ctrl_enable(void) { return 1; }
void __gpufreq_dump_infra_status(char *log_buf, int *log_len, int log_size) { (void)log_buf; (void)log_len; (void)log_size; }

static void *kcalloc(size_t n, size_t size, int flags) { (void)flags; return calloc(n, size); }
static void *kzalloc(size_t size, int flags) { (void)flags; return calloc(1, size); }
static void regulator_put(struct regulator *r) { (void)r; abort(); }
static void iounmap(void __iomem *p) { (void)p; abort(); }

static int __gpufreq_init_platform_info(struct platform_device *pdev)
{
	(void)pdev;
	g_gpueb_support = knob_gpueb;
	return 0;
}
static int __gpufreq_init_pmic(struct platform_device *pdev) { (void)pdev; g_pmic = kzalloc(sizeof(*g_pmic), GFP_KERNEL); return 0; }
static int __gpufreq_init_clk(struct platform_device *pdev) { (void)pdev; g_clk = kzalloc(sizeof(*g_clk), GFP_KERNEL); return 0; }
static int __gpufreq_init_mtcmos(struct platform_device *pdev) { (void)pdev; return 0; }
static int __gpufreq_init_segment_id(struct platform_device *pdev) { (void)pdev; g_gpu.segment_id = knob_segment; return 0; }
static void __gpufreq_init_shader_present(void) {}
static void __gpufreq_init_acp(void) {}
static void __gpufreq_segment_adjustment(struct platform_device *pdev) { (void)pdev; }
static void __gpufreq_aging_adjustment(void) {}
static void __gpufreq_avs_adjustment(void) {}
static void __gpufreq_apply_aging(unsigned int apply) { (void)apply; abort(); }
static void __gpufreq_measure_power(void) {}
static void __gpufreq_set_springboard(void) {}
static void __mt_gpufreq_setup_opp_power_table(int num)
{
	g_power_table = kzalloc(num * sizeof(struct mt_gpufreq_power_table_info), GFP_KERNEL);
}
static int __gpufreq_init_opp_idx(void)
{
	g_gpu.cur_oppidx = 0;
	g_gpu.cur_freq = g_gpu.working_table[0].freq;
	return 0;
}

int __gpufreq_power_control(enum gpufreq_power_state power)
{
	if (power == GPU_PWR_ON)
		g_dvfs_state &= ~DVFS_POWEROFF;
	else
		g_dvfs_state |= DVFS_POWEROFF;
	return 1;
}

int __gpufreq_generic_commit_gpu(int target_oppidx, enum gpufreq_dvfs_state key)
{
	if (target_oppidx < 0 || target_oppidx >= g_gpu.opp_num)
		return GPUFREQ_EINVAL;
	if (g_dvfs_state & ~key)
		return GPUFREQ_SUCCESS;
	g_gpu.cur_oppidx = target_oppidx;
	g_gpu.cur_freq = g_gpu.working_table[target_oppidx].freq;
	knob_commits++;
	event("commit");
	return GPUFREQ_SUCCESS;
}

static void kfree(const void *p)
{
	if (p && p == g_gpu.working_table)
		event("free_working_table");
	free((void *)p);
}
