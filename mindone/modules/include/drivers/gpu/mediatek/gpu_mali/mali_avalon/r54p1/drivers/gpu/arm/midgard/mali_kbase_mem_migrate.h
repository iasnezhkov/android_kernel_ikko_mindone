/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_MIGRATE_H
#define _KBASE_MEM_MIGRATE_H

#include <linux/version_compat_defs.h>

#include <linux/types.h>

struct kbase_device;
struct file;
struct page;

#define PAGE_STATUS_MASK ((u8)0x3F)
#define PAGE_STATUS_GET(status) (status & PAGE_STATUS_MASK)
#define PAGE_STATUS_SET(status, value) ((status & ~PAGE_STATUS_MASK) | (value & PAGE_STATUS_MASK))

#define PAGE_ISOLATE_SHIFT (7)
#define PAGE_ISOLATE_MASK ((u8)1 << PAGE_ISOLATE_SHIFT)
#define PAGE_ISOLATE_SET(status, value) \
	((status & ~PAGE_ISOLATE_MASK) | (value << PAGE_ISOLATE_SHIFT))
#define IS_PAGE_ISOLATED(status) ((bool)(status & PAGE_ISOLATE_MASK))

#define PAGE_MOVABLE_SHIFT (6)
#define PAGE_MOVABLE_MASK ((u8)1 << PAGE_MOVABLE_SHIFT)
#define PAGE_MOVABLE_CLEAR(status) ((status) & ~PAGE_MOVABLE_MASK)
#define PAGE_MOVABLE_SET(status) (status | PAGE_MOVABLE_MASK)

#define IS_PAGE_MOVABLE(status) ((bool)(status & PAGE_MOVABLE_MASK))

bool kbase_alloc_page_metadata(struct kbase_device *kbdev, struct page *p, dma_addr_t dma_addr,
			       u8 group_id);

bool kbase_is_page_migration_enabled(void);

void kbase_free_page_later(struct kbase_device *kbdev, struct page *p);

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)

void kbase_mem_migrate_set_address_space_ops(struct kbase_device *kbdev, struct file *const filp);
#endif

void kbase_mem_migrate_init(struct kbase_device *kbdev);

void kbase_mem_migrate_term(struct kbase_device *kbdev);

#if MALI_UNIT_TEST

int kbase_migrate_page_allocated_mapped(struct page *old_page, struct page *new_page);
#endif

#endif
