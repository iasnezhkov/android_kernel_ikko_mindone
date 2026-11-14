/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef SCPSYS_MT6789_STUBS_H
#define SCPSYS_MT6789_STUBS_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint64_t resource_size_t;

#define __iomem
#define __init
#define __exit
#define __maybe_unused __attribute__((unused))
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof(*(p)->member) * (n))
#define USEC_PER_SEC 1000000L
#define IS_ENABLED(x) 1
#define CONFIG_PM 1
#define WARN_ON(x) harness_warn_on(!!(x), #x)
#define unlikely(x) (x)
#define likely(x) (x)
#define EXPORT_SYMBOL(s) extern int harness_unused_declaration
#define EXPORT_SYMBOL_GPL(s) extern int harness_unused_declaration
#define MODULE_LICENSE(s) extern int harness_unused_declaration
#define MODULE_DESCRIPTION(s) extern int harness_unused_declaration
#define MODULE_AUTHOR(s) extern int harness_unused_declaration
#define MODULE_DEVICE_TABLE(t, n) extern int harness_unused_declaration
#define THIS_MODULE NULL
#define GFP_KERNEL 0
#define MAX_ERRNO 4095
#define of_match_ptr(p) (p)
#define IORESOURCE_MEM 0x200

int harness_warn_on(int cond, const char *text);

static inline void *ERR_PTR(long error)
{
	return (void *)error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

static inline bool IS_ERR(const void *ptr)
{
	return (unsigned long)ptr >= (unsigned long)-MAX_ERRNO;
}

static inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR(ptr);
}

static inline void *ERR_CAST(const void *ptr)
{
	return (void *)ptr;
}

void harness_log(int level, const char *fmt, ...);

#define pr_err(...) harness_log(3, __VA_ARGS__)
#define pr_warn(...) harness_log(4, __VA_ARGS__)
#define pr_notice(...) harness_log(5, __VA_ARGS__)
#define pr_info(...) harness_log(6, __VA_ARGS__)
#define dev_err(dev, ...) harness_log(3, __VA_ARGS__)
#define dev_warn(dev, ...) harness_log(4, __VA_ARGS__)
#define dev_notice(dev, ...) harness_log(5, __VA_ARGS__)
#define dev_info(dev, ...) harness_log(6, __VA_ARGS__)
#define dev_dbg(dev, ...) harness_log(7, __VA_ARGS__)

struct device;
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...);
void *dev_err_cast_probe(const struct device *dev, const void *err_ptr, const char *fmt, ...);

struct property {
	const char *name;
	int count;
	const char *const *strings;
	const u32 *cells;
	struct property *next;
};

struct device_node {
	const char *name;
	const char *full_name;
	struct device_node *parent;
	struct device_node *child;
	struct device_node *sibling;
	struct device_node *allnext;
	struct property *properties;
	int index;
	const char *label;
	bool disabled;
};

struct device_driver {
	const char *name;
	void *owner;
	bool suppress_bind_attrs;
	const struct of_device_id *of_match_table;
};

struct device {
	struct device_node *of_node;
	struct device *parent;
	struct device_driver *driver;
};

struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

struct resource {
	resource_size_t start;
	resource_size_t end;
};

struct platform_device {
	const char *name;
	struct device dev;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	struct device_driver driver;
};

#define module_platform_driver(drv) struct platform_driver *const harness_driver = &drv
#define builtin_platform_driver(drv) struct platform_driver *const harness_driver = &drv

const void *of_device_get_match_data(const struct device *dev);
struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res);

void *devm_kzalloc(struct device *dev, size_t size, int gfp);
void *devm_kcalloc(struct device *dev, size_t n, size_t size, int gfp);
void *devm_kmalloc_array(struct device *dev, size_t n, size_t size, int gfp);

u32 readl(const volatile void __iomem *addr);
void writel(u32 value, volatile void __iomem *addr);
void udelay(unsigned long us);
void usleep_range(unsigned long min, unsigned long max);

#define HARNESS_POLL_LIMIT 2000

#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us) ({ \
	int __ret = 0; \
	unsigned long __n = 0; \
	for (;;) { \
		(val) = op(addr); \
		if (cond) { \
			harness_poll_done(); \
			break; \
		} \
		if (++__n > HARNESS_POLL_LIMIT) { \
			(val) = op(addr); \
			__ret = (cond) ? 0 : -ETIMEDOUT; \
			harness_poll_timeout(__ret, #op); \
			break; \
		} \
	} \
	__ret; \
})
#define readx_poll_timeout_atomic readx_poll_timeout
#define readl_poll_timeout_atomic(addr, val, cond, d, t) readx_poll_timeout(readl, addr, val, cond, d, t)
#define readl_poll_timeout(addr, val, cond, d, t) readx_poll_timeout(readl, addr, val, cond, d, t)

