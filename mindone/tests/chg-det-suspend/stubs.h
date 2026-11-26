// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1U << (n))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p, v) (*(p) = (v))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define GFP_KERNEL 0
#define pr_notice(fmt, ...) do { } while (0)
#define pr_warn(fmt, ...) do { } while (0)

static inline bool IS_ERR(const void *p) { return (uintptr_t)p >= (uintptr_t)-4095; }
static inline long PTR_ERR(const void *p) { return (long)(intptr_t)p; }
static inline void *ERR_PTR(long e) { return (void *)(intptr_t)e; }

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
	struct list_head *prev = head->prev;
	head->prev = n; n->next = head; n->prev = prev; prev->next = n;
}
static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev; entry->prev->next = entry->next;
}
#define list_first_entry(ptr, type, member) container_of((ptr)->next, type, member)
#define list_next_entry(pos, member) container_of((pos)->member.next, typeof(*(pos)), member)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member); \
	     &pos->member != (head); pos = list_next_entry(pos, member))

struct mutex { int locked; int rank; };
static int lock_depth;
static int lock_ranks[8];
#define DEFINE_MUTEX(name) struct mutex name = { 0, 1 }
static inline void mutex_init(struct mutex *m) { m->locked = 0; m->rank = 2; }
static inline void mutex_lock(struct mutex *m)
{
	assert(!m->locked);
	if (lock_depth > 0 && m->rank > 0)
		assert(lock_ranks[lock_depth - 1] < m->rank);
	m->locked = 1;
	lock_ranks[lock_depth++] = m->rank;
}
static inline void mutex_unlock(struct mutex *m)
{
	assert(m->locked && lock_depth > 0);
	lock_depth--;
	m->locked = 0;
}

typedef struct { int locked; } spinlock_t;
static inline void spin_lock_init(spinlock_t *s) { s->locked = 0; }
#define spin_lock_irqsave(s, f) do { (void)(f); assert(!(s)->locked); (s)->locked = 1; } while (0)
#define spin_unlock_irqrestore(s, f) do { (void)(f); assert((s)->locked); (s)->locked = 0; } while (0)

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
struct work_struct { work_func_t func; };
struct delayed_work { struct work_struct work; int scheduled; unsigned long delay; };
static inline struct delayed_work *to_delayed_work(struct work_struct *w)
{
	return container_of(w, struct delayed_work, work);
}
#define INIT_DELAYED_WORK(dw, fn) do { (dw)->work.func = (fn); (dw)->scheduled = 0; (dw)->delay = 0; } while (0)
static struct workqueue_struct { int unused; } stub_system_wq;
#define system_wq (&stub_system_wq)
static inline unsigned long msecs_to_jiffies(unsigned int m) { return m; }

static void *kzalloc(size_t sz, int gfp) { (void)gfp; return calloc(1, sz); }
static void kfree(void *p) { free(p); }

enum power_supply_property {
	POWER_SUPPLY_PROP_ONLINE = 1,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT = 2,
	POWER_SUPPLY_PROP_TYPE = 3,
	POWER_SUPPLY_PROP_USB_TYPE = 4,
	POWER_SUPPLY_PROP_VOLTAGE_NOW = 5,
};
enum power_supply_type { POWER_SUPPLY_TYPE_UNKNOWN = 0, POWER_SUPPLY_TYPE_USB = 4 };
union power_supply_propval { int intval; };
struct power_supply { void *drvdata; };
struct power_supply_desc {
	const char *name;
	enum power_supply_type type;
	const enum power_supply_property *properties;
	size_t num_properties;
	int (*get_property)(struct power_supply *, enum power_supply_property, union power_supply_propval *);
};
struct power_supply_config { void *fwnode; };
static inline void *power_supply_get_drvdata(struct power_supply *p) { return p->drvdata; }

struct device { void *drvdata; };
struct platform_device { struct device dev; };
struct i2c_client { struct device dev; };
static inline void *dev_fwnode(struct device *d) { return d; }
static inline struct i2c_client *to_i2c_client(struct device *d) { return container_of(d, struct i2c_client, dev); }
static inline void *i2c_get_clientdata(struct i2c_client *c) { return c->dev.drvdata; }

struct wakeup_source { int events; unsigned int last_ms; };
struct charger_device { int dummy; };
struct pinctrl { int dummy; };
struct pinctrl_state { int dummy; };
struct regulator_dev { int dummy; };

#define ETA6965_REG_08 0x08
#define REG08_VBUS_STAT_MASK 0xe0
#define REG08_CHRG_STAT_MASK 0x18
#define REG08_PG_STAT_MASK 0x04

static struct {
	unsigned int pmic_0a88;
	int regmap_err;
	int bc12_online_err;
	int bc12_type_err;
	int bc12_online_calls;
	int bc12_changed_calls;
	int settle_calls;
	int ws_holds;
	int ws_registered;
	int ws_unregistered;
	int cancel_calls;
	int psy_changed_calls;
	u8 eta_reg08;
	int eta_io_err;
	int eta_ws_events;
	struct power_supply fake_chr_psy;
	struct wakeup_source fake_chr_ws;
} sim;

