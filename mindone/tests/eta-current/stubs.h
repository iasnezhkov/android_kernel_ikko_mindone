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
#ifndef ENODATA
#define ENODATA 61
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef uint8_t u8;
typedef uint32_t u32;
struct mutex { pthread_mutex_t native; };
struct device { int unused; };
struct charger_device { void *data; };
struct i2c_client {
    u8 regs[256];
    int reads, writes, read_errno, write_errno;
    int read_at, write_at, dpm_code;
    unsigned int sleeps;
    bool hardware_ichg_clamp;
};
static struct i2c_client *sleep_client;
static void mutex_lock(struct mutex *m) { assert(pthread_mutex_lock(&m->native) == 0); }
static void mutex_unlock(struct mutex *m) { assert(pthread_mutex_unlock(&m->native) == 0); }
static void *charger_get_data(struct charger_device *d) { return d->data; }
static void msleep(unsigned int ms) { sleep_client->sleeps += ms; }
static int i2c_smbus_read_byte_data(struct i2c_client *c, u8 reg)
{
    assert(reg != 9);
    c->reads++;
    if (c->read_at == c->reads)
        return c->read_errno;
    if (reg == 10 && c->dpm_code >= 0 && (c->regs[0] & 31) >= c->dpm_code)
        return c->regs[10] | 64;
    return c->regs[reg];
}
static int i2c_smbus_write_byte_data(struct i2c_client *c, u8 reg, u8 value)
{
    c->writes++;
    if (c->write_at == c->writes)
        return c->write_errno;
    if (c->hardware_ichg_clamp && reg == 2 && (value & 63) > 50)
        value = (value & 192) | 50;
    c->regs[reg] = value;
    return 0;
}
