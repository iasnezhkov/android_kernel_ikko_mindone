#include "stubs.h"
#include "eta_source.h"

static unsigned int discrepancies;
static bool strict;
static void expect(u32 actual, u32 wanted, const char *group)
{
    if (actual == wanted)
        return;
    discrepancies++;
    if (strict) {
        fprintf(stderr, "contract %s: actual=%u expected=%u\n", group, actual, wanted);
        exit(17);
    }
}
static void reset(struct eta6965_device *e, struct i2c_client *c, struct charger_device *d)
{
    memset(e, 0, sizeof(*e));
    memset(c, 0, sizeof(*c));
    assert(pthread_mutex_init(&e->lock.native, NULL) == 0);
    e->client = c;
    c->dpm_code = -1;
    c->regs[8] = 4;
    d->data = e;
    sleep_client = c;
}
static void finish(struct eta6965_device *e)
{
    assert(pthread_mutex_destroy(&e->lock.native) == 0);
}
static u32 input_code(u32 request)
{
    if (request < 100000)
        return 0;
    if (request >= 3200000)
        return 31;
    return request / 100000 - 1;
}
static u32 charge_code(u32 request)
{
    return request >= 3000000 ? 50 : request / 60000;
}
static void decodes(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    u32 value;
    unsigned int raw;
    reset(&e, &c, &d);
    for (raw = 0; raw < 256; raw++) {
        c.regs[0] = raw;
        value = 0xdeadbeef;
        assert(eta_op_get_iinlim(&d, &value) == 0);
        expect(value, 100000 + (raw & 31) * 100000, "REG00 decode");
        assert(eta6965_get_iinlim_uA(&e, &value) == 0);
        expect(value, 100000 + (raw & 31) * 100000, "CURRENT_MAX decode");
        c.regs[2] = raw;
        assert(eta_op_get_ichg(&d, &value) == 0);
        expect(value, ((raw & 63) > 50 ? 50 : (raw & 63)) * 60000, "REG02 decode");
    }
    assert(c.writes == 0);
    finish(&e);
}
static void boundaries(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    u32 value, requests[8];
    unsigned int step, edge, high;
    reset(&e, &c, &d);
    c.regs[4] = 0xa5;
    c.regs[5] = 0x8f;
    c.regs[7] = 0x4f;
    for (step = 0; step <= 34; step++) {
        u32 at = step * 100000;
        requests[0] = at ? at - 1 : 0;
        requests[1] = at;
        requests[2] = at + 1;
        for (edge = 0; edge < 3; edge++) {
            u32 request = requests[edge];
            for (high = 0; high < 8; high++) {
                c.regs[0] = high << 5;
                assert(eta_op_set_iinlim(&d, request) == 0);
                assert((c.regs[0] & 224) == high << 5);
                expect(c.regs[0] & 31, input_code(request), "REG00 encode");
                assert(eta_op_get_iinlim(&d, &value) == 0);
                expect(value, 100000 + input_code(request) * 100000, "REG00 request roundtrip");
            }
        }
    }
    for (step = 0; step <= 56; step++) {
        u32 at = step * 60000;
        requests[0] = at ? at - 1 : 0;
        requests[1] = at;
        requests[2] = at + 1;
        for (edge = 0; edge < 3; edge++) {
            u32 request = requests[edge];
            for (high = 0; high < 4; high++) {
                c.regs[2] = high << 6;
                assert(eta_op_set_ichg(&d, request) == 0);
                assert((c.regs[2] & 192) == high << 6);
                expect(c.regs[2] & 63, charge_code(request), "REG02 encode");
                assert(eta_op_get_ichg(&d, &value) == 0);
                expect(value, charge_code(request) * 60000, "REG02 request roundtrip");
            }
        }
    }
    assert(eta_op_set_iinlim(&d, UINT32_MAX) == 0);
    expect(c.regs[0] & 31, 31, "REG00 unsigned saturation");
    assert(eta_op_set_ichg(&d, UINT32_MAX) == 0);
    expect(c.regs[2] & 63, 50, "REG02 unsigned saturation");
    assert(c.regs[4] == 0xa5 && c.regs[5] == 0x8f && c.regs[7] == 0x4f);
    finish(&e);
}
static void register_roundtrip(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    unsigned int code;
    u32 current;
    reset(&e, &c, &d);
    for (code = 0; code < 32; code++) {
        c.regs[0] = 0xe0 | code;
        assert(eta_op_get_iinlim(&d, &current) == 0);
        assert(eta_op_set_iinlim(&d, current) == 0);
        expect(c.regs[0], 0xe0 | code, "PD old-IIN restore");
    }
    finish(&e);
}
static void aicl(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    unsigned int code, limit;
    u32 current;
    for (code = 0; code < 32; code++) {
        reset(&e, &c, &d);
        c.regs[0] = 0xe0 | code;
        assert(eta_op_run_aicl(&d, &current) == 0);
        expect(c.regs[0], 0xe0 | code, "AICL healthy register");
        expect(current, 100000 + code * 100000, "AICL decoded current");
        assert(eta_op_set_iinlim(&d, current) == 0);
        expect(c.regs[0], 0xe0 | code, "AICL Basic reapply");
        finish(&e);
    }
    for (limit = 1; limit < 32; limit++) {
        reset(&e, &c, &d);
        c.regs[0] = 0xbf;
        c.dpm_code = limit;
        assert(eta_op_run_aicl(&d, &current) == 0);
        expect(c.regs[0], 0xa0 | (limit - 1), "AICL DPM register");
        expect(current, limit * 100000, "AICL DPM decode");
        assert(eta_op_set_iinlim(&d, current) == 0);
        expect(c.regs[0], 0xa0 | (limit - 1), "AICL DPM Basic reapply");
        finish(&e);
    }
}
static void errors(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    u32 value;
    unsigned int which, error, phase;
    int failures[] = { -EIO, -EREMOTEIO, -ENODATA };
    int (*gets[])(struct charger_device *, u32 *) = { eta_op_get_iinlim, eta_op_get_ichg };
    int (*sets[])(struct charger_device *, u32) = { eta_op_set_iinlim, eta_op_set_ichg };
    for (which = 0; which < 2; which++) {
        for (error = 0; error < ARRAY_SIZE(failures); error++) {
            reset(&e, &c, &d);
            value = 0xdeadbeef;
            c.read_at = 1;
            c.read_errno = failures[error];
            assert(gets[which](&d, &value) == failures[error]);
            assert(value == 0xdeadbeef && c.writes == 0);
            finish(&e);
            for (phase = 0; phase < 2; phase++) {
                reset(&e, &c, &d);
                memset(c.regs, 0xa5, sizeof(c.regs));
                c.read_at = phase == 0 ? 1 : 0;
                c.read_errno = failures[error];
                c.write_at = phase == 1 ? 1 : 0;
                c.write_errno = failures[error];
                assert(sets[which](&d, 2500000) == failures[error]);
                for (unsigned int reg = 0; reg < 256; reg++)
                    assert(c.regs[reg] == 0xa5);
                finish(&e);
            }
        }
        reset(&e, &c, &d);
        e.suspended = true;
        value = 0xdeadbeef;
        assert(gets[which](&d, &value) == -ENODATA);
        assert(sets[which](&d, 3000000) == -ENODATA);
        assert(value == 0xdeadbeef && c.reads == 0 && c.writes == 0);
        finish(&e);
    }
}
static void clamp_and_normal(void)
{
    struct eta6965_device e;
    struct i2c_client c;
    struct charger_device d;
    u32 value;
    reset(&e, &c, &d);
    c.hardware_ichg_clamp = true;
    assert(eta_op_set_ichg(&d, 3240000) == 0);
    assert((c.regs[2] & 63) == 50);
    assert(eta_op_get_ichg(&d, &value) == 0 && value == 3000000);
    assert(eta_op_set_iinlim(&d, 500000) == 0 && (c.regs[0] & 31) == 4);
    assert(eta_op_set_iinlim(&d, 2000000) == 0 && (c.regs[0] & 31) == 19);
    assert(eta_op_set_ichg(&d, 480000) == 0 && (c.regs[2] & 63) == 8);
    assert(eta_op_set_ichg(&d, 1680000) == 0 && (c.regs[2] & 63) == 28);
    c.regs[4] = 7;
    assert(eta_op_set_cv(&d, 4450000) == 0);
    assert(c.regs[4] == (18 << 3 | 7));
    assert(eta6965_get_cv_uV(&e, &value) == 0 && value == 4424000);
    finish(&e);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    strict = strcmp(argv[1], "audit") != 0;
    errors();
    clamp_and_normal();
    if (strcmp(argv[1], "roundtrip") == 0)
        register_roundtrip();
    else {
        decodes();
        boundaries();
        register_roundtrip();
        aicl();
    }
    printf("{\"groups\":6,\"datasheet_discrepancies\":%u}\n", discrepancies);
    return 0;
}