static void sim_reset(void)
{
	memset(&sim, 0, sizeof(sim));
	lock_depth = 0;
}

static bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw, unsigned long delay)
{
	(void)wq;
	dw->scheduled = 1;
	dw->delay = delay;
	return true;
}

static bool cancel_delayed_work_sync(struct delayed_work *dw)
{
	bool was = dw->scheduled != 0;
	sim.cancel_calls++;
	dw->scheduled = 0;
	return was;
}

static void power_supply_changed(struct power_supply *psy) { (void)psy; sim.psy_changed_calls++; }
static void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	ws->events++;
	ws->last_ms = msec;
	sim.eta_ws_events++;
}

static struct power_supply *power_supply_register(struct device *d, const struct power_supply_desc *desc, const struct power_supply_config *cfg)
{
	(void)d; (void)desc; (void)cfg;
	return &sim.fake_chr_psy;
}
static void power_supply_unregister(struct power_supply *p) { (void)p; }

static int mindone_chr_boot_tag(void *p, unsigned char *t) { (void)p; (void)t; return 0; }
static int mindone_chr_irq_active(void *p) { (void)p; return 1; }
static void *mindone_chr_iio_get(void *p) { (void)p; return (void *)0x1; }
static int mindone_chr_iio_read(void *c, int *v) { (void)c; *v = 500; return 0; }
static void *mindone_chr_regmap_get(void *p) { (void)p; return (void *)0x2; }
static int mindone_chr_regmap_read(void *m, unsigned int addr, unsigned int *v)
{
	(void)m; assert(addr == 0xa88);
	if (sim.regmap_err) return sim.regmap_err;
	*v = sim.pmic_0a88;
	return 0;
}
static void mindone_chr_regmap_put(void *m) { (void)m; }
static void *mindone_chr_bc12_get(void *p) { (void)p; return (void *)0x3; }
static int mindone_chr_bc12_set_online(void *s, int online)
{
	(void)s; (void)online;
	sim.bc12_online_calls++;
	return sim.bc12_online_err;
}
static int mindone_chr_bc12_type(void *s, int *v)
{
	(void)s;
	if (sim.bc12_type_err) return sim.bc12_type_err;
	*v = POWER_SUPPLY_TYPE_USB;
	return 0;
}
static int mindone_chr_bc12_usb_type(void *s, int *v)
{
	(void)s;
	if (sim.bc12_type_err) return sim.bc12_type_err;
	*v = 1;
	return 0;
}
static int mindone_chr_bc12_changed(void *s, int online)
{
	(void)s; (void)online;
	sim.bc12_changed_calls++;
	return sim.bc12_online_err;
}
static void mindone_chr_bc12_put(void *s) { (void)s; }
static void mindone_chr_settle(void) { sim.settle_calls++; }
static void mindone_chr_power_off(void) {}
static void *mindone_chr_irq_register(void *p, void (*cb)(void *), void *ctx) { (void)p; (void)cb; (void)ctx; return (void *)0x4; }
static void mindone_chr_irq_unregister(void *irq) { (void)irq; }
static void *mindone_chr_ws_register(void) { sim.ws_registered++; return &sim.fake_chr_ws; }
static void mindone_chr_ws_unregister(void *ws) { (void)ws; sim.ws_unregistered++; }
static void mindone_chr_ws_hold(void *ws, unsigned int ms) { (void)ws; (void)ms; sim.ws_holds++; }

struct eta6965_device {
	struct i2c_client *client;
	struct device *dev;
	struct mutex lock;
	struct charger_device *chg_dev;
	bool suspended;
	bool notify_on_resume;
	bool suspend_reg08_valid;
	u8 suspend_reg08;
	struct wakeup_source *ws;
	int usb_input_current_limit;
	struct pinctrl *pinctrl;
	struct pinctrl_state *ce_low;
	struct pinctrl_state *ce_high;
};

struct eta6965_platform {
	struct eta6965_device *eta;
	struct device *supplier;
	struct pinctrl *pinctrl;
	struct regulator_dev *rdev;
	struct power_supply *psy;
	bool active;
};

static DEFINE_MUTEX(eta6965_binding_lock);
static struct eta6965_platform *eta6965_otg_binding;

static int mindone_eta_io_read(void *ctx, u8 reg, u8 *val)
{
	(void)ctx; assert(reg == ETA6965_REG_08);
	if (sim.eta_io_err) return sim.eta_io_err;
	*val = sim.eta_reg08;
	return 0;
}

static int eta6965_psy_read_property(struct eta6965_device *eta, enum power_supply_property prop, int *val)
{
	(void)prop;
	if (READ_ONCE(eta->suspended))
		return -ENODATA;
	*val = 1;
	return 0;
}
