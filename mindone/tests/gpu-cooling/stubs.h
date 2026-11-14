/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IS_ENABLED(x) 0
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define unlikely(x) (x)
#define likely(x) (x)
#define __maybe_unused __attribute__((unused))
#define __iomem
#define GFP_KERNEL 0
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))
#define BUG_ON(x) assert(!(x))
#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#define ERR_PTR(e) ((void *)(long)(e))
#define PTR_ERR(p) ((long)(p))
#define IS_ERR(p) IS_ERR_VALUE(p)

void event(const char *name);
void get_random_bytes(void *buf, size_t len);
#define pr_info(...) event("log")
#define pr_debug(...) ((void)0)

struct mutex { int held; };
#define DEFINE_MUTEX(name) struct mutex name = { 0 }
static inline void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static inline void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }

struct device_node { const char *name; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct regulator;
struct clk;

struct thermal_cooling_device;
struct thermal_cooling_device_ops {
	int (*get_max_state)(struct thermal_cooling_device *, unsigned long *);
	int (*get_cur_state)(struct thermal_cooling_device *, unsigned long *);
	int (*set_cur_state)(struct thermal_cooling_device *, unsigned long);
	int (*get_requested_power)(struct thermal_cooling_device *, uint32_t *);
	int (*state2power)(struct thermal_cooling_device *, unsigned long, uint32_t *);
	int (*power2state)(struct thermal_cooling_device *, uint32_t, unsigned long *);
};
struct thermal_cooling_device {
	const char *type;
	void *devdata;
	struct device_node *np;
	const struct thermal_cooling_device_ops *ops;
	unsigned long max_state;
};
struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node *np, const char *type,
	void *devdata, const struct thermal_cooling_device_ops *ops);
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev);
