#include "stubs.h"
#include "eta_source.h"

struct charger_data {
    int input_current_limit_by_aicl;
    int input_current_limit;
};
struct mtk_charger {
    bool disable_aicl, enable_dynamic_mivr, charger_unlimited, fast;
    struct { int max_dmivr_charger_current; } data;
    struct charger_device *chg1_dev;
};
static unsigned int aicl_calls;
static int last_aicl_return;
static bool support_fast_charging(struct mtk_charger *info) { return info->fast; }
static int charger_dev_run_aicl(struct charger_device *dev, u32 *output)
{
    aicl_calls++;
    return last_aicl_return = eta_op_run_aicl(dev, output);
}
#include "basic_slice.h"

static struct i2c_client client;
static struct device physical;
static struct eta6965_device eta;
static struct charger_device charger;
static unsigned int tests, baseline_false_success;

static void prepare(u8 code)
{
    memset(&client, 0, sizeof(client));
    active_client = &client;
    client.dpm_code = client.pg_lost_code = -1;
    client.regs[0] = 0xe0 | code;
    client.regs[8] = 4;
    eta.client = &client;
    eta.dev = &physical;
    physical.client = &client;
    client.data = &eta;
    eta.suspended = false;
    charger.data = &eta;
    sleep_hook = NULL;
    unlock_hook = NULL;
    aicl_calls = 0;
    last_aicl_return = 0;
}

static void healthy(void)
{
    for (u8 code = 0; code <= 31; code++) {
        for (int dpm = -1; dpm <= code; dpm++) {
            prepare(code);
            client.dpm_code = dpm;
            u32 output = 0xaabbccdd;
            assert(eta_op_run_aicl(&charger, &output) == 0);
            u8 good = dpm < 0 ? code : dpm > 0 ? dpm - 1 : 0;
            assert(output == 100000u + good * 100000u);
            assert(client.regs[0] == (0xe0 | good));
            assert(client.max_lock_depth == 1 && client.lock_depth == 0);
            assert(client.sleeps <= 1600);
            tests++;
        }
        for (int lost = 1; lost <= code; lost++) {
            prepare(code);
            client.pg_lost_code = lost;
            u32 output = 0;
            assert(eta_op_run_aicl(&charger, &output) == 0);
            assert(output == 100000u + (lost - 1) * 100000u);
            tests++;
        }
    }
    prepare(31);
    u32 output = 0;
    assert(eta_op_run_aicl(&charger, &output) == 0);
    assert(client.sleeps == 1600 && client.sleep_count == 32);
    printf("TRACE ");
    for (unsigned int i = 0; i < client.event_count; i++)
        printf("%c%02x:%02x ", client.events[i].write ? 'W' : 'R', client.events[i].reg, client.events[i].value);
    puts("");
    puts("healthy-probe-stop-register-order: PASS");
}

static void every_access_error(bool baseline)
{
    prepare(4);
    u32 output = 0;
    assert(eta_op_run_aicl(&charger, &output) == 0);
    struct bus_event trace[512];
    unsigned int total = client.event_count;
    memcpy(trace, client.events, total * sizeof(trace[0]));
    for (unsigned int at = 1; at <= total; at++) {
        prepare(4);
        client.error_at[0] = at;
        client.error_value[0] = trace[at - 1].write ? -EREMOTEIO : -EIO;
        output = 0xaabbccdd;
        int ret = eta_op_run_aicl(&charger, &output);
        if (baseline && at > 3) {
            assert(ret == 0 && output != 0xaabbccdd);
            baseline_false_success++;
        } else {
            assert(ret == client.error_value[0] && output == 0xaabbccdd);
        }
        assert(client.event_count <= total + 3);
        assert(client.lock_depth == 0 && client.max_lock_depth == 1);
        tests++;
    }
    puts("all-actual-access-errno-output-contract: PASS");
}

static unsigned int final_write(void)
{
    prepare(3);
    client.dpm_code = 3;
    u32 output = 0;
    assert(eta_op_run_aicl(&charger, &output) == 0);
    assert(output == 300000);
    unsigned int at = client.event_count;
    while (at && !client.events[at - 1].write)
        at--;
    assert(at);
    return at;
}

