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
#define EPROBE_DEFER 517
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define PSY_EVENT_PROP_CHANGED 0
#define MAX_ERRNO 4095
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-MAX_ERRNO)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))

#define MTK_USB_EXTCON_AUDIO_DETACH 0
#define MTK_USB_EXTCON_AUDIO_ATTACH 1

enum usb_role {
	USB_ROLE_NONE,
	USB_ROLE_HOST,
	USB_ROLE_DEVICE,
};

enum {
	TYPEC_STATE_SAFE,
	TYPEC_STATE_USB,
	TYPEC_STATE_MODAL,
};

enum {
	TYPEC_MODE_USB2 = TYPEC_STATE_MODAL,
	TYPEC_MODE_USB3,
	TYPEC_MODE_USB4,
	TYPEC_MODE_AUDIO,
	TYPEC_MODE_DEBUG,
	TYPEC_MODE_MAX,
};

enum {
	EXTCON_NONE = 0,
	EXTCON_USB = 1,
	EXTCON_USB_HOST = 2,
};

enum power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};

enum power_supply_property {
	POWER_SUPPLY_PROP_USB_TYPE,
};

union power_supply_propval {
	int intval;
};

struct fwnode_handle {
	int unused;
};

struct device_node {
	int unused;
};

struct device {
	struct device_node *of_node;
	struct fwnode_handle fwnode;
};

struct platform_device {
	struct device dev;
	void *drvdata;
};

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long event, void *data);
	struct notifier_block *next;
};

struct raw_notifier_head {
	struct notifier_block *head;
};

struct mutex {
	int held;
};

typedef struct {
	int held;
} spinlock_t;

struct work_struct {
	void (*func)(struct work_struct *work);
	int pending;
};

struct workqueue_struct {
	int unused;
};

struct extcon_dev {
	int state[3];
};

struct power_supply {
	int usb_type;
	int fail;
};

struct usb_role_switch;
typedef int (*usb_role_switch_set_t)(struct usb_role_switch *sw, enum usb_role role);
typedef enum usb_role (*usb_role_switch_get_t)(struct usb_role_switch *sw);

struct usb_role_switch {
	void *drvdata;
	usb_role_switch_set_t set;
	usb_role_switch_get_t get;
	enum usb_role role;
	int sets;
	struct fwnode_handle *fwnode;
};

struct usb_role_switch_desc {
	struct fwnode_handle *fwnode;
	struct device *usb2_port;
	struct device *usb3_port;
	struct device *udc;
	usb_role_switch_set_t set;
	usb_role_switch_get_t get;
	bool allow_userspace_control;
	void *driver_data;
	const char *name;
};

struct typec_altmode {
	int svid;
};

struct typec_mux_state {
	struct typec_altmode *alt;
	unsigned long mode;
	void *data;
};

struct typec_mux_dev;
typedef int (*typec_mux_set_fn_t)(struct typec_mux_dev *mux, struct typec_mux_state *state);

struct typec_mux_dev {
	void *drvdata;
	typec_mux_set_fn_t set;
	struct fwnode_handle *fwnode;
};

struct typec_mux_desc {
	struct fwnode_handle *fwnode;
	typec_mux_set_fn_t set;
	const char *name;
	void *drvdata;
};

#define DEFINE_MUTEX(name) struct mutex name = { 0 }
#define RAW_NOTIFIER_HEAD(name) struct raw_notifier_head name = { NULL }
#define EXPORT_SYMBOL_GPL(sym) extern int sym##_exported
#define INIT_WORK(w, f) do { (w)->func = (f); (w)->pending = 0; } while (0)

static int world_bypass = 1;
static struct power_supply world_psy;
static struct usb_role_switch world_musb;
static int world_musb_present = 1;
static int world_mux_fail;
static int world_psy_present = 1;
static struct usb_role_switch world_port;
static struct typec_mux_dev world_mux;
static struct notifier_block *world_psy_nb;
static struct workqueue_struct world_wq;
static struct extcon_dev world_edev;
static int world_port_registered;
static int world_mux_registered;
static int world_devm_actions;
static void (*world_actions[16])(void *);
static void *world_action_data[16];

static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
static void spin_lock_init(spinlock_t *l) { l->held = 0; }
#define spin_lock_irqsave(l, f) do { (void)(f); assert(!(l)->held); (l)->held = 1; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); assert((l)->held); (l)->held = 0; } while (0)

static int raw_notifier_chain_register(struct raw_notifier_head *h, struct notifier_block *nb)
{
	nb->next = h->head;
	h->head = nb;
	return 0;
}

static int raw_notifier_chain_unregister(struct raw_notifier_head *h, struct notifier_block *nb)
{
	struct notifier_block **p;

	for (p = &h->head; *p; p = &(*p)->next) {
		if (*p == nb) {
			*p = nb->next;
			return 0;
		}
	}
	return -ENOENT;
}

static int raw_notifier_call_chain(struct raw_notifier_head *h, unsigned long event, void *data)
{
	struct notifier_block *nb;

	for (nb = h->head; nb; nb = nb->next)
		nb->notifier_call(nb, event, data);
	return 0;
}

