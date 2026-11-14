// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#define FAILED_OFF_HEAD "notify_off,pmu_suspend,ged_off,last_opp,provider_off,bus_check,gpufreq_error,gpufreq_error,gpufreq_error,"
#if MODEL < 2
#define FAILED_OFF_EVENTS FAILED_OFF_HEAD "platform_error,"
#else
#define FAILED_OFF_EVENTS FAILED_OFF_HEAD "ged_on,platform_error,"
#endif

static void clear_events(void) { events_length = 0; events[0] = 0; }
static bool hw_on(void) { return g_gpu.buck_count == 1 && g_gpu.mtcmos_count == 1 && g_gpu.cg_count == 1; }
static bool hw_off(void) { return !g_gpu.buck_count && !g_gpu.mtcmos_count && !g_gpu.cg_count; }
static void reset_state(int count, bool active) {
    g_gpu.power_count = count;
    g_gpu.buck_count = g_gpu.mtcmos_count = g_gpu.cg_count = count ? 1 : 0;
    g_dvfs_state = count ? DVFS_FREE : DVFS_POWEROFF;
    g_bus_idle_timeout = false;
    mfg_powered = active;
    ged_state = active ? GED_POWER_ON : GED_POWER_OFF;
    notify_state = pmu_state = -1;
    g_curFreqID = 0; g_is_suspend = 0; last_commit = 17;
    power_calls = on_calls = off_calls = commit_calls = ctrl_enable_calls = 0;
    probe_off_commit = false;
    bringup = __gpufreq_bringup();
    power_enabled = __gpufreq_power_ctrl_enable();
    g_gpueb_support = 0; g_shared_status = NULL;
    test_fp.power_control = test_power_control;
    gpufreq_fp = &test_fp;
    bus_phase = phase_reads = reads_total = delay_calls = 0;
    for (int i = 0; i < 8; i++) idle_read_after[i] = 1;
    clear_events();
    assert(!gpufreq_lock && !gpufreq_power_lock && !mfg_pm_lock && !g_mfg_lock);
}
static void report(const char *name) {
    printf("model%d %s count=%d hw=%d/%d/%d timeout=%d dvfs_off=%d mfg=%d ged=%d on=%d off=%d commits=%d checks=%d reads=%d events=%s\n",
           MODEL, name, g_gpu.power_count, g_gpu.buck_count, g_gpu.mtcmos_count, g_gpu.cg_count,
           g_bus_idle_timeout, !!(g_dvfs_state & DVFS_POWEROFF), mfg_powered, ged_state,
           on_calls, off_calls, commit_calls, bus_phase, reads_total, events);
    groups++;
}

int main(void) {
    struct kbase_device kbdev = {0};

    reset_state(0, false);
    assert(!bringup && power_enabled);
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 1 && hw_on() && !(g_dvfs_state & DVFS_POWEROFF));
    assert(mfg_powered && ged_state == GED_POWER_ON && commit_calls == 1 && notify_state == 1 && pmu_state == 1);
    assert(strcmp(events, "provider_on,buck_on,mtcmos_on,clock_on,timestamp,commit,ged_on,notify_on,pmu_resume,") == 0);
    report("healthy_on");

    clear_events();
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 0 && hw_off() && (g_dvfs_state & DVFS_POWEROFF) && !g_bus_idle_timeout);
    assert(!mfg_powered && ged_state == GED_POWER_OFF && g_curFreqID == last_commit && notify_state == 0 && pmu_state == 0);
    assert(strcmp(events, "notify_off,pmu_suspend,ged_off,last_opp,provider_off,bus_check,clock_off,mtcmos_off,buck_off,") == 0);
    report("healthy_off");

    int calls = power_calls;
    pm_callback_power_off(&kbdev);
    assert(power_calls == calls);
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(g_gpu.power_count == 1 && on_calls == 2 && commit_calls == 2);
    report("repeat_balanced");

    reset_state(1, true);
    probe_off_commit = true;
    idle_read_after[0] = 0;
    pm_callback_power_off(&kbdev);
    assert(off_calls == 1 && reads_total == 100000 && delay_calls == 100000 && hw_on() && !(g_dvfs_state & DVFS_POWEROFF));
#if MODEL == 0 || MODEL == 2
    assert(g_gpu.power_count == 0 && g_bus_idle_timeout && hw_on());
#else
    assert(g_gpu.power_count == 1 && g_bus_idle_timeout && hw_on());
#endif
#if MODEL < 2
    assert(!mfg_powered && ged_state == GED_POWER_OFF && off_calls == 1);
#else
    assert(mfg_powered && ged_state == GED_POWER_ON && off_calls == 1);
#endif
    assert(strcmp(events, FAILED_OFF_EVENTS) == 0);
    report("failed_off");

    probe_off_commit = false;
    clear_events();
#if MODEL == 0
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 1 && hw_on() && !g_bus_idle_timeout && on_calls == 1);
    assert(mfg_powered && ged_state == GED_POWER_ON && commit_calls == 1);
    assert(strcmp(events, "provider_on,commit,ged_on,notify_on,pmu_resume,") == 0);
#elif MODEL == 1
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 2 && hw_on() && g_bus_idle_timeout && on_calls == 1);
    assert(mfg_powered && ged_state == GED_POWER_ON && commit_calls == 1);
