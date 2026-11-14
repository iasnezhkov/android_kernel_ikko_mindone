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

#define GFP_KERNEL 0
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define PSY_EVENT_PROP_CHANGED 0
#define MAX_ERRNO 4095
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-MAX_ERRNO)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define MODULE_DEVICE_TABLE(a, b)
#define module_platform_driver(d)
#define MODULE_DESCRIPTION(d)
#define MODULE_LICENSE(l)
#define dev_info(dev, ...) ((void)(dev))

typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { int event; } pm_message_t;

enum power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_PD_SPR_AVS,
	POWER_SUPPLY_USB_TYPE_PD_PPS_SPR_AVS,
};

enum power_supply_property {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

union power_supply_propval {
	int intval;
};

struct fwnode_handle {
	int id;
};

struct device {
	struct device *parent;
	struct fwnode_handle *fwnode;
};

struct platform_device {
	struct device dev;
	void *drvdata;
};

struct of_device_id {
	const char *compatible;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	struct {
		const char *name;
		const struct of_device_id *of_match_table;
	} driver;
};

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long event, void *data);
};

struct srcu_notifier_head {
	struct notifier_block *nb;
};

struct mutex {
	int held;
};

struct work_struct {
	void (*func)(struct work_struct *work);
	int pending;
};

struct delayed_work {
	struct work_struct work;
	long due;
};

struct power_supply {
	struct device dev;
	int online;
	int usb_type;
	int uv;
	int ua;
	int fail;
	int refs;
};

static struct fwnode_handle world_typec_node = { 1 };
static struct device world_tcpc_dev = { NULL, &world_typec_node };
static struct power_supply world_tcpm = { .dev = { &world_tcpc_dev, &world_typec_node } };
static struct device world_eta_dev = { NULL, NULL };
static struct power_supply world_eta = { .dev = { &world_eta_dev, NULL } };
static struct fwnode_handle world_adapter_node = { 2 };
static int world_tcpm_present = 1;
static struct notifier_block *world_psy_nb;
static long world_now;
static struct work_struct *world_works[4];
static struct delayed_work *world_delayed[4];
static int world_nworks, world_ndelayed;
static int world_name_ok;

static void mutex_init(struct mutex *m) { m->held = 0; }
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }

#define INIT_WORK(w, f) do { (w)->func = (f); (w)->pending = 0; } while (0)
#define INIT_DELAYED_WORK(d, f) do { (d)->work.func = (f); (d)->work.pending = 0; (d)->due = -1; } while (0)
#define to_delayed_work(w) container_of(w, struct delayed_work, work)

static long msecs_to_jiffies(long ms) { return ms; }

static bool schedule_work(struct work_struct *w)
{
	int i;

	for (i = 0; i < world_nworks; i++)
		if (world_works[i] == w)
			break;
	if (i == world_nworks) {
		assert(world_nworks < 4);
		world_works[world_nworks++] = w;
	}
	if (w->pending)
		return false;
	w->pending = 1;
	return true;
}

static bool schedule_delayed_work(struct delayed_work *d, long delay)
{
	int i;

	for (i = 0; i < world_ndelayed; i++)
		if (world_delayed[i] == d)
			break;
	if (i == world_ndelayed) {
		assert(world_ndelayed < 4);
		world_delayed[world_ndelayed++] = d;
	}
	if (d->due >= 0)
		return false;
	d->due = world_now + delay;
	return true;
}

static bool cancel_delayed_work(struct delayed_work *d)
{
	bool was = d->due >= 0;

	d->due = -1;
	return was;
}

static bool delayed_work_pending(struct delayed_work *d)
{
	return d->due >= 0;
}

static bool cancel_work_sync(struct work_struct *w)
{
	bool was = w->pending;

	w->pending = 0;
	return was;
}

static bool cancel_delayed_work_sync(struct delayed_work *d)
{
	return cancel_delayed_work(d);
}

static void run_ready(void)
{
	int i, again = 1;

	while (again) {
		again = 0;
		for (i = 0; i < world_nworks; i++) {
			if (world_works[i]->pending) {
				world_works[i]->pending = 0;
				world_works[i]->func(world_works[i]);
				again = 1;
			}
		}
		for (i = 0; i < world_ndelayed; i++) {
			if (world_delayed[i]->due >= 0 && world_delayed[i]->due <= world_now) {
				world_delayed[i]->due = -1;
				world_delayed[i]->work.func(&world_delayed[i]->work);
				again = 1;
			}
		}
	}
}

static void advance(long ms)
{
	long end = world_now + ms;

	run_ready();
	while (world_now < end) {
		world_now++;
		run_ready();
	}
}

static void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	(void)dev;
	(void)gfp;
	return calloc(1, size);
}

static struct fwnode_handle *dev_fwnode(struct device *dev)
{
	return dev->fwnode;
}

static int device_property_read_string(struct device *dev, const char *name, const char **value)
{
	(void)dev;
	assert(!strcmp(name, "adapter_name"));
	*value = "pd_adapter";
	world_name_ok = 1;
	return 0;
}

static struct fwnode_handle *fwnode_find_reference(struct fwnode_handle *fwnode, const char *name, unsigned int index)
{
	assert(fwnode == &world_adapter_node && !strcmp(name, "typec") && index == 0);
	return &world_typec_node;
}

static void fwnode_handle_put(struct fwnode_handle *fwnode)
{
	(void)fwnode;
}

static int dev_err_probe(struct device *dev, int err, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return err;
}

static struct power_supply *power_supply_get_by_reference(struct fwnode_handle *fwnode, const char *name)
{
	assert(fwnode == &world_adapter_node && !strcmp(name, "typec"));
	if (!world_tcpm_present)
		return NULL;
	world_tcpm.refs++;
	return &world_tcpm;
}

static void power_supply_put(struct power_supply *psy)
{
	psy->refs--;
}

static int power_supply_get_property(struct power_supply *psy, enum power_supply_property prop,
				     union power_supply_propval *val)
{
	assert(psy == &world_tcpm);
	if (psy->fail)
		return -ENODEV;
	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = psy->online;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = psy->usb_type;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = psy->uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = psy->ua;
		break;
	}
	return 0;
}

static int power_supply_reg_notifier(struct notifier_block *nb)
{
	world_psy_nb = nb;
	return 0;
}

static void power_supply_unreg_notifier(struct notifier_block *nb)
{
	assert(nb == world_psy_nb);
	world_psy_nb = NULL;
}

static void platform_set_drvdata(struct platform_device *pdev, void *data)
{
	pdev->drvdata = data;
}

static void *platform_get_drvdata(struct platform_device *pdev)
{
	return pdev->drvdata;
}

static int srcu_notifier_call_chain(struct srcu_notifier_head *head, unsigned long event, void *data)
{
	if (head->nb)
		head->nb->notifier_call(head->nb, event, data);
	return 0;
}
