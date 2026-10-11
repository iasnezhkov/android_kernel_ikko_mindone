static void misc_reset(void)
{
    reset();
    memset(&power_supply_notifier, 0, sizeof(power_supply_notifier));
    misc_thread_errno = misc_thread_calls = misc_worker_live = misc_wakes = 0;
    misc_registered = early_activation = early_activation_ret = 0;
    misc_publications = 0;
    complete_during_acquire = NULL;
}

static void ready_inputs(struct mtk_battery *gm)
{
    gm->cs.init = true;
    gm->fg_cust_data.car_tune_value = 1000;
    gm->gauge->hw_status.r_fg_value = 50;
    for (int i = 0; i < GAUGE_IRQ_MAX; i++)
        gm->gauge->irq_requested[i] = true;
}

static void check_805_failure(struct mtk_battery *gm, int expected)
{
    int prior_threads = misc_thread_calls;
    int prior_requests = request_calls;
    for (int retry = 0; retry < 3; retry++) {
        assert(activate_805(gm) == expected);
        assert(!gm->algo.active && gm->init_flag == 0 && activation_calls == 0);
        assert(misc_publications == 0);
        assert(misc_thread_calls == prior_threads && request_calls == prior_requests);
    }
}

static void startup_errors(void)
{
    const int errors[] = {-ENOMEM, -EINTR, -EAGAIN};
    for (unsigned int i = 0; i < ARRAY_SIZE(errors); i++) {
        for (int older = 0; older < 2; older++) {
            misc_reset();
            struct mtk_battery gm;
            struct mtk_gauge gauge;
            struct platform_device pdev;
            fixture(&gm, &gauge, &pdev);
            ready_inputs(&gm);
            gm.kernel_service_error = older ? -EACCES : 0;
            misc_thread_errno = errors[i];
            mtk_power_misc_init(&gm);
            gm.is_probe_done = true;
            int expected = older ? -EACCES : errors[i];
            assert(gm.kernel_service_error == expected);
            assert(misc_thread_calls == 1 && misc_worker_live == 0 && misc_registered == 1);
            assert(power_supply_notifier.head == &gm.sdc.psy_nb);
            check_805_failure(&gm, expected);
            wake_up_power_misc(&gm.sdc);
            gm.cur_bat_temp = BATTERY_SHUTDOWN_TEMPERATURE;
            struct power_supply_desc descriptor = {.name = "battery"};
            struct power_supply supply = {.desc = &descriptor};
            assert(mtk_power_misc_psy_event(&gm.sdc.psy_nb, 0, &supply) == NOTIFY_DONE);
            assert(gm.sdc.timeout && gm.sdc.overheat && misc_wakes == 2 && misc_worker_live == 0);
        }
    }
    puts("PASS actual worker errors, first errno, callback/wakeup queue and repeated805 refusal");
}

static void duplicate_notifier(void)
{
    for (int booting = 0; booting < 2; booting++) {
        misc_reset();
        system_state = booting ? SYSTEM_BOOTING : SYSTEM_RUNNING;
        struct mtk_battery gm;
        struct mtk_gauge gauge;
        struct platform_device pdev;
        fixture(&gm, &gauge, &pdev);
        ready_inputs(&gm);
        assert(power_supply_reg_notifier(&gm.sdc.psy_nb) == 0);
        mtk_power_misc_init(&gm);
        gm.is_probe_done = true;
        assert(gm.kernel_service_error == -EEXIST && misc_registered == 1);
        assert(power_supply_notifier.head == &gm.sdc.psy_nb && gm.sdc.psy_nb.next == NULL);
        assert(power_supply_notifier.rwsem == 0 && misc_worker_live == 1);
        check_805_failure(&gm, -EEXIST);
    }
    puts("PASS exact ACK duplicate-notifier errno retains prior registration during boot/runtime");
}

