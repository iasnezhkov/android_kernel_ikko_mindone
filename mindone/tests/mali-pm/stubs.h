/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IS_ENABLED(x) x
#define unlikely(x) (x)
#define __maybe_unused __attribute__((unused))
#define GPUFREQ_TRACE_START(...)
#define GPUFREQ_TRACE_END(...)
#define GPUFREQ_LOGD(...) ((void)0)
#define GPUFREQ_LOGE(...) event("gpufreq_error")
#define KBASE_PLATFORM_LOGI(...) event("platform_info")
#define KBASE_PLATFORM_LOGE(...) event("platform_error")
#define CMD_POWER_CONTROL 1

typedef uint32_t u32;
struct kbase_device { void *dev; };
struct gpufreq_status { int power_count, buck_count, mtcmos_count, cg_count; };
struct gpufreq_shared_status { unsigned int power_control; };
struct gpufreq_platform_fp {
    unsigned int (*power_ctrl_enable)(void);
    int (*power_control)(enum gpufreq_power_state power);
};
struct gpufreq_ipi_data { int cmd_id; union { int power_state; int return_value; } u; };

static struct gpufreq_status g_gpu;
static int g_dvfs_state;
static bool g_bus_idle_timeout;
static int gpufreq_lock, gpufreq_power_lock, gpufreq_ipi_lock, mfg_pm_lock, g_mfg_lock;
static bool mfg_powered;
static int g_curFreqID;
static int g_is_suspend;
static unsigned int g_gpueb_support;
static struct gpufreq_shared_status *g_shared_status;
static struct gpufreq_platform_fp *gpufreq_fp;
static struct gpufreq_ipi_data g_recv_msg;
static unsigned long g_ipi_irq_flags;
static unsigned char registers[4096];
static void *g_mfg_top_base = registers;
static void *g_sleep __maybe_unused = registers;
static bool bringup, power_enabled;
static enum ged_gpu_power_state ged_state;
static int notify_state = -1, pmu_state = -1;
static int power_calls, on_calls, off_calls, commit_calls, ctrl_enable_calls, last_commit = 17;
static bool probe_off_commit;
static int bus_phase, phase_reads, idle_read_after[8];
static int reads_total, delay_calls, groups;
static char events[8192];
static size_t events_length;

static void event(const char *s) {
    int n = snprintf(events + events_length, sizeof(events) - events_length, "%s,", s);
    assert(n > 0 && (size_t)n < sizeof(events) - events_length);
    events_length += (size_t)n;
}
static void mutex_lock(int *m) { assert(*m == 0); *m = 1; }
static void mutex_unlock(int *m) { assert(*m == 1); *m = 0; }
static void raw_spin_lock_irqsave(int *lock, unsigned long flags) { (void)lock; (void)flags; abort(); }
static void raw_spin_unlock_irqrestore(int *lock, unsigned long flags) { (void)lock; (void)flags; abort(); }
static int gpufreq_ipi_to_gpueb(struct gpufreq_ipi_data data) { (void)data; abort(); }
static jmp_buf panic_jump;
static __attribute__((noreturn, format(printf, 1, 2))) void panic(const char *fmt, ...) { (void)fmt; event("panic"); longjmp(panic_jump, 1); }

static void __gpufreq_footprint_power_count(int n) { (void)n; }
static void __gpufreq_footprint_power_step(int n) { (void)n; }
static int __gpufreq_buck_control(enum gpufreq_power_state p) {
    g_gpu.buck_count += p == GPU_PWR_ON ? 1 : -1; event(p == GPU_PWR_ON ? "buck_on" : "buck_off"); return 0;
}
static int __gpufreq_mtcmos_control(enum gpufreq_power_state p) {
    g_gpu.mtcmos_count += p == GPU_PWR_ON ? 1 : -1; event(p == GPU_PWR_ON ? "mtcmos_on" : "mtcmos_off"); return 0;
}
static int __gpufreq_clock_control(enum gpufreq_power_state p) {
    g_gpu.cg_count += p == GPU_PWR_ON ? 1 : -1; event(p == GPU_PWR_ON ? "clock_on" : "clock_off"); return 0;
}
static void writel(u32 value, void *address) {
    if (address == registers + 0xb4) {
        assert(value == 1);
        bus_phase++; phase_reads = 0; event("bus_check");
        assert(bus_phase >= 1 && bus_phase <= 8);
    } else if (address == registers + 0x130) {
        assert(value == 3); event("timestamp");
    } else {
        assert(address == registers + 0x170 && value == 3);
    }
}
static u32 readl(void *address) {
    assert(address == registers + 0x178);
    phase_reads++; reads_total++;
    int after = idle_read_after[bus_phase - 1];
    return after > 0 && phase_reads >= after ? 4 : 0;
}
static void udelay(int value) { assert(value == 10); delay_calls++; }

static int mtk_common_gpufreq_bringup(void) { return bringup; }
static int gpufreq_commit(enum gpufreq_target target, int oppidx) {
    assert(target == TARGET_DEFAULT && oppidx >= 0 && mfg_pm_lock == 1);
    commit_calls++; event("commit"); return 0;
}
static unsigned long ged_dvfs_get_last_commit_idx(void) { event("last_opp"); return (unsigned long)last_commit; }
static void ged_dvfs_gpu_clock_switch_notify(enum ged_gpu_power_state state) {
    assert(g_mfg_lock == 1);
    ged_state = state;
    event(state == GED_POWER_ON ? "ged_on" : state == GED_SLEEP ? "ged_sleep" : "ged_off");
}
static void mtk_notify_gpu_power_change(int power_on) {
    assert(g_mfg_lock == 1);
    notify_state = power_on; event(power_on ? "notify_on" : "notify_off");
}
static void MTK_LTR_gpu_pmu_resume(void) { assert(g_mfg_lock == 1); pmu_state = 1; event("pmu_resume"); }
static void MTK_LTR_gpu_pmu_suspend(void) { assert(g_mfg_lock == 1); pmu_state = 0; event("pmu_suspend"); }

int __gpufreq_power_control(enum gpufreq_power_state power);
int mtk_common_gpufreq_commit(int opp_idx);
static void __gpufreq_check_bus_idle(void);

static unsigned int test_power_ctrl_enable(void) { ctrl_enable_calls++; return power_enabled; }
static int test_power_control(enum gpufreq_power_state power) {
    assert(g_mfg_lock == 1 && mfg_pm_lock == 0 && gpufreq_power_lock == 1 && gpufreq_lock == 0);
    power_calls++;
    if (power == GPU_PWR_ON) on_calls++; else off_calls++;
    event(power == GPU_PWR_ON ? "provider_on" : "provider_off");
    if (power == GPU_PWR_OFF && probe_off_commit) {
        assert(!mfg_powered && ged_state == GED_POWER_OFF);
        assert(mtk_common_gpufreq_commit(last_commit) == -1);
    }
    return __gpufreq_power_control(power);
}
static struct gpufreq_platform_fp test_fp = { test_power_ctrl_enable, test_power_control };
