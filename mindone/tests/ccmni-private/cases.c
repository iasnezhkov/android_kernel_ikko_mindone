// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct net_device device, other_device;
static struct ccmni_instance instance, second, target_instance;
static struct ccmni_ccci_ops ops, target_ops;
static struct ccmni_ctl_block control, target_control;

static void reset(void) {
    memset(&device, 0, sizeof(device));
    memset(&other_device, 0, sizeof(other_device));
    memset(&instance, 0, sizeof(instance));
    memset(&second, 0, sizeof(second));
    memset(&target_instance, 0, sizeof(target_instance));
    memset(&ops, 0, sizeof(ops));
    memset(&target_ops, 0, sizeof(target_ops));
    memset(&control, 0, sizeof(control));
    memset(&target_control, 0, sizeof(target_control));
    memset(ccmni_ctl_blk, 0, sizeof(ccmni_ctl_blk));
    memset(order, 0, sizeof(order));
    order_length = 0;
    copies = disables = wakes = opens = copy_fault = port_errno = 0;
    copied_pointer = NULL;
    copied_size = 0;
    compat_mode = false;
    compat_input = 0;
    compat_output = NULL;
    user_request = user_payload = NULL;
    user_request_size = 0;
    strcpy(device.name, "ccmni1");
    strcpy(other_device.name, "ccmni2");
    device.netdev_ops = &ccmni_netdev_ops;
    device.private = &instance;
    device.present = device.running = true;
    instance.dev = &device;
    instance.usage = 1;
    second.dev = &other_device;
    instance.ch.multiq = 1;
    ops.ccmni_num = 2;
    ops.ccci_handle_port_list = port_open;
    control.ccci_ops = &ops;
    control.ccmni_inst[0] = &instance;
    control.ccmni_inst[1] = &second;
    ccmni_ctl_blk[0] = &control;
    target_ops.ccmni_num = 2;
    target_control.ccci_ops = &target_ops;
    target_control.ccmni_inst[0] = &target_instance;
    target_control.ccmni_inst[1] = &second;
    target_instance.dev = &other_device;
    target_instance.md_id = MD_SYS3;
    target_instance.ch.multiq = 1;
}

static int native(int command, struct ifreq *request) {
    struct ifreq kernel_request;
    void *data;
    struct ifreq original = *request;
    user_request = request;
    user_request_size = sizeof(*request);
    user_payload = command == SIOCFWDFILTER ? request->ifr_data : NULL;
    assert(get_user_ifreq(&kernel_request, &data, request) == 0);
    int result = dev_siocdevprivate(&device, &kernel_request, data, command);
    assert(!memcmp(request, &original, sizeof(original)));
    assert(!memcmp(&kernel_request, &original, sizeof(original)));
    return result;
}

static int compat(int command, struct compat_ifreq *request) {
    struct ifreq kernel_request;
    void *data;
    struct compat_ifreq original = *request;
    compat_mode = true;
    user_request = request;
    user_request_size = sizeof(*request);
    user_payload = command == SIOCFWDFILTER ? compat_output : NULL;
    assert(get_user_ifreq(&kernel_request, &data, request) == 0);
    assert(copied_size == sizeof(struct compat_ifreq));
    int result = dev_siocdevprivate(&device, &kernel_request, data, command);
    assert(!memcmp(request, &original, sizeof(original)));
    assert(!memcmp(&kernel_request, &original, sizeof(original)));
    struct compat_ifreq output;
    memset(&output, 0xff, sizeof(output));
    assert(put_user_ifreq(&kernel_request, &output) == 0);
    assert(copied_size == sizeof(output));
    assert(!memcmp(&output, &original, sizeof(original)));
    return result;
}

static void scalars(void) {
    for (int mode = 0; mode < 2; mode++) {
        reset();
        struct ifreq request = {0};
        struct compat_ifreq request32 = {0};
        int value = (37 << 16) | 0x300;
        request.ifr_ifru.ifru_ivalue = value;
        request32.ifr_ifru.ifru_ivalue = value;
        assert((mode ? compat(SIOCSTXQSTATE, &request32) : native(SIOCSTXQSTATE, &request)) == 0);
        assert(instance.usage == 0 && disables == 1 && device.watchdog_timeo == 37 * HZ);
        request.ifr_ifru.ifru_ivalue = request32.ifr_ifru.ifru_ivalue = 1;
        assert((mode ? compat(SIOCSTXQSTATE, &request32) : native(SIOCSTXQSTATE, &request)) == 0);
        assert(instance.usage == 1 && wakes == 1 && device.watchdog_timeo == CCMNI_NETDEV_WDT_TO);
        assert((mode ? compat(SIOCACKPRIO, &request32) : native(SIOCACKPRIO, &request)) == 0);
        assert(instance.ack_prio_en == 1 && second.ack_prio_en == 0);
        request.ifr_ifru.ifru_ivalue = request32.ifr_ifru.ifru_ivalue = 0;
        assert((mode ? compat(SIOCACKPRIO, &request32) : native(SIOCACKPRIO, &request)) == 0);
        assert(instance.ack_prio_en == 0);
        assert((mode ? compat(SIOCCCMNICFG, &request32) : native(SIOCCCMNICFG, &request)) == 0);
        ccmni_ctl_blk[MD_SYS3] = &target_control;
        request.ifr_ifru.ifru_ivalue = request32.ifr_ifru.ifru_ivalue = MD_SYS3;
        assert((mode ? compat(SIOCCCMNICFG, &request32) : native(SIOCCCMNICFG, &request)) == 0);
        assert(instance.md_id == MD_SYS3 && instance.dev == &device && instance.usage == 1);
    }
    reset();
    struct ifreq request = {0};
    device.running = false;
    assert(native(SIOCSTXQSTATE, &request) == 0 && disables == 0 && instance.usage == 1);
    device.running = true;
    assert(native(SIOCSTXQSTATE, &request) == 0 && device.watchdog_timeo == 60 * HZ);
    reset();
    instance.dev = &other_device;
    request.ifr_ifru.ifru_ivalue = MD_SYS2;
    assert(native(SIOCCCMNICFG, &request) == 0 && instance.dev == &device && instance.usage == 0);
    puts("PASS native/compat scalar union, queue state, watchdog, ACK priority and iRAT");
}

