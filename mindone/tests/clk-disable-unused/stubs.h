/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPROBE_DEFER 517
#define MAX_ERRNO 4095

static const char *case_name = "";

#define expect(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s: Assertion failed: %s (%s:%d)\n", case_name, #cond, __FILE__, __LINE__); \
		abort(); \
	} \
} while (0)

static inline void *ERR_PTR(long error) { return (void *)error; }
static inline long PTR_ERR(const void *ptr) { return (long)ptr; }
static inline bool IS_ERR(const void *ptr) { return (uintptr_t)ptr >= (uintptr_t)-MAX_ERRNO; }
static inline bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR(ptr); }

static int log_lines, verbose;

__attribute__((format(printf, 1, 2)))
static void log_line(const char *format, ...)
{
	va_list args;

	log_lines++;
	if (!verbose)
		return;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

#define pr_notice(...) log_line(__VA_ARGS__)
#define pr_info(...) log_line(__VA_ARGS__)

enum rpm_status { RPM_ACTIVE, RPM_SUSPENDED };

struct device_node { const char *full_name; };
struct dev_pm_info { int disable_depth; int usage_count; enum rpm_status runtime_status; };
struct device { struct device_node *of_node; struct dev_pm_info power; };
struct platform_device { const char *name; struct device dev; };

struct clk_core;
struct clk_hw { struct clk_core *core; };
struct clk { struct clk_core *core; bool handle; };
struct clk_core {
	const char *name;
	struct clk_core *parent;
	bool in_domain;
	bool hw_on;
	int prepare_count;
	int enable_count;
	struct clk_hw hw;
	struct clk own;
};

#define MAX_SLOTS 16

struct slot { struct clk_core *core; long get_error; };

static struct device_node node = { "disable-unused-clk-model" };
static struct platform_device pdev_model;
static struct slot slots[MAX_SLOTS];
static int count_result, nslots;
static int resume_error;
static int handles_live, handles_made, puts_made;
static int enable_calls, fail_at_call, failures_injected, last_index, failed_index;
static int gets_made, rpm_enables;

static bool domain_on(void)
{
	return pdev_model.dev.power.runtime_status == RPM_ACTIVE;
}

static int of_count_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name)
{
	expect(np == &node);
	expect(!strcmp(list_name, "clocks") && !strcmp(cells_name, "#clock-cells"));
	return count_result;
}

static void pm_runtime_enable(struct device *dev)
{
	expect(dev == &pdev_model.dev);
	expect(dev->power.disable_depth > 0);
	dev->power.disable_depth--;
	rpm_enables++;
}

static int rpm_resume_get(struct device *dev)
{
	expect(dev == &pdev_model.dev);
	dev->power.usage_count++;
	gets_made++;
	if (dev->power.disable_depth > 0)
		return dev->power.runtime_status == RPM_ACTIVE ? 1 : -EACCES;
	if (dev->power.runtime_status == RPM_ACTIVE)
		return 1;
	if (resume_error)
		return resume_error;
	dev->power.runtime_status = RPM_ACTIVE;
	return 0;
}

static void pm_runtime_put_noidle(struct device *dev)
{
	expect(dev == &pdev_model.dev);
	expect(dev->power.usage_count > 0);
	dev->power.usage_count--;
}

static int pm_runtime_get_sync(struct device *dev)
{
	return rpm_resume_get(dev);
}

static int pm_runtime_resume_and_get(struct device *dev)
{
	int ret = rpm_resume_get(dev);

	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}
	return 0;
}

static int pm_runtime_put_sync(struct device *dev)
{
	expect(dev == &pdev_model.dev);
	expect(dev->power.usage_count > 0);
	dev->power.usage_count--;
	if (!dev->power.usage_count && !dev->power.disable_depth && dev->power.runtime_status == RPM_ACTIVE)
		dev->power.runtime_status = RPM_SUSPENDED;
	return 0;
}

static struct clk *of_clk_get(struct device_node *np, int index)
{
	struct clk *clk;

	expect(np == &node);
	expect(index >= 0 && index < nslots);
	if (slots[index].get_error)
		return ERR_PTR(slots[index].get_error);
	clk = calloc(1, sizeof(*clk));
	expect(clk);
	clk->core = slots[index].core;
	clk->handle = true;
	last_index = index;
	handles_live++;
	handles_made++;
	return clk;
}

static void clk_put(struct clk *clk)
{
	expect(clk && !IS_ERR(clk));
	expect(clk->handle);
	expect(handles_live > 0);
	handles_live--;
	puts_made++;
	free(clk);
}

static struct clk *clk_get_parent(struct clk *clk)
{
	if (!clk)
		return NULL;
	return clk->core->parent ? &clk->core->parent->own : NULL;
}

static int core_prepare_enable(struct clk_core *core)
{
	int ret;

	if (!core)
		return 0;
	if (core->in_domain)
		expect(domain_on());
	ret = core_prepare_enable(core->parent);
	if (ret)
		return ret;
	core->prepare_count++;
	core->enable_count++;
	core->hw_on = true;
	return 0;
}

static void core_disable_unprepare(struct clk_core *core)
{
	if (!core)
		return;
	if (core->in_domain)
		expect(domain_on());
	expect(core->enable_count > 0 && core->prepare_count > 0);
	core->enable_count--;
	core->prepare_count--;
	if (!core->enable_count)
		core->hw_on = false;
	core_disable_unprepare(core->parent);
}

static int clk_prepare_enable(struct clk *clk)
{
	if (!clk)
		return 0;
	expect(!IS_ERR(clk) && clk->core);
	if (++enable_calls == fail_at_call) {
		failures_injected++;
		failed_index = last_index;
		return -EIO;
	}
	return core_prepare_enable(clk->core);
}

static void clk_disable_unprepare(struct clk *clk)
{
	if (!clk)
		return;
	expect(!IS_ERR(clk) && clk->core);
	core_disable_unprepare(clk->core);
}

static bool __clk_is_enabled(struct clk *clk)
{
	expect(clk && clk->core);
	if (clk->core->in_domain)
		expect(domain_on());
	return clk->core->hw_on;
}

static struct clk_hw *__clk_get_hw(struct clk *clk)
{
	return clk ? &clk->core->hw : NULL;
}

static bool clk_hw_is_prepared(const struct clk_hw *hw)
{
	expect(hw && hw->core);
	return hw->core->prepare_count > 0;
}

static const char *__clk_get_name(const struct clk *clk)
{
	return clk ? clk->core->name : NULL;
}
