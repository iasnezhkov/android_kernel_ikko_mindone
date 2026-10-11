#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
#undef ENODATA
#define ENODATA 61
#undef ETIMEDOUT
#define ETIMEDOUT 110
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WRITE_ONCE(a, b) ((a) = (b))
typedef uint8_t u8;
typedef uint32_t u32;
struct i2c_client;
struct device { struct i2c_client *client; };
struct mutex { pthread_mutex_t native; };
struct charger_device { void *data; };
struct bus_event { bool write; u8 reg, value; };
struct i2c_client {
    u8 regs[256];
    struct bus_event events[512];
    unsigned int event_count, sleeps, sleep_count;
    unsigned int error_at[2];
    int error_value[2], dpm_code, pg_lost_code;
    unsigned int skip_write_at, apply_error_at;
    unsigned int corrupt_read_at;
    unsigned int lock_depth, max_lock_depth;
    void *data;
};
static struct i2c_client *active_client;
static void (*sleep_hook)(unsigned int);
static void (*unlock_hook)(void);
static void mutex_lock(struct mutex *m)
{
    assert(pthread_mutex_lock(&m->native) == 0);
    active_client->lock_depth++;
    if (active_client->max_lock_depth < active_client->lock_depth)
        active_client->max_lock_depth = active_client->lock_depth;
}
static void mutex_unlock(struct mutex *m)
{
    assert(active_client->lock_depth == 1);
    active_client->lock_depth--;
    assert(pthread_mutex_unlock(&m->native) == 0);
    if (unlock_hook)
        unlock_hook();
}
static void *charger_get_data(struct charger_device *d) { return d->data; }
static struct i2c_client *to_i2c_client(struct device *d) { return d->client; }
static void *i2c_get_clientdata(struct i2c_client *c) { return c->data; }
static void msleep(unsigned int ms)
{
    assert(active_client->lock_depth == 0);
    active_client->sleeps += ms;
    active_client->sleep_count++;
    if (sleep_hook)
        sleep_hook(active_client->sleep_count);
}
static int record_bus(struct i2c_client *c, bool write, u8 reg, u8 value)
{
    assert(reg != 9 && c->lock_depth == 1 && c->event_count < ARRAY_SIZE(c->events));
    c->events[c->event_count++] = (struct bus_event){write, reg, value};
    for (unsigned int i = 0; i < ARRAY_SIZE(c->error_at); i++)
        if (c->error_at[i] == c->event_count)
            return c->error_value[i];
    return 0;
}
static int i2c_smbus_read_byte_data(struct i2c_client *c, u8 reg)
{
    int ret = record_bus(c, false, reg, 0);
    if (ret)
        return ret;
    u8 value = c->regs[reg];
    if (reg == 8 && c->pg_lost_code >= 0 && (c->regs[0] & 31) >= c->pg_lost_code)
        value &= ~4;
    if (reg == 10 && c->dpm_code >= 0 && (c->regs[0] & 31) >= c->dpm_code)
        value |= 64;
    if (c->corrupt_read_at == c->event_count)
        value ^= 1;
    return value;
}
static int i2c_smbus_write_byte_data(struct i2c_client *c, u8 reg, u8 value)
{
    int ret = record_bus(c, true, reg, value);
    if ((!ret || c->apply_error_at == c->event_count) && c->skip_write_at != c->event_count)
        c->regs[reg] = value;
    return ret;
}
