// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static uint8_t packet[300], src_mac[6], dst_mac[6];
static struct wiphy wiphy = { &glue };
static struct wireless_dev wdev;
static unsigned cases;
static struct nlattr *make_request(unsigned declared, unsigned actual, unsigned missing,
                                  int reverse, int stop, unsigned id)
{
    uint8_t bytes[1024] = {0};
    struct nlattr *outer = (void *)bytes;
    unsigned cursor = NLA_HDRLEN;
    uint16_t length = declared;
    uint8_t index = id;
    uint32_t period = 90000;
    const void *values[] = {NULL, &index, &length, packet, src_mac, dst_mac, &period};
    unsigned sizes[] = {0, 1, 2, actual, 6, 6, 4};
    unsigned order[] = {1, 2, 4, 5, 6, 3};
    for (unsigned i = 0; i < 6; i++) {
        unsigned type = reverse ? order[5-i] : order[i];
        if (type == missing || (stop && type != MKEEP_ALIVE_ATTRIBUTE_ID))
            continue;
        struct nlattr *a = (void *)(bytes + cursor);
        a->nla_type = type;
        a->nla_len = NLA_HDRLEN + sizes[type];
        memcpy(nla_data(a), values[type], sizes[type]);
        cursor += NLA_ALIGN(a->nla_len);
    }
    outer->nla_type = NLA_F_NESTED;
    outer->nla_len = cursor;
    struct nlattr *result = malloc(cursor);
    assert(result);
    memcpy(result, bytes, cursor);
    request_attr_count = 0;
    for (unsigned offset = NLA_HDRLEN; offset < cursor;) {
        struct nlattr *a = (void *)((char *)result + offset);
        request_attrs[request_attr_count++] = a;
        offset += NLA_ALIGN(a->nla_len);
    }
    return result;
}
static void reset(void)
{
    assert(live_allocations == 0);
    allocation_calls = ioctl_calls = sends = fail_allocation = 0;
    send_status = ioctl_status = 0;
    request_attr_count = 0;
    glue.u4ReadyFlag = 1;
    memset(&captured, 0xff, sizeof(captured));
}
static int invoke(struct nlattr *request, int stop)
{
    return stop ? mtk_cfg80211_vendor_packet_keep_alive_stop(&wiphy, &wdev,
                    nla_data(request), nla_len(request)) :
                  mtk_cfg80211_vendor_packet_keep_alive_start(&wiphy, &wdev,
                    nla_data(request), nla_len(request));
}
static void check_reject(unsigned length, unsigned actual, unsigned missing, int stop)
{
    reset();
    struct nlattr *request = make_request(length, actual, missing, 0, stop, 15);
    assert(invoke(request, stop) == -EINVAL);
    assert(ioctl_calls == 0 && sends == 0 && live_allocations == 0);
    free(request);
    cases++;
}
static void check_valid(unsigned length, int reverse, int stop, unsigned id)
{
    reset();
    struct nlattr *request = make_request(length, length, 0, reverse, stop, id);
    assert(invoke(request, stop) == 0);
    assert(ioctl_calls == 1 && sends == 1 && live_allocations == 0);
    assert(captured.enable == !stop && captured.index == id);
    assert(captured.u2IpPktLen == (stop ? 0 : (int)length));
    if (!stop) {
        assert(memcmp(captured.pIpPkt, packet, length) == 0);
        assert(memcmp(captured.ucSrcMacAddr, src_mac, 6) == 0);
        assert(memcmp(captured.ucDstMacAddr, dst_mac, 6) == 0);
        assert(captured.u4PeriodMsec == 90000);
        for (unsigned i = length; i < sizeof(captured.pIpPkt); i++)
            assert(captured.pIpPkt[i] == 0);
    }
    free(request);
    cases++;
}
static void check_oid(int length, bool enable, uint32_t status)
{
    reset();
    struct PARAM_PACKET_KEEPALIVE_T request = {0};
    request.enable = enable;
    request.index = 15;
    request.u2IpPktLen = length;
    uint32_t info = 0;
    assert(wlanoidPacketKeepAlive(&adapter, &request, sizeof(request), &info) == status);
    assert(info == sizeof(request));
    assert(sends == (status == 0) && live_allocations == 0);
    cases++;
}
static void check_stop_without_id(void)
{
    reset();
    struct nlattr *request = make_request(28, 28, MKEEP_ALIVE_ATTRIBUTE_ID, 0, 0, 15);
    assert(invoke(request, 1) == -EINVAL);
    assert(sends == 0 && live_allocations == 0);
    free(request);
    cases++;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    packet_size = sizeof(struct PARAM_PACKET_KEEPALIVE_T);
    packet_data_offset = offsetof(struct PARAM_PACKET_KEEPALIVE_T, pIpPkt);
    packet_data_size = sizeof(captured.pIpPkt);
    for (unsigned i = 0; i < sizeof(packet); i++) packet[i] = i ^ 0xa5;
    for (unsigned i = 0; i < 6; i++) { src_mac[i] = i + 2; dst_mac[i] = i + 8; }
    if (!strcmp(argv[1], "signed-copy")) { check_reject(32768, 256, 0, 0); return 0; }
    if (!strcmp(argv[1], "short-copy")) { check_reject(256, 1, 0, 0); return 0; }
    if (!strcmp(argv[1], "missing-start")) { check_reject(28, 28, MKEEP_ALIVE_ATTRIBUTE_IP_PKT, 0); return 0; }
    if (!strcmp(argv[1], "missing-stop")) {
        check_stop_without_id();
        return 0;
    }
    if (!strcmp(argv[1], "oid-length")) { check_oid(-1, true, WLAN_STATUS_INVALID_DATA); return 0; }
    if (!strcmp(argv[1], "not-ready")) {
        reset();
        struct nlattr *request = make_request(28, 28, 0, 0, 0, 15);
        glue.u4ReadyFlag = 0;
        assert(invoke(request, 0) == -EFAULT);
        assert(ioctl_calls == 0 && sends == 0 && live_allocations == 0);
        free(request);
        return 0;
    }
    assert(!strcmp(argv[1], "all"));
    for (unsigned length = 1; length <= 256; length++) {
        check_valid(length, 0, 0, 15);
        check_valid(length, 1, 0, 255);
    }
    check_valid(0, 0, 1, 0);
    check_valid(0, 0, 1, 15);
    check_valid(0, 1, 1, 255);
    reset();
    struct nlattr *ordered = make_request(28, 28, 0, 1, 0, 15);
    assert(invoke(ordered, 1) == 0);
    assert(sends == 1 && !captured.enable && captured.index == 15 && live_allocations == 0);
    free(ordered);
    cases++;
    for (unsigned variant = 0; variant < 2; variant++) {
        reset();
        struct nlattr *request = make_request(28, 28, 0, 0, 0, 15);
        uint32_t period = variant ? UINT32_MAX : 0;
        for (unsigned i = 0; i < request_attr_count; i++) {
            if (request_attrs[i]->nla_type == MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC)
                memcpy(nla_data(request_attrs[i]), &period, sizeof(period));
        }
        assert(invoke(request, 0) == 0);
        assert(captured.u4PeriodMsec == period && sends == 1 && live_allocations == 0);
        free(request);
        cases++;
    }
    for (unsigned missing = 1; missing <= 6; missing++)
        check_reject(28, 28, missing, 0);
    unsigned invalid[] = {0, 257, 32767, 32768, 65535};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
        check_reject(invalid[i], 256, 0, 0);
    check_reject(256, 1, 0, 0);
    check_reject(1, 256, 0, 0);
    check_reject(257, 257, 0, 0);
    check_reject(28, 28, MKEEP_ALIVE_ATTRIBUTE_ID, 1);
    check_stop_without_id();
    for (unsigned type = 1; type <= 6; type++) {
        for (unsigned length = 0; length <= 7; length++) {
            if (type == MKEEP_ALIVE_ATTRIBUTE_IP_PKT ||
                (type == MKEEP_ALIVE_ATTRIBUTE_ID && length == 1) ||
                (type == MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN && length == 2) ||
                (type == MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC && length == 4) ||
                ((type == MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR ||
                  type == MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR) && length == 6))
                continue;
            reset();
            struct nlattr *request = make_request(28, 28, 0, 0, 0, 15);
            for (unsigned i = 0; i < request_attr_count; i++) {
                if (request_attrs[i]->nla_type == type)
                    ((struct nlattr *)request_attrs[i])->nla_len = NLA_HDRLEN + length;
            }
            assert(invoke(request, 0) == -EINVAL);
            assert(sends == 0 && live_allocations == 0);
            free(request);
            cases++;
        }
    }
    for (int stop = 0; stop <= 1; stop++) {
        reset();
        int (*handler)(struct wiphy *, struct wireless_dev *, const void *, int) =
            stop ? mtk_cfg80211_vendor_packet_keep_alive_stop :
                   mtk_cfg80211_vendor_packet_keep_alive_start;
        struct nlattr *input = make_request(28, 28, 0, 0, stop, 15);
        assert(handler(&wiphy, &wdev, NULL, nla_len(input)) == -EINVAL);
        assert(handler(&wiphy, &wdev, nla_data(input), 0) == -EINVAL);
        assert(handler(&wiphy, &wdev, nla_data(input), -1) == -EINVAL);
        assert(handler(&wiphy, &wdev, nla_data(input), 1) == -EINVAL);
        assert(sends == 0 && live_allocations == 0);
        free(input);
        cases += 4;
        for (unsigned fail = 1; fail <= 2; fail++) {
            reset();
            fail_allocation = fail;
            struct nlattr *request = make_request(28, 28, 0, 0, stop, 15);
            assert(invoke(request, stop) == -ENOMEM);
            assert(sends == 0 && live_allocations == 0);
            free(request);
            cases++;
        }
        reset();
        struct nlattr *request = make_request(28, 28, 0, 0, stop, 15);
        glue.u4ReadyFlag = 0;
        assert(invoke(request, stop) == -EFAULT);
        assert(ioctl_calls == 0 && sends == 0 && live_allocations == 0);
        glue.u4ReadyFlag = 1;
        ioctl_status = 0xc0000001;
        assert(invoke(request, stop) == (int)ioctl_status);
        assert(sends == 0 && live_allocations == 0);
        ioctl_status = 0;
        send_status = 0xc0000001;
        assert(invoke(request, stop) == (int)send_status);
        assert(sends == 1 && live_allocations == 0);
        free(request);
        cases += 3;
    }
    check_oid(0, false, 0);
    check_oid(28, true, 0);
    check_oid(256, true, 0);
    check_oid(0, true, WLAN_STATUS_INVALID_DATA);
    check_oid(-1, true, WLAN_STATUS_INVALID_DATA);
    check_oid(257, true, WLAN_STATUS_INVALID_DATA);
    reset();
    uint32_t info = 0;
    struct PARAM_PACKET_KEEPALIVE_T request = {0};
    assert(wlanoidPacketKeepAlive(&adapter, NULL, sizeof(request), &info) == WLAN_STATUS_INVALID_DATA);
    assert(wlanoidPacketKeepAlive(&adapter, &request, sizeof(request)-1, &info) == WLAN_STATUS_BUFFER_TOO_SHORT);
    assert(sends == 0 && live_allocations == 0);
    cases += 2;
    printf("PASS %u exact-handler and OID cases\n", cases);
    return 0;
}