static void cleanup_failures(bool baseline)
{
    const int provider_errno[] = {-ENXIO, -ETIMEDOUT, -ENOMEM};
    for (unsigned int i = 0; i < ARRAY_SIZE(provider_errno); i++) {
        prepare(3);
        client.error_at[0] = 6;
        client.error_value[0] = provider_errno[i];
        u32 output = 0xaabbccdd;
        int ret = eta_op_run_aicl(&charger, &output);
        assert(baseline ? ret == 0 && output == 100000 : ret == provider_errno[i] && output == 0xaabbccdd);
        assert((client.regs[0] & 31) == 0);
        tests++;
    }
    unsigned int write_at = final_write();
    prepare(3);
    client.dpm_code = 3;
    client.error_at[0] = write_at;
    client.error_value[0] = -EREMOTEIO;
    u32 output = 0xaabbccdd;
    int ret = eta_op_run_aicl(&charger, &output);
    assert((client.regs[0] & 31) == 3);
    assert(baseline ? ret == 0 && output == 300000 : ret == -EREMOTEIO && output == 0xaabbccdd);
    tests++;
    prepare(3);
    client.dpm_code = 3;
    client.error_at[0] = write_at;
    client.error_value[0] = -EREMOTEIO;
    client.apply_error_at = write_at;
    output = 0xaabbccdd;
    ret = eta_op_run_aicl(&charger, &output);
    assert((client.regs[0] & 31) == 2);
    assert(baseline ? ret == 0 && output == 300000 : ret == -EREMOTEIO && output == 0xaabbccdd);
    tests++;
    prepare(3);
    client.dpm_code = 3;
    client.skip_write_at = write_at;
    output = 0xaabbccdd;
    ret = eta_op_run_aicl(&charger, &output);
    assert((client.regs[0] & 31) == 3);
    assert(baseline ? ret == 0 && output == 300000 : ret == -EIO && output == 0xaabbccdd);
    tests++;
    if (!baseline) {
        prepare(3);
        client.dpm_code = 3;
        client.error_at[0] = write_at + 1;
        client.error_value[0] = -ENODATA;
        output = 0xaabbccdd;
        assert(eta_op_run_aicl(&charger, &output) == -ENODATA && output == 0xaabbccdd);
        assert((client.regs[0] & 31) == 2);
        tests++;
    }
    prepare(3);
    client.error_at[0] = 6;
    client.error_value[0] = -EIO;
    output = 0xaabbccdd;
    ret = eta_op_run_aicl(&charger, &output);
    assert(baseline ? ret == 0 && output == 100000 : ret == -EIO && output == 0xaabbccdd);
    assert((client.regs[0] & 31) == 0);
    tests++;
    prepare(3);
    client.error_at[0] = 6;
    client.error_value[0] = -EIO;
    client.error_at[1] = 8;
    client.error_value[1] = -EREMOTEIO;
    output = 0xaabbccdd;
    ret = eta_op_run_aicl(&charger, &output);
    assert(baseline ? ret == 0 && output == 100000 : ret == -EIO && output == 0xaabbccdd);
    assert((client.regs[0] & 31) == 1);
    tests++;
    prepare(3);
    client.error_at[0] = 3;
    client.error_value[0] = -EREMOTEIO;
    client.apply_error_at = 3;
    output = 0xaabbccdd;
    assert(eta_op_run_aicl(&charger, &output) == -EREMOTEIO && output == 0xaabbccdd);
    assert((client.regs[0] & 31) == 0);
    tests++;
    puts("cleanup-errno-priority-unknown-hardware: PASS");
}

static void suspend_on_first_sleep(unsigned int count)
{
    if (count == 1)
        assert(eta6965_suspend(&physical) == 0);
}

static void pm_and_retry(bool baseline)
{
    prepare(3);
    sleep_hook = suspend_on_first_sleep;
    u32 output = 0xaabbccdd;
    int ret = eta_op_run_aicl(&charger, &output);
    assert(eta.suspended && client.event_count == 3);
    assert(baseline ? ret == 0 && output == 100000 : ret == -ENODATA && output == 0xaabbccdd);
    assert(eta6965_resume(&physical) == 0);
    sleep_hook = NULL;
    assert(eta_op_set_iinlim(&charger, 500000) == 0);
    assert(eta_op_run_aicl(&charger, &output) == 0 && output == 500000);
    tests += 2;
    puts("actual-pm-callback-interleaving-recovery: PASS");
}

