// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef int64_t s64;
typedef s64 ktime_t;
typedef ssize_t ssize_t;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define I2C_M_RD 0x0001
#define GFP_KERNEL 0
#define NOTIFY_DONE 0x0000
#define NOTIFY_OK 0x0001

typedef struct { int v; } atomic_t;
static inline int atomic_read(const atomic_t *a) { return a->v; }
static inline void atomic_set(atomic_t *a, int v) { a->v = v; }
static inline void atomic_inc(atomic_t *a) { a->v++; }

struct mutex { int locked; };
static inline void mutex_init(struct mutex *m) { m->locked = 0; }
static inline void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static inline int mutex_lock_interruptible(struct mutex *m) { mutex_lock(m); return 0; }
static inline void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }

typedef struct { int locked; } spinlock_t;
static inline void spin_lock_init(spinlock_t *s) { s->locked = 0; }
#define spin_lock_irqsave(s, f) do { (void)(f); assert(!(s)->locked); (s)->locked = 1; } while (0)
#define spin_unlock_irqrestore(s, f) do { (void)(f); assert((s)->locked); (s)->locked = 0; } while (0)

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
struct work_struct { work_func_t func; };
struct delayed_work { struct work_struct work; int scheduled; unsigned long delay; };
static inline struct delayed_work *to_delayed_work(struct work_struct *w) {
	return container_of(w, struct delayed_work, work);
}
#define INIT_DELAYED_WORK(dw, fn) do { (dw)->work.func = (fn); (dw)->scheduled = 0; (dw)->delay = 0; } while (0)
static struct workqueue_struct { int unused; } stub_system_wq;
#define system_wq (&stub_system_wq)

struct notifier_block;
typedef int (*notifier_fn_t)(struct notifier_block *nb, unsigned long action, void *data);
struct notifier_block { notifier_fn_t notifier_call; };

enum imgsensor_power_action {
	IMGSENSOR_POWER_PIN_ON = 1,
	IMGSENSOR_POWER_ON,
	IMGSENSOR_POWER_OFF,
};

struct imgsensor_power_event {
	int sensor_idx;
	const char *pin;
};

struct device { void *drvdata; };
struct device_attribute { int dummy; };
struct i2c_adapter { int nr; };
struct i2c_client { u16 addr; struct i2c_adapter *adapter; struct device dev; };
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };

static inline void *dev_get_drvdata(struct device *d) { return d->drvdata; }
static inline void i2c_set_clientdata(struct i2c_client *c, void *p) { c->dev.drvdata = p; }
static inline void *i2c_get_clientdata(const struct i2c_client *c) { return c->dev.drvdata; }

struct kernel_param { void *arg; };
struct kernel_param_ops {
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
};

static inline u16 get_unaligned_be16(const void *p)
{
	const u8 *b = p;
	return ((u16)b[0] << 8) | b[1];
}

static inline void put_unaligned_be16(u16 val, void *p)
{
	u8 *b = p;
	b[0] = (u8)(val >> 8);
	b[1] = (u8)(val & 0xff);
}

static inline unsigned long msecs_to_jiffies(const unsigned int m) { return m; }

static ktime_t stub_now_ms;
static inline ktime_t ktime_get(void) { return stub_now_ms; }
static inline s64 ktime_ms_delta(ktime_t later, ktime_t earlier) { return later - earlier; }

static int kstrtoint(const char *s, unsigned int base, int *res)
{
	char *end = NULL;
	long v;

	if (!s || !*s)
		return -EINVAL;
	errno = 0;
	v = strtol(s, &end, (int)base);
	if (errno == ERANGE || v < INT_MIN || v > INT_MAX)
		return -ERANGE;
	if (end == s || (*end != '\0' && *end != '\n'))
		return -EINVAL;
	*res = (int)v;
	return 0;
}

static int param_set_uint_minmax(const char *val, const struct kernel_param *kp,
				 unsigned int min, unsigned int max)
{
	int v, ret = kstrtoint(val, 0, &v);
	if (ret)
		return ret;
	if (v < (int)min || v > (int)max)
		return -EINVAL;
	*(unsigned int *)kp->arg = (unsigned int)v;
	return 0;
}

static int param_get_uint(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, 32, "%u\n", *(unsigned int *)kp->arg);
}

static int param_get_int(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, 32, "%d\n", *(int *)kp->arg);
}

static bool sysfs_streq(const char *s1, const char *s2)
{
	size_t n = strlen(s2);
	if (strncmp(s1, s2, n) != 0)
		return false;
	return s1[n] == '\0' || (s1[n] == '\n' && s1[n + 1] == '\0');
}

static ssize_t sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(buf, 4096, fmt, ap);
	va_end(ap);
	return n;
}

static inline int dev_err_probe(struct device *dev, int err, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return err;
}

