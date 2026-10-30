/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef __PINCTRL_CORE_API_H
#define __PINCTRL_CORE_API_H

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>

struct device_node;
struct function_desc;
struct group_desc;

int pinctrl_utils_reserve_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned int *reserved_maps,
		unsigned int *num_maps, unsigned int reserve);
int pinctrl_utils_add_map_configs(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned int *reserved_maps,
		unsigned int *num_maps, const char *group,
		unsigned long *configs, unsigned int num_configs,
		enum pinctrl_map_type type);
void pinctrl_utils_free_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map *map, unsigned int num_maps);
int pinconf_generic_parse_dt_config(struct device_node *np,
				    struct pinctrl_dev *pctldev,
				    unsigned long **configs,
				    unsigned int *nconfigs);

#endif
