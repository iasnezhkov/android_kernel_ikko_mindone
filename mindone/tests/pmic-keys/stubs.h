/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1) & (~0U >> (32 - 1 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define GFP_KERNEL 0
#define EV_KEY 1
#define BUS_HOST 0x19
#define IRQF_ONESHOT 0x2000
#define IRQF_TRIGGER_HIGH 0x0004
#define fallthrough do {} while (0)
#define MODULE_DEVICE_TABLE(t, n)
#define MODULE_LICENSE(s)
#define MODULE_AUTHOR(s)
#define MODULE_DESCRIPTION(s)
#define DEFINE_SIMPLE_DEV_PM_OPS(name, s, r) \
	const struct dev_pm_ops name = { .suspend = (s), .resume = (r) }
#define pm_sleep_ptr(p) (p)
#define module_platform_driver(d) \
	struct platform_driver *pmic_keys_test_driver(void) { return &(d); }

#define MT6397_CHRSTATUS 0x100
#define MT6397_OCSTATUS2 0x102
#define MT6397_INT_RSV 0x104
#define MT6397_TOP_RST_MISC 0x126
#define MT6323_CHRSTATUS 0x200
#define MT6323_INT_MISC_CON 0x204
#define MT6323_TOP_RST_MISC 0x126
#define MT6331_TOPSTATUS 0x300
#define MT6331_INT_MISC_CON 0x304
#define MT6331_TOP_RST_MISC 0x126
#define MT6357_TOPSTATUS 0x28
#define MT6357_PSC_TOP_INT_CON0 0x910
#define MT6357_TOP_RST_MISC 0x14c
#define MT6358_TOPSTATUS 0x28
#define MT6358_PSC_TOP_INT_CON0 0x910
#define MT6358_TOP_RST_MISC 0x14c
#define MT6359_TOPSTATUS 0x28
#define MT6359_PSC_TOP_INT_CON0 0x910
#define MT6359_TOP_RST_MISC 0x14c

typedef int irqreturn_t;
#define IRQ_HANDLED 1
typedef irqreturn_t (*irq_handler_t)(int, void *);

struct regmap {
	u32 regs[0x1000];
};

struct device_node {
	u32 keycode;
	bool wakeup;
	u32 lp_mode;
	u32 lp_sec;
	int num_children;
	struct device_node *children[4];
};

struct of_device_id {
	const char *compatible;
	const void *data;
};

struct device;

struct dev_pm_ops {
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
};

struct device_driver {
	const char *name;
	const struct of_device_id *of_match_table;
	const struct dev_pm_ops *pm;
};

struct device {
	struct device *parent;
	struct device_node *of_node;
	void *drvdata;
	struct regmap *regmap;
	const char *compatible;
	int wakeup_capable;
	int stay_awake_count;
};

struct platform_device {
	struct device dev;
};

struct platform_driver {
	int (*probe)(struct platform_device *);
	struct device_driver driver;
};

struct input_id {
	unsigned short bustype, vendor, product, version;
};

struct input_dev {
	const char *name;
	struct input_id id;
	int registered;
	unsigned int caps[4];
	int cap_count;
	unsigned int last_code;
	int last_val;
	int report_count;
	int sync_count;
};

static struct {
	irq_handler_t irq_fn[16];
	void *irq_data[16];
	int irq_wake[16];
	struct input_dev input;
	void *allocs[16];
	int alloc_count;
} sim;

static void sim_reset(void)
{
	for (int i = 0; i < sim.alloc_count; i++)
		free(sim.allocs[i]);
	memset(&sim, 0, sizeof(sim));
}

static inline void *dev_get_drvdata(const struct device *d) { return d->drvdata; }
static inline void platform_set_drvdata(struct platform_device *p, void *d) { p->dev.drvdata = d; }
static inline struct regmap *dev_get_regmap(struct device *d, const char *n) { (void)n; return d ? d->regmap : NULL; }

static inline void *devm_kzalloc(struct device *d, size_t sz, int gfp)
{
	void *p = calloc(1, sz);
	(void)d; (void)gfp;
	if (sim.alloc_count < 16)
		sim.allocs[sim.alloc_count++] = p;
	return p;
}

static inline struct input_dev *devm_input_allocate_device(struct device *d)
{
	(void)d;
	memset(&sim.input, 0, sizeof(sim.input));
	return &sim.input;
}

static inline int input_register_device(struct input_dev *i) { i->registered = 1; return 0; }
static inline void input_set_capability(struct input_dev *i, unsigned int t, unsigned int c)
{
	(void)t;
	if (i->cap_count < 4)
		i->caps[i->cap_count++] = c;
}
static inline void input_report_key(struct input_dev *i, unsigned int c, int v)
{
	i->last_code = c;
	i->last_val = v;
	i->report_count++;
}
static inline void input_sync(struct input_dev *i) { i->sync_count++; }

static inline void device_init_wakeup(struct device *d, bool enable) { d->wakeup_capable = enable ? 1 : 0; }
static inline void pm_stay_awake(struct device *d) { d->stay_awake_count++; }
static inline void pm_relax(struct device *d) { d->stay_awake_count--; }

static inline int regmap_read(struct regmap *r, u32 reg, u32 *val)
{
	*val = r->regs[reg];
	return 0;
}

static inline int regmap_update_bits(struct regmap *r, u32 reg, u32 mask, u32 val)
{
	r->regs[reg] = (r->regs[reg] & ~mask) | (val & mask);
	return 0;
}

static inline const struct of_device_id *of_match_device(const struct of_device_id *tbl, const struct device *d)
{
	for (; tbl && tbl->compatible; tbl++) {
		if (d->compatible && strcmp(tbl->compatible, d->compatible) == 0)
			return tbl;
	}
	return NULL;
}

static inline unsigned int of_get_available_child_count(const struct device_node *np)
{
	return (unsigned int)np->num_children;
}

static inline int of_property_read_u32(const struct device_node *np, const char *name, u32 *out)
{
	if (strcmp(name, "power-off-time-sec") == 0) { *out = np->lp_sec; return 0; }
	if (strcmp(name, "mediatek,long-press-mode") == 0) { *out = np->lp_mode; return 0; }
	if (strcmp(name, "linux,keycodes") == 0) { *out = np->keycode; return 0; }
	return -EINVAL;
}

static inline bool of_property_read_bool(const struct device_node *np, const char *name)
{
	if (strcmp(name, "wakeup-source") == 0)
		return np->wakeup;
	return false;
}

#define for_each_child_of_node_scoped(parent, child) \
	for (int __ci = 0; __ci < (parent)->num_children; __ci++) \
		for (struct device_node *child = (parent)->children[__ci], *__once = child; __once; __once = NULL)

static inline int platform_get_irq_byname(struct platform_device *p, const char *name)
{
	(void)p;
	if (strcmp(name, "powerkey") == 0) return 1;
	if (strcmp(name, "homekey") == 0) return 2;
	if (strcmp(name, "powerkey_r") == 0) return 3;
	if (strcmp(name, "homekey_r") == 0) return 4;
	return -ENXIO;
}

static inline int devm_request_threaded_irq(struct device *d, unsigned int irq,
					    irq_handler_t h, irq_handler_t fn,
					    unsigned long flags, const char *name, void *data)
{
	(void)d; (void)h; (void)flags; (void)name;
	sim.irq_fn[irq] = fn;
	sim.irq_data[irq] = data;
	return 0;
}

static inline int enable_irq_wake(unsigned int irq) { sim.irq_wake[irq]++; return 0; }
static inline int disable_irq_wake(unsigned int irq) { sim.irq_wake[irq]--; return 0; }

#define dev_err(d, fmt, ...) do { (void)(d); } while (0)
#define dev_dbg(d, fmt, ...) do { (void)(d); } while (0)

struct platform_driver *pmic_keys_test_driver(void);
