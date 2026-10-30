// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct {
    struct nlmsghdr header;
    struct fgd_nl_msg_t message;
} packet;
static struct sk_buff skb;

static void request(void)
{
    memset(&packet, 0, sizeof(packet));
    packet.header.nlmsg_len = NLMSG_HDRLEN + FGD_NL_HDR_LEN;
    packet.header.nlmsg_seq = 73;
    packet.message.fgd_cmd = FG_DAEMON_CMD_GET_PTIM_SAMPLE_V1;
    packet.message.fgd_ret_data_len = 56;
    packet.message.identity = FGD_NL_MAGIC;
    skb = (struct sk_buff){ .len = packet.header.nlmsg_len, .data = &packet, .creds = { .pid = 42 }, .portid = 4242 };
}

static void reset(void)
{
    reset_adc();
    battery = (struct mtk_battery){ .gauge = &gauge, .fgd_pid = 42,
        .fg_cust_data = { .car_tune_value = 1000 }, .bs_data = { .psy = &supply },
        .ptim_lk_v = 39000, .ptim_lk_i = 777 };
    gauge = (struct mtk_gauge){ .gm = &battery, .chan_ptim_sample = &channel,
        .psy = &supply, .hw_status = { .r_fg_value = DEFAULT_R_FG } };
    allocations = frees = sent = ready_calls = legacy_reads = 0;
    scale_calls = raw_calls = time_calls = injected_raw_status = 0;
    first_time = 100;
    last_time = 200;
    request();
}

static struct fgd_ptim_sample_v1 reply(void)
{
    struct fgd_ptim_sample_v1 result;
    int before = sent;
    mtk_battery_netlink_handler(&skb);
    CHECK(sent == before + 1 && allocations == frees);
    CHECK(captured.fgd_cmd == 0x10000U && captured.fgd_data_len == 56);
    CHECK(last_seq == 73 && ready_calls == 0 && legacy_reads == 0);
    memcpy(&result, captured.fgd_data, sizeof(result));
    CHECK(result.version == 1 && result.size == 56);
    CHECK(!gauge.ops_lock.held && !adc.lock.held && wake_count == 0);
    cases++;
    return result;
}

static void failure(int expected)
{
    u64 sequence = gauge.ptim_sequence;
    struct fgd_ptim_sample_v1 result = reply();
    CHECK(result.status == expected);
    const unsigned char *bytes = (const unsigned char *)&result;
    for (size_t i = 12; i < sizeof(result); i++)
        CHECK(bytes[i] == 0);
    CHECK(gauge.ptim_sequence == sequence);
}

static void rejected(void)
{
    int before_alloc = allocations, before_io = io_count, before_sent = sent;
    int before_scale = scale_calls, before_raw = raw_calls, before_legacy = legacy_reads;
    forbid_allocation = true;
    mtk_battery_netlink_handler(&skb);
    forbid_allocation = false;
    CHECK(allocations == before_alloc && io_count == before_io && sent == before_sent);
    CHECK(scale_calls == before_scale && raw_calls == before_raw && legacy_reads == before_legacy);
    cases++;
}

static void scalar(unsigned int command, int expected)
{
    request();
    packet.message.fgd_cmd = command;
    packet.message.fgd_ret_data_len = 4;
    int before = sent, value;
    mtk_battery_netlink_handler(&skb);
    CHECK(sent == before + 1 && captured.fgd_cmd == command && captured.fgd_data_len == 4);
    memcpy(&value, captured.fgd_data, sizeof(value));
    CHECK(value == expected && allocations == frees && raw_calls == 0 && io_count == 0);
    cases++;
}

