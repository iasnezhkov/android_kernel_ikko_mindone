/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef LVTS_MT6789_STUBS_H
#define LVTS_MT6789_STUBS_H

#include <errno.h>
#include <limits.h>
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
typedef int64_t s64;

#define __iomem
#define __init
#define __exit
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (64 - 1 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_AUTHOR(s)
#define MODULE_DESCRIPTION(s)
#define MODULE_LICENSE(s)
#define module_platform_driver(drv) \
	struct platform_driver *lvts_test_driver(void) { return &(drv); }
#define NOIRQ_SYSTEM_SLEEP_PM_OPS(s, r) .suspend_noirq = (s), .resume_noirq = (r),

#define GFP_KERNEL 0
#define IRQF_ONESHOT 0x2000
#define THERMAL_TEMP_INVALID (-274000)

enum thermal_trip_type {
	THERMAL_TRIP_ACTIVE = 0,
	THERMAL_TRIP_PASSIVE,
	THERMAL_TRIP_HOT,
	THERMAL_TRIP_CRITICAL,
};

enum thermal_notify_event {
	THERMAL_TRIP_VIOLATED = 1,
};

typedef int irqreturn_t;
#define IRQ_NONE 0
#define IRQ_HANDLED 1

typedef irqreturn_t (*irq_handler_t)(int, void *);

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return (void *)error; }
static inline long PTR_ERR(const void *ptr) { return (long)ptr; }
static inline bool IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }

static inline s64 div_s64(s64 dividend, int divisor) { return dividend / divisor; }
static inline u64 div_u64(u64 dividend, u32 divisor) { return dividend / divisor; }

#define MT7988_CPU_0 0
#define MT7988_CPU_1 1
#define MT7988_ETH2P5G_0 2
#define MT7988_ETH2P5G_1 3
#define MT7988_TOPS_0 4
#define MT7988_TOPS_1 5
#define MT7988_ETHWARP_0 6
#define MT7988_ETHWARP_1 7
#define MT8186_LITTLE_CPU0 0
#define MT8186_LITTLE_CPU1 1
#define MT8186_LITTLE_CPU2 2
#define MT8186_CAM 3
#define MT8186_BIG_CPU0 4
#define MT8186_BIG_CPU1 5
#define MT8186_NNA 6
#define MT8186_ADSP 7
#define MT8186_GPU 8
#define MT8188_MCU_LITTLE_CPU0 0
#define MT8188_MCU_LITTLE_CPU1 1
#define MT8188_MCU_LITTLE_CPU2 2
#define MT8188_MCU_LITTLE_CPU3 3
#define MT8188_MCU_BIG_CPU0 4
#define MT8188_MCU_BIG_CPU1 5
#define MT8188_AP_APU 0
#define MT8188_AP_GPU0 1
#define MT8188_AP_GPU1 2
#define MT8188_AP_ADSP 3
#define MT8188_AP_VDO 4
#define MT8188_AP_INFRA 5
#define MT8188_AP_CAM1 6
#define MT8188_AP_CAM2 7
#define MT8192_MCU_BIG_CPU0 0
#define MT8192_MCU_BIG_CPU1 1
#define MT8192_MCU_BIG_CPU2 2
#define MT8192_MCU_BIG_CPU3 3
#define MT8192_MCU_LITTLE_CPU0 4
#define MT8192_MCU_LITTLE_CPU1 5
#define MT8192_MCU_LITTLE_CPU2 6
#define MT8192_MCU_LITTLE_CPU3 7
#define MT8192_AP_VPU0 0
#define MT8192_AP_VPU1 1
#define MT8192_AP_GPU0 2
#define MT8192_AP_GPU1 3
#define MT8192_AP_INFRA 4
#define MT8192_AP_CAM 5
#define MT8192_AP_MD0 6
#define MT8192_AP_MD1 7
#define MT8192_AP_MD2 8
#define MT8195_MCU_BIG_CPU0 0
#define MT8195_MCU_BIG_CPU1 1
#define MT8195_MCU_BIG_CPU2 2
#define MT8195_MCU_BIG_CPU3 3
#define MT8195_MCU_LITTLE_CPU0 4
#define MT8195_MCU_LITTLE_CPU1 5
#define MT8195_MCU_LITTLE_CPU2 6
#define MT8195_MCU_LITTLE_CPU3 7
#define MT8195_AP_VPU0 0
#define MT8195_AP_VPU1 1
#define MT8195_AP_GPU0 2
#define MT8195_AP_GPU1 3
#define MT8195_AP_VDEC 4
#define MT8195_AP_IMG 5
#define MT8195_AP_INFRA 6
#define MT8195_AP_CAM0 7
#define MT8195_AP_CAM1 8

struct device_node {
	int dummy;
};

struct property {
	int idx;
};

struct of_device_id {
	const char *compatible;
	const void *data;
};

struct device;

struct dev_pm_ops {
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
};

struct device_driver {
	const char *name;
	const struct of_device_id *of_match_table;
	const struct dev_pm_ops *pm;
};

struct device {
	struct device_node *of_node;
	const void *match_data;
	void *drvdata;
	const char *name;
};

