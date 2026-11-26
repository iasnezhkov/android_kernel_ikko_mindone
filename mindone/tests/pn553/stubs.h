// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#ifndef PN553_TEST_STUBS_H
#define PN553_TEST_STUBS_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef long loff_t;
typedef int irqreturn_t;
typedef void *fl_owner_t;

#define __user
#define __init
#define __exit
#define IRQ_HANDLED 1
#define IRQ_NONE 0
#define IRQF_TRIGGER_HIGH 0x00000004
#define IRQF_NO_AUTOEN 0x00040000
#define I2C_FUNC_I2C 1
#define GFP_KERNEL 0
#define MISC_DYNAMIC_MINOR 255
#define THIS_MODULE NULL
#define O_NONBLOCK 0x0800
#define ERESTARTSYS 512
#define EPROBE_DEFER 517
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile typeof(x) *)&(x) = (v))
#define _IOW(t, nr, sz) (0x40000000U | ((unsigned int)(t) << 8) | (nr))
#define _IOR(t, nr, sz) (0x80000000U | ((unsigned int)(t) << 8) | (nr))
#define MODULE_DEVICE_TABLE(a, b)
#define MODULE_DESCRIPTION(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)
#define module_init(x)
#define module_exit(x)
#define DEFINE_SIMPLE_DEV_PM_OPS(name, s, r) const int name = 0
#define pm_sleep_ptr(x) (x)
#define PROBE_PREFER_ASYNCHRONOUS 1
#define GPIOF_IN 1
#define DL_FLAG_AUTOREMOVE_CONSUMER 1

struct device {
	void *drvdata;
	void *of_node;
	bool wakeup;
};
struct i2c_adapter { int dummy; };
struct i2c_client {
	struct device dev;
	struct i2c_adapter *adapter;
	int irq;
	const char *name;
};
struct i2c_device_id { char name[32]; };
struct of_device_id { const char *compatible; };
struct device_driver {
	const char *name;
	const struct of_device_id *of_match_table;
	const void *pm;
	int probe_type;
};
struct i2c_driver {
	int (*probe)(struct i2c_client *);
	void (*remove)(struct i2c_client *);
	const struct i2c_device_id *id_table;
	struct device_driver driver;
};
struct platform_device {
	struct device dev;
};
struct platform_driver {
	int (*probe)(struct platform_device *);
	struct device_driver driver;
};
struct pinctrl { int dummy; };
struct pinctrl_state { const char *name; };
struct gpio_desc { int level; };
struct inode { int dummy; };
struct file {
	void *private_data;
	unsigned int f_flags;
};
struct file_operations {
	void *owner;
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, fl_owner_t);
	int (*release)(struct inode *, struct file *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
};
struct miscdevice {
	int minor;
	const char *name;
	const struct file_operations *fops;
	struct device *parent;
};
typedef struct { int locked; } spinlock_t;
struct mutex { int locked; };
typedef struct { int wake_count; } wait_queue_head_t;
struct device_node { int dummy; };

struct sim_state {
	int irq_level;
	int irq_enable_calls;
	int irq_disable_calls;
	int irq_wake_enable_calls;
	int irq_wake_disable_calls;
	int irq_requested;
	unsigned long irq_req_flags;
	bool irq_req_had_drvdata;
	bool irq_req_had_wakeup;
	int ven_high_calls;
	int ven_low_calls;
	int dwl_high_calls;
	int dwl_low_calls;
	int i2c_recv_calls;
	int i2c_send_calls;
	int i2c_send_fail_first;
	int wake_all_calls;
	int wait_return_rc;
	void (*wait_hook)(void);
	struct pinctrl_state st_ven_high;
	struct pinctrl_state st_ven_low;
	struct pinctrl_state st_dwl_high;
	struct pinctrl_state st_dwl_low;
	struct pinctrl_state st_irq_init;
	struct pinctrl pinctrl;
	struct gpio_desc irq_gpio;
	struct platform_device pins_pdev;
	struct device_node pins_node;
	void *allocs[32];
	int alloc_count;
};

