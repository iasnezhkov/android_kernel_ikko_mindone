// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
    memset(&gDevWmt, 0, sizeof(gDevWmt));
    memset(&host_op, 0, sizeof(host_op));
    memset(bank_rc, 0, sizeof(bank_rc));
    memset(registers, 0, sizeof(registers));
    memset(&g_dmp_info, 0xaa, sizeof(g_dmp_info));
    memset(&alarm_dmp_info, 0xaa, sizeof(alarm_dmp_info));
    logs[0] = actions[0] = 0;
    gDevWmt.state = 1;
    gMtkWmtCtx.eDrvStatus[0] = DRV_STS_FUNC_ON;
    bank_partial = bank_zero = provider_calls = pcr_calls = sleeps = 0;
    spin_depth = sleep_depth = lock_failure = 0;
    put_failure = no_free = no_completion = coredump = copy_failure = 0;
    readable = g_sleep_counter_enable = 1;
    osc_override = gating_override = MTK_WCN_BOOL_TRUE;
    conn_reg.mcu_conn_hif_on_base = 0x1000;
    conn_reg.mcu_cfg_on_base = 0x1800;
    conn_reg.mcu_top_misc_on_base = 0x2000;
    conn_read_count = conn_write_count = 0;
    table.consys_ic_sleep_info_enable_ctrl = enable_fn;
    table.consys_ic_sleep_info_read_ctrl = read_fn;
    table.consys_ic_sleep_info_clear = clear_fn;
    wmt_consys_ic_ops = &table;
    g_dump_info_buf_len = 0;
    g_dump_info_read_lock = 0;
    dmp_info_buf_ptr = dmp_info_buf;
}