static void filters(void) {
    for (int mode = 0; mode < 2; mode++) {
        for (int version = 0x40; version <= 0x60; version += 0x20) {
            reset();
            struct ccmni_flt_act payload = { .action = CCMNI_FLT_ADD, .flt = { .ver = version, .s_pref = 32 } };
            payload.flt.ipv6.saddr[0] = 0x1234;
            struct ifreq request = {0};
            struct compat_ifreq request32;
            memset(&request32, 0xa5, sizeof(request32));
            request.ifr_data = &payload;
            request32.ifr_data = 0x24681357;
            compat_input = 0x24681357;
            compat_output = &payload;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == 0);
            assert(instance.flt_cnt == 1 && !memcmp(&instance.flt_tbl[0], &payload.flt, sizeof(payload.flt)));
            assert(copied_pointer == &payload && copied_size == (mode ? sizeof(request32) : sizeof(payload)));
            payload.action = CCMNI_FLT_DEL;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == 0);
            assert(instance.flt_cnt == 0);
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == -ENXIO);
            payload.action = CCMNI_FLT_ADD;
            instance.flt_cnt = CCMNI_FLT_NUM;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == -ENOMEM);
            payload.action = CCMNI_FLT_FLUSH;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == 0);
            assert(instance.flt_cnt == 0);
            payload.action = 99;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == 0);
            assert(instance.flt_cnt == 0);
            payload.flt.ver = 0x50;
            assert((mode ? compat(SIOCFWDFILTER, &request32) : native(SIOCFWDFILTER, &request)) == -EINVAL);
            assert(instance.flt_cnt == 0);
        }
    }
    reset();
    struct ifreq request = {0};
    assert(native(SIOCFWDFILTER, &request) == -EFAULT);
    assert(instance.flt_cnt == 0);
    copy_fault = 1;
    struct ccmni_flt_act payload = { .action = CCMNI_FLT_ADD, .flt = { .ver = 0x40 } };
    request.ifr_data = &payload;
    assert(dev_siocdevprivate(&device, &request, &payload, SIOCFWDFILTER) == -EFAULT);
    assert(instance.flt_cnt == 0);
    puts("PASS native/compat IPv4/IPv6 filter pointer, add/delete/flush and errno");
}

static void pending(void) {
    for (int mode = 0; mode < 2; mode++) {
        reset();
        struct ifreq request = {0};
        struct compat_ifreq request32 = {0};
        instance.pkt_queue_work.pending = true;
        assert((mode ? compat(SIOPUSHPENDING, &request32) : native(SIOPUSHPENDING, &request)) == 0);
        assert(opens == 1 && !instance.pkt_queue_work.pending && !strcmp(order, "CFP"));
    }
    reset();
    struct ifreq request = {0};
    instance.pkt_queue_work.running = true;
    port_errno = -EIO;
    assert(native(SIOPUSHPENDING, &request) == 0);
    assert(opens == 2 && !instance.pkt_queue_work.running && !strcmp(order, "CFPP"));
    puts("PASS pending/running-work ordering and existing logged-only port error");
}