#define dev_info(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_warn(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_dbg(dev, fmt, ...) do { (void)(dev); } while (0)
#define module_param_cb(name, ops, arg, perm)
#define MODULE_PARM_DESC(name, desc)
#define DEVICE_ATTR_RW(name) struct device_attribute dev_attr_##name = { 0 }
#define DEVICE_ATTR_RO(name) struct device_attribute dev_attr_##name = { 0 }

static void *stub_alloc_ptrs[16];
static int stub_alloc_count;

static void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	void *p;
	(void)dev;
	(void)gfp;
	p = calloc(1, size);
	assert(p && stub_alloc_count < 16);
	stub_alloc_ptrs[stub_alloc_count++] = p;
	return p;
}

static void stub_free_all(void)
{
	for (int i = 0; i < stub_alloc_count; i++)
		free(stub_alloc_ptrs[i]);
	stub_alloc_count = 0;
}

struct xfer_log {
	int is_write;
	u16 addr;
	u8 reg;
	u16 val;
};

static struct {
	int slow_nr;
	u16 slow_addr;
	unsigned int slow_hz;
	int slow_calls;
	int slow_fail_rc;
	struct notifier_block *registered_nb;
	int notifier_fail_rc;
	int unregister_calls;
	u16 regs[128];
	int nack_reads_left;
	int force_write_err;
	int drop_power_on_read_n;
	int read_count;
	void (*on_msleep)(void);
	struct xfer_log log[128];
	int log_count;
	int cancel_calls;
	int msleep_calls;
} sim;

static void sim_reset(void)
{
	memset(&sim, 0, sizeof(sim));
	stub_now_ms = 1000;
	for (int i = 0; i < 128; i++)
		sim.regs[i] = 0xffff;
	sim.regs[0x00] = 0x0000;
	sim.regs[0x01] = 0x5a30;
	sim.regs[0x0a] = 0x0001;
	sim.regs[0x19] = 0x0001;
	sim.regs[0x20] = 0x0010;
	sim.regs[0x21] = (u16)(s16)-25;
	sim.regs[0x26] = 0x0002;
	sim.regs[0x34] = 0x002a;
	sim.regs[0x36] = (u16)(s16)-12;
	sim.regs[0x77] = 0x4120;
	sim.regs[0x78] = 0x0000;
	sim.regs[0x79] = 0xc0a0;
	sim.regs[0x7a] = 0x0000;
	sim.regs[0x7e] = 0x01f4;
}

static int mtk_i2c_set_slow_target(int nr, u16 addr, unsigned int hz)
{
	sim.slow_calls++;
	if (sim.slow_fail_rc)
		return sim.slow_fail_rc;
	sim.slow_nr = nr;
	sim.slow_addr = addr;
	sim.slow_hz = hz;
	return 0;
}

static int imgsensor_power_register_notifier(struct notifier_block *nb)
{
	if (sim.notifier_fail_rc)
		return sim.notifier_fail_rc;
	sim.registered_nb = nb;
	return 0;
}

static int imgsensor_power_unregister_notifier(struct notifier_block *nb)
{
	sim.unregister_calls++;
	if (sim.registered_nb == nb)
		sim.registered_nb = NULL;
	return 0;
}

static bool mod_delayed_work(struct workqueue_struct *wq,
			     struct delayed_work *dwork, unsigned long delay)
{
	(void)wq;
	dwork->scheduled = 1;
	dwork->delay = delay;
	return true;
}

static bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	bool was = dwork->scheduled != 0;
	sim.cancel_calls++;
	dwork->scheduled = 0;
	return was;
}

static void msleep(unsigned int msecs)
{
	sim.msleep_calls++;
	stub_now_ms += msecs;
	if (sim.on_msleep)
		sim.on_msleep();
}

static int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	(void)adap;
	if (num == 2 && msgs[0].flags == 0 && msgs[0].len == 1 &&
	    msgs[1].flags == I2C_M_RD && msgs[1].len == 2) {
		u8 reg = msgs[0].buf[0] & 0x7f;
		sim.read_count++;
		if (sim.nack_reads_left > 0) {
			sim.nack_reads_left--;
			return -ENXIO;
		}
		put_unaligned_be16(sim.regs[reg], msgs[1].buf);
		if (sim.log_count < 128)
			sim.log[sim.log_count++] = (struct xfer_log){0, msgs[0].addr, reg, sim.regs[reg]};
		return 2;
	}
	if (num == 1 && msgs[0].flags == 0 && msgs[0].len == 3) {
		u8 reg = msgs[0].buf[0] & 0x7f;
		u16 val = get_unaligned_be16(&msgs[0].buf[1]);
		if (sim.force_write_err)
			return sim.force_write_err;
		sim.regs[reg] = val;
		if (sim.log_count < 128)
			sim.log[sim.log_count++] = (struct xfer_log){1, msgs[0].addr, reg, val};
		return 1;
	}
	return -EINVAL;
}
