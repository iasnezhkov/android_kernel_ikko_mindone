// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static int recording_provider(struct power_supply *psy, enum power_supply_property property,
                              const union power_supply_propval *value) {
    assert(calls < (int)ARRAY_SIZE(call_property));
    call_property[calls] = property;
    call_value[calls] = value->intval;
    call_owner[calls] = psy == &master ? 1 : 2;
    calls++;
#if CHECKED_REQUESTS
    if (provider_paused) {
        assert(charger_cooler_lock.held && pthread_equal(charger_cooler_lock.owner, pthread_self()));
        assert(!pthread_mutex_lock(&sequence_lock));
        if (calls == 1) {
            provider_entered = true;
            assert(!pthread_cond_broadcast(&sequence_changed));
            while (!provider_release) sequence_wait();
        }
        assert(!pthread_mutex_unlock(&sequence_lock));
    }
#endif
    if (calls == failure_call) return failure_errno;
    return psy_charger_set_property(psy, property, value);
}
static const struct power_supply_desc desc = { .set_property = recording_provider };
static const struct charger_cooling_platform_data pdata = {
    .state_to_charger_limit = cooling_state_to_charger_limit_v1,
};
static void reset(void) {
#if CHECKED_REQUESTS
    assert(!charger_cooler_lock.held);
    provider_paused = provider_entered = provider_release = waiter_blocked = waiter_done = false;
#endif
    memset(&info, 0, sizeof(info));
    memset(&master, 0, sizeof(master));
    memset(&slave, 0, sizeof(slave));
    memset(&unrelated, 0, sizeof(unrelated));
    memset(&cooler, 0, sizeof(cooler));
    memset(&primary_device, 0, sizeof(primary_device));
    memset(&secondary_device, 0, sizeof(secondary_device));
    primary_device.ops = secondary_device.ops = &charger_ops;
    memset(&charger_cl_data, 0, sizeof(charger_cl_data));
    charger_cl_data.cur_current = -1;
    calls = failure_call = failure_errno = 0;
    master = (struct power_supply){ .use_cnt = 1, .desc = &desc, .private = &info };
    slave = master;
    unrelated = master;
    info.psy1 = &master;
    info.psy2 = &slave;
    info.chg1_dev = &primary_device;
    info.chg2_dev = &secondary_device;
    info.enable_hv_charging = true;
    for (int i = 0; i < CHGS_SETTING_MAX; i++) {
        info.chg_data[i].thermal_charging_current_limit = -1;
        info.chg_data[i].thermal_input_current_limit = -1;
    }
    cooler.dev = &device;
    cooler.max_state = CHARGER_STATE_NUM - 1;
    cooler.chg_psy = &master;
    cooler.s_chg_psy = &slave;
    cooler.type = DUAL_CHARGER;
    cooler.throttle = charger_throttle;
    cooler.pdata = &pdata;
    thermal.devdata = &cooler;
}
static void registered_contract(void) {
    reset();
    enum power_supply_property properties[] = {
        POWER_SUPPLY_PROP_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
        POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
    };
    assert(properties[0] == 8 && properties[1] == 30 && properties[2] == 38);
    for (size_t i = 0; i < ARRAY_SIZE(properties); i++) {
        bool present = false;
        for (size_t j = 0; j < ARRAY_SIZE(charger_psy_properties); j++)
            present |= properties[i] == charger_psy_properties[j];
        assert(present && psy_charger_property_is_writeable(&master, properties[i]) == 1);
    }
    assert(!psy_charger_property_is_writeable(&master, POWER_SUPPLY_PROP_CAPACITY));
    puts("PASS registered three writable properties and signed numeric IDs");
}
static void healthy_requests(void) {
    for (unsigned long state = 0; state < CHARGER_STATE_NUM; state++) {
        reset();
        cooler.target_state = state;
        assert(!cooling_state_to_charger_limit_v1(&cooler));
        assert(calls == 4 && info.wakes == 4);
        assert(info.chg_data[CHG1_SETTING].thermal_charging_current_limit ==
               master_charger_state_to_current_limit[state]);
        assert(info.chg_data[CHG2_SETTING].thermal_charging_current_limit ==
               slave_charger_state_to_current_limit[state]);
        assert(call_property[0] == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX);
        assert(call_property[1] == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
        assert(call_property[2] == POWER_SUPPLY_PROP_VOLTAGE_MAX);
        assert(call_property[3] == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX);
        assert(call_owner[0] == 1 && call_owner[1] == 1 && call_owner[2] == 1 && call_owner[3] == 2);
        assert(call_value[1] == (state == 8 ? 0 : -1));
        assert(call_value[2] == (state == 8 ? 0 : 1));
        assert(info.enable_hv_charging == (state != 8));
        assert(master.changed == 1 && slave.changed == 1);
        assert(wake_cc[0] == master_charger_state_to_current_limit[state]);
        assert(wake_iin[0] == -1 && wake_hv[0] == 1 && wake_slave[0] == -1);
        assert(wake_iin[1] == (state == 8 ? 0 : -1) && wake_hv[1] == 1 && wake_slave[1] == -1);
        assert(wake_hv[2] == (state == 8 ? 0 : 1) && wake_slave[2] == -1);
        assert(wake_slave[3] == slave_charger_state_to_current_limit[state]);
    }
    reset();
    cooler.type = SINGLE_CHARGER;
    cooler.target_state = 5;
    assert(!cooling_state_to_charger_limit_v1(&cooler) && calls == 3 && !slave.changed);
    puts("PASS all nine unchanged request tuples, dual/single, asynchronous acceptance");
}
static void first_error_partial_state(void) {
    const int errors[] = { -ENODEV, -EIO, -EAGAIN, -EOPNOTSUPP };
    for (int step = 1; step <= 4; step++) {
        reset();
        cooler.target_state = 8;
        failure_call = step;
        failure_errno = errors[step - 1];
        assert(cooling_state_to_charger_limit_v1(&cooler) == failure_errno);
        assert(calls == step && info.wakes == step - 1);
        assert(info.chg_data[0].thermal_charging_current_limit == (step > 1 ? 0 : -1));
        assert(info.chg_data[0].thermal_input_current_limit == (step > 2 ? 0 : -1));
        assert(info.enable_hv_charging == (step <= 3));
        assert(!slave.changed && master.changed == (step == 4));
    }
    puts("PASS callback first errno and observed partial request fields at every step");
}
static void reproduce_hidden_error_retry(void) {
    for (int step = 1; step <= 4; step++) {
        reset();
        failure_call = step;
        failure_errno = -EIO;
        unsigned long reported = 99;
#if CHECKED_REQUESTS
        assert(charger_cooling_set_cur_state(&thermal, 8) == -EIO);
        assert(!charger_cooler_lock.held);
        assert(!charger_cooling_get_cur_state(&thermal, &reported) && reported == 0);
        assert(cooler.target_state == 0 && cooler.request_dirty && charger_cl_data.request_dirty);
        assert(charger_cl_data.cur_state == 0 && charger_cl_data.cur_current == -1);
        char buffer[32] = {0};
        assert(charger_curr_show(NULL, NULL, buffer) == -ENODATA && !buffer[0]);
#else
        assert(!charger_cooling_set_cur_state(&thermal, 8));
        assert(!charger_cooling_get_cur_state(&thermal, &reported) && reported == 8);
        assert(charger_cl_data.cur_state == 8 && charger_cl_data.cur_current == 0);
#endif
        int before = calls;
        failure_call = 0;
#if CHECKED_REQUESTS
        assert(!charger_cooling_set_cur_state(&thermal, 8) && calls == before + 4);
        assert(!cooler.request_dirty && !charger_cl_data.request_dirty);
        assert(!charger_cooling_get_cur_state(&thermal, &reported) && reported == 8);
        assert(charger_curr_show(NULL, NULL, buffer) == 2 && !strcmp(buffer, "0\n"));
        assert(!charger_cooling_set_cur_state(&thermal, 8) && calls == before + 4);
        reset();
        failure_call = step;
        failure_errno = -EIO;
        assert(charger_cooling_set_cur_state(&thermal, 8) == -EIO);
        before = calls;
        failure_call = 0;
        assert(!charger_cooling_set_cur_state(&thermal, 0) && calls == before + 4);
        assert(info.chg_data[0].thermal_charging_current_limit == -1);
        assert(info.chg_data[0].thermal_input_current_limit == -1 && info.enable_hv_charging);
        assert(!cooler.request_dirty && !charger_cl_data.request_dirty);
#else
        assert(!charger_cooling_set_cur_state(&thermal, 8) && calls == before);
#endif
    }
    reset();
    assert(charger_cooling_set_cur_state(&thermal, 9) == -EINVAL && !calls);
    cooler.throttle = NULL;
    assert(charger_cooling_set_cur_state(&thermal, 1) == -EINVAL && !calls);
#if CHECKED_REQUESTS
    puts("PASS checked first errno, coherent accepted request, failed-state and old-state retry at all four failure steps");
#else
    puts("REPRO callback failure is hidden, state published and identical retry suppressed");
#endif
}
static void availability(void) {
    reset();
    union power_supply_propval value = { .intval = 123 };
    master.use_cnt = 0;
    assert(power_supply_set_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODEV);
    assert(!calls && !info.wakes);
    master.use_cnt = 1;
    const struct power_supply_desc absent = {0};
    master.desc = &absent;
    assert(power_supply_set_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODEV);
    master.desc = &desc;
    master.private = NULL;
    assert(psy_charger_set_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -EINVAL);
    reset();
#if CHECKED_REQUESTS
    assert(psy_charger_set_property(&unrelated, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODEV);
#else
    assert(!psy_charger_set_property(&unrelated, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value));
#endif
    assert(!info.wakes && info.chg_data[0].thermal_input_current_limit == -1);
    assert(psy_charger_set_property(&master, POWER_SUPPLY_PROP_CAPACITY, &value) == -EINVAL);
    cooler.chg_psy = NULL;
#if CHECKED_REQUESTS
    assert(cooling_state_to_charger_limit_v1(&cooler) == -ENODEV && !calls);
    cooler.chg_psy = (void *)(intptr_t)-EPROBE_DEFER;
    assert(cooling_state_to_charger_limit_v1(&cooler) == -EPROBE_DEFER && !calls);
#else
    assert(cooling_state_to_charger_limit_v1(&cooler) == -1 && !calls);
#endif
    cooler.chg_psy = &master;
    cooler.s_chg_psy = NULL;
    cooler.target_state = 8;
#if CHECKED_REQUESTS
    assert(cooling_state_to_charger_limit_v1(&cooler) == -ENODEV && !calls);
    cooler.s_chg_psy = (void *)(intptr_t)-EAGAIN;
    assert(cooling_state_to_charger_limit_v1(&cooler) == -EAGAIN && !calls);
    puts("PASS checked unavailable master/slave before writes and unmapped-provider errno");
#else
    assert(!cooling_state_to_charger_limit_v1(&cooler) && calls == 3);
    puts("PASS ACK dead-provider guards; REPRO unmapped provider and absent slave false success");
#endif
}
static void readback(void) {
    reset();
    union power_supply_propval value = { .intval = 0x12345 };
    primary_device.cc = 960000;
    primary_device.iin = 500000;
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value));
    assert(value.intval == 960000 && info.chg_data[0].thermal_charging_current_limit == -1);
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value));
    assert(value.intval == 500000);
    primary_device.cc_errno = -EIO;
    primary_device.iin_errno = -ENODEV;
    assert(get_charger_charging_current(&info, &primary_device) == -EIO);
    assert(get_charger_input_current(&info, &primary_device) == -ENODEV);
