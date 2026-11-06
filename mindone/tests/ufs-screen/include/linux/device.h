/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef UFS_TEST_DEVICE_H
#define UFS_TEST_DEVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <linux/notifier.h>
#define BUS_NOTIFY_ADD_DEVICE 0x00000001
#define BUS_NOTIFY_DEL_DEVICE 0x00000002
struct device {
	int refs;
};
struct bus_type {
	int unused;
};
void put_device(struct device *dev);
struct device *bus_find_device_by_name(const struct bus_type *bus, struct device *start, const char *name);
int bus_register_notifier(const struct bus_type *bus, struct notifier_block *nb);
int bus_unregister_notifier(const struct bus_type *bus, struct notifier_block *nb);
#endif
