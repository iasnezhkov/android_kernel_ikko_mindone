// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void setup(void)
{
    assert(MD_THROTTLING_COOLER_LV_COMMAND == TMC_CTRL_CMD_COOLER_LV);
    memset(&ctrl, 0, sizeof(ctrl));
    memset(&per, 0, sizeof(per));
    memset(&packet, 0, sizeof(packet));
    ctrl.md_id = MD_SYS1;
    ctrl.plat_val.md_gen = 6293;
    ctrl.txq[0].ringbuf = &ctrl;
    port.skb_from_pool = 1;
    port.hif_id = CCIF_HIF_ID;
    port.md_id = MD_SYS1;
    proxy.md_id = MD_SYS1;
    proxy.sys_port = &port;
    proxy_table[MD_SYS1] = &proxy;
    ops.send_skb = md_ccif_op_send_skb;
    ccci_hif[CCIF_HIF_ID] = &ctrl;
    ccci_hif_op[CCIF_HIF_ID] = &ops;
    dispatch_state = READY;
    cut_at = 50;
    flow.head_magic = FLOW_CTRL_HEAD;
    flow.tail_magic = FLOW_CTRL_TAIL;
}
static int send_command(unsigned int command)
{
    return exact_throttling_case(MD_SYS1, ID_THROTTLING_CFG, (char *)&command, sizeof(command));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    setup();
    const char *name = argv[1];
    unsigned int command = mutt_lv_to_tmc_msg(1, 4);
    int result = 0, stopped;
    if (!strcmp(name, "healthy-command-parity")) {
        unsigned int commands[] = {mutt_lv_to_tmc_msg(0, 0), mutt_lv_to_tmc_msg(1, 7), TMC_COOLER_LV_DISABLE_MSG, reduce_tx_pwr_to_tmc_msg(0, 4), reduce_tx_pwr_to_tmc_msg(1, 8), scg_off_to_tmc_msg(1), scg_off_to_tmc_msg(0), duty_ctrl_to_tmc_msg(1, 255, 0), duty_ctrl_to_tmc_msg(3, 4, 1), TMC_THROTTLE_DISABLE_MSG, ca_ctrl_to_tmc_msg(1), ca_ctrl_to_tmc_msg(0), pa_ctrl_to_tmc_msg(1), pa_ctrl_to_tmc_msg(0)};
        for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            assert(send_command(commands[i]) == 0);
            assert(sent.channel == CCCI_SYSTEM_TX && sent.data[0] == CCCI_MAGIC_NUM);
            assert(sent.data[1] == MD_THROTTLING);
            assert(sent.reserved == commands[i]);
            assert(writes == (int)i + 1 && kicks == writes && frees == writes && pool_calls == writes);
            assert(!wait_calls && !sleep_calls && !lock_depth);
        }
    } else if (!strcmp(name, "mutt-empty-pool")) {
        pool_failures = 1000;
        stopped = setjmp(cutoff);
        if (!stopped) result = send_command(command);
        if (EXPECT_NONBLOCKING) { assert(stopped == 0 && result == -CCCI_ERR_ALLOCATE_MEMORY_FAIL); assert(pool_calls == 1 && !sleep_calls); }
        else { assert(stopped == 1 && pool_calls == 51 && sleep_ms == 5000); }
    } else if (!strcmp(name, "mutt-full-ring") || !strcmp(name, "mutt-full-usb") || !strcmp(name, "mutt-invalid-flow") || !strcmp(name, "mutt-signal-pending")) {
        ring_failures = 1000;
        if (!strcmp(name, "mutt-full-usb")) per.data_usb_bypass = 1;
        if (!strcmp(name, "mutt-invalid-flow")) { ctrl.flow_ctrl = &flow; flow.head_magic = 0; }
        if (!strcmp(name, "mutt-signal-pending")) signal_pending = 1;
        stopped = setjmp(cutoff);
        if (!stopped) result = send_command(command);
        if (EXPECT_NONBLOCKING) {
            assert(stopped == 0);
            assert(result == (per.data_usb_bypass ? -ENOMEM : -EBUSY));
            assert(ring_checks == 1 && frees == 1 && !wait_calls && !sleep_calls && !writes && !lock_depth);
        } else { assert(stopped == 1 && ring_checks == 51 && !wait_calls && !sleep_calls); lock_depth = 0; }
    } else if (!strcmp(name, "full-ring-state-race")) {
        state_flip_at = 3;
        ring_failures = 1000;
        assert(send_command(command) == (EXPECT_NONBLOCKING ? -EBUSY : -ETXTBSY));
        assert(ring_checks == 1 && frees == 1 && !writes && !wait_calls && !sleep_calls && !lock_depth);
    } else if (!strcmp(name, "modem-off")) {
        enabled = 0;
        assert(send_command(command) == -CCCI_ERR_MD_INDEX_NOT_FOUND);
        assert(!pool_calls && !ring_checks);
    } else if (!strcmp(name, "gated-before-send") || !strcmp(name, "exception-before-send")) {
        dispatch_state = !strcmp(name, "gated-before-send") ? GATED : EXCEPTION;
        assert(send_command(command) == -CCCI_ERR_MD_NOT_READY);
        assert(!pool_calls && !ring_checks);
    } else if (!strcmp(name, "unrelated-classes-preserved")) {
        for (unsigned int command_class = 0; command_class <= 255; command_class++) {
            if (command_class == TMC_CTRL_CMD_COOLER_LV) continue;
            unsigned int preserved_command = command_class | (1 << 8) | (0x5a << 16) | (0xa5U << 24);
            pool_failures = 1000;
            pool_calls = sleep_calls = sleep_ms = 0;
            stopped = setjmp(cutoff);
            if (!stopped) result = send_command(preserved_command);
            assert(stopped == 1);
            assert(pool_calls == 51 && sleep_ms == 5000 && !writes);
        }
    } else if (!strcmp(name, "invalid-id")) {
        assert(ccci_port_send_msg_to_md(-1, CCCI_SYSTEM_TX, MD_THROTTLING, command, 0) == -ENODEV);
        assert(ccci_port_send_msg_to_md(MAX_MD_NUM, CCCI_SYSTEM_TX, MD_THROTTLING, command, 0) == -ENODEV);
        assert(!pool_calls);
    } else if (!strcmp(name, "ready-pool-recovery")) {
        pool_failures = 1;
        if (EXPECT_NONBLOCKING) {
            assert(send_command(command) == -CCCI_ERR_ALLOCATE_MEMORY_FAIL && !writes);
            assert(send_command(command) == 0 && writes == 1 && !sleep_calls && pool_calls == 2);
        } else assert(send_command(command) == 0 && writes == 1 && pool_calls == 2 && sleep_ms == 100);
    } else if (!strcmp(name, "transport-readback-contract")) {
        write_result = -CCCI_RINGBUF_PARAM_ERR;
        assert(send_command(command) == 0 && writes == 1 && kicks == 1 && frees == 1);
    } else if (!strcmp(name, "length-handling")) {
        unsigned int lengths[] = {0, 1, 4, 8};
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
            assert(exact_throttling_case(MD_SYS1, ID_THROTTLING_CFG, (char *)&command, lengths[i]) == 0);
        assert(writes == 4 && sent.reserved == command);
    } else if (!strcmp(name, "off-null-pointer")) {
        enabled = 0;
        assert(exact_throttling_case(MD_SYS1, ID_THROTTLING_CFG, NULL, 0) == -CCCI_ERR_MD_INDEX_NOT_FOUND);
        assert(!pool_calls);
    } else assert(0);
    assert(signal_queries == 0);
    printf("PASS %s/%s\n", EXPECT_NONBLOCKING ? "class3" : "baseline", name);
    return 0;
}
