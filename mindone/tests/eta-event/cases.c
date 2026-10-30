// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(void)
{
    struct eta6965_device eta = {0}, other = {0};
    struct power_supply psy = {0};
    struct charger_device dev = { .data = &eta, .ops = &eta6965_chg_ops };
    struct mtk_charger info = { .chg1_dev = &dev };
    expected_psy = &psy;
    CHECK(eta6965_chg_ops.event == eta_op_event);
    for (int scenario = 0; scenario < 5; scenario++) {
        struct eta6965_platform binding = {true, &eta, &psy};
        eta6965_otg_binding = &binding;
        if (scenario == 1) eta6965_otg_binding = NULL;
        if (scenario == 2) binding.active = false;
        if (scenario == 3) binding.eta = &other;
        if (scenario == 4) binding.psy = NULL;
        for (int path = 0; path < 3; path++) {
            eta6965_binding_lock = (struct mutex){0};
            changed = enables = alg_stops = srcu_locks = srcu_unlocks = 0;
            int ret;
            if (path == 0) ret = eta_op_event(&dev, EVENT_RECHARGE, 0);
            else if (path == 1) ret = charger_dev_do_event(&dev, EVENT_RECHARGE, 0);
            else ret = enable_charging(&info, true);
            CHECK(ret == (scenario == 0 ? 0 : -ENODEV));
            CHECK(changed == (scenario == 0 ? 1u : 0u));
            CHECK(!eta6965_binding_lock.held);
            CHECK(eta6965_binding_lock.locks == 1 && eta6965_binding_lock.unlocks == 1);
            CHECK(srcu_locks == (path == 0 ? 0u : 1u) && srcu_unlocks == srcu_locks);
            CHECK(enables == (path == 2 ? 1u : 0u) && alg_stops == 0);
        }
    }
    changed = srcu_locks = 0;
    enable_result = -EIO;
    CHECK(enable_charging(&info, true) == -EIO);
    CHECK(changed == 0 && srcu_locks == 0);
    puts("PASS five binding states through callback, real class dispatch and basic enable");
    return 0;
}