void harness_poll_timeout(int ret, const char *what);
void harness_poll_done(void);

struct regmap;
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val);

static inline int regmap_set_bits(struct regmap *map, unsigned int reg, unsigned int bits)
{
	return regmap_update_bits(map, reg, bits, bits);
}

static inline int regmap_clear_bits(struct regmap *map, unsigned int reg, unsigned int bits)
{
	return regmap_update_bits(map, reg, bits, 0);
}

#define regmap_read_poll_timeout(map, addr, val, cond, sleep_us, timeout_us) ({ \
	int __ret = 0; \
	unsigned long __n = 0; \
	unsigned int __v; \
	for (;;) { \
		regmap_read((map), (addr), &__v); \
		(val) = __v; \
		if (cond) { \
			harness_poll_done(); \
			break; \
		} \
		if (++__n > HARNESS_POLL_LIMIT) { \
			__ret = -ETIMEDOUT; \
			harness_poll_timeout(__ret, "regmap"); \
			break; \
		} \
	} \
	__ret; \
})
#define regmap_read_poll_timeout_atomic regmap_read_poll_timeout

struct regmap *syscon_node_to_regmap(struct device_node *np);
struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np, const char *property);
struct regmap *device_node_to_regmap(struct device_node *np);

struct clk;
struct clk_bulk_data {
	const char *id;
	struct clk *clk;
};

struct clk *devm_clk_get(struct device *dev, const char *id);
struct clk *of_clk_get(struct device_node *np, int index);
unsigned int of_clk_get_parent_count(const struct device_node *np);
int clk_prepare_enable(struct clk *clk);
void clk_disable_unprepare(struct clk *clk);
int clk_bulk_prepare_enable(int num, const struct clk_bulk_data *clks);
void clk_bulk_disable_unprepare(int num, const struct clk_bulk_data *clks);
void clk_bulk_put(int num, struct clk_bulk_data *clks);

struct regulator;
struct regulator *devm_regulator_get_optional(struct device *dev, const char *id);
struct regulator *devm_of_regulator_get_optional(struct device *dev, struct device_node *node, const char *id);
int regulator_enable(struct regulator *r);
int regulator_disable(struct regulator *r);
int regulator_is_enabled(struct regulator *r);

struct device_node *of_node_get(struct device_node *np);
void of_node_put(struct device_node *np);
struct property *of_find_property(const struct device_node *np, const char *name, int *lenp);
int of_property_read_u32(const struct device_node *np, const char *name, u32 *value);
struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index);
int of_count_phandle_with_args(const struct device_node *np, const char *list, const char *cells);
struct device_node *of_find_node_with_property(struct device_node *from, const char *name);
struct device_node *of_get_next_child(const struct device_node *np, struct device_node *prev);
struct device_node *of_get_next_available_child(const struct device_node *np, struct device_node *prev);
const char *of_prop_next_string(struct property *prop, const char *cur);

#define for_each_child_of_node(parent, child) \
	for (child = of_get_next_child(parent, NULL); child; child = of_get_next_child(parent, child))
#define for_each_available_child_of_node(parent, child) \
	for (child = of_get_next_available_child(parent, NULL); child; \
	     child = of_get_next_available_child(parent, child))
#define of_property_for_each_string(np, propname, prop, s) \
	for (prop = of_find_property(np, propname, NULL), s = of_prop_next_string(prop, NULL); s; \
	     s = of_prop_next_string(prop, s))

#define GENPD_FLAG_ALWAYS_ON BIT(2)
#define GENPD_FLAG_ACTIVE_WAKEUP BIT(3)

struct dev_power_governor;

struct generic_pm_domain {
	const char *name;
	unsigned int flags;
	int (*power_off)(struct generic_pm_domain *domain);
	int (*power_on)(struct generic_pm_domain *domain);
	int (*set_performance_state)(struct generic_pm_domain *genpd, unsigned int state);
};

struct genpd_onecell_data {
	struct generic_pm_domain **domains;
	unsigned int num_domains;
};

int pm_genpd_init(struct generic_pm_domain *genpd, struct dev_power_governor *gov, bool is_off);
int pm_genpd_add_subdomain(struct generic_pm_domain *genpd, struct generic_pm_domain *subdomain);
int pm_genpd_remove(struct generic_pm_domain *genpd);
int of_genpd_add_provider_onecell(struct device_node *np, struct genpd_onecell_data *data);

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
};

struct blocking_notifier_head {
	int count;
};

#define BLOCKING_NOTIFIER_HEAD(name) struct blocking_notifier_head name
int blocking_notifier_chain_register(struct blocking_notifier_head *nh, struct notifier_block *nb);
int blocking_notifier_chain_unregister(struct blocking_notifier_head *nh, struct notifier_block *nb);
int blocking_notifier_call_chain(struct blocking_notifier_head *nh, unsigned long val, void *v);

#endif
