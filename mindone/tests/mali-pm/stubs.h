/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CONFIG_MTK_GPUFREQ_V2 1
#define CONFIG_MALI_MIDGARD_DVFS 1
#define CONFIG_MALI_MTK_DVFS_POLICY 1
#define IS_ENABLED(x) x
#define CONFIG_MTK_AEE_IPANIC 0
#define CONFIG_MTK_GPU_SWPM_SUPPORT 0
#define DEFINE_MUTEX(x) static int x
#define unlikely(x) (x)
#define __maybe_unused __attribute__((unused))
#define GPUFREQ_TRACE_START(...)
#define GPUFREQ_TRACE_END(...)
#define GPUFREQ_LOGD(...)
#define GPUFREQ_LOGE(...) event("error")
#define dev_dbg(dev, ...) ((void)(dev))
#define dev_info(dev, ...) ((void)(dev), event("callback_error"))
#define GPUFREQ_EINVAL (-EINVAL)
#define DVFS_POWEROFF 2
#define TARGET_DEFAULT 0

typedef uint32_t u32;
enum gpufreq_power_state { POWER_OFF, POWER_ON };
enum gpu_dvfs_status_step {
    GPU_DVFS_STATUS_STEP_1, GPU_DVFS_STATUS_STEP_2,
    GPU_DVFS_STATUS_STEP_3, GPU_DVFS_STATUS_STEP_4,
    GPU_DVFS_STATUS_STEP_5, GPU_DVFS_STATUS_STEP_6,
    GPU_DVFS_STATUS_STEP_7, GPU_DVFS_STATUS_STEP_8,
    GPU_DVFS_STATUS_STEP_9, GPU_DVFS_STATUS_STEP_A,
    GPU_DVFS_STATUS_STEP_B
};
struct kbase_device { void *dev; };
static struct { int power_count, buck_count, mtcmos_count, cg_count; } g_gpu;
static int gpufreq_lock, mfg_pm_lock, g_mfg_lock;
static bool mfg_powered;
static int g_cur_opp_idx = 44;
static int g_dvfs_state;
static bool g_bus_idle_timeout;
static unsigned char registers[4096];
static void *g_mfg_top_base = registers;
static void *g_sleep __maybe_unused = registers;
static bool bringup, power_enabled = true, ged_on;
static int power_calls, on_calls, off_calls, commit_calls, last_commit = 17;
static int wrapper_error;
static bool probe_off_commit;
static int bus_phase, idle_read_after[4] = {1, 1, 1, 1}, phase_reads;
static int reads_total, delay_calls;
static char events[8192];
static size_t events_length;
static void event(const char *s) {
    int n = snprintf(events + events_length, sizeof(events) - events_length, "%s,", s);
    assert(n > 0 && (size_t)n < sizeof(events) - events_length);
    events_length += (size_t)n;
}
static void mutex_lock(int *m) { assert(*m == 0); *m = 1; }
static void mutex_unlock(int *m) { assert(*m == 1); *m = 0; }
static void gpu_dvfs_status_footprint(enum gpu_dvfs_status_step s) { (void)s; }
static void __gpufreq_footprint_power_count(int n) { (void)n; }
static void __gpufreq_footprint_power_step(int n) { (void)n; }
#define GPUFREQ_POWER_STEP_01 1
#define GPUFREQ_POWER_STEP_02 2
#define GPUFREQ_POWER_STEP_03 3
#define GPUFREQ_POWER_STEP_04 4
#define GPUFREQ_POWER_STEP_05 5
#define GPUFREQ_POWER_STEP_06 6
#define GPUFREQ_POWER_STEP_07 7
#define GPUFREQ_POWER_STEP_08 8
#define GPUFREQ_POWER_STEP_09 9
static void __gpufreq_check_pending_exception(void) {}
static int __gpufreq_buck_control(enum gpufreq_power_state p) {
    g_gpu.buck_count += p ? 1 : -1; event(p ? "buck_on" : "buck_off"); return 0;
}
static int __gpufreq_mtcmos_control(enum gpufreq_power_state p) {
    g_gpu.mtcmos_count += p ? 1 : -1; event(p ? "mtcmos_on" : "mtcmos_off"); return 0;
}
static int __gpufreq_clock_control(enum gpufreq_power_state p) {
    g_gpu.cg_count += p ? 1 : -1; event(p ? "clock_on" : "clock_off"); return 0;
}
static void writel(u32 value, void *address) {
    if (address == registers + 0xb4) { assert(value == 1); bus_phase++; phase_reads = 0; event("bus_check"); }
    else { assert(address == registers + 0x170 && value == 3); }
}
static u32 readl(void *address) {
    if (address != registers + 0x178) return 0;
    phase_reads++; reads_total++;
    int after = idle_read_after[(bus_phase - 1) % 4];
    return after > 0 && phase_reads >= after ? 4 : 0;
}
static void udelay(int value) { assert(value == 10); delay_calls++; }
static int mtk_common_gpufreq_bringup(void) { return bringup; }
static int gpufreq_power_ctrl_enable(void) { return power_enabled; }
static void gpufreq_set_timestamp(void) { event("timestamp"); }
static void ged_dvfs_gpu_clock_switch_notify(bool on) { ged_on = on; event(on ? "ged_on" : "ged_off"); }
static int mtk_common_ged_dvfs_get_last_commit_idx(void) { event("last_opp"); return last_commit; }
static int gpufreq_commit(int target, int idx) { assert(target == 0 && idx >= 0); commit_calls++; event("commit"); return 0; }
static int __gpufreq_power_control(enum gpufreq_power_state power);
static void __gpufreq_check_bus_idle(void);
static int mtk_common_gpufreq_commit(int opp_idx);
static int gpufreq_power_control(enum gpufreq_power_state power) {
    assert(g_mfg_lock == 1 && mfg_pm_lock == 0);
    power_calls++; if (power) on_calls++; else off_calls++;
    event(power ? "provider_on" : "provider_off");
    if (!power && probe_off_commit) {
        assert(!mfg_powered && !ged_on);
        assert(mtk_common_gpufreq_commit(last_commit) == -1);
    }
    if (wrapper_error) return wrapper_error;
    return __gpufreq_power_control(power);
}
static void gpufreq_check_bus_idle(void) { __gpufreq_check_bus_idle(); }