int main(void)
{
    CHECK(sizeof(struct fgd_ptim_sample_v1) == 56);
    CHECK(_Alignof(struct fgd_ptim_sample_v1) == 8);
    CHECK(offsetof(struct fgd_ptim_sample_v1, version) == 0);
    CHECK(offsetof(struct fgd_ptim_sample_v1, size) == 4);
    CHECK(offsetof(struct fgd_ptim_sample_v1, status) == 8);
    CHECK(offsetof(struct fgd_ptim_sample_v1, flags) == 12);
    CHECK(offsetof(struct fgd_ptim_sample_v1, sequence) == 16);
    CHECK(offsetof(struct fgd_ptim_sample_v1, started_ns) == 24);
    CHECK(offsetof(struct fgd_ptim_sample_v1, acquired_ns) == 32);
    CHECK(offsetof(struct fgd_ptim_sample_v1, voltage_01mv) == 40);
    CHECK(offsetof(struct fgd_ptim_sample_v1, current_01ma) == 44);
    CHECK(offsetof(struct fgd_ptim_sample_v1, r_fg_value) == 48);
    CHECK(offsetof(struct fgd_ptim_sample_v1, car_tune_value) == 52);
    CHECK(FG_DAEMON_CMD_GET_PTIM_SAMPLE_V1 == 0x10000U && FGD_PTIM_SAMPLE_VALID == 1);
    CHECK(FG_DAEMON_CMD_GET_RAC == 14 && FG_DAEMON_CMD_GET_PTIM_VBAT == 15);
    CHECK(FG_DAEMON_CMD_GET_PTIM_I == 16 && FG_DAEMON_CMD_GET_BH_DATA == 117);
    CHECK(FG_DAEMON_CMD_FROM_USER_NUMBER == 118 && FGD_NL_HDR_LEN == 32);

    reset(); injected_raw_status = -ENODATA; failure(-ENODATA);
    reset(); injected_raw_status = -EIO; failure(-EIO);
    reset(); injected_raw_status = -ETIMEDOUT; failure(-ETIMEDOUT);
    reset(); next_voltage = 0; failure(-ENODATA);
    reset(); fail_at = 1; failure(-EIO);
    reset(); ready_after = -1; failure(-ETIMEDOUT);
    reset(); gauge.chan_ptim_sample = NULL; failure(-EOPNOTSUPP); CHECK(io_count == 0);
    reset(); gauge.chan_ptim_sample = ERR_PTR(-EOPNOTSUPP); failure(-EOPNOTSUPP);
    reset(); struct auxadc_info unsupported = {0}; adc.info = &unsupported; failure(-EOPNOTSUPP); CHECK(io_count == 0);
    reset(); battery.disableGM30 = 1; failure(-EOPNOTSUPP); CHECK(scale_calls == 0);
    reset(); battery.gauge = NULL; failure(-ENODEV); CHECK(scale_calls == 0);
    struct fgd_ptim_sample_v1 direct;
    memset(&direct, 0xa5, sizeof(direct));
    fg_daemon_get_ptim_sample(NULL, &direct);
    CHECK(direct.status == -ENODEV && direct.flags == 0 && direct.sequence == 0);

    reset();
    struct fgd_ptim_sample_v1 result = reply();
    CHECK(result.status == 0 && result.flags == 1 && result.sequence == 1);
    CHECK(result.voltage_01mv == 41190 && result.current_01ma > 0);
    CHECK(result.started_ns == 100 && result.acquired_ns == 200);
    CHECK(result.r_fg_value == DEFAULT_R_FG && result.car_tune_value == 1000);
    int cleanup_steps[8], cleanup_count = 0;
    for (int i = io_count - 8; i < io_count - 1; i++)
        cleanup_steps[cleanup_count++] = i + 1;
    for (int i = 0; i < cleanup_count; i++) {
        reset();
        fail_at = cleanup_steps[i];
        failure(-EIO);
        CHECK(adc.imp_cleanup_pending);
    }

    reset();
    result = reply();
    CHECK(result.sequence == 1 && gauge.ptim_sequence == 1);
    injected_raw_status = -EIO;
    failure(-EIO);
    injected_raw_status = 0;
    next_voltage = 24000;
    next_current = 1000;
    first_time = 300;
    last_time = 400;
    result = reply();
    CHECK(result.status == 0 && result.flags == 1 && result.sequence == 2);
    CHECK(result.voltage_01mv == 39550 && result.current_01ma < 0);
    CHECK(result.started_ns == 300 && result.acquired_ns == 400);

    const unsigned int raw_currents[] = {1000, 0, 65400};
    const int signs[] = {-1, 0, 1};
    for (unsigned int i = 0; i < ARRAY_SIZE(raw_currents); i++) {
        reset(); next_current = raw_currents[i]; result = reply();
        CHECK(result.status == 0 && result.flags == 1);
        CHECK((result.current_01ma > 0) - (result.current_01ma < 0) == signs[i]);
    }

    const unsigned int lengths[] = {57, 0, 1, 4, 55, 9200, UINT_MAX};
    for (unsigned int i = 0; i < ARRAY_SIZE(lengths); i++) {
        reset(); packet.message.fgd_ret_data_len = lengths[i]; rejected();
    }
    reset(); packet.message.fgd_data_len = 1; rejected();
    reset(); packet.message.fgd_data_len = UINT_MAX; rejected();
    reset(); packet.message.fgd_subcmd = 1; rejected();
    reset(); packet.message.fgd_subcmd_para1 = 1; rejected();
    const unsigned int transport_lengths[] = {0, 15, 16, 47, 49, 52, 100, UINT_MAX};
    for (unsigned int i = 0; i < ARRAY_SIZE(transport_lengths); i++) {
        reset(); packet.header.nlmsg_len = transport_lengths[i]; rejected();
        reset(); skb.len = transport_lengths[i]; rejected();
    }
    reset(); packet.header.nlmsg_len++; skb.len++; rejected();
    reset(); packet.message.identity = 0; rejected();
    reset(); skb.creds.pid++; skb.portid = 9001;
    {
        int before = sent, ready = ready_calls;
        mtk_battery_netlink_handler(&skb);
        CHECK(sent == before + 1 && last_port == 9001 && last_seq == 73);
        CHECK(captured.fgd_cmd == FG_DAEMON_CMD_GET_PTIM_SAMPLE_V1 && captured.fgd_data_len == 56);
        CHECK(ready_calls == ready && allocations == frees);
    }
    reset(); skb.creds.pid++; packet.message.fgd_cmd = FG_DAEMON_CMD_GET_PTIM_VBAT;
    packet.message.fgd_ret_data_len = 4; rejected();

    reset(); battery.init_flag = 1;
    scalar(FG_DAEMON_CMD_GET_INIT_FLAG, 1);
    scalar(FG_DAEMON_CMD_GET_RAC, 87);
    scalar(FG_DAEMON_CMD_GET_PTIM_VBAT, 41230);
    scalar(FG_DAEMON_CMD_GET_PTIM_I, -1234);
    reset();
    scalar(FG_DAEMON_CMD_GET_PTIM_VBAT, 39000);
    scalar(FG_DAEMON_CMD_GET_PTIM_I, 777);
    CHECK(ready_calls == 0);
    finish();
    return 0;
}