static void unavailable(void) {
    struct ifreq request = {0};
    for (int command = SIOCSTXQSTATE; command <= SIOPUSHPENDING; command++) {
        reset();
        instance.md_id = -1;
        assert(native(command, &request) == -EINVAL);
        instance.md_id = MAX_MD_NUM;
        assert(native(command, &request) == -EINVAL);
        instance.md_id = 0;
        instance.index = -1;
        assert(native(command, &request) == -EINVAL);
        instance.index = ARRAY_SIZE(control.ccmni_inst);
        assert(native(command, &request) == -EINVAL);
        instance.index = 0;
        instance.dev = NULL;
        assert(native(command, &request) == -ENODEV);
        instance.dev = &device;
        ccmni_ctl_blk[0] = NULL;
        assert(native(command, &request) == -ENODEV);
        ccmni_ctl_blk[0] = &control;
        control.ccci_ops = NULL;
        assert(native(command, &request) == -ENODEV);
        control.ccci_ops = &ops;
        control.ccmni_inst[0] = NULL;
        assert(native(command, &request) == -ENODEV);
        assert(opens == 0 && disables == 0 && instance.flt_cnt == 0);
    }
    reset();
    request.ifr_ifru.ifru_ivalue = -1;
    assert(native(SIOCCCMNICFG, &request) == -EINVAL);
    request.ifr_ifru.ifru_ivalue = MAX_MD_NUM;
    assert(native(SIOCCCMNICFG, &request) == -EINVAL);
    request.ifr_ifru.ifru_ivalue = MD_SYS2;
    assert(native(SIOCCCMNICFG, &request) == -ENODEV);
    ccmni_ctl_blk[MD_SYS2] = &target_control;
    target_control.ccci_ops = NULL;
    assert(native(SIOCCCMNICFG, &request) == -ENODEV);
    target_control.ccci_ops = &target_ops;
    target_control.ccmni_inst[0] = NULL;
    assert(native(SIOCCCMNICFG, &request) == -ENODEV);
    target_control.ccmni_inst[0] = &target_instance;
    target_instance.dev = NULL;
    assert(native(SIOCCCMNICFG, &request) == -ENODEV);
    reset();
    request.ifr_ifru.ifru_ivalue = MD_SYS3;
    ccmni_ctl_blk[MD_SYS3] = &target_control;
    target_ops.ccmni_num = 0;
    assert(native(SIOCCCMNICFG, &request) == 0 && instance.md_id == 0);
    target_ops.ccmni_num = -1;
    assert(native(SIOCCCMNICFG, &request) == -EINVAL);
    target_ops.ccmni_num = 33;
    assert(native(SIOCCCMNICFG, &request) == -EINVAL);
    reset();
    ops.ccci_handle_port_list = NULL;
    assert(native(SIOPUSHPENDING, &request) == -EOPNOTSUPP && order_length == 0);
    instance.md_id = MD_SYS3;
    ccmni_ctl_blk[MD_SYS3] = &control;
    assert(native(SIOPUSHPENDING, &request) == -EOPNOTSUPP && order_length == 0);
    reset();
    request.ifr_ifru.ifru_ivalue = 1;
    control.ccmni_inst[1] = NULL;
    assert(native(SIOCACKPRIO, &request) == -ENODEV && instance.ack_prio_en == 0);
    ops.ccmni_num = 0;
    assert(native(SIOCACKPRIO, &request) == -EINVAL);
    ops.ccmni_num = 33;
    assert(native(SIOCACKPRIO, &request) == -EINVAL);
    reset();
    instance.index = ops.ccmni_num;
    control.ccmni_inst[instance.index] = &instance;
    assert(native(SIOPUSHPENDING, &request) == 0 && opens == 1);
    puts("PASS unavailable modem/target/MD3/callback, bounded instances, atomic ACK refusal and LAN index");
}

static void routing(void) {
    reset();
    int (*expected)(struct net_device *, struct ifreq *, void *, int) = ccmni_siocdevprivate;
    assert(ccmni_netdev_ops.ndo_siocdevprivate == expected);
    struct ifreq request = {0};
    for (int command = SIOPUSHPENDING + 1; command < SIOCDEVPRIVATE + 16; command++)
        assert(native(command, &request) == -EOPNOTSUPP);
    device.present = false;
    assert(native(SIOPUSHPENDING, &request) == -ENODEV && opens == 0);
    reset();
    struct net_device_ops old = ccmni_netdev_ops;
    old.ndo_siocdevprivate = NULL;
    device.netdev_ops = &old;
    assert(old.ndo_do_ioctl == ccmni_ioctl);
    for (int command = SIOCSTXQSTATE; command <= SIOPUSHPENDING; command++)
        assert(native(command, &request) == -EOPNOTSUPP);
    assert(opens == 0 && disables == 0);
    assert(old.ndo_do_ioctl(&device, &request, SIOPUSHPENDING) == 0 && opens == 1);
    reset();
    copy_fault = 1;
    struct ifreq kernel_request;
    void *data;
    assert(get_user_ifreq(&kernel_request, &data, &request) == -EFAULT);
    compat_mode = true;
    assert(get_user_ifreq(&kernel_request, &data, &request) == -EFAULT);
    puts("PASS ACK routing, old-dispatch failure, unsupported/present-device and ifreq copy faults");
}

int main(int argc, char **argv) {
    _Static_assert(sizeof(struct ifreq) == 40, "native ifreq ABI");
    _Static_assert(sizeof(struct compat_ifreq) == 32, "compat ifreq ABI");
    _Static_assert(offsetof(struct ifreq, ifr_ifru) == offsetof(struct compat_ifreq, ifr_ifru), "scalar ABI");
    _Static_assert(sizeof(struct ccmni_flt_act) == 40, "filter ABI");
    if (argc == 2 && !strcmp(argv[1], "routing")) routing();
    else { scalars(); filters(); pending(); unavailable(); routing(); }
    return 0;
}
