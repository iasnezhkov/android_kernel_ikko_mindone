/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MEM_LOWLEVEL_H
#define _KBASE_MEM_LOWLEVEL_H

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

#include <linux/dma-mapping.h>

#define GPU_PAGE_SIZE SZ_4K
#define GPU_PAGE_MASK (~(SZ_4K - 1))
#define GPU_PAGES_PER_CPU_PAGE (PAGE_SIZE / GPU_PAGE_SIZE)

#define KBASE_PHY_PAGES_FLAG_DEFAULT (0)
#define KBASE_PHY_PAGES_FLAG_CLEAR (1 << 0)
#define KBASE_PHY_PAGES_FLAG_POISON (1 << 1)

#define KBASE_PHY_PAGES_SUPPORTED_FLAGS \
	(KBASE_PHY_PAGES_FLAG_DEFAULT | KBASE_PHY_PAGES_FLAG_CLEAR | KBASE_PHY_PAGES_FLAG_POISON)

#define KBASE_PHY_PAGES_POISON_VALUE \
	0xFD

enum kbase_sync_type { KBASE_SYNC_TO_CPU, KBASE_SYNC_TO_DEVICE };

struct tagged_addr {
	phys_addr_t tagged_addr;
};

#define HUGE_PAGE (1u << 0)
#define HUGE_HEAD (1u << 1)
#define FROM_PARTIAL (1u << 2)

#define NUM_PAGES_IN_2MB_LARGE_PAGE (SZ_2M / PAGE_SIZE)

#define KBASE_INVALID_PHYSICAL_ADDRESS (~(phys_addr_t)0 & PAGE_MASK)

#ifndef phys_to_page
#define phys_to_page(phys) (pfn_to_page((phys) >> PAGE_SHIFT))
#endif

static inline phys_addr_t as_phys_addr_t(struct tagged_addr t)
{
	return t.tagged_addr & GPU_PAGE_MASK;
}

static inline struct page *as_page(struct tagged_addr t)
{
	return phys_to_page(as_phys_addr_t(t));
}

static inline struct tagged_addr as_tagged(phys_addr_t phys)
{
	struct tagged_addr t;

	t.tagged_addr = phys & GPU_PAGE_MASK;
	return t;
}

static inline struct tagged_addr as_tagged_tag(phys_addr_t phys, int tag)
{
	struct tagged_addr t;

	t.tagged_addr = (phys & GPU_PAGE_MASK) | (tag & ~GPU_PAGE_MASK);
	return t;
}

static inline bool is_huge(struct tagged_addr t)
{
	return t.tagged_addr & HUGE_PAGE;
}

static inline bool is_huge_head(struct tagged_addr t)
{
	phys_addr_t mask = HUGE_HEAD | HUGE_PAGE;

	return mask == (t.tagged_addr & mask);
}

static inline bool is_partial(struct tagged_addr t)
{
	return t.tagged_addr & FROM_PARTIAL;
}

static inline unsigned int index_in_large_page(struct tagged_addr t)
{
	WARN_ON(!is_huge(t));

	return (PFN_DOWN(as_phys_addr_t(t)) & (NUM_PAGES_IN_2MB_LARGE_PAGE - 1));
}

static inline bool is_valid_addr(struct tagged_addr t)
{
	return (as_phys_addr_t(t) != KBASE_INVALID_PHYSICAL_ADDRESS);
}

#endif
