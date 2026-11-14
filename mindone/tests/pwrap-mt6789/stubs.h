/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef PWRAP_MT6789_STUBS_H
#define PWRAP_MT6789_STUBS_H

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
typedef uint64_t resource_size_t;

#define __iomem
#define __init
#define __exit
#define __maybe_unused __attribute__((unused))
#define fallthrough __attribute__((fallthrough))
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define IS_BUILTIN(x) 0
#define HZ 100
#define U16_MAX 0xffff
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
#define IRQF_TRIGGER_HIGH 0x4
#define IRQ_HANDLED 1

typedef int irqreturn_t;
typedef irqreturn_t (*irq_handler_t)(int irq, void *dev_id);

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

void harness_log(int level, const char *fmt, ...);

#define pr_err(...) harness_log(3, __VA_ARGS__)
#define pr_notice(...) harness_log(5, __VA_ARGS__)
#define pr_info(...) harness_log(6, __VA_ARGS__)
#define dev_err(dev, ...) harness_log(3, __VA_ARGS__)
#define dev_warn(dev, ...) harness_log(4, __VA_ARGS__)
#define dev_notice(dev, ...) harness_log(5, __VA_ARGS__)
#define dev_info(dev, ...) harness_log(6, __VA_ARGS__)
#define dev_dbg(dev, ...) harness_log(7, __VA_ARGS__)

struct ratelimit_state {
	int unused;
};

#define DEFINE_RATELIMIT_STATE(name, interval, burst) struct ratelimit_state name
int __ratelimit(struct ratelimit_state *rs);

unsigned long harness_jiffies(void);
#define jiffies harness_jiffies()
#define usecs_to_jiffies(us) ((unsigned long)(us) / 100 + 1)
#define time_after(a, b) ((long)((b) - (a)) < 0)

struct device;
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...);

struct device_node {
	const char *name;
	const char *const *compatible;
	int ncompatible;
	struct device_node *child;
};

struct device {
	struct device_node *of_node;
	const void *match_data;
	void *drvdata;
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
	const char *name;
};

struct platform_device {
	const char *name;
	struct device dev;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	struct {
		const char *name;
		void *owner;
		const struct of_device_id *of_match_table;
	} driver;
};

#define module_platform_driver(drv) struct platform_driver *const harness_driver = &drv
#define postcore_initcall(fn) extern int harness_unused_declaration
int platform_driver_register(struct platform_driver *drv);

static inline void platform_set_drvdata(struct platform_device *pdev, void *data)
{
	pdev->dev.drvdata = data;
}

const void *of_device_get_match_data(const struct device *dev);
const struct of_device_id *of_match_node(const struct of_device_id *matches, const struct device_node *node);
struct resource *platform_get_resource_byname(struct platform_device *pdev, unsigned int type, const char *name);
void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res);
void __iomem *devm_platform_ioremap_resource_byname(struct platform_device *pdev, const char *name);
int platform_get_irq(struct platform_device *pdev, unsigned int num);
int devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler, unsigned long flags,
		     const char *name, void *dev_id);
void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);
int of_platform_populate(struct device_node *root, const void *matches, const void *lookup, struct device *parent);
void of_platform_depopulate(struct device *parent);
void *devm_kzalloc(struct device *dev, size_t size, int gfp);

struct reset_control;
struct reset_control *devm_reset_control_get(struct device *dev, const char *id);
int reset_control_reset(struct reset_control *rstc);

struct clk;
struct clk_bulk_data {
	const char *id;
	struct clk *clk;
};

struct clk *devm_clk_get(struct device *dev, const char *id);
int clk_prepare_enable(struct clk *clk);
void clk_disable_unprepare(struct clk *clk);
int devm_clk_bulk_get_all_enabled(struct device *dev, struct clk_bulk_data **clks);

u32 readl(const volatile void __iomem *addr);
void writel(u32 value, volatile void __iomem *addr);
void udelay(unsigned long us);
void msleep(unsigned int ms);
void usleep_range(unsigned long min, unsigned long max);

#define HARNESS_POLL_LIMIT 2000

#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us) ({ \
	int __ret = 0; \
	unsigned long __n = 0; \
	for (;;) { \
		(val) = op(addr); \
		if (cond) \
			break; \
		if (++__n > HARNESS_POLL_LIMIT) { \
			(val) = op(addr); \
			__ret = (cond) ? 0 : -ETIMEDOUT; \
			break; \
		} \
	} \
	__ret; \
})

struct regmap;
struct regmap_config {
	int reg_bits;
	int val_bits;
	int reg_stride;
	int (*reg_read)(void *context, unsigned int reg, unsigned int *val);
	int (*reg_write)(void *context, unsigned int reg, unsigned int val);
	unsigned int max_register;
};

struct regmap *devm_regmap_init(struct device *dev, const void *bus, void *bus_context,
				const struct regmap_config *config);

#endif
