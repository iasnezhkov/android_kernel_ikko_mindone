
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
#ifndef ENODATA
#define ENODATA 61
#endif
#define loff_t int64_t
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WRITE_ONCE(p, v) ((p) = (v))
#define chr_info(...) ((void)0)
#define chr_err(...) ((void)0)
typedef uint8_t u8;
typedef uint32_t u32;
struct mutex { pthread_mutex_t native; };
struct i2c_client;
struct device { void *data; struct i2c_client *client; };
struct i2c_client {
    struct device dev;
    u8 regs[256];
    int reads, writes, read_error_at, read_error, write_error;
    int reg_reads[256], readback_xor_at;
    u8 readback_xor_mask;
    bool discard_timer_write;
};
struct device_attribute { int unused; };
struct charger_device { void *data; };
struct seq_file { void *private; char output[32]; };
struct mtk_charger {
    struct charger_device *chg1_dev;
    int safety_timer_cmd;
    bool sw_safety_timer_setting, enable_sw_safety_timer;
};
struct inode { void *data; };
struct file { struct inode *inode; };
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bus_cond = PTHREAD_COND_INITIALIZER;
static bool pause_read, read_entered, release_read;
static bool copy_error;
static int callback_error;
static void mutex_lock(struct mutex *lock) { assert(pthread_mutex_lock(&lock->native) == 0); }
static void mutex_unlock(struct mutex *lock) { assert(pthread_mutex_unlock(&lock->native) == 0); }
static void *dev_get_drvdata(struct device *dev) { return dev->data; }
static struct i2c_client *to_i2c_client(struct device *dev) { return dev->client; }
static void *i2c_get_clientdata(struct i2c_client *client) { return client->dev.data; }
static void *charger_get_data(struct charger_device *dev) { return dev->data; }
static struct inode *file_inode(struct file *file) { return file->inode; }
static void *pde_data(struct inode *inode) { return inode->data; }
static int i2c_smbus_read_byte_data(struct i2c_client *client, u8 reg)
{
    client->reads++;
    client->reg_reads[reg]++;
    pthread_mutex_lock(&bus_lock);
    if (pause_read) {
        read_entered = true;
        pthread_cond_broadcast(&bus_cond);
        while (!release_read)
            pthread_cond_wait(&bus_cond, &bus_lock);
    }
    pthread_mutex_unlock(&bus_lock);
    if (client->reads == client->read_error_at)
        return client->read_error;
    if (client->reads == client->readback_xor_at)
        return client->regs[reg] ^ client->readback_xor_mask;
    return client->regs[reg];
}
static int i2c_smbus_write_byte_data(struct i2c_client *client, u8 reg, u8 value)
{
    client->writes++;
    if (client->write_error)
        return client->write_error;
    if (client->discard_timer_write && reg == 5)
        value = (value & ~0x0c) | (client->regs[reg] & 0x0c);
    client->regs[reg] = value;
    return 0;
}
static int sysfs_emit(char *buf, const char *format, ...)
{
    va_list args;
    int ret;
    va_start(args, format);
    ret = vsnprintf(buf, 4096, format, args);
    va_end(args);
    return ret;
}
static void seq_printf(struct seq_file *seq, const char *format, int value)
{
    snprintf(seq->output, sizeof(seq->output), format, value);
}
static int copy_from_user(char *dest, const char *source, size_t size)
{
    if (copy_error)
        return 1;
    memcpy(dest, source, size);
    return 0;
}
static int kstrtou32(const char *input, int base, unsigned int *out)
{
    unsigned long value;
    char *end;
    if (!input[0] || input[0] == '-' || isspace((unsigned char)input[0]))
        return -EINVAL;
    errno = 0;
    value = strtoul(input, &end, base);
    if (errno == ERANGE || value > UINT_MAX)
        return -ERANGE;
    if (end == input || (*end && !(end[0] == '\n' && !end[1])))
        return -EINVAL;
    *out = value;
    return 0;
}

#include "eta_source.h"

static int charger_dev_enable_safety_timer(struct charger_device *dev, bool enable)
{
    if (callback_error)
        return callback_error;
    return eta_op_enable_safety_timer(dev, enable);
}
static int charger_dev_is_safety_timer_enabled(struct charger_device *dev, bool *enable)
{
    if (callback_error)
        return callback_error;
    return eta_op_is_safety_timer_enabled(dev, enable);
}

