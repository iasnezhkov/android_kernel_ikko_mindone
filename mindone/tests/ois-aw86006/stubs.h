/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef OIS_AW86006_STUBS_H
#define OIS_AW86006_STUBS_H

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int64_t ktime_t;
typedef long long loff_t;

#define BIT(n) (1UL << (n))
#define U8_MAX 255
#define PAGE_SIZE 4096
#define USEC_PER_MSEC 1000L
#define HZ 250
#define GFP_KERNEL 0
#define NOTIFY_DONE 0
#define I2C_M_RD 0x0001
#define I2C_LOCK_SEGMENT 2
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))

struct kobject {
	int unused;
};

struct device {
	struct kobject kobj;
	void *driver_data;
};

struct file;

struct mutex {
	int locked;
};

typedef struct {
	int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *a)
{
	return a->counter;
}

static inline void atomic_set(atomic_t *a, int v)
{
	a->counter = v;
}

static inline int atomic_xchg(atomic_t *a, int v)
{
	int old = a->counter;

	a->counter = v;
	return old;
}

static inline void mutex_init(struct mutex *m)
{
	m->locked = 0;
}

static inline void mutex_lock(struct mutex *m)
{
	assert(!m->locked);
	m->locked = 1;
}

static inline int mutex_lock_interruptible(struct mutex *m)
{
	mutex_lock(m);
	return 0;
}

static inline void mutex_unlock(struct mutex *m)
{
	assert(m->locked);
	m->locked = 0;
}

struct work_struct {
	void (*func)(struct work_struct *work);
};

struct delayed_work {
	struct work_struct work;
	ktime_t due;
	bool pending;
	bool running;
};

#define to_delayed_work(w) container_of(w, struct delayed_work, work)

void harness_init_dwork(struct delayed_work *dw, void (*fn)(struct work_struct *));
bool schedule_delayed_work(struct delayed_work *dw, unsigned long delay);
bool cancel_delayed_work_sync(struct delayed_work *dw);
#define INIT_DELAYED_WORK(dw, fn) harness_init_dwork(dw, fn)

static inline unsigned long msecs_to_jiffies(unsigned int ms)
{
	return (ms + 1000 / HZ - 1) / (1000 / HZ);
}

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
	struct notifier_block *next;
	int priority;
};

ktime_t ktime_get(void);
void usleep_range(unsigned long min, unsigned long max);
void msleep(unsigned int ms);

static inline ktime_t ktime_add_ms(ktime_t kt, u64 ms)
{
	return kt + (ktime_t)ms * 1000000;
}

static inline bool ktime_after(ktime_t a, ktime_t b)
{
	return a > b;
}

static inline s64 ktime_us_delta(ktime_t later, ktime_t earlier)
{
	return (later - earlier) / 1000;
}

static inline s64 ktime_ms_delta(ktime_t later, ktime_t earlier)
{
	return (later - earlier) / 1000000;
}

static inline u32 get_unaligned_le32(const void *p)
{
	const u8 *b = p;

	return b[0] | b[1] << 8 | b[2] << 16 | (u32)b[3] << 24;
}

static inline void put_unaligned_le32(u32 v, void *p)
{
	u8 *b = p;

	b[0] = v;
	b[1] = v >> 8;
	b[2] = v >> 16;
	b[3] = v >> 24;
}

static inline void put_unaligned_be16(u16 v, void *p)
{
	u8 *b = p;

	b[0] = v >> 8;
	b[1] = v;
}

static inline void *memchr_inv(const void *s, int c, size_t n)
{
	const u8 *p = s;
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i] != (u8)c)
			return (void *)(p + i);
	return NULL;
}

static inline ssize_t strscpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	assert(size);
	if (len >= size) {
		memcpy(dst, src, size - 1);
		dst[size - 1] = 0;
		return -E2BIG;
	}
	memcpy(dst, src, len + 1);
	return len;
}

void *harness_devm_alloc(size_t size);
#define devm_kzalloc(dev, size, gfp) harness_devm_alloc(size)
#define kmalloc(size, gfp) malloc(size)
#define kfree(p) free(p)