#if CHECKED_REQUESTS
    value.intval = 0x12345;
    assert(psy_charger_get_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value) == -EIO);
    assert(value.intval == 0x12345);
    assert(psy_charger_get_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODEV);
    assert(value.intval == 0x12345);
#else
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value));
    assert(value.intval == -EIO);
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value));
    assert(value.intval == -ENODEV);
#endif
    info.chg1_dev = NULL;
    assert(psy_charger_get_property(&master, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODATA);
    value.intval = 0x12345;
#if CHECKED_REQUESTS
    assert(psy_charger_get_property(&unrelated, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value) == -ENODEV);
#else
    assert(!psy_charger_get_property(&unrelated, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &value));
#endif
    assert(value.intval == 0x12345);
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_VOLTAGE_MAX, &value));
    assert(value.intval == 1);
    info.chg1_dev = &primary_device;
    primary_device.ops = NULL;
    u32 output = 12345;
    assert(charger_dev_get_charging_current(NULL, &output) == -EOPNOTSUPP && output == 12345);
    assert(charger_dev_get_charging_current(&primary_device, &output) == -EOPNOTSUPP && output == 12345);
    assert(charger_dev_get_input_current(&primary_device, &output) == -EOPNOTSUPP && output == 12345);
    const struct charger_ops unavailable = {0};
    primary_device.ops = &unavailable;
    assert(charger_dev_get_charging_current(&primary_device, &output) == -EOPNOTSUPP && output == 12345);
    assert(charger_dev_get_input_current(&primary_device, &output) == -EOPNOTSUPP && output == 12345);
    value.intval = 0x12345;
