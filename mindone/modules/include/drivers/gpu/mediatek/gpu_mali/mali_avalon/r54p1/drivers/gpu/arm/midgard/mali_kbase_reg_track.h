/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_REG_TRACK_H_
#define _KBASE_REG_TRACK_H_

#include <linux/types.h>
#include <linux/rbtree.h>

struct kbase_context;
struct kbase_va_region;
struct kbase_device;
struct kmem_cache;

#if MALI_USE_CSF

#define KBASE_REG_ZONE_BITS 3
#else

#define KBASE_REG_ZONE_BITS 2
#endif

#if MALI_USE_CSF
#define KBASE_REG_ZONE_MAX 6ul
#else
#define KBASE_REG_ZONE_MAX 4ul
#endif

#define KBASE_REG_ZONE_SHIFT (11ul)
#define KBASE_REG_ZONE_MASK (((1 << KBASE_REG_ZONE_BITS) - 1ul) << KBASE_REG_ZONE_SHIFT)

#if KBASE_REG_ZONE_MAX > (1 << KBASE_REG_ZONE_BITS)
#error "Too many zones for the number of zone bits defined"
#endif

#define KBASE_REG_ZONE_CUSTOM_VA_BASE (0x100000000ULL >> PAGE_SHIFT)

#if MALI_USE_CSF

#define KBASE_REG_ZONE_CUSTOM_VA_SIZE (((1ULL << 43) >> PAGE_SHIFT) - KBASE_REG_ZONE_CUSTOM_VA_BASE)
#else

#define KBASE_REG_ZONE_CUSTOM_VA_SIZE (((1ULL << 44) >> PAGE_SHIFT) - KBASE_REG_ZONE_CUSTOM_VA_BASE)

#endif

#define KBASE_REG_ZONE_EXEC_VA_MAX_PAGES ((1ULL << 32) >> PAGE_SHIFT)
#define KBASE_REG_ZONE_EXEC_VA_SIZE KBASE_REG_ZONE_EXEC_VA_MAX_PAGES

#if MALI_USE_CSF
#define KBASE_REG_ZONE_MCU_SHARED_BASE (0x04000000ULL >> PAGE_SHIFT)
#define MCU_SHARED_ZONE_SIZE (((0x08000000ULL) >> PAGE_SHIFT) - KBASE_REG_ZONE_MCU_SHARED_BASE)

#define KBASE_REG_ZONE_EXEC_VA_BASE_64 ((1ULL << 47) >> PAGE_SHIFT)
#define KBASE_REG_ZONE_EXEC_VA_BASE_32 ((1ULL << 43) >> PAGE_SHIFT)

#define KBASE_REG_ZONE_EXEC_FIXED_VA_SIZE KBASE_REG_ZONE_EXEC_VA_MAX_PAGES

#define KBASE_REG_ZONE_FIXED_VA_END_64 ((1ULL << 48) >> PAGE_SHIFT)
#define KBASE_REG_ZONE_FIXED_VA_END_32 ((1ULL << 44) >> PAGE_SHIFT)

#endif

enum kbase_memory_zone {
	SAME_VA_ZONE,
	CUSTOM_VA_ZONE,
	EXEC_VA_ZONE,
#if IS_ENABLED(MALI_USE_CSF)
	EXEC_FIXED_VA_ZONE,
	FIXED_VA_ZONE,
	MCU_SHARED_ZONE,
#endif
	MEMORY_ZONE_MAX,
#if IS_ENABLED(MALI_USE_CSF)
	CONTEXT_ZONE_MAX = FIXED_VA_ZONE + 1
#else
	CONTEXT_ZONE_MAX = EXEC_VA_ZONE + 1
#endif
};

struct kbase_reg_zone {
	struct rb_root reg_rbtree;
	u64 base_pfn;
	u64 va_size_pages;
	enum kbase_memory_zone id;
	struct kmem_cache *cache;
};

unsigned long kbase_zone_to_bits(enum kbase_memory_zone zone);

enum kbase_memory_zone kbase_bits_to_zone(unsigned long zone_bits);

char *kbase_reg_zone_get_name(enum kbase_memory_zone zone);

static inline bool kbase_is_ctx_reg_zone(enum kbase_memory_zone zone)
{
#if MALI_USE_CSF
	return !(zone == MCU_SHARED_ZONE);
#else
	return true;
#endif
}

int kbase_region_tracker_init(struct kbase_context *kctx);

int kbase_region_tracker_init_jit(struct kbase_context *kctx, u64 jit_va_pages, int max_allocations,
				  int trim_level, int group_id, u64 phys_pages_limit);

int kbase_region_tracker_init_exec(struct kbase_context *kctx, u64 exec_va_pages);

void kbase_region_tracker_term(struct kbase_context *kctx);

void kbase_region_tracker_term(struct kbase_context *kctx);

struct kbase_va_region *
kbase_region_tracker_find_region_enclosing_address(struct kbase_context *kctx, u64 gpu_addr);

struct kbase_va_region *kbase_region_tracker_find_region_base_address(struct kbase_context *kctx,
								      u64 gpu_addr);

void kbase_remove_va_region(struct kbase_device *kbdev, struct kbase_va_region *reg);

struct kbase_context *kbase_reg_to_kctx(struct kbase_va_region *reg);

struct kbase_va_region *kbase_alloc_free_region(struct kbase_reg_zone *zone, u64 start_pfn,
						size_t nr_pages);

struct kbase_va_region *kbase_ctx_alloc_free_region(struct kbase_context *kctx,
						    enum kbase_memory_zone id, u64 start_pfn,
						    size_t nr_pages);

int kbase_add_va_region(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr,
			size_t nr_pages, size_t align);

int kbase_add_va_region_rbtree(struct kbase_device *kbdev, struct kbase_va_region *reg, u64 addr,
			       size_t nr_pages, size_t align);

void kbase_free_alloced_region(struct kbase_va_region *reg);

int kbase_reg_zone_init(struct kbase_device *kbdev, struct kbase_reg_zone *zone,
			enum kbase_memory_zone id, u64 base_pfn, u64 va_size_pages);

void kbase_reg_zone_term(struct kbase_reg_zone *zone);

struct kbase_reg_zone *kbase_ctx_reg_zone_get_nolock(struct kbase_context *kctx,
						     enum kbase_memory_zone zone);

struct kbase_reg_zone *kbase_ctx_reg_zone_get(struct kbase_context *kctx,
					      enum kbase_memory_zone zone);

static inline u64 kbase_reg_zone_end_pfn(struct kbase_reg_zone *zone)
{
	if (WARN_ON(!zone))
		return 0;

	return zone->base_pfn + zone->va_size_pages;
}

#endif