#include "proc_source.h"

static void init(struct eta6965_device *eta, struct i2c_client *client,
                 struct charger_device *charger)
{
    memset(eta, 0, sizeof(*eta));
    memset(client, 0, sizeof(*client));
    assert(pthread_mutex_init(&eta->lock.native, NULL) == 0);
    eta->client = client;
    eta->dev = &client->dev;
    client->dev.data = eta;
    client->dev.client = client;
    charger->data = eta;
    callback_error = 0;
    copy_error = false;
}
static void test_timer(void)
{
    struct eta6965_device eta;
    struct i2c_client client;
    struct charger_device charger;
    bool enabled;
    u8 first, second;
    char status[4096];
    int reads, writes;
    init(&eta, &client, &charger);
    client.regs[5] = 0xf3;
    client.regs[7] = 0x40;
    assert(eta6965_hw_init(&eta) == 0);
    assert(client.regs[5] == 0xcf);
    assert(client.regs[7] == 0x43);
    assert(eta_op_is_safety_timer_enabled(&charger, &enabled) == 0 && enabled);
    assert(eta_op_enable_safety_timer(&charger, false) == 0);
    assert(client.regs[5] == 0xc7);
    client.regs[5] = 0xc3;
    assert(eta_op_enable_safety_timer(&charger, true) == 0 && client.regs[5] == 0xcf);
    assert(safety_timer_status_show(&client.dev, NULL, status) > 0);
    assert(strcmp(status, "reg05=0xcf reg07=0x43 enabled=1 duration_hours=10 regulation_half_rate=1\n") == 0);
    client.read_error_at = client.reads + 1;
    client.read_error = -EREMOTEIO;
    enabled = false;
    assert(eta_op_is_safety_timer_enabled(&charger, &enabled) == -EREMOTEIO && !enabled);
    client.read_error_at = client.reads + 2;
    first = 0xaa;
    second = 0xbb;
    assert(eta6965_read_safety_state(&eta, &first, &second) == -EREMOTEIO);
    assert(first == 0xaa && second == 0xbb);
    client.write_error = -EREMOTEIO;
    assert(eta_op_enable_safety_timer(&charger, false) == -EREMOTEIO);
    client.write_error = 0;
    client.read_error_at = client.reads + 2;
    assert(eta_op_enable_safety_timer(&charger, false) == -EREMOTEIO);
    client.discard_timer_write = true;
    assert(eta_op_enable_safety_timer(&charger, true) == -EIO);
    client.discard_timer_write = false;
    assert(eta6965_suspend(&client.dev) == 0);
    reads = client.reads;
    writes = client.writes;
    enabled = true;
    first = 0xaa;
    strcpy(status, "unchanged");
    assert(eta6965_read_byte(&eta, 5, &first) == -ENODATA && first == 0xaa);
    assert(eta_op_is_safety_timer_enabled(&charger, &enabled) == -ENODATA && enabled);
    assert(eta_op_enable_safety_timer(&charger, true) == -ENODATA);
    assert(eta6965_update_bits(&eta, 5, 8, 8) == -ENODATA);
    assert(safety_timer_status_show(&client.dev, NULL, status) == -ENODATA);
    assert(strcmp(status, "unchanged") == 0);
    assert(client.reads == reads && client.writes == writes);
    assert(pthread_mutex_trylock(&eta.lock.native) == 0);
    mutex_unlock(&eta.lock);
    assert(eta6965_resume(&client.dev) == 0);
    assert(eta_op_enable_safety_timer(&charger, true) == 0);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
    init(&eta, &client, &charger);
    client.discard_timer_write = true;
    assert(eta6965_hw_init(&eta) == -EIO);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
}
static void test_register_contracts(void)
{
    struct eta6965_device eta;
    struct i2c_client client;
    struct charger_device charger;
    bool enabled;
    char status[4096], expected[128];
    u32 voltage;
    _Static_assert(ETA6965_REG_05 == 5 && ETA6965_REG_07 == 7, "ETA6965 register addresses");
    _Static_assert(REG05_EN_TIMER_MASK == 0x08, "ETA6965 EN_TIMER bit 3");
    _Static_assert(REG05_CHG_TIMER_MASK == 0x04, "ETA6965 5/10-hour selector bit 2");
    _Static_assert(REG05_WATCHDOG_MASK == 0x30, "ETA6965 watchdog bits 5:4");
    _Static_assert(REG07_TMR2X_EN_MASK == 0x40, "ETA6965 regulation half-rate bit 6");
    _Static_assert(REG04_VREG_MASK == 0xf8, "ETA6965 VREG bits 7:3");
    init(&eta, &client, &charger);
    for (unsigned int seed = 0; seed < 256; seed++) {
        client.regs[5] = seed;
        client.regs[7] = seed;
        assert(eta6965_hw_init(&eta) == 0);
        assert(client.regs[5] == ((seed & ~0x3c) | 0x0c));
        assert(client.regs[7] == (seed | 0x03));
        client.regs[5] = seed;
        assert(eta_op_enable_safety_timer(&charger, true) == 0);
        assert(client.regs[5] == (seed | 0x0c));
        client.regs[5] = seed;
        assert(eta_op_enable_safety_timer(&charger, false) == 0);
        assert(client.regs[5] == (seed & ~0x08));
        client.regs[5] = seed;
        enabled = !(seed & 0x08);
        assert(eta_op_is_safety_timer_enabled(&charger, &enabled) == 0);
        assert(enabled == !!(seed & 0x08));
        for (unsigned int half_rate = 0; half_rate < 2; half_rate++) {
            client.regs[7] = 0x03 | (half_rate << 6);
            snprintf(expected, sizeof(expected),
                     "reg05=0x%02x reg07=0x%02x enabled=%u duration_hours=%u regulation_half_rate=%u\n",
                     seed, client.regs[7], !!(seed & 0x08), (seed & 0x04) ? 10 : 5, half_rate);
            assert(safety_timer_status_show(&client.dev, NULL, status) > 0);
            assert(strcmp(status, expected) == 0);
        }
    }
    for (unsigned int bit = 0; bit < 8; bit++) {
        client.readback_xor_at = client.reads + 2;
        client.readback_xor_mask = 1u << bit;
        assert(eta_op_enable_safety_timer(&charger, true) == ((bit == 2 || bit == 3) ? -EIO : 0));
    }
    client.readback_xor_at = 0;
    client.read_error = -EREMOTEIO;
    for (int failed_read = 1; failed_read <= 2; failed_read++) {
        strcpy(status, "unchanged");
        client.read_error_at = client.reads + failed_read;
        assert(safety_timer_status_show(&client.dev, NULL, status) == -EREMOTEIO);
        assert(strcmp(status, "unchanged") == 0);
    }
    client.read_error_at = 0;
    assert(client.reg_reads[9] == 0);
    for (unsigned int code = 0; code <= 24; code++) {
        client.regs[4] = (code << 3) | 0x07;
        assert(eta6965_get_cv_uV(&eta, &voltage) == 0);
        assert(voltage == 3848000u + code * 32000u);
    }
    client.regs[4] = 0x07;
    assert(eta_op_set_cv(&charger, 4450000) == 0 && client.regs[4] == 0x97);
    assert(eta6965_get_cv_uV(&eta, &voltage) == 0 && voltage == 4424000);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
    puts("ETA independent register contracts: PASS (256 states, 512 decodes, masked readback, no REG09, VREG)");
}
static void test_proc(void)
{
    struct eta6965_device eta;
    struct i2c_client client;
    struct charger_device charger;
    struct mtk_charger info;
    struct inode inode;
    struct file file;
    struct seq_file seq;
    loff_t offset = 0;
    init(&eta, &client, &charger);
    info = (struct mtk_charger){ &charger, -1, true, false };
    inode.data = &info;
    file.inode = &inode;
    seq.private = &info;
    seq.output[0] = 0;
    callback_error = -EOPNOTSUPP;
    assert(mtk_chg_en_safety_timer_show(&seq, NULL) == -EOPNOTSUPP && !seq.output[0]);
    assert(mtk_chg_en_safety_timer_write(&file, "1\n", 2, &offset) == -EOPNOTSUPP);
    assert(info.safety_timer_cmd == -1 && !info.enable_sw_safety_timer);
    callback_error = -EIO;
    assert(mtk_chg_en_safety_timer_write(&file, "1", 1, &offset) == -EIO);
    assert(info.safety_timer_cmd == -1 && !info.enable_sw_safety_timer);
    callback_error = 0;
    assert(mtk_chg_en_safety_timer_write(&file, "2", 1, &offset) == -EINVAL);
    assert(mtk_chg_en_safety_timer_write(&file, "invalid", 7, &offset) == -EINVAL);
    assert(mtk_chg_en_safety_timer_write(&file, "", 0, &offset) == -EINVAL);
    assert(mtk_chg_en_safety_timer_write(&file, "1", 32, &offset) == -EINVAL);
    assert(info.safety_timer_cmd == -1 && !info.enable_sw_safety_timer && !client.writes);
    copy_error = true;
    assert(mtk_chg_en_safety_timer_write(&file, "1", 1, &offset) == -EFAULT);
    copy_error = false;
    assert(mtk_chg_en_safety_timer_write(&file, "1\n", 2, &offset) == 2);
    assert(info.safety_timer_cmd == 1 && info.enable_sw_safety_timer);
    assert(mtk_chg_en_safety_timer_show(&seq, NULL) == 0 && strcmp(seq.output, "1\n") == 0);
    assert(mtk_chg_en_safety_timer_write(&file, "0", 1, &offset) == 1);
    assert(info.safety_timer_cmd == 0 && !info.enable_sw_safety_timer);
    seq.private = NULL;
    assert(mtk_chg_en_safety_timer_show(&seq, NULL) == -EINVAL);
    inode.data = NULL;
    assert(mtk_chg_en_safety_timer_write(&file, "1", 1, &offset) == -EINVAL);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
}
struct race {
    struct eta6965_device *eta;
    atomic_bool started, finished;
    int ret;
    u8 output;
};
static void *reader(void *arg)
{
    struct race *race = arg;
    race->ret = eta6965_read_byte(race->eta, 5, &race->output);
    return NULL;
}
static void *suspender(void *arg)
{
    struct race *race = arg;
    atomic_store(&race->started, true);
    assert(eta6965_suspend(race->eta->dev) == 0);
    atomic_store(&race->finished, true);
    return NULL;
}
static void test_suspend_drain(void)
{
    struct eta6965_device eta;
    struct i2c_client client;
    struct charger_device charger;
    struct race race;
    pthread_t read_thread, suspend_thread;
    int attempts = 1000;
    init(&eta, &client, &charger);
    client.regs[5] = 0x55;
    race = (struct race){ .eta = &eta };
    atomic_init(&race.started, false);
    atomic_init(&race.finished, false);
    pthread_mutex_lock(&bus_lock);
    pause_read = true;
    read_entered = false;
    release_read = false;
    pthread_mutex_unlock(&bus_lock);
    assert(pthread_create(&read_thread, NULL, reader, &race) == 0);
    pthread_mutex_lock(&bus_lock);
    while (!read_entered)
        pthread_cond_wait(&bus_cond, &bus_lock);
    pthread_mutex_unlock(&bus_lock);
    assert(pthread_create(&suspend_thread, NULL, suspender, &race) == 0);
    while (!atomic_load(&race.started) && attempts--)
        usleep(1000);
    assert(atomic_load(&race.started) && !atomic_load(&race.finished));
    assert(pthread_mutex_trylock(&eta.lock.native) == EBUSY);
    pthread_mutex_lock(&bus_lock);
    release_read = true;
    pause_read = false;
    pthread_cond_broadcast(&bus_cond);
    pthread_mutex_unlock(&bus_lock);
    assert(pthread_join(read_thread, NULL) == 0);
    assert(pthread_join(suspend_thread, NULL) == 0);
    assert(race.ret == 0 && race.output == 0x55 && eta.suspended);
    assert(atomic_load(&race.finished));
    assert(eta_op_enable_safety_timer(&charger, true) == -ENODATA);
    assert(client.reads == 1 && client.writes == 0);
    assert(eta6965_resume(&client.dev) == 0);
    assert(eta_op_enable_safety_timer(&charger, true) == 0);
    assert(pthread_mutex_destroy(&eta.lock.native) == 0);
}
int main(void)
{
    test_timer();
    test_register_contracts();
    test_proc();
    test_suspend_drain();
    puts("ETA safety exact-source fixtures: PASS (timer/readback/errno/proc/PM drain)");
    return 0;
}
