static void reset(void)
{
    memset(registers, 0, sizeof(registers));
    memset(irq_enabled, 0, sizeof(irq_enabled));
    operations = failure_at = enables = disables = enable_errno = disable_errno = 0;
    cell_get_errno = cell_read_errno = cell_write_errno = cell_reads = cell_writes = cell_puts = 0;
    cell_short_read = cell_short_write = ambiguous_write = false;
    operation_errno = -EIO;
    rtc = 0xd5;
    registers[RG_FGADC_CAR_CON0] = 100;
}
static void handover(struct mtk_gauge *gauge)
{
    const int errors[] = {-EIO, -ETIMEDOUT, -EACCES};
    for (unsigned int e = 0; e < sizeof(errors) / sizeof(errors[0]); e++) {
        reset();
        failure_at = 1;
        operation_errno = errors[e];
        assert(fgauge_set_info(gauge, GAUGE_PROP_IS_NVRAM_FAIL_MODE, 1) == errors[e]);
        assert(fgauge_set_info(gauge, GAUGE_PROP_CON0_SOC, 6100) == 0);
        reset();
        failure_at = 1;
        operation_errno = errors[e];
        assert(fgauge_set_info(gauge, GAUGE_PROP_CON0_SOC, 6100) == errors[e]);
    }
    reset();
    registers[RG_SYSTEM_INFO_CON0] = 0xa55a;
    unsigned int prior = registers[RG_SYSTEM_INFO_CON0];
    assert(fgauge_set_info(gauge, GAUGE_PROP_IS_NVRAM_FAIL_MODE, 1) == 0);
    assert(registers[RG_SYSTEM_INFO_CON0] == (prior | 0x10));
    assert(fgauge_set_info(gauge, GAUGE_PROP_CON0_SOC, 6100) == 0);
    int value = -1;
    assert(fgauge_get_info(gauge, GAUGE_PROP_CON0_SOC, &value) == 0 && value == 61);
    assert(fgauge_get_info(gauge, GAUGE_PROP_IS_NVRAM_FAIL_MODE, &value) == 0 && value == 1);
    for (int fault = 0; fault < 5; fault++) {
        reset();
        int expected = -EIO;
        if (fault == 0) cell_get_errno = expected;
        if (fault == 1) cell_read_errno = expected;
        if (fault == 2) { cell_short_read = true; expected = -ENODATA; }
        if (fault == 3) cell_write_errno = expected;
        if (fault == 4) cell_short_write = true;
        assert(rtc_ui_soc_set(gauge, NULL, 63) == expected);
        assert(rtc == 0xd5);
        assert(cell_writes == (fault >= 3));
    }
    reset();
    assert(rtc_ui_soc_set(gauge, NULL, -1) == -ERANGE);
    assert(rtc_ui_soc_set(gauge, NULL, 101) == -ERANGE);
    assert(cell_reads == 0 && cell_writes == 0);
    assert(rtc_ui_soc_set(gauge, NULL, 63) == 0 && rtc == 0xbf);
    assert(rtc_ui_soc_get(gauge, NULL, &value) == 0 && value == 63);
    rtc = 0;
    assert(rtc_ui_soc_set(gauge, NULL, 63) == 0 && rtc == 63);
    puts("PASS exact PMIC handover errno/bit preservation and RTC get/read/write/short-result/range/valid-bit contracts");
}
static void thresholds(struct mtk_gauge *gauge)
{
    int (*setters[])(struct mtk_gauge *, struct mtk_gauge_sysfs_field_info *, int) = {
        coulomb_interrupt_ht_set, coulomb_interrupt_lt_set, vbat_lt_set, en_l_vbat_set
    };
    const int values[] = {200, 200, 34000, 1};
    const enum gauge_irq irqs[] = {COULOMB_H_IRQ, COULOMB_L_IRQ, VBAT_L_IRQ, VBAT_L_IRQ};
    for (unsigned int i = 0; i < sizeof(setters) / sizeof(setters[0]); i++) {
        reset();
        gauge->vbat_l_en = false;
        assert(setters[i](gauge, NULL, values[i]) == 0);
        int total = operations;
        assert(total > 0);
        for (int fault = 1; fault <= total; fault++) {
            for (int ambiguous = 0; ambiguous < 2; ambiguous++) {
                reset();
                gauge->vbat_l_en = false;
                failure_at = fault;
                ambiguous_write = ambiguous;
                assert(setters[i](gauge, NULL, values[i]) == -EIO);
                assert(!irq_enabled[irqs[i]]);
                if (i == 3)
                    assert(!gauge->vbat_l_en);
            }
        }
        if (i != 2) {
            reset();
            gauge->vbat_l_en = false;
            enable_errno = -EBUSY;
            assert(setters[i](gauge, NULL, values[i]) == -EBUSY);
            assert(!irq_enabled[irqs[i]]);
        }
    }
    reset();
    disable_errno = -ENXIO;
    assert(coulomb_interrupt_ht_set(gauge, NULL, 0) == -ENXIO);
    assert(coulomb_interrupt_lt_set(gauge, NULL, 0) == -ENXIO);
    assert(en_l_vbat_set(gauge, NULL, 1) == -ENXIO && operations == 0);
    gauge->hw_status.r_fg_value = 0;
    assert(coulomb_interrupt_ht_set(gauge, NULL, 200) == -EINVAL);
    gauge->hw_status.r_fg_value = 50;
    gauge->gm->fg_cust_data.car_tune_value = 0;
    assert(coulomb_interrupt_lt_set(gauge, NULL, 200) == -EINVAL);
    gauge->gm->fg_cust_data.car_tune_value = 1000;
    reset();
    gauge->vbat_h_en = true;
    gauge->vbat_l_en = true;
    assert(en_l_vbat_set(gauge, NULL, 0) == 0 && !gauge->vbat_l_en);
    assert((registers[RG_AUXADC_LBAT2_5] & (1 << AUXADC_LBAT2_EN_MIN_SHIFT)) != 0);
    gauge->vbat_h_en = false;
    assert(en_l_vbat_set(gauge, NULL, 0) == 0);
    assert((registers[RG_AUXADC_LBAT2_5] & (1 << AUXADC_LBAT2_EN_MIN_SHIFT)) == 0);
    puts("PASS all native threshold fault positions, ambiguous failed writes, disabled IRQ on failure and successful shared VBAT enable semantics");
}
static void plugout(void)
{
    reset();
    struct mtk_gauge gauge = { 0 };
    enable_errno = -EBUSY;
    assert(bat_plugout_en_set(&gauge, NULL, 2) == -EBUSY);
    assert(!irq_enabled[BAT_PLUGOUT_IRQ]);
    enable_errno = 0;
    assert(bat_plugout_en_set(&gauge, NULL, 2) == 0);
    assert(irq_enabled[BAT_PLUGOUT_IRQ]);
    disable_errno = -ENODEV;
    assert(bat_plugout_en_set(&gauge, NULL, 0) == -ENODEV);
    assert(irq_enabled[BAT_PLUGOUT_IRQ]);
    disable_errno = 0;
    assert(bat_plugout_en_set(&gauge, NULL, 0) == 0);
    assert(!irq_enabled[BAT_PLUGOUT_IRQ]);
    puts("PASS bat plugout wrapper propagates checked IRQ enable/disable errors");
}
int main(void)
{
    plugout();
    struct mtk_battery gm = { .fg_cust_data.car_tune_value = 1000 };
    struct platform_device pdev = {0};
    struct mtk_gauge gauge = { .gm = &gm, .pdev = &pdev,
        .hw_status = { .r_fg_value = 50, .vbat2_det_counter = 3, .vbat2_det_time = 2 } };
    handover(&gauge);
    thresholds(&gauge);
    return 0;
}
