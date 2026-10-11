static void invalid(struct mtk_gauge *gauge, enum gauge_irq irq, int expected)
{
    int calls = enable_calls + disable_calls;
    assert(validate_gauge_irq(gauge, irq) == expected);
    assert(enable_gauge_irq(gauge, irq) == expected);
    assert(disable_gauge_irq(gauge, irq) == expected);
    assert(enable_calls + disable_calls == calls);
}
int main(void)
{
    struct mtk_gauge gauge = {0};
    invalid(NULL, COULOMB_H_IRQ, -EINVAL);
    invalid(&gauge, (enum gauge_irq)-1, -EINVAL);
    invalid(&gauge, GAUGE_IRQ_MAX, -EINVAL);
    assert(lookup_calls == 0);
    const int numbers[] = {0, -ENXIO, -EPROBE_DEFER, -EIO};
    for (unsigned int i = 0; i < GAUGE_IRQ_MAX; i++) {
        for (unsigned int j = 0; j < sizeof(numbers) / sizeof(numbers[0]); j++) {
            gauge.irq_no[i] = numbers[j];
            invalid(&gauge, i, numbers[j] ? numbers[j] : -ENXIO);
        }
        gauge.irq_no[i] = 25;
        descriptor_present = false;
        invalid(&gauge, i, -ENODEV);
        descriptor_present = true;
        descriptor.depth = 2;
        assert(validate_gauge_irq(&gauge, i) == 0);
        assert(enable_gauge_irq(&gauge, i) == -EBUSY);
        assert(disable_gauge_irq(&gauge, i) == 0);
        assert(descriptor.depth == 2);
        descriptor.depth = 1;
        int enabled = enable_calls, disabled = disable_calls;
        assert(disable_gauge_irq(&gauge, i) == 0);
        assert(disable_calls == disabled);
        assert(enable_gauge_irq(&gauge, i) == 0);
        assert(enable_calls == enabled + 1 && descriptor.depth == 0);
        assert(enable_gauge_irq(&gauge, i) == 0);
        assert(enable_calls == enabled + 1);
        assert(disable_gauge_irq(&gauge, i) == 0);
        assert(disable_calls == disabled + 1 && descriptor.depth == 1);
        assert(disable_gauge_irq(&gauge, i) == 0);
        assert(disable_calls == disabled + 1);
    }
    puts("PASS actual IRQ guards: invalid enum, zero/deferred/failed mapping, missing descriptor, nesting and idempotent transitions");
    return 0;
}