static int core_sample(CONSYS_STATE_DMP_OP *sample)
{
    WMT_OP op = {.au4OpData = {(SIZE_T)sample, sample->version}};
    return opfunc_get_consys_state(&op);
}
static void banks(void)
{
    CONSYS_STATE state, saved;
    memset(&state, 0xaa, sizeof(state));
    saved = state;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == 0);
    assert(provider_calls == 5 && strcmp(actions, "01234") == 0);
    assert(strcmp(logs, "TOP:100,1000;MCU:101,1001;BT:102,1002;WIFI:103,1003;GPS:104,1004\n") == 0);
    assert(state.sleep_timer[3] == 1003 && !memcmp(state.lp, saved.lp, sizeof(state.lp)));
    for (int bank = 0; bank < 5; bank++) {
        reset();
        memset(&state, 0xaa, sizeof(state));
        saved = state;
        bank_rc[bank] = -4 - bank;
        assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == -4 - bank);
        assert(provider_calls == 5 && strcmp(actions, "01234") == 0);
        assert(!memcmp(&state, &saved, sizeof(state)) && !strstr(logs, "TOP:"));
    }
    reset();
    bank_partial = 1;
    bank_rc[1] = -37;
    bank_rc[4] = -71;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == -37);
    assert(!memcmp(&state, &saved, sizeof(state)) && spin_depth == 0);
    reset();
    bank_rc[3] = 2;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == 2);
    assert(!memcmp(&state, &saved, sizeof(state)) && !strstr(logs, "TOP:"));
    reset();
    bank_zero = 1;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == 0);
    assert(state.sleep_timer[3] == 0 && strstr(logs, "WIFI:0,0"));
    reset();
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(NULL) == 0 && provider_calls == 5);
}
static void missing(void)
{
    CONSYS_STATE state, saved;
    memset(&state, 0xaa, sizeof(state));
    saved = state;
    for (int absent = 0; absent < 4; absent++) {
        reset();
        if (absent == 0) wmt_consys_ic_ops = NULL;
        if (absent == 1) table.consys_ic_sleep_info_read_ctrl = NULL;
        if (absent == 2) table.consys_ic_sleep_info_enable_ctrl = NULL;
        if (absent == 3) table.consys_ic_sleep_info_clear = NULL;
        assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == -EOPNOTSUPP);
        assert(!provider_calls && !memcmp(&state, &saved, sizeof(state)) && !spin_depth);
    }
    reset();
    g_sleep_counter_enable = 0;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == -21);
    assert(!provider_calls && !memcmp(&state, &saved, sizeof(state)) && !spin_depth);
}
static void core(void)
{
    CONSYS_STATE_DMP_OP sample = {.status = WMT_DUMP_STATE_SCHEDULED, .version = 7, .times = 5, .cpu_sleep_ms = 2};
    assert(core_sample(&sample) == 0 && !sample.lock);
    assert(strcmp(actions, "01234RPDPDPDPDPDOG") == 0 && pcr_calls == 5 && sleeps == 5);
    reset();
    bank_rc[1] = -37;
    osc_override = -83;
    gating_override = -99;
    assert(core_sample(&sample) == -37);
    assert(pcr_calls == 5 && sleeps == 5 && strstr(actions, "OG"));
    reset();
    osc_override = 0;
    gating_override = -99;
    assert(core_sample(&sample) == -2 && strstr(actions, "OG"));
    reset();
    osc_override = -83;
    assert(core_sample(&sample) == -83);
    reset();
    gating_override = 0;
    assert(core_sample(&sample) == -3);
    reset();
    gating_override = -99;
    assert(core_sample(&sample) == -99);
    reset();
    gating_override = 2;
    assert(core_sample(&sample) == -3);
    reset();
    table.consys_ic_sleep_info_read_ctrl = NULL;
    assert(core_sample(&sample) == -EOPNOTSUPP && !provider_calls);
    reset();
    readable = 0;
    assert(core_sample(&sample) == -1 && !pcr_calls && !strstr(actions, "O"));
    reset();
    readable = 0;
    bank_rc[0] = -37;
    assert(core_sample(&sample) == -37);
    reset();
    gMtkWmtCtx.eDrvStatus[0] = 0;
    assert(core_sample(&sample) == -1 && !provider_calls);
    reset();
    sample.status = WMT_DUMP_STATE_NONE;
    assert(core_sample(&sample) == -1 && !provider_calls);
    reset();
    sample.status = WMT_DUMP_STATE_SCHEDULED;
    WMT_OP stale = {.au4OpData = {(SIZE_T)&sample, 6}};
    assert(opfunc_get_consys_state(&stale) == -1 && !provider_calls);
}
static void native(void)
{
    UINT64 counter = 0xaaaaaaaaaaaaaaaaULL, timer = counter;
    conn_reg.mcu_conn_hif_on_base = 0;
    assert(consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_WF, &counter, &timer) == -1);
    conn_reg.mcu_conn_hif_on_base = 0x1000;
    assert(consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_MAX, &counter, &timer) == -2);
    assert(consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_WF, NULL, NULL) == -3);
    assert(consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_WF, &counter, &timer) == -4);
    assert(counter == 0xaaaaaaaaaaaaaaaaULL && timer == counter && !conn_write_count);
    reset();
    registers[(0x1000 + CONN_HOST_CR_SLEEP_CNT_OFFSET) / 4] = 0x21;
    registers[(0x1000 + CONN_SLEEP_INFO_CTRL_OFFSET) / 4] = 1 << 11;
    registers[(0x1000 + CONN_SLEEP_INFO_READ_CTRL_COUNT_OFFSET) / 4] = 10;
    registers[(0x1000 + CONN_SLEEP_INFO_READ_CTRL_TIMER_OFFSET) / 4] = 12345;
    assert(consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_WF, &counter, &timer) == 0);
    assert(counter == 10 && timer == 12345 && conn_write_count == 3);
    assert(strcmp(actions, "rrrwrw4rw1rrr") == 0);
    reset();
    table.consys_ic_sleep_info_read_ctrl = consys_sleep_info_read_ctrl;
    CONSYS_STATE state, saved;
    memset(&state, 0xaa, sizeof(state));
    saved = state;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == -4);
    assert(!memcmp(&state, &saved, sizeof(state)) && !conn_write_count && !strstr(logs, "TOP:"));
    reset();
    table.consys_ic_sleep_info_read_ctrl = consys_sleep_info_read_ctrl;
    registers[(0x1000 + CONN_HOST_CR_SLEEP_CNT_OFFSET) / 4] = 0x21;
    assert(mtk_wcn_consys_sleep_info_read_all_ctrl(&state) == 0 && conn_write_count == 15);
    for (int bank = 0; bank < WMT_SLEEP_COUNT_MAX; bank++)
        assert(state.sleep_counter[bank] == 0 && state.sleep_timer[bank] == 0);
    assert(strstr(logs, "WIFI:0,0"));
}
static void osc(void)
{
    CONSYS_STATE state, saved;
    memset(&state, 0xaa, sizeof(state));
    saved = state;
    conn_reg.mcu_cfg_on_base = 0;
    assert(consys_dump_osc_state(&state) == MTK_WCN_BOOL_FALSE);
    assert(!memcmp(&state, &saved, sizeof(state)) && !conn_write_count && !conn_read_count);
    reset();
    conn_reg.mcu_top_misc_on_base = 0;
    assert(consys_dump_osc_state(&state) == MTK_WCN_BOOL_FALSE && !conn_write_count);
    reset();
    registers[(0x2000 + CONN_CFG_ON_MON_FLAG_RECORD_ADDR_OFFSET) / 4] = 1 << 12;
    assert(consys_dump_osc_state(&state) == MTK_WCN_BOOL_TRUE);
    assert(state.lp[1] == 1 << 12 && conn_write_count == 6 && strcmp(actions, "wwwwwwr") == 0);
    reset();
    conn_reg.mcu_conn_hif_on_base = 0;
    saved = state;
    assert(consys_dump_gating_state(&state) == MTK_WCN_BOOL_FALSE && !memcmp(&state, &saved, sizeof(state)));
    reset();
    registers[(0x1000 + CONN_MCU_MAILBOX_DBG) / 4] = 7;
    assert(consys_dump_gating_state(&state) == MTK_WCN_BOOL_TRUE && state.sw_state.is_gating == 0);
    assert(conn_read_count == 1 && conn_write_count == 0);
    reset();
    registers[(0x1000 + CONN_MCU_MAILBOX_DBG) / 4] = 0x123456;
    assert(consys_dump_gating_state(&state) == MTK_WCN_BOOL_TRUE && state.gating[1] == 0x123456);
    assert(strstr(logs, "no gating info found"));
}
static void library(void)
{
    CONSYS_STATE_DMP_INFO info, saved;
    memset(&info, 0xaa, sizeof(info));
    saved = info;
    assert(wmt_lib_dmp_consys_state(&info, 5, 0) == MTK_WCN_BOOL_TRUE);
    assert(info.cpu_pcr[0] == 42 && info.state.sleep_timer[3] == 1003);
    assert(host_op.result == 123456 && !gDevWmt.state_dmp_req.consys_ops[0].status);
    for (int failure = 0; failure < 8; failure++) {
        reset();
        info = saved;
        if (failure == 0) bank_rc[3] = -37;
        if (failure == 1) gating_override = -83;
        if (failure == 2) no_free = 1;
        if (failure == 3) put_failure = 1;
        if (failure == 4) no_completion = 1;
        if (failure == 5) coredump = 1;
        if (failure == 6) gMtkWmtCtx.eDrvStatus[0] = 0;
        if (failure == 7) table.consys_ic_sleep_info_read_ctrl = NULL;
        assert(wmt_lib_dmp_consys_state(&info, 5, 0) == MTK_WCN_BOOL_FALSE);
        assert(!memcmp(&info, &saved, sizeof(info)) && !sleep_depth);
        assert(!gDevWmt.state_dmp_req.consys_ops[0].status);
    }
    reset();
    assert(wmt_lib_dmp_consys_state(&info, 11, 0) == MTK_WCN_BOOL_FALSE);
    reset();
    gDevWmt.state_dmp_req.consys_ops[0].status = WMT_DUMP_STATE_SCHEDULED;
    gDevWmt.state_dmp_req.consys_ops[1].status = WMT_DUMP_STATE_SCHEDULED;
    assert(wmt_lib_dmp_consys_state(&info, 5, 0) == MTK_WCN_BOOL_FALSE && !provider_calls);
}
static void consumers(void)
{
    char output[256], saved[256];
    memset(output, 0xaa, sizeof(output));
    memcpy(saved, output, sizeof(output));
    loff_t position = 0;
    assert(wmt_dev_proc_for_dump_info_read(NULL, output, sizeof(output), &position) > 0);
    assert(!strncmp(output, "0x0000002a;", 11) && position > 0 && !sleep_depth);
    position = 0;
    provider_calls = 0;
    bank_rc[3] = -37;
    memcpy(output, saved, sizeof(output));
    assert(wmt_dev_proc_for_dump_info_read(NULL, output, sizeof(output), &position) == -EIO);
    assert(!g_dump_info_buf_len && !position && !memcmp(output, saved, sizeof(output)));
    reset();
    position = 0;
    ssize_t bytes, total = 0;
    while ((bytes = wmt_dev_proc_for_dump_info_read(NULL, output + total, 16, &position)) > 0)
        total += bytes;
    assert(bytes == 0 && total == position && total > 32 && total < 256 && provider_calls == 5);
    assert(!strncmp(output, "0x0000002a;", 11));
    reset();
    position = 0;
    bank_rc[3] = -37;
    memcpy(output, saved, sizeof(output));
    assert(wmt_dev_proc_for_dump_info_read(NULL, output, sizeof(output), &position) == -EIO);
    assert(!memcmp(output, saved, sizeof(output)) && position == 0 && !sleep_depth);
    reset();
    position = 0;
    lock_failure = 1;
    assert(wmt_dev_proc_for_dump_info_read(NULL, output, sizeof(output), &position) == -EIO);
    reset();
    position = 0;
    copy_failure = 1;
    assert(wmt_dev_proc_for_dump_info_read(NULL, output, sizeof(output), &position) == -EFAULT && !position && !sleep_depth);
    reset();
    plat_resume_handler(NULL);
    assert(strstr(logs, "0x0000002a;") && !strstr(logs, "unavailable"));
    reset();
    bank_zero = 1;
    plat_resume_handler(NULL);
    assert(strstr(logs, ";0,0,0,0,0;0,0,0,0,0") && !strstr(logs, "unavailable"));
    reset();
    gMtkWmtCtx.eDrvStatus[0] = 0;
    plat_resume_handler(NULL);
    assert(strstr(logs, "unavailable") && !strstr(logs, "TOP:0,0"));
    reset();
    bank_rc[3] = -37;
    plat_resume_handler(NULL);
    assert(strstr(logs, "unavailable") && !strstr(logs, "0x0000002a;") && !strstr(logs, "TOP:"));
    reset();
    gating_override = -99;
    plat_resume_handler(NULL);
    assert(strstr(logs, "unavailable") && !strstr(logs, "0x0000002a;"));
    reset();
    wmt_alarm_dmp_info_handler(NULL);
    assert(strstr(logs, "0x0000002a;"));
    reset();
    bank_rc[3] = -37;
    wmt_alarm_dmp_info_handler(NULL);
    assert(!strstr(logs, "0x0000002a;") && !strstr(logs, "TOP:") && !sleep_depth);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    reset();
    if (!strcmp(argv[1], "banks")) banks();
    else if (!strcmp(argv[1], "missing")) missing();
    else if (!strcmp(argv[1], "core")) core();
    else if (!strcmp(argv[1], "native")) native();
    else if (!strcmp(argv[1], "osc")) osc();
    else if (!strcmp(argv[1], "library")) library();
    else if (!strcmp(argv[1], "consumers")) consumers();
    else abort();
    assert(!spin_depth && !sleep_depth);
    printf("PASS %s\n", argv[1]);
    return 0;
}
