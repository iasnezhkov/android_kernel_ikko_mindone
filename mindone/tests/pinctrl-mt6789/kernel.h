/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef MINDONE_PINCTRL_HARNESS_KERNEL_H
#define MINDONE_PINCTRL_HARNESS_KERNEL_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int spinlock_t;
typedef unsigned int gfp_t;
typedef unsigned long irq_hw_number_t;

#define __iomem
#define __init
#define __exit
#define __maybe_unused __attribute__((unused))
#define unlikely(x) (x)
#define likely(x) (x)
#define BIT(n) (1UL << (n))
#define U16_MAX 0xffff
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ENOTSUPP 524
#define GFP_KERNEL 0
#define __GFP_ZERO 0
#define IS_ENABLED(x) 1
#define IS_ERR(p) ((void)(p), 0)
#define PTR_ERR(p) ((void)(p), 0L)
#define WARN_ON_ONCE(x) (x)
#define WARN_ON(x) (x)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_DEVICE_TABLE(a, b)
#define CONFIG_DEBUG_FS 1
#define CONFIG_PM_SLEEP 1
#define CONFIG_EINT_MTK 1
#define LINUX_VERSION_CODE 0x61220
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define arch_initcall(fn) static int (*const harness_initcall_##fn)(void) __attribute__((unused)) = fn
#define module_init(fn) arch_initcall(fn)
#define pm_sleep_ptr(p) (p)
#define dev_err(dev, ...) ((void)(dev))
#define dev_warn(dev, ...) ((void)(dev))
#define dev_info(dev, ...) ((void)(dev))
#define dev_notice(dev, ...) ((void)(dev))
#define dev_dbg(dev, ...) ((void)(dev))
#define pr_debug(...) ((void)0)
#define spin_lock_init(l) ((void)(l))
#define spin_lock_irqsave(l, f) ((void)(l), (void)(f))
#define spin_unlock_irqrestore(l, f) ((void)(l), (void)(f))
#define of_property_read_bool(np, name) ((void)(np), false)
#define of_property_count_strings(np, name) ((void)(np), 0)
#define of_iomap(np, i) ((void)(np), (void *)0)
#define iounmap(p) ((void)(p))
#define irq_of_parse_and_map(np, i) ((void)(np), 0)
#define devm_kzalloc(dev, size, gfp) calloc(1, (size))
#define devm_kmalloc_array(dev, n, size, gfp) calloc((n), (size))
#define devm_kfree(dev, p) ((void)(p))

struct device_node {
	int unused;
};

struct device {
	struct device_node *of_node;
};

struct platform_device {
	struct device dev;
};

struct gpio_chip {
	int base;
	unsigned int ngpio;
};

struct dev_pm_ops {
	int unused;
};

struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

struct device_driver {
	const char *name;
	const struct of_device_id *of_match_table;
	const struct dev_pm_ops *pm;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	struct device_driver driver;
};

enum pinctrl_map_type {
	PIN_MAP_TYPE_INVALID,
	PIN_MAP_TYPE_DUMMY_STATE,
	PIN_MAP_TYPE_MUX_GROUP,
	PIN_MAP_TYPE_CONFIGS_PIN,
	PIN_MAP_TYPE_CONFIGS_GROUP,
};

struct irq_domain;
struct irq_data;
struct pinctrl_dev;
struct pinctrl_gpio_range;
struct pinctrl_map;
struct seq_file;
struct group_desc;
struct function_desc;
struct pinfunction;
struct pingroup;
struct fwnode_handle;

u32 harness_readl(const volatile void *addr);
void harness_writel(u32 value, volatile void *addr);
void *harness_drvdata(struct pinctrl_dev *pctldev);

#define readl_relaxed(a) harness_readl(a)
#define readl(a) harness_readl(a)
#define writel_relaxed(v, a) harness_writel((v), (a))
#define writel(v, a) harness_writel((v), (a))
#define pinctrl_dev_get_drvdata(p) harness_drvdata(p)

static inline int platform_driver_register(struct platform_driver *drv)
{
	(void)drv;
	return 0;
}

static inline int get_count_order(unsigned int count)
{
	int order;

	if (!count)
		return -1;
	order = 31 - __builtin_clz(count);
	if (count & (count - 1))
		order++;
	return order;
}

static inline int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int n;

	if (!size)
		return 0;
	va_start(args, fmt);
	n = vsnprintf(buf, size, fmt, args);
	va_end(args);
	if (n < 0)
		return 0;
	return (size_t)n >= size ? (int)size - 1 : n;
}

#include "pinconf-generic-gen.h"

#endif
