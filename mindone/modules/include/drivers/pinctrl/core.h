/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 ST-Ericsson SA
 */

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/types.h>

#include <linux/pinctrl/machine.h>

struct dentry;
struct device;
struct device_node;
struct module;

struct pinctrl;
struct pinctrl_desc;
struct pinctrl_gpio_range;
struct pinctrl_state;

struct pinctrl_dev {
	struct list_head node;
	const struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
#ifdef CONFIG_GENERIC_PINCTRL_GROUPS
	struct radix_tree_root pin_group_tree;
	unsigned int num_groups;
#endif
#ifdef CONFIG_GENERIC_PINMUX_FUNCTIONS
	struct radix_tree_root pin_function_tree;
	unsigned int num_functions;
#endif
	struct list_head gpio_ranges;
	struct device *dev;
	struct module *owner;
	void *driver_data;
	struct pinctrl *p;
	struct pinctrl_state *hog_default;
	struct pinctrl_state *hog_sleep;
	struct mutex mutex;
#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif
};

struct pinctrl {
	struct list_head node;
	struct device *dev;
	struct list_head states;
	struct pinctrl_state *state;
	struct list_head dt_maps;
	struct kref users;
};

struct pinctrl_state {
	struct list_head node;
	const char *name;
	struct list_head settings;
};

struct pinctrl_setting_mux {
	unsigned int group;
	unsigned int func;
};

struct pinctrl_setting_configs {
	unsigned int group_or_pin;
	unsigned long *configs;
	unsigned int num_configs;
};

struct pinctrl_setting {
	struct list_head node;
	enum pinctrl_map_type type;
	struct pinctrl_dev *pctldev;
	const char *dev_name;
	union {
		struct pinctrl_setting_mux mux;
		struct pinctrl_setting_configs configs;
	} data;
};

struct pin_desc {
	struct pinctrl_dev *pctldev;
	const char *name;
	bool dynamic_name;
	void *drv_data;

#ifdef CONFIG_PINMUX
	unsigned int mux_usecount;
	const char *mux_owner;
	const struct pinctrl_setting_mux *mux_setting;
	const char *gpio_owner;
	struct mutex mux_lock;
#endif
};

struct pinctrl_maps {
	struct list_head node;
	const struct pinctrl_map *maps;
	unsigned int num_maps;
};

#ifdef CONFIG_GENERIC_PINCTRL_GROUPS

#include <linux/pinctrl/pinctrl.h>

struct group_desc {
	struct pingroup grp;
	void *data;
};

#define PINCTRL_GROUP_DESC(_name, _pins, _num_pins, _data)	\
(struct group_desc) {						\
	.grp = PINCTRL_PINGROUP(_name, _pins, _num_pins),	\
	.data = _data,						\
}

int pinctrl_generic_get_group_count(struct pinctrl_dev *pctldev);

const char *pinctrl_generic_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int group_selector);

int pinctrl_generic_get_group_pins(struct pinctrl_dev *pctldev,
				   unsigned int group_selector,
				   const unsigned int **pins,
				   unsigned int *npins);

struct group_desc *pinctrl_generic_get_group(struct pinctrl_dev *pctldev,
					     unsigned int group_selector);

int pinctrl_generic_add_group(struct pinctrl_dev *pctldev, const char *name,
			      const unsigned int *pins, int num_pins, void *data);

int pinctrl_generic_remove_group(struct pinctrl_dev *pctldev,
				 unsigned int group_selector);

#endif

struct pinctrl_dev *get_pinctrl_dev_from_devname(const char *dev_name);
struct pinctrl_dev *get_pinctrl_dev_from_of_node(struct device_node *np);
int pin_get_from_name(struct pinctrl_dev *pctldev, const char *name);
const char *pin_get_name(struct pinctrl_dev *pctldev, const unsigned int pin);
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group);

static inline struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev,
					    unsigned int pin)
{
	return radix_tree_lookup(&pctldev->pin_desc_tree, pin);
}

extern struct pinctrl_gpio_range *
pinctrl_find_gpio_range_from_pin_nolock(struct pinctrl_dev *pctldev,
					unsigned int pin);

extern int pinctrl_force_sleep(struct pinctrl_dev *pctldev);
extern int pinctrl_force_default(struct pinctrl_dev *pctldev);

extern struct mutex pinctrl_maps_mutex;
extern struct list_head pinctrl_maps;

#define for_each_pin_map(_maps_node_, _map_)						\
	list_for_each_entry(_maps_node_, &pinctrl_maps, node)				\
		for (unsigned int __i = 0;						\
		     __i < _maps_node_->num_maps && (_map_ = &_maps_node_->maps[__i]);	\
		     __i++)