static void basic_retry(bool baseline)
{
    prepare(4);
    struct charger_data data = {-1, 500000};
    struct mtk_charger info = {.chg1_dev = &charger};
    client.error_at[0] = 6;
    client.error_value[0] = -EIO;
    basic_slice(&info, &data);
    assert(aicl_calls == 1);
    assert(baseline ? last_aicl_return == 0 && data.input_current_limit_by_aicl == 100000 :
           last_aicl_return == -EIO && data.input_current_limit_by_aicl == -1);
    assert(data.input_current_limit == (baseline ? 100000 : 500000));
    assert(eta_op_set_iinlim(&charger, data.input_current_limit) == 0);
    unsigned int before = client.event_count;
    client.error_at[0] = 0;
    data.input_current_limit = 500000;
    basic_slice(&info, &data);
    assert(aicl_calls == (baseline ? 1u : 2u));
    assert(data.input_current_limit_by_aicl == (baseline ? 100000 : 500000));
    assert(baseline ? client.event_count == before : client.event_count > before);
    tests++;
    prepare(4);
    data = (struct charger_data){-1, 500000};
    info.disable_aicl = true;
    basic_slice(&info, &data);
    assert(aicl_calls == 0 && data.input_current_limit_by_aicl == -1);
    info.disable_aicl = false;
    info.fast = true;
    basic_slice(&info, &data);
    assert(aicl_calls == 0 && data.input_current_limit_by_aicl == -1);
    tests++;
    prepare(4);
    data = (struct charger_data){-1, 500000};
    info.fast = false;
    assert(eta6965_suspend(&physical) == 0);
    for (int cycle = 1; cycle <= 3; cycle++) {
        basic_slice(&info, &data);
        assert(data.input_current_limit_by_aicl == -1 && aicl_calls == (unsigned int)cycle);
        assert(last_aicl_return == -ENODATA && client.event_count == 0);
    }
    assert(eta6965_resume(&physical) == 0);
    basic_slice(&info, &data);
    assert(data.input_current_limit_by_aicl == 500000 && aicl_calls == 4);
    tests++;
    puts("exact-basic-eligible-slices-retry-and-policy-reapply: PASS");
}

static void interleave_setter(unsigned int count)
{
    if (count == 1)
        assert(eta_op_set_iinlim(&charger, 200000) == 0);
}

static unsigned int final_unlock_at;
static void change_after_verified_unlock(void)
{
    if (client.event_count == final_unlock_at) {
        unlock_hook = NULL;
        assert(eta_op_set_iinlim(&charger, 200000) == 0);
    }
}

static void ownership_limits(bool baseline)
{
    prepare(4);
    sleep_hook = interleave_setter;
    u32 output = 0;
    assert(eta_op_run_aicl(&charger, &output) == 0 && output == 500000);
    assert((client.regs[0] & 31) == 4);
    tests++;
    if (!baseline) {
        prepare(4);
        assert(eta_op_run_aicl(&charger, &output) == 0);
        final_unlock_at = client.event_count;
        prepare(4);
        unlock_hook = change_after_verified_unlock;
        assert(eta_op_run_aicl(&charger, &output) == 0 && output == 500000);
        assert((client.regs[0] & 31) == 1);
        tests++;
    }
    puts("individual-io-locks-not-whole-transaction: PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    bool baseline = strcmp(argv[1], "baseline") == 0;
    assert(baseline || strcmp(argv[1], "contract") == 0 || strcmp(argv[1], "healthy") == 0);
    assert(pthread_mutex_init(&eta.lock.native, NULL) == 0);
    healthy();
    if (strcmp(argv[1], "healthy") != 0) {
        every_access_error(baseline);
        cleanup_failures(baseline);
        pm_and_retry(baseline);
        basic_retry(baseline);
        ownership_limits(baseline);
    }
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
    printf("{\"checks\":%u,\"baseline_false_success\":%u}\n", tests, baseline_false_success);
    return 0;
}
