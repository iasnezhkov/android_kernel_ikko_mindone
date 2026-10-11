#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifndef ENODATA
#define ENODATA 61
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef uint8_t u8;
struct mutex { pthread_mutex_t native; };
struct device { int unused; };
struct i2c_client { u8 regs[256]; };
static void mutex_lock(struct mutex *lock) { assert(pthread_mutex_lock(&lock->native) == 0); }
static void mutex_unlock(struct mutex *lock) { assert(pthread_mutex_unlock(&lock->native) == 0); }
static int i2c_smbus_read_byte_data(struct i2c_client *client, u8 reg)
{
    return client->regs[reg];
}
static int i2c_smbus_write_byte_data(struct i2c_client *client, u8 reg, u8 value)
{
    client->regs[reg] = value;
    return 0;
}

#include "eta_source.h"

int main(void)
{
    struct i2c_client client = { .regs = { [5] = 0xf3, [7] = 0x40 } };
    struct eta6965_device eta = { .client = &client };
    assert(pthread_mutex_init(&eta.lock.native, NULL) == 0);
    assert(eta6965_hw_init(&eta) == 0);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
    if ((client.regs[5] & 0x0c) != 0x0c) {
        printf("ETA initializer FAIL: REG05=0x%02x, expected timer bits 0x0c\n", client.regs[5]);
        return 1;
    }
    printf("ETA candidate initializer: PASS (REG05=0x%02x, enabled 10-hour timer)\n", client.regs[5]);
    return 0;
}