struct platform_device {
	struct device dev;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	struct device_driver driver;
};

struct resource {
	u32 start;
	u32 end;
};

struct clk {
	int enabled;
	int enable_calls;
	int disable_calls;
};

struct reset_control {
	int assert_calls;
	int deassert_calls;
	int asserted;
};

struct nvmem_cell {
	int idx;
};

struct thermal_trip {
	enum thermal_trip_type type;
	int temperature;
	int hysteresis;
};

struct thermal_zone_device;

struct thermal_zone_device_ops {
	int (*get_temp)(struct thermal_zone_device *tz, int *temp);
	int (*set_trips)(struct thermal_zone_device *tz, int low, int high);
	int (*set_trip_temp)(struct thermal_zone_device *tz,
			     const struct thermal_trip *trip, int temp);
};

struct thermal_zone_device {
	int dt_id;
	void *priv;
	const struct thermal_zone_device_ops *ops;
	int updates;
	char type[32];
};

static inline void *dev_get_drvdata(const struct device *dev) { return dev->drvdata; }
static inline void dev_set_drvdata(struct device *dev, void *data) { dev->drvdata = data; }
static inline void *platform_get_drvdata(const struct platform_device *pdev) { return pdev->dev.drvdata; }
static inline void platform_set_drvdata(struct platform_device *pdev, void *data) { pdev->dev.drvdata = data; }
static inline struct device_node *dev_of_node(struct device *dev) { return dev->of_node; }
static inline const char *dev_name(const struct device *dev) { return dev->name ? dev->name : "lvts"; }
static inline const void *of_device_get_match_data(const struct device *dev) { return dev->match_data; }
static inline void *thermal_zone_device_priv(struct thermal_zone_device *tz) { return tz->priv; }
static inline const char *thermal_zone_device_type(struct thermal_zone_device *tz) { return tz->type; }

#define dev_info(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_dbg(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_err(dev, fmt, ...) do { (void)(dev); } while (0)
#define pr_debug(fmt, ...) do { } while (0)
#define dev_err_probe(dev, err, fmt, ...) ({ (void)(dev); (err); })

typedef struct { int locked; } spinlock_t;
static inline void spin_lock_init(spinlock_t *l) { l->locked = 0; }
#define spin_lock_irqsave(l, f) do { (void)(f); (l)->locked++; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); (l)->locked--; } while (0)

void *devm_kzalloc(struct device *dev, size_t size, int gfp);
void *devm_krealloc(struct device *dev, void *ptr, size_t size, int gfp);
void devm_kfree(struct device *dev, const void *ptr);
void kfree(const void *ptr);
int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data);

u32 lvts_model_readl(const void __iomem *addr);
void lvts_model_writel(u32 val, void __iomem *addr);
#define readl(a) lvts_model_readl(a)
#define writel(v, a) lvts_model_writel((v), (a))

void udelay(unsigned long us);
void usleep_range(unsigned long min, unsigned long max);

#define readl_poll_timeout(addr, val, cond, delay_us, timeout_us) ({ \
	int __rc = -ETIMEDOUT; \
	unsigned long __t = 0; \
	for (;;) { \
		(val) = readl(addr); \
		if (cond) { __rc = 0; break; } \
		if (__t >= (timeout_us)) break; \
		if (delay_us) udelay(delay_us); \
		__t += (delay_us) ? (delay_us) : 1; \
	} \
	__rc; \
})

#define readl_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	readl_poll_timeout(addr, val, cond, delay_us, timeout_us)

struct clk *devm_clk_get_enabled(struct device *dev, const char *id);
int clk_prepare_enable(struct clk *clk);
void clk_disable_unprepare(struct clk *clk);
struct resource *platform_get_mem_or_io(struct platform_device *pdev, unsigned int num);
void __iomem *devm_platform_get_and_ioremap_resource(struct platform_device *pdev,
						     unsigned int index,
						     struct resource **res);
struct reset_control *devm_reset_control_get_by_index(struct device *dev, int index);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);
int platform_get_irq(struct platform_device *pdev, unsigned int num);
int devm_request_threaded_irq(struct device *dev, unsigned int irq,
			      irq_handler_t handler, irq_handler_t thread_fn,
			      unsigned long irqflags, const char *devname, void *dev_id);

const char *lvts_model_next_cell(int *idx);
#define of_property_for_each_string(np, propname, prop, s) \
	for ((prop) = (struct property *)0, (s) = lvts_model_next_cell(NULL); \
	     (s) != NULL; \
	     (s) = lvts_model_next_cell((int *)1))

struct nvmem_cell *of_nvmem_cell_get(struct device_node *np, const char *name);
void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len);
void nvmem_cell_put(struct nvmem_cell *cell);

struct thermal_zone_device *devm_thermal_of_zone_register(
	struct device *dev, int sensor_id, void *data,
	const struct thermal_zone_device_ops *ops);
void thermal_zone_device_update(struct thermal_zone_device *tz,
				enum thermal_notify_event event);

struct platform_driver *lvts_test_driver(void);

#endif
