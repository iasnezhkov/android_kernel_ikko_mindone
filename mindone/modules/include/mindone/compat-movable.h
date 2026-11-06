/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_MOVABLE_H
#define MINDONE_COMPAT_MOVABLE_H

#include <linux/migrate.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_DRIVER_PAGE_MIGRATION 0

static inline void mindone_set_page_movable(struct page *page,
					    const struct movable_operations *ops)
{
}

static inline void mindone_clear_page_movable(struct page *page)
{
}
#else
#define MINDONE_DRIVER_PAGE_MIGRATION 1

static inline void mindone_set_page_movable(struct page *page,
					    const struct movable_operations *ops)
{
	__SetPageMovable(page, ops);
}

static inline void mindone_clear_page_movable(struct page *page)
{
	__ClearPageMovable(page);
}
#endif

#endif