int harness_log(const char *level, const struct device *dev, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
#define dev_info(dev, ...) harness_log("info", dev, __VA_ARGS__)
#define dev_err(dev, ...) harness_log("err", dev, __VA_ARGS__)
#define dev_err_probe(dev, err, ...) (harness_log("err", dev, __VA_ARGS__), (err))

int scnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int sysfs_emit_at(char *buf, int at, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos, const void *from, size_t available);

int kstrtobool(const char *s, bool *res);
int kstrtoint(const char *s, unsigned int base, int *res);
int kstrtouint(const char *s, unsigned int base, unsigned int *res);

struct kernel_param;

struct kernel_param_ops {
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
};

struct kernel_param {
	const char *name;
	const struct kernel_param_ops *ops;
	void *arg;
	unsigned short perm;
};

struct kparam_string {
	unsigned int maxlen;
	char *string;
};

extern const struct kernel_param_ops param_ops_bool;
extern const struct kernel_param_ops param_ops_string;
int param_set_uint_minmax(const char *val, const struct kernel_param *kp, unsigned int min, unsigned int max);
int param_get_uint(char *buffer, const struct kernel_param *kp);
int param_get_int(char *buffer, const struct kernel_param *kp);
void harness_param_register(const struct kernel_param *kp);

#define module_param_cb(name, ops_, arg_, perm_)						\
	static const struct kernel_param __param_##name = { #name, ops_, arg_, perm_ };		\
	__attribute__((constructor)) static void __param_reg_##name(void)			\
	{											\
		harness_param_register(&__param_##name);					\
	}											\
	static const int __param_tail_##name __attribute__((unused))
#define module_param_named(name, var, type, perm) module_param_cb(name, &param_ops_##type, &var, perm)
#define module_param_string(name, str, len, perm)						\
	static struct kparam_string __kps_##name = { len, str };				\
	module_param_cb(name, &param_ops_string, &__kps_##name, perm)
#define MODULE_PARM_DESC(name, desc) static const char __parm_desc_##name[] __attribute__((unused)) = desc
#define MODULE_DEVICE_TABLE(type, name) static const void *const __mod_##type##_table __attribute__((unused)) = &name
#define MODULE_DESCRIPTION(d) static const char __mod_description[] __attribute__((unused)) = d
#define MODULE_LICENSE(l) static const char __mod_license[] __attribute__((unused)) = l

struct attribute {
	const char *name;
	unsigned short mode;
};

struct device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
};

struct bin_attribute {
	struct attribute attr;
	size_t size;
	ssize_t (*read)(struct file *file, struct kobject *kobj, const struct bin_attribute *attr, char *buf,
			loff_t off, size_t count);
};

struct attribute_group {
	const char *name;
	struct attribute **attrs;
	const struct bin_attribute *const *bin_attrs;
};

#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { #_name, 0444 }, .show = _name##_show }
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { #_name, 0200 }, .store = _name##_store }
#define BIN_ATTR_RO(_name, _size) \
	struct bin_attribute bin_attr_##_name = { .attr = { #_name, 0444 }, .size = (_size), .read = _name##_read }
#define __ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group *_name##_groups[] = { &_name##_group, NULL }

#define kobj_to_dev(k) container_of(k, struct device, kobj)

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

struct firmware {
	size_t size;
	const u8 *data;
};

int request_firmware(const struct firmware **fw, const char *name, struct device *dev);
void release_firmware(const struct firmware *fw);

struct i2c_adapter {
	int nr;
};

struct i2c_client {
	unsigned short addr;
	struct i2c_adapter *adapter;
	struct device dev;
};

struct i2c_msg {
	u16 addr;
	u16 flags;
	u16 len;
	u8 *buf;
};

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
void i2c_lock_bus(struct i2c_adapter *adap, unsigned int flags);
void i2c_unlock_bus(struct i2c_adapter *adap, unsigned int flags);

#define i2c_set_clientdata(c, d) ((c)->dev.driver_data = (d))
#define i2c_get_clientdata(c) ((c)->dev.driver_data)

struct of_device_id {
	char compatible[128];
};

struct i2c_device_id {
	char name[20];
	unsigned long driver_data;
};

struct device_driver {
	const char *name;
	const struct of_device_id *of_match_table;
	const struct attribute_group **dev_groups;
};

struct i2c_driver {
	struct device_driver driver;
	int (*probe)(struct i2c_client *client);
	void (*remove)(struct i2c_client *client);
	const struct i2c_device_id *id_table;
};

#define module_i2c_driver(drv) static struct i2c_driver *const harness_driver = &(drv)

#endif
