static void reset(void)
{
    for (int i = 0; i < allocation_count; i++)
        free(allocations[i]);
    allocation_count = 0;
    memset(descriptors, 0, sizeof(descriptors));
    request_calls = free_calls = requested_live = 0;
    request_fail_at = car_errno = hw_errno = files_errno = 0;
    wake_fail = notifier_errno = thread_fail = battery_psy_errno = 0;
    gauge_psy_errno = group_errno = netlink_errno = mapping_errno = 0;
    wake_live = notifier_live = worker_live = battery_worker_live = file_groups = 0;
    thermal_live = supply_live = gauge_groups = netlink_live = misc_calls = activation_calls = 0;
    request_errno = -EIO;
    hw_id = 0x5800;
    battery_thread_errno = battery_notifier_errno = battery_sysfs_errno = 0;
    kernel_algo = false;
    active_gauge = NULL;
}
static void fixture(struct mtk_battery *gm, struct mtk_gauge *gauge, struct platform_device *pdev)
{
    memset(gm, 0, sizeof(*gm));
    memset(gauge, 0, sizeof(*gauge));
    memset(pdev, 0, sizeof(*pdev));
    gm->gauge = gauge;
    gauge->gm = gm;
    gauge->pdev = pdev;
    pdev->dev.data = gauge;
    gm->mtk_battery_sk = gauge;
    for (int i = 0; i < GAUGE_IRQ_MAX; i++)
        gauge->irq_no[i] = 100 + i;
    active_gauge = gauge;
}
static void preserve(struct mtk_gauge *gauge)
{
    gauge->irq_requested[ZCV_IRQ] = true;
    requested_live++;
}
static void requests(void)
{
    const int versions[] = {0, 500, 1000, 1100, 2000, 2001};
    const int counts[] = {0, 3, 2, 2, 10, 10};
    for (unsigned int i = 0; i < ARRAY_SIZE(versions); i++) {
        reset();
        struct mtk_battery gm;
        struct mtk_gauge gauge;
        struct platform_device pdev;
        fixture(&gm, &gauge, &pdev);
        assert(mtk_battery_request_irqs(&gm, versions[i]) == 0);
        assert(request_calls == counts[i] && requested_live == counts[i]);
        assert(mtk_battery_request_irqs(&gm, versions[i]) == 0);
        assert(request_calls == counts[i]);
        release_gauge_irqs(&gm);
        assert(requested_live == 0 && free_calls == counts[i]);
    }
    const int errors[] = {-EIO, -EPROBE_DEFER, -EBUSY};
    for (int existing = 0; existing < 2; existing++) {
        for (int failure = 1; failure <= 10 - existing; failure++) {
            for (unsigned int e = 0; e < ARRAY_SIZE(errors); e++) {
                reset();
                struct mtk_battery gm;
                struct mtk_gauge gauge;
                struct platform_device pdev;
                fixture(&gm, &gauge, &pdev);
                if (existing)
                    preserve(&gauge);
                request_fail_at = failure;
                request_errno = errors[e];
                assert(mtk_battery_request_irqs(&gm, GAUGE_HW_V2000) == errors[e]);
                assert(requested_live == existing && free_calls == failure - 1);
                assert(gauge.irq_requested[ZCV_IRQ] == (existing != 0));
                request_fail_at = 0;
                assert(mtk_battery_request_irqs(&gm, GAUGE_HW_V2000) == 0);
                assert(requested_live == 10);
            }
        }
    }
    puts("PASS exact version-specific IRQ requests, every errno position, owned-only unwind and retry");
}
static void daemon_cases(void)
{
    reset();
    struct mtk_battery gm;
    struct mtk_gauge gauge;
    struct platform_device pdev;
    fixture(&gm, &gauge, &pdev);
    hw_errno = -EREMOTEIO;
    assert(mtk_battery_daemon_init(&pdev) == -EREMOTEIO);
    assert(request_calls == 0 && file_groups == 0);
    reset();
    fixture(&gm, &gauge, &pdev);
    preserve(&gauge);
    files_errno = -EACCES;
    assert(mtk_battery_daemon_init(&pdev) == -EACCES);
    assert(requested_live == 1 && gauge.irq_requested[ZCV_IRQ]);
    assert(free_calls == 9 && file_groups == 0);
    puts("PASS daemon setup preserves HW/sysfs errno and existing ownership");
}
static void coulomb(void)
{
    for (int fault = 0; fault < 6; fault++) {
        reset();
        struct mtk_battery gm;
        struct mtk_gauge gauge;
        struct platform_device pdev;
        fixture(&gm, &gauge, &pdev);
        assert(mtk_battery_request_irqs(&gm, GAUGE_HW_V2000) == 0);
        int expected = -EIO;
        if (fault == 0) car_errno = expected;
        if (fault == 1) request_fail_at = 11;
        if (fault == 2) request_fail_at = 12;
        if (fault == 3) { wake_fail = 1; expected = -ENOMEM; }
        if (fault == 4) notifier_errno = expected;
        if (fault == 5) { thread_fail = 1; expected = -EAGAIN; }
#ifndef CONFIG_PM
        if (fault == 4)
            continue;
#endif
        assert(gauge_coulomb_service_init(&gm) == expected);
        assert(!gm.cs.init && wake_live == 0 && notifier_live == 0 && worker_live == 0);
        assert(requested_live == 10 && !gauge.irq_requested[COULOMB_H_IRQ] && !gauge.irq_requested[COULOMB_L_IRQ]);
        car_errno = request_fail_at = wake_fail = notifier_errno = thread_fail = 0;
        assert(gauge_coulomb_service_init(&gm) == 0);
        assert(gm.cs.init && requested_live == 12 && wake_live == 1 && worker_live == 1);
        int calls = request_calls;
        assert(gauge_coulomb_service_init(&gm) == 0 && request_calls == calls);
    }
    puts("PASS CAR/IRQ/wakeup/notifier/thread failures leave no running worker and allow clean retry");
}
static void probe(void)
{
    for (int fault = 0; fault < 11; fault++) {
        reset();
        struct mt6397_chip chip = {0};
        struct device parent = { .data = &chip };
        struct platform_device pdev = { .dev.parent = &parent };
        int expected = -EIO;
        if (fault == 0) { mapping_errno = -EPROBE_DEFER; expected = mapping_errno; }
        if (fault == 1) battery_psy_errno = expected;
        if (fault == 2) gauge_psy_errno = expected;
        if (fault == 3) group_errno = expected;
        if (fault == 4) netlink_errno = expected;
        if (fault == 5) hw_errno = expected;
        if (fault == 6) request_fail_at = 5;
        if (fault == 7) files_errno = expected;
        if (fault == 8) request_fail_at = 12;
        if (fault == 9) { wake_fail = 1; expected = -ENOMEM; }
        if (fault == 10) { thread_fail = 1; expected = -EAGAIN; }
        assert(mt6358_gauge_probe(&pdev) == expected);
        assert(worker_live == 0 && battery_worker_live == 0 && wake_live == 0 && notifier_live == 0);
        assert(requested_live == 0 && thermal_live == 0 && supply_live == 0 && gauge_groups == 0 && netlink_live == 0);
        assert(misc_calls == 0 && activation_calls == 0);
    }
    reset();
    struct mt6397_chip chip = {0};
    struct device parent = { .data = &chip };
    struct platform_device pdev = { .dev.parent = &parent };
    assert(mt6358_gauge_probe(&pdev) == 0);
    struct mtk_gauge *gauge = pdev.dev.data;
    assert(gauge->gm->is_probe_done && gauge->gm->cs.init);
    assert(requested_live == 12 && worker_live == 1 && battery_worker_live == 1);
    assert(wake_live == 1 && thermal_live == 1 && supply_live == 2 && gauge_groups == 1 && netlink_live == 1);
    assert(misc_calls == 1 && activation_calls == 0);
    puts("PASS actual init-to-probe caller propagation and cleanup before workers; full healthy N startup");
}
static void service_readiness(void)
{
    for (int fault = 0; fault < 4; fault++) {
#ifndef CONFIG_PM
        if (fault == 1) continue;
#endif
        reset();
        if (fault == 0 || fault == 3) battery_thread_errno = -EAGAIN;
        if (fault == 1 || fault == 3) battery_notifier_errno = -EACCES;
        if (fault == 2 || fault == 3) battery_sysfs_errno = -EIO;
        int expected = fault == 0 || fault == 3 ? -EAGAIN : fault == 1 ? -EACCES : -EIO;
        struct mt6397_chip chip = {0};
        struct device parent = { .data = &chip };
        struct platform_device pdev = { .dev.parent = &parent };
        assert(mt6358_gauge_probe(&pdev) == 0);
        struct mtk_battery *gm = ((struct mtk_gauge *)pdev.dev.data)->gm;
        assert(gm->cs.init && worker_live == 1 && gm->is_probe_done);
        assert(gm->kernel_service_error == expected);
        for (int retry = 0; retry < 2; retry++) {
            assert(battery_enable_kernel_algo(gm) == expected);
            assert(!gm->algo.active && activation_calls == 0 && !gm->algo_lock.held);
        }
        assert(requested_live == 12 && supply_live == 2 && wake_live == 1);
        assert(battery_worker_live == !(fault == 0 || fault == 3));
    }
    puts("PASS actual battery worker/PM/sysfs setup faults retain diagnostic N lifetimes and repeatedly refuse Y with first errno");
}
static void readiness(void)
{
    reset();
    struct mtk_battery gm;
    struct mtk_gauge gauge;
    struct platform_device pdev;
    fixture(&gm, &gauge, &pdev);
    gm.is_probe_done = true;
    gm.cs.init = true;
    gm.fg_cust_data.car_tune_value = 1000;
    gauge.hw_status.r_fg_value = 50;
    for (int i = 0; i < GAUGE_IRQ_MAX; i++)
        gauge.irq_requested[i] = true;
    gm.kernel_setup_error = -EIO;
    assert(battery_enable_kernel_algo(&gm) == -EUCLEAN);
    assert(!gm.algo.active && activation_calls == 0 && !gm.algo_lock.held);
    gm.kernel_setup_error = 0;
    gm.cs.init = false;
    assert(battery_enable_kernel_algo(&gm) == -EAGAIN);
    gm.cs.init = true;
    for (int i = 0; i < GAUGE_IRQ_MAX; i++) {
        gauge.irq_requested[i] = false;
        assert(battery_enable_kernel_algo(&gm) == -ENXIO);
        assert(!gm.algo.active && activation_calls == 0);
        gauge.irq_requested[i] = true;
    }
    gauge.hw_status.r_fg_value = 0;
    assert(battery_enable_kernel_algo(&gm) == -EINVAL);
    gauge.hw_status.r_fg_value = 50;
    assert(battery_enable_kernel_algo(&gm) == 0);
    assert(gm.algo.active && gm.init_flag == 1 && activation_calls == 1);
    gm.kernel_setup_error = -EIO;
    assert(battery_enable_kernel_algo(&gm) == 0 && activation_calls == 1);
    puts("PASS actual activation readiness: reboot latch, common service, every required IRQ, calibration and active idempotence");
}
int main(void)
{
    requests();
    daemon_cases();
    coulomb();
    probe();
    service_readiness();
    readiness();
    reset();
    return 0;
}
