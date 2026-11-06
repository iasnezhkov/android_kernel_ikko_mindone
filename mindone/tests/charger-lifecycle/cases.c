// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define WARN_ON_ONCE(n) ((void)(n))
#define pr_info(...) ((void)0)
#define dev_info(...) ((void)0)

typedef uint32_t u32;
struct device_node { const char *name; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; void *data; };
struct thermal_cooling_device { int active; };
struct power_supply { int refs; };
#include "types.h"

static int failures, checks, fault, configuration, node_refs, group;
static int property_error, node_absent, lock_depth;
static struct power_supply master, slave;
static struct thermal_cooling_device thermal;
static struct device_node node = { "charger-cooler" };
static struct charger_cooler_info charger_cl_data;
static int charger_cooler_lock, charger_cooler_attr_group, charger_cooling_ops;
static void *kernel_kobj;
static void *allocations[16];
static int allocation_count;
static char events[64];
static size_t event_count;
static void (*unregister_hook)(void);

static void check(bool value, const char *label)
{
	checks++;
	if (!value) {
		failures++;
		fprintf(stderr, "FAIL fault=%d config=%d %s\n", fault, configuration, label);
	}
}

static void event(char value)
{
	if (event_count < sizeof(events) - 1)
		events[event_count++] = value;
	events[event_count] = 0;
}

static void mutex_lock(int *lock) { (void)lock; lock_depth++; }
static void mutex_unlock(int *lock) { (void)lock; lock_depth--; }
static int charger_throttle(struct charger_cooling_device *cdev, unsigned long state)
{
	(void)cdev;
	(void)state;
	return 0;
}
static int state_to_limit(struct charger_cooling_device *cdev) { (void)cdev; return 0; }
static const struct charger_cooling_platform_data pdata = { .state_to_charger_limit = state_to_limit };
static const void *of_device_get_match_data(struct device *dev) { (void)dev; return &pdata; }
static void *devm_kzalloc(struct device *dev, size_t size, int flags)
{
	(void)dev;
	(void)flags;
	if (fault == 1)
		return NULL;
	void *p = calloc(1, size);
	if (!p || allocation_count == 16)
		abort();
	allocations[allocation_count++] = p;
	return p;
}
static struct device_node *of_find_compatible_node(void *a, void *b, const char *name)
{
	(void)a;
	(void)b;
	(void)name;
	if (node_absent)
		return NULL;
	node_refs++;
	return &node;
}
static int of_property_read_u32(struct device_node *np, const char *name, u32 *value)
{
	(void)name;
	if (!np || property_error)
		return -EINVAL;
	*value = configuration;
	return 0;
}
static void of_node_put(struct device_node *np) { if (np) node_refs--; }
static struct power_supply *power_supply_get_by_name(const char *name)
{
	bool secondary = strcmp(name, "mtk-slave-charger") == 0;
	if (fault == (secondary ? 4 : 2))
		return NULL;
	if (fault == (secondary ? 5 : 3))
		return ERR_PTR(-EIO);
	struct power_supply *p = secondary ? &slave : &master;
	p->refs++;
	return p;
}
static void power_supply_put(struct power_supply *p)
{
	check(p == &master || p == &slave, "valid supply release");
	if (p != &master && p != &slave)
		return;
	check(p->refs > 0, "no double put");
	p->refs--;
	event(p == &master ? 'm' : 's');
}
static int sysfs_create_group(void *kobj, int *attrs)
{
	(void)kobj;
	(void)attrs;
	if (fault == 6)
		return -ENOMEM;
	if (group)
		return -EEXIST;
	group = 1;
	return 0;
}
static void sysfs_remove_group(void *kobj, int *attrs)
{
	(void)kobj;
	(void)attrs;
	check(group == 1, "remove owned group");
	group = 0;
	event('g');
}
static struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node *np,
	const char *name, void *data, int *ops)
{
	(void)np;
	(void)name;
	(void)data;
	(void)ops;
	if (fault == 7 || fault == 8)
		return ERR_PTR(fault == 7 ? -ENOMEM : -EPROBE_DEFER);
	thermal.active++;
	return &thermal;
}
static void thermal_cooling_device_unregister(struct thermal_cooling_device *p)
{
	if (unregister_hook) {
		void (*hook)(void) = unregister_hook;
		unregister_hook = NULL;
		hook();
	}
	check(p == &thermal && thermal.active == 1, "unregister owned cooling device");
	check(master.refs == 1 && (!configuration || slave.refs == 1), "providers survive thermal unregister");
	thermal.active--;
	event('t');
}
static void platform_set_drvdata(struct platform_device *pdev, void *p) { pdev->data = p; }
static void *platform_get_drvdata(struct platform_device *pdev) { return pdev->data; }
#include "functions.h"