static struct sim_state sim;

static void sim_reset(void)
{
	int i;

	for (i = 0; i < sim.alloc_count; i++)
		free(sim.allocs[i]);
	memset(&sim, 0, sizeof(sim));
	sim.st_ven_high.name = "ven_high";
	sim.st_ven_low.name = "ven_low";
	sim.st_dwl_high.name = "dwn_high";
	sim.st_dwl_low.name = "dwn_low";
	sim.st_irq_init.name = "irq_init";
}

static void *devm_kzalloc(struct device *dev, size_t sz, int gfp)
{
	void *p = calloc(1, sz);
	(void)dev;
	(void)gfp;
	assert(sim.alloc_count < 32);
	sim.allocs[sim.alloc_count++] = p;
	return p;
}

static bool IS_ERR(const void *p) { return (uintptr_t)p >= (uintptr_t)-4095L; }
static long PTR_ERR(const void *p) { return (long)(intptr_t)p; }
static void *ERR_PTR(long e) { return (void *)(intptr_t)e; }
static void mutex_init(struct mutex *m) { m->locked = 0; }
static void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static int mutex_trylock(struct mutex *m) { if (m->locked) return 0; m->locked = 1; return 1; }
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }
static void spin_lock_init(spinlock_t *l) { l->locked = 0; }
#define spin_lock_irqsave(l, f) do { (f) = 0; assert(!(l)->locked); (l)->locked = 1; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); assert((l)->locked); (l)->locked = 0; } while (0)
static void init_waitqueue_head(wait_queue_head_t *q) { q->wake_count = 0; }
static void wake_up(wait_queue_head_t *q) { q->wake_count++; }
static void wake_up_all(wait_queue_head_t *q) { q->wake_count++; sim.wake_all_calls++; }
static void usleep_range(unsigned long a, unsigned long b) { (void)a; (void)b; }
static int gpiod_get_value(struct gpio_desc *g) { (void)g; return sim.irq_level; }
static int gpiod_to_irq(struct gpio_desc *g) { (void)g; return 42; }
static void disable_irq_nosync(int irq) { (void)irq; sim.irq_disable_calls++; }
static void enable_irq(int irq) { (void)irq; sim.irq_enable_calls++; }
static int enable_irq_wake(int irq) { (void)irq; sim.irq_wake_enable_calls++; return 0; }
static int disable_irq_wake(int irq) { (void)irq; sim.irq_wake_disable_calls++; return 0; }
static void device_init_wakeup(struct device *d, bool en) { d->wakeup = en; }
static bool device_may_wakeup(struct device *d) { return d->wakeup; }
static void pm_wakeup_event(struct device *d, unsigned int ms) { (void)d; (void)ms; }
static void i2c_set_clientdata(struct i2c_client *c, void *p) { c->dev.drvdata = p; }
static void *i2c_get_clientdata(struct i2c_client *c) { return c->dev.drvdata; }
static void *dev_get_drvdata(struct device *d) { return d->drvdata; }
static void platform_set_drvdata(struct platform_device *p, void *d) { p->dev.drvdata = d; }
static void *platform_get_drvdata(struct platform_device *p) { return p->dev.drvdata; }
static bool i2c_check_functionality(struct i2c_adapter *a, u32 f) { (void)a; (void)f; return true; }
static int misc_register(struct miscdevice *m) { (void)m; return 0; }
static void misc_deregister(struct miscdevice *m) { (void)m; }
static void put_device(struct device *d) { (void)d; }
static struct device_node *of_find_compatible_node(void *a, void *b, const char *c) { (void)a; (void)b; (void)c; return &sim.pins_node; }
static struct platform_device *of_find_device_by_node(struct device_node *n) { (void)n; return &sim.pins_pdev; }
static void of_node_put(struct device_node *n) { (void)n; }
static int devm_add_action_or_reset(struct device *d, void (*fn)(void *), void *arg) { (void)d; (void)fn; (void)arg; return 0; }
static void *device_link_add(struct device *a, struct device *b, u32 f) { (void)a; (void)b; (void)f; return (void *)1; }
static const char *dev_name(struct device *d) { (void)d; return "sim-dev"; }
static int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *s)
{
	(void)p;
	if (s == &sim.st_ven_high)
		sim.ven_high_calls++;
	else if (s == &sim.st_ven_low)
		sim.ven_low_calls++;
	else if (s == &sim.st_dwl_high)
		sim.dwl_high_calls++;
	else if (s == &sim.st_dwl_low)
		sim.dwl_low_calls++;
	return 0;
}
static struct pinctrl *devm_pinctrl_get(struct device *d) { (void)d; return &sim.pinctrl; }
static struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p, const char *n)
{
	(void)p;
	if (!strcmp(n, "ven_high")) return &sim.st_ven_high;
	if (!strcmp(n, "ven_low")) return &sim.st_ven_low;
	if (!strcmp(n, "dwn_high")) return &sim.st_dwl_high;
	if (!strcmp(n, "dwn_low")) return &sim.st_dwl_low;
	if (!strcmp(n, "irq_init")) return &sim.st_irq_init;
	return ERR_PTR(-ENOENT);
}
static int of_get_named_gpio(void *n, const char *p, int i) { (void)n; (void)p; (void)i; return 10; }
static int devm_gpio_request_one(struct device *d, int g, unsigned long f, const char *l) { (void)d; (void)g; (void)f; (void)l; return 0; }
static struct gpio_desc *gpio_to_desc(int g) { (void)g; return &sim.irq_gpio; }
static int devm_request_irq(struct device *d, int irq, irqreturn_t (*fn)(int, void *),
			    unsigned long flags, const char *name, void *data)
{
	(void)irq; (void)fn; (void)name; (void)data;
	sim.irq_requested = 1;
	sim.irq_req_flags = flags;
	sim.irq_req_had_drvdata = d->drvdata != NULL;
	sim.irq_req_had_wakeup = d->wakeup;
	return 0;
}
static int i2c_master_recv(struct i2c_client *c, u8 *buf, int len)
{
	(void)c;
	sim.i2c_recv_calls++;
	memset(buf, 0xA5, len);
	return len;
}
static int i2c_master_send(struct i2c_client *c, const u8 *buf, int len)
{
	(void)c; (void)buf;
	sim.i2c_send_calls++;
	if (sim.i2c_send_fail_first > 0) {
		sim.i2c_send_fail_first--;
		return -ENXIO;
	}
	return len;
}
static unsigned long copy_to_user(void *to, const void *from, unsigned long n) { memcpy(to, from, n); return 0; }
static unsigned long copy_from_user(void *to, const void *from, unsigned long n) { memcpy(to, from, n); return 0; }
#define put_user(val, ptr) ((*(ptr) = (val)), 0)
static long compat_ptr_ioctl(struct file *f, unsigned int c, unsigned long a) { (void)f; (void)c; (void)a; return 0; }
static int platform_driver_register(struct platform_driver *d) { (void)d; return 0; }
static void platform_driver_unregister(struct platform_driver *d) { (void)d; }
static int i2c_add_driver(struct i2c_driver *d) { (void)d; return 0; }
static void i2c_del_driver(struct i2c_driver *d) { (void)d; }

#define dev_info(d, fmt, ...) do { (void)(d); } while (0)
#define dev_warn_ratelimited(d, fmt, ...) do { (void)(d); } while (0)
#define dev_err_probe(d, r, fmt, ...) ((void)(d), (r))

#define wait_event_interruptible(wq, cond) ({ \
	int __ret = 0; \
	(void)(wq); \
	if (!(cond)) { \
		if (sim.wait_hook) \
			sim.wait_hook(); \
		if (sim.wait_return_rc) \
			__ret = sim.wait_return_rc; \
		else if (!(cond)) \
			__ret = -ETIMEDOUT; \
	} \
	__ret; \
})

#endif
