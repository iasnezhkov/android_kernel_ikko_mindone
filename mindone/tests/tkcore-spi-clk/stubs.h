// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define GFP_KERNEL 0
#define EXPORT_SYMBOL(x)
#define pr_info(fmt, ...) do { } while (0)
#define pr_warn(fmt, ...) do { } while (0)
#define pr_err(fmt, ...) do { } while (0)
#define TEEC_ERROR_ITEM_NOT_FOUND (-2)
#define TEEC_ERROR_NOT_SUPPORTED (-95)

#define TEE_CLKMGR_TOKEN_NOT_LEGACY (0x1)
#define TEE_CLKMGR_TOKEN_ID_MASK (0x7fffu)
#define TEE_CLKMGR_TOKEN_ID_SHIFT (1)
#define TEE_CLKMGR_TOKEN_TYPE_MASK (0xffffu)
#define TEE_CLKMGR_TOKEN_TYPE_SHIFT (16)
#define TEE_CLKMGR_TOKEN(type, id) \
	(((type) << TEE_CLKMGR_TOKEN_TYPE_SHIFT) | \
	((id) << TEE_CLKMGR_TOKEN_ID_SHIFT) | \
	TEE_CLKMGR_TOKEN_NOT_LEGACY)
#define TEE_CLKMGR_OP_ENABLE (0x1)

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define list_first_entry(ptr, type, member) \
	container_of((ptr)->next, type, member)
#define list_next_entry(pos, member) \
	container_of((pos)->member.next, typeof(*(pos)), member)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_next_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member), \
	     n = list_next_entry(pos, member); \
	     &pos->member != (head); \
	     pos = n, n = list_next_entry(n, member))

static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
	entry->next = NULL;
	entry->prev = NULL;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
	struct list_head *next = head->next;
	head->next = n;
	n->prev = head;
	n->next = next;
	next->prev = n;
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
	struct list_head *prev = head->prev;
	head->prev = n;
	n->next = head;
	n->prev = prev;
	prev->next = n;
}

struct mutex { int locked; };
#define DEFINE_MUTEX(name) struct mutex name = { 0 }
static inline void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static inline void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }

static void *kmalloc(size_t sz, int gfp) { (void)gfp; return malloc(sz); }
static void kfree(void *p) { free(p); }

struct clk { int id; };
static struct clk *last_enabled_clk;
static struct clk *last_disabled_clk;
static int enable_calls;
static int disable_calls;
static int enable_fail_rc;

static int clk_prepare_enable(struct clk *clk)
{
	enable_calls++;
	last_enabled_clk = clk;
	return enable_fail_rc;
}

static void clk_disable_unprepare(struct clk *clk)
{
	disable_calls++;
	last_disabled_clk = clk;
}

int tee_clkmgr_register(const char *clkname, int id, void *e, void *d,
			void *p0, void *p1, void *p2, size_t argnum);
int tee_clkmgr_unregister(const char *clkname, int id);
int tee_clkmgr_handle(uint32_t token, uint32_t op);
#define tee_clkmgr_register1(clkname, id, e, d, p0) \
	tee_clkmgr_register((clkname), (id), (void *)(e), (void *)(d), \
			    (void *)(p0), NULL, NULL, 1)