static void concurrent_probe(void)
{
	struct platform_device pdev = { .dev.of_node = &node };
	charger_cl_data.cur_current = 700000;
	check(charger_cooling_probe(&pdev) == -EEXIST, "singleton admission held through thermal retirement");
	check(charger_cl_data.cur_current == 700000, "retiring instance diagnostics preserved");
	check(!pdev.data && thermal.active == 1 && group == 1, "concurrent probe cannot publish");
}

static void reset(int type, int failure)
{
	for (int i = 0; i < allocation_count; i++)
		free(allocations[i]);
	allocation_count = 0;
	configuration = type;
	fault = failure;
	node_refs = group = master.refs = slave.refs = thermal.active = 0;
	property_error = node_absent = lock_depth = 0;
	event_count = 0;
	events[0] = 0;
	unregister_hook = NULL;
	charger_cl_data.request_dirty = true;
}

int main(void)
{
	for (int type = 0; type <= 1; type++) {
		for (int failure = 0; failure <= 8; failure++) {
			if (!type && (failure == 4 || failure == 5))
				continue;
			reset(type, failure);
			struct platform_device pdev = { .dev.of_node = &node };
			int expected = failure == 0 ? 0 : failure == 2 || failure == 4 || failure == 8 ?
				-EPROBE_DEFER : failure == 3 || failure == 5 ? -EIO : -ENOMEM;
			check(charger_cooling_probe(&pdev) == expected, "probe errno");
			check(node_refs == 0, "OF reference balanced");
			check(lock_depth == 0, "mutex balanced");
			if (!failure) {
				check(pdev.data && group == 1 && thermal.active == 1, "published resources");
				check(master.refs == 1 && slave.refs == type, "retained provider references");
				check(!charger_cl_data.request_dirty && charger_cl_data.cur_current == -1, "fresh diagnostic state");
				charger_cooling_remove(&pdev);
				check(strcmp(events, type ? "tgsm" : "tgm") == 0, "remove drains interfaces before provider puts");
			}
			check(!pdev.data && !group && !thermal.active && !master.refs && !slave.refs, "all resources retired");
			fault = 0;
			int ret = charger_cooling_probe(&pdev);
			check(ret == 0, "retry after failure or remove");
			if (!ret)
				charger_cooling_remove(&pdev);
		}
	}
	for (int mode = 0; mode < 2; mode++) {
		reset(1, 0);
		property_error = mode == 0;
		node_absent = mode == 1;
		check(get_charger_type() == 0 && node_refs == 0, "OF fallback and release");
	}
	reset(1, 0);
	struct platform_device first = { .dev.of_node = &node }, second = { .dev.of_node = &node };
	check(charger_cooling_probe(&first) == 0, "first instance");
	charger_cl_data.cur_current = 500000;
	check(charger_cooling_probe(&second) == -EEXIST, "reject duplicate global group");
	check(group == 1 && thermal.active == 1 && master.refs == 1 && slave.refs == 1,
		"duplicate failure preserves first instance and its refs");
	check(charger_cl_data.cur_current == 500000, "duplicate failure preserves diagnostics");
	charger_cooling_remove(&first);
	for (int type = 0; type <= 1; type++) {
		reset(type, 0);
		struct platform_device retiring = { .dev.of_node = &node };
		check(charger_cooling_probe(&retiring) == 0, "retiring instance setup");
		unregister_hook = concurrent_probe;
		charger_cooling_remove(&retiring);
		check(!group && !thermal.active && !master.refs && !slave.refs, "concurrent failure and removal balance resources");
	}
	reset(0, 0);
	printf("checks=%d failures=%d\n", checks, failures);
	return failures != 0;
}
