// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
void get_random_bytes(void *buf, size_t len) { (void)buf; (void)len; abort(); }
int __gpufreq_get_idx_by_fstack(unsigned int freq) { (void)freq; abort(); }
int __gpufreq_get_idx_by_vgpu(unsigned int volt) { (void)volt; abort(); }
int __gpufreq_get_idx_by_vstack(unsigned int volt) { (void)volt; abort(); }
int __gpufreq_get_idx_by_pgpu(unsigned int power) { (void)power; abort(); }
int __gpufreq_get_idx_by_pstack(unsigned int power) { (void)power; abort(); }
int __gpufreq_get_batt_oc_idx(int level) { (void)level; abort(); }
int __gpufreq_get_batt_percent_idx(int level) { (void)level; abort(); }
int __gpufreq_get_low_batt_idx(int level) { (void)level; abort(); }
int __gpufreq_get_cur_idx_stack(void) { abort(); }
int __gpufreq_get_opp_num_stack(void) { abort(); }
int __gpufreq_generic_commit_stack(int oppidx, enum gpufreq_dvfs_state key) { (void)oppidx; (void)key; abort(); }
int __gpufreq_generic_commit_dual(int gpu, int stack, enum gpufreq_dvfs_state key) { (void)gpu; (void)stack; (void)key; abort(); }
