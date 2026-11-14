// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
void test_provider_config(unsigned int segment, unsigned int gpueb, unsigned int bringup)
{
	knob_segment = segment;
	knob_gpueb = gpueb;
	knob_bringup = bringup;
}
int test_probe(struct platform_device *pdev) { return __gpufreq_pdrv_probe(pdev); }
void test_remove(struct platform_device *pdev) { __gpufreq_pdrv_remove(pdev); }
void test_power(int on) { __gpufreq_power_control(on ? GPU_PWR_ON : GPU_PWR_OFF); }
int test_cur_oppidx(void) { return g_gpu.cur_oppidx; }
int test_commits(void) { return knob_commits; }
int test_opp_num(void) { return g_gpu.opp_num; }
int test_signed_opp_num(void) { return (int)SIGNED_OPP_GPU_NUM; }
unsigned int test_working_freq(int idx) { return g_gpu.working_table[idx].freq; }
unsigned int test_signed_freq(int idx) { return g_default_gpu[idx].freq; }
int test_segment_upbound(void) { return g_gpu.segment_upbound; }
int test_cooling_live(void) { return g_cooling_dev != NULL; }
