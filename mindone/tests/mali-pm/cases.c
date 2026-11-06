// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset_state(int count, bool active) {
    g_gpu.power_count = count; g_gpu.buck_count = g_gpu.mtcmos_count = g_gpu.cg_count = count ? 1 : 0;
    g_dvfs_state = count ? 0 : DVFS_POWEROFF;
    mfg_powered = active; ged_on = active;
    g_cur_opp_idx = 44; last_commit = 17;
    power_calls = on_calls = off_calls = commit_calls = 0;
    wrapper_error = 0;
    probe_off_commit = false;
    bringup = false; power_enabled = true;
    bus_phase = phase_reads = reads_total = delay_calls = 0;
    g_bus_idle_timeout = false; events_length = 0; events[0] = 0;
    for (int i = 0; i < 4; i++) idle_read_after[i] = 1;
    assert(gpufreq_lock == 0 && mfg_pm_lock == 0 && g_mfg_lock == 0);
}
static void report(const char *name) {
    printf("%s count=%d active=%d ged=%d on=%d off=%d commits=%d reads=%d delays=%d events=%s\n",
           name, g_gpu.power_count, mfg_powered, ged_on, on_calls, off_calls,
           commit_calls, reads_total, delay_calls, events);
}
int main(void) {
    struct kbase_device kbdev = {0};
    reset_state(0, false);
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(g_gpu.power_count == 1 && mfg_powered && ged_on && commit_calls == 1);
    assert(strcmp(events, "provider_on,buck_on,mtcmos_on,clock_on,timestamp,commit,ged_on,") == 0);
    report("healthy_on");
    events_length = 0; events[0] = 0;
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 0 && !mfg_powered && !ged_on && g_cur_opp_idx == last_commit);
    assert(strcmp(events, "ged_off,last_opp,bus_check,provider_off,bus_check,clock_off,mtcmos_off,buck_off,") == 0);
    report("healthy_off");
    int calls = power_calls;
    pm_callback_power_off(&kbdev);
    assert(power_calls == calls);
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(g_gpu.power_count == 1 && on_calls == 2);
    report("repeat_balanced");

    reset_state(1, true);
    probe_off_commit = true;
    idle_read_after[0] = 0; idle_read_after[1] = 0;
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && g_gpu.buck_count == 1 && g_gpu.mtcmos_count == 1 && g_gpu.cg_count == 1);
    assert(reads_total == 200000 && delay_calls == 200000 && off_calls == 1 && !(g_dvfs_state & DVFS_POWEROFF));
#if PROPOSED
    assert(mfg_powered && ged_on);
#else
    assert(!mfg_powered && !ged_on);
#endif
    report("persistent_bus_busy");
    assert(pm_callback_power_on(&kbdev) == 0);
#if PROPOSED
    assert(g_gpu.power_count == 1 && on_calls == 0 && commit_calls == 0);
#else
    assert(g_gpu.power_count == 2 && on_calls == 1 && commit_calls == 1);
#endif
    report("on_after_failed_off");
    idle_read_after[2] = 1; idle_read_after[3] = 1;
    pm_callback_power_off(&kbdev);
#if PROPOSED
    assert(g_gpu.power_count == 0 && g_gpu.buck_count == 0 && off_calls == 2);
#else
    assert(g_gpu.power_count == 1 && g_gpu.buck_count == 1 && off_calls == 2);
#endif
    report("bus_recovery_after_cycle");

    reset_state(1, true);
    idle_read_after[0] = 1; idle_read_after[1] = 0;
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && reads_total == 100001 && delay_calls == 100000);
#if PROPOSED
    assert(mfg_powered && ged_on);
#else
    assert(!mfg_powered && !ged_on);
#endif
    report("second_bus_check_failure");

    reset_state(1, true);
    idle_read_after[0] = 0; idle_read_after[1] = 1;
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 0 && !mfg_powered && !ged_on && reads_total == 100001);
    report("first_check_timeout_second_ready");

    reset_state(1, false);
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(g_gpu.power_count == 2 && on_calls == 1);
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && g_gpu.buck_count == 1 && !mfg_powered);
    report("external_reference_preserved");

    reset_state(0, false);
    power_enabled = false;
    assert(pm_callback_power_on(&kbdev) == 0);
    pm_callback_power_off(&kbdev);
    assert(power_calls == 0 && mfg_powered && ged_on);
    report("power_control_disabled");

    reset_state(0, false);
    bringup = true;
    assert(pm_callback_power_on(&kbdev) == 0);
    pm_callback_power_off(&kbdev);
    assert(power_calls == 0 && mfg_powered && !ged_on);
    report("bringup_unchanged");

    reset_state(1, true);
    assert(mtk_common_gpufreq_commit(-1) == -1 && commit_calls == 0);
    assert(mtk_common_gpufreq_commit(17) == 0 && commit_calls == 1);
    mtk_common_pm_mfg_idle();
    assert(mtk_common_gpufreq_commit(17) == -1);
    report("commit_admission");

    reset_state(0, false);
    wrapper_error = -ENOENT;
    assert(pm_callback_power_on(&kbdev) == 1);
    assert(g_gpu.power_count == 0 && !mfg_powered && !ged_on && commit_calls == 0);
    wrapper_error = 0;
    assert(pm_callback_power_on(&kbdev) == 0);
    assert(g_gpu.power_count == 1 && mfg_powered && ged_on);
    report("wrapper_missing_provider_reset_indication");

    reset_state(1, true);
    idle_read_after[0] = 0; idle_read_after[1] = 0;
    pm_callback_power_off(&kbdev);
    idle_read_after[2] = 0; idle_read_after[3] = 0;
#if PROPOSED
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && off_calls == 2 && mfg_powered && ged_on);
#else
    pm_callback_power_off(&kbdev);
    assert(g_gpu.power_count == 1 && off_calls == 1 && !mfg_powered && !ged_on);
#endif
    report("repeated_failed_off");
    assert(gpufreq_lock == 0 && mfg_pm_lock == 0 && g_mfg_lock == 0);
    puts("PASS 14 exact-source control groups");
    return 0;
}