#else
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(on_calls == 0 && commit_calls == 0 && hw_on() && mfg_powered);
    assert(g_gpu.power_count == (MODEL == 2 ? 0 : 1));
#endif
    report("on_after_failed_off");

    clear_events();
    pm_callback_power_off(&kbdev);
#if MODEL == 0 || MODEL == 3
    assert(g_gpu.power_count == 0 && hw_off() && (g_dvfs_state & DVFS_POWEROFF) && !mfg_powered && bus_phase == 2);
#elif MODEL == 1
    assert(g_gpu.power_count == 1 && hw_on() && !mfg_powered && bus_phase == 1);
#else
    assert(g_gpu.power_count == -1 && hw_on() && mfg_powered && ged_state == GED_POWER_ON && bus_phase == 1);
#endif
    report("off_after_bus_recovery");

    assert(pm_callback_power_on(&kbdev) == (MODEL == 2 ? 0 : 1));
    pm_callback_power_off(&kbdev);
#if MODEL == 0 || MODEL == 3
    assert(g_gpu.power_count == 0 && hw_off() && !mfg_powered);
#elif MODEL == 1
    assert(g_gpu.power_count == 1 && hw_on() && !mfg_powered);
#else
    assert(g_gpu.power_count == -2 && hw_on() && mfg_powered);
#endif
    report("next_cycle");

    reset_state(1, true);
    idle_read_after[0] = 0; idle_read_after[1] = 0;
    pm_callback_power_off(&kbdev);
    pm_callback_power_on(&kbdev);
    pm_callback_power_off(&kbdev);
    pm_callback_power_on(&kbdev);
    pm_callback_power_off(&kbdev);
#if MODEL == 0
    assert(g_gpu.power_count == 0 && hw_off() && bus_phase == 3 && off_calls == 3 && on_calls == 2);
#elif MODEL == 1
    assert(g_gpu.power_count == 1 && hw_on() && bus_phase == 1 && off_calls == 3 && on_calls == 2);
#elif MODEL == 2
    assert(g_gpu.power_count == -2 && hw_on() && bus_phase == 1 && off_calls == 3 && on_calls == 0);
#else
    assert(g_gpu.power_count == 0 && hw_off() && bus_phase == 3 && off_calls == 3 && on_calls == 0);
#endif
    report("repeated_failed_off");

#if MODEL == 0
    reset_state(1, true);
    idle_read_after[0] = 0;
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 0 && g_bus_idle_timeout && hw_on());
    assert(__gpufreq_power_control(GPU_PWR_ON) == 1);
    assert(g_gpu.power_count == 1 && !g_bus_idle_timeout && hw_on());
    assert(__gpufreq_power_control(GPU_PWR_OFF) == 0);
    assert(g_gpu.power_count == 0 && hw_off() && (g_dvfs_state & DVFS_POWEROFF));
    clear_events();
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 1 && hw_on() && strncmp(events, "provider_on,buck_on,", 20) == 0);
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 0 && hw_off() && !mfg_powered);
    report("provider_user_after_failed_off");
#endif

    reset_state(1, false);
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 2 && on_calls == 1 && hw_on());
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && hw_on() && bus_phase == 0 && !mfg_powered);
    report("external_reference_preserved");

    reset_state(0, false);
    power_enabled = false;
    assert(pm_callback_power_on(&kbdev) == 1);
    pm_callback_power_off(&kbdev);
    assert(power_calls == 0 && mfg_powered && ged_state == GED_POWER_ON && g_gpu.power_count == 0);
    report("power_control_disabled");

    reset_state(0, false);
    bringup = true;
    assert(pm_callback_power_on(&kbdev) == 1);
    pm_callback_power_off(&kbdev);
    assert(power_calls == 0 && ctrl_enable_calls == 0 && mfg_powered && ged_state == GED_POWER_OFF);
    report("bringup_unchanged");

    reset_state(1, true);
    assert(mtk_common_gpufreq_commit(-1) == -1 && commit_calls == 0);
    assert(mtk_common_gpufreq_commit(17) == 0 && commit_calls == 1);
    mtk_common_pm_mfg_idle();
    assert(mtk_common_gpufreq_commit(17) == -1 && commit_calls == 1);
    mtk_common_pm_mfg_active();
    bringup = true;
    assert(mtk_common_gpufreq_commit(17) == -1 && commit_calls == 1);
    report("commit_admission");

    reset_state(0, false);
    g_is_suspend = 1;
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(power_calls == 0 && !mfg_powered && ged_state == GED_POWER_OFF && notify_state == 1);
    report("suspended_power_on_discarded");

    reset_state(0, false);
    test_fp.power_control = NULL;
    if (!setjmp(panic_jump)) {
        pm_callback_power_on(&kbdev);
        abort();
    }
    assert(g_mfg_lock && !gpufreq_power_lock && strstr(events, "panic,"));
    g_mfg_lock = 0;
    assert(g_gpu.power_count == 0 && !mfg_powered && ged_state == GED_POWER_OFF && commit_calls == 0);
    test_fp.power_control = test_power_control;
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 1 && mfg_powered && ged_state == GED_POWER_ON);
    report("wrapper_missing_provider");

    assert(!gpufreq_lock && !gpufreq_power_lock && !mfg_pm_lock && !g_mfg_lock);
    printf("PASS model%d %d groups\n", MODEL, groups);
    return 0;
}