#if CHECKED_REQUESTS
    assert(psy_charger_get_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value) == -EOPNOTSUPP);
    assert(value.intval == 0x12345);
#else
    assert(!psy_charger_get_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value));
    assert(value.intval == -EOPNOTSUPP);
#endif
#if CHECKED_REQUESTS
    puts("PASS checked readback first errno without output publication and healthy programmed-current scale");
#else
    puts("PASS programmed-current readback distinction; REPRO I/O errno hidden as successful negative payload");
#endif
}
static void competing_writer(void) {
    reset();
    assert(!charger_cooling_set_cur_state(&thermal, 5) && calls == 4);
    assert(info.chg_data[0].thermal_charging_current_limit == 1000000);
    union power_supply_propval value = { .intval = 2000000 };
    assert(!power_supply_set_property(&master, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &value));
    assert(info.chg_data[0].thermal_charging_current_limit == 2000000 && calls == 5);
    assert(!charger_cooling_set_cur_state(&thermal, 5) && calls == 5);
    unsigned long state = 99;
    assert(!charger_cooling_get_cur_state(&thermal, &state) && state == 5);
    assert(charger_cl_data.cur_current == 1000000);
    puts("LIMIT external property writer replaces request while cooler caches prior accepted state; no ownership arbitration claimed");
}
#if CHECKED_REQUESTS
struct sequence_result {
    int kind, rc;
    unsigned long state;
    char diagnostic[32];
};
static void *first_request(void *unused) {
    (void)unused;
    assert(charger_cooling_set_cur_state(&thermal, 8) == -EIO);
    return NULL;
}
static void *waiting_operation(void *arg) {
    struct sequence_result *result = arg;
    observe_lock = true;
    if (result->kind == 0)
        result->rc = charger_cooling_get_cur_state(&thermal, &result->state);
    else if (result->kind == 1)
        result->rc = charger_curr_show(NULL, NULL, result->diagnostic);
    else
        result->rc = charger_cooling_set_cur_state(&thermal, 5);
    assert(!pthread_mutex_lock(&sequence_lock));
    waiter_done = true;
    assert(!pthread_cond_broadcast(&sequence_changed));
    assert(!pthread_mutex_unlock(&sequence_lock));
    return NULL;
}
static void serialized_requests(void) {
    for (int kind = 0; kind < 3; kind++) {
        reset();
        failure_call = 1;
        failure_errno = -EIO;
        provider_paused = true;
        struct sequence_result result = { .kind = kind, .rc = 99, .state = 99 };
        pthread_t first, waiting;
        assert(!pthread_create(&first, NULL, first_request, NULL));
        assert(!pthread_mutex_lock(&sequence_lock));
        while (!provider_entered) sequence_wait();
        assert(!pthread_mutex_unlock(&sequence_lock));
        assert(!pthread_create(&waiting, NULL, waiting_operation, &result));
        assert(!pthread_mutex_lock(&sequence_lock));
        while (!waiter_blocked && !waiter_done) sequence_wait();
        assert(waiter_blocked && !waiter_done);
        provider_release = true;
        assert(!pthread_cond_broadcast(&sequence_changed));
        assert(!pthread_mutex_unlock(&sequence_lock));
        assert(!pthread_join(first, NULL) && !pthread_join(waiting, NULL));
        assert(!charger_cooler_lock.held);
        if (kind == 0)
            assert(!result.rc && result.state == 0 && cooler.request_dirty && calls == 1);
        else if (kind == 1)
            assert(result.rc == -ENODATA && !result.diagnostic[0] && cooler.request_dirty && calls == 1);
        else {
            assert(!result.rc && cooler.target_state == 5 && !cooler.request_dirty && calls == 5);
            assert(info.chg_data[0].thermal_charging_current_limit == 1000000);
            assert(info.chg_data[0].thermal_input_current_limit == -1 && info.enable_hv_charging);
            assert(info.chg_data[1].thermal_charging_current_limit == 1000000);
        }
    }
    puts("PASS deterministic blocked state/diagnostic readers and serialized latest setter after failed request");
}
#endif
int main(void) {
    registered_contract();
    healthy_requests();
    first_error_partial_state();
    reproduce_hidden_error_retry();
    availability();
    readback();
    competing_writer();
#if CHECKED_REQUESTS
    serialized_requests();
#endif
    return 0;
}
