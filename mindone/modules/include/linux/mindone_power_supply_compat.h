// SPDX-License-Identifier: GPL-2.0
#ifndef _LINUX_MINDONE_POWER_SUPPLY_COMPAT_H
#define _LINUX_MINDONE_POWER_SUPPLY_COMPAT_H
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/version.h>

static inline struct power_supply *
mindone_power_supply_get_by_reference(struct fwnode_handle *fwnode,
                                     const char *property)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return power_supply_get_by_reference(fwnode, property);
#else
	return power_supply_get_by_phandle(to_of_node(fwnode), property);
#endif
}

static inline struct power_supply *
mindone_devm_power_supply_get_by_reference(struct device *dev,
                                          const char *property)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return devm_power_supply_get_by_reference(dev, property);
#else
	return devm_power_supply_get_by_phandle(dev, property);
#endif
}
#endif
