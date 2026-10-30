/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct device { int unused; };
struct thermal_cooling_device { void *devdata; };
struct list_head { struct list_head *next, *prev; };
struct mutex { const char *name; int held; unsigned int acquisitions; };

#define IS_ENABLED(value) 0
#define DEFINE_MUTEX(name) struct mutex name = { #name, 0, 0 }
#define LIST_HEAD(name) struct list_head name = { &name, &name }
#define max(left, right) ((left) > (right) ? (left) : (right))
#define container_of(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
#define list_for_each_entry(position, head, member) \
	for (struct list_head *fixture_node = (head)->next; \
		fixture_node != (head) && ((position) = container_of(fixture_node, __typeof__(*(position)), member), 1); \
		fixture_node = fixture_node->next)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_info(dev, ...) ((void)(dev))

static void mutex_lock(struct mutex *lock);
static void mutex_unlock(struct mutex *lock);
static int exec_ccci_kern_func_by_md_id(int md_id, unsigned int id, char *buffer, unsigned int length);