static bool queue_work(struct workqueue_struct *wq, struct work_struct *w)
{
	(void)wq;
	if (w->pending)
		return false;
	w->pending = 1;
	return true;
}

static struct work_struct *world_work;

static void drain(void)
{
	while (world_work && world_work->pending) {
		world_work->pending = 0;
		world_work->func(world_work);
	}
}

static int power_supply_get_property(struct power_supply *psy, enum power_supply_property prop,
				     union power_supply_propval *val)
{
	assert(prop == POWER_SUPPLY_PROP_USB_TYPE);
	if (psy->fail)
		return -EAGAIN;
	val->intval = psy->usb_type;
	return 0;
}

static int usb_role_switch_set_role(struct usb_role_switch *sw, enum usb_role role)
{
	sw->role = role;
	sw->sets++;
	return 0;
}

static void *usb_role_switch_get_drvdata(struct usb_role_switch *sw)
{
	return sw->drvdata;
}

static void *typec_mux_get_drvdata(struct typec_mux_dev *mux)
{
	return mux->drvdata;
}

static const char *usb_role_string(enum usb_role role)
{
	static const char *const names[] = { "none", "host", "device" };

	return names[role];
}

static int extcon_set_state_sync(struct extcon_dev *edev, unsigned int id, bool state)
{
	edev->state[id] = state;
	return 0;
}

#define dev_info(dev, ...) ((void)(dev))
#define dev_warn(dev, ...) ((void)(dev))
#define dev_err(dev, ...) ((void)(dev))

static int dev_err_probe(struct device *dev, int err, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return err;
}

static void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	(void)dev;
	(void)gfp;
	return calloc(1, size);
}

static bool of_property_present(struct device_node *np, const char *name)
{
	(void)np;
	assert(!strcmp(name, "mediatek,bypss-typec-sink"));
	return world_bypass;
}

static struct power_supply *mindone_devm_power_supply_get_by_reference(struct device *dev, const char *name)
{
	(void)dev;
	assert(!strcmp(name, "charger"));
	return world_psy_present ? &world_psy : NULL;
}

static struct usb_role_switch *usb_role_switch_get(struct device *dev)
{
	(void)dev;
	return world_musb_present ? &world_musb : ERR_PTR(-EPROBE_DEFER);
}

static void usb_role_switch_put(struct usb_role_switch *sw)
{
	(void)sw;
}

static int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data)
{
	(void)dev;
	assert(world_devm_actions < 16);
	world_actions[world_devm_actions] = action;
	world_action_data[world_devm_actions++] = data;
	return 0;
}

static void devm_release_all(void)
{
	while (world_devm_actions) {
		world_devm_actions--;
		world_actions[world_devm_actions](world_action_data[world_devm_actions]);
	}
}

static struct extcon_dev *devm_extcon_dev_allocate(struct device *dev, const unsigned int *cable)
{
	(void)dev;
	(void)cable;
	memset(&world_edev, 0, sizeof(world_edev));
	return &world_edev;
}

static int devm_extcon_dev_register(struct device *dev, struct extcon_dev *edev)
{
	(void)dev;
	(void)edev;
	return 0;
}

static struct workqueue_struct *alloc_ordered_workqueue(const char *name, int flags)
{
	(void)name;
	(void)flags;
	return &world_wq;
}

static void destroy_workqueue(struct workqueue_struct *wq)
{
	(void)wq;
	drain();
}

static void platform_set_drvdata(struct platform_device *pdev, void *data)
{
	pdev->drvdata = data;
}

static int power_supply_reg_notifier(struct notifier_block *nb)
{
	world_psy_nb = nb;
	return 0;
}

static void power_supply_unreg_notifier(struct notifier_block *nb)
{
	assert(world_psy_nb == nb);
	world_psy_nb = NULL;
}

static struct fwnode_handle *dev_fwnode(struct device *dev)
{
	return &dev->fwnode;
}

static struct typec_mux_dev *typec_mux_register(struct device *dev, const struct typec_mux_desc *desc)
{
	(void)dev;
	if (world_mux_fail)
		return ERR_PTR(-ENOMEM);
	world_mux.drvdata = desc->drvdata;
	world_mux.set = desc->set;
	world_mux.fwnode = desc->fwnode;
	world_mux_registered = 1;
	return &world_mux;
}

static void typec_mux_unregister(struct typec_mux_dev *mux)
{
	assert(mux == &world_mux);
	world_mux_registered = 0;
}

static struct usb_role_switch *usb_role_switch_register(struct device *dev, const struct usb_role_switch_desc *desc)
{
	(void)dev;
	assert(!desc->allow_userspace_control);
	world_port.drvdata = desc->driver_data;
	world_port.set = desc->set;
	world_port.get = desc->get;
	world_port.fwnode = desc->fwnode;
	world_port_registered = 1;
	return &world_port;
}

static void usb_role_switch_unregister(struct usb_role_switch *sw)
{
	assert(sw == &world_port);
	world_port_registered = 0;
}