static void combined_errors(void)
{
    misc_reset();
    struct mtk_battery gm;
    struct mtk_gauge gauge;
    struct platform_device pdev;
    fixture(&gm, &gauge, &pdev);
    ready_inputs(&gm);
    assert(power_supply_reg_notifier(&gm.sdc.psy_nb) == 0);
    misc_thread_errno = -EINTR;
    mtk_power_misc_init(&gm);
    gm.is_probe_done = true;
    assert(gm.kernel_service_error == -EINTR && misc_worker_live == 0 && misc_registered == 1);
    assert(power_supply_notifier.head == &gm.sdc.psy_nb);
    check_805_failure(&gm, -EINTR);
    puts("PASS combined thread/notifier failures retain first error and preexisting notifier");
}

static void healthy_and_disabled(void)
{
    for (int disabled = 0; disabled < 2; disabled++) {
        misc_reset();
        struct mtk_battery gm;
        struct mtk_gauge gauge;
        struct platform_device pdev;
        fixture(&gm, &gauge, &pdev);
        ready_inputs(&gm);
        gm.disableGM30 = disabled;
        mtk_power_misc_init(&gm);
        gm.is_probe_done = true;
        assert(gm.kernel_service_error == 0 && misc_registered == 1);
        assert(misc_thread_calls == !disabled && misc_worker_live == !disabled);
        if (disabled)
            check_805_failure(&gm, -EAGAIN);
        else {
            assert(activate_805(&gm) == 0);
            assert(gm.algo.active && gm.init_flag == 1 && activation_calls == 1);
        }
    }
    puts("PASS successful setup and intentionally disabled worker contract");
}

static void diagnostic_boot(void)
{
    for (int optin = 0; optin < 2; optin++) {
        misc_reset();
        struct mt6397_chip chip = {0};
        struct device parent = {.data = &chip};
        struct platform_device pdev = {.dev.parent = &parent};
        misc_thread_errno = -ENOMEM;
        kernel_algo = optin;
        assert(mt6358_gauge_probe(&pdev) == 0);
        struct mtk_battery *gm = ((struct mtk_gauge *)pdev.dev.data)->gm;
        assert(gm->is_probe_done && gm->kernel_service_error == -ENOMEM && !gm->algo.active);
        assert(worker_live == 1 && battery_worker_live == 1 && misc_worker_live == 0);
        assert(supply_live == 2 && wake_live == 1 && requested_live == 12 && netlink_live == 1);
        check_805_failure(gm, -ENOMEM);
    }
    puts("PASS actual probe retains live diagnostic N allocations after required monitor failure");
}

static void early_setup_window(void)
{
    misc_reset();
    struct mtk_battery gm;
    struct mtk_gauge gauge;
    struct platform_device pdev;
    fixture(&gm, &gauge, &pdev);
    ready_inputs(&gm);
    assert(!gm.is_probe_done);
#ifdef EXPECT_EARLY_BARRIER
    check_805_failure(&gm, -EAGAIN);
#else
    assert(activate_805(&gm) == 0 && gm.algo.active);
    misc_thread_errno = -ENOMEM;
    mtk_power_misc_init(&gm);
    assert(gm.kernel_service_error == -ENOMEM && gm.algo.active);
    assert(activate_805(&gm) == 0);
#endif
    puts("PASS extracted805 incomplete-setup window control");
}

static void completion_interleaving(void)
{
#ifdef EXPECT_EARLY_BARRIER
    misc_reset();
    struct mtk_battery gm;
    struct mtk_gauge gauge;
    struct platform_device pdev;
    fixture(&gm, &gauge, &pdev);
    ready_inputs(&gm);
    complete_during_acquire = &gm;
    check_805_failure(&gm, -EINTR);
    assert(gm.is_probe_done && gm.kernel_service_error == -EINTR);
#endif
    puts("PASS completion acquire precedes service/error reads under controlled interleaving");
}

int main(void)
{
    startup_errors();
    duplicate_notifier();
    combined_errors();
    healthy_and_disabled();
    diagnostic_boot();
    early_setup_window();
    completion_interleaving();
    misc_reset();
    return 0;
}
