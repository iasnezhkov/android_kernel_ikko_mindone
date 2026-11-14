/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VERSION_COMPAT_DEFS_H_
#define _VERSION_COMPAT_DEFS_H_

#include <linux/highmem.h>
#include <linux/timer.h>
#include <linux/iopoll.h>

#include <linux/overflow.h>

#include <linux/bitops.h>
#include <linux/bits.h>

#ifndef BITS_PER_TYPE
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)
#endif

#ifdef CONFIG_MMU
#define kbase_mmap_min_addr CONFIG_DEFAULT_MMAP_MIN_ADDR

#ifdef CONFIG_LSM_MMAP_MIN_ADDR
#if (CONFIG_LSM_MMAP_MIN_ADDR > CONFIG_DEFAULT_MMAP_MIN_ADDR)

#undef kbase_mmap_min_addr
#define kbase_mmap_min_addr CONFIG_LSM_MMAP_MIN_ADDR
#define KBASE_COMPILED_MMAP_MIN_ADDR_MSG \
	"* MALI kbase_mmap_min_addr compiled to CONFIG_LSM_MMAP_MIN_ADDR, no runtime update possible! *"
#endif
#endif

#if (kbase_mmap_min_addr == CONFIG_DEFAULT_MMAP_MIN_ADDR)
#define KBASE_COMPILED_MMAP_MIN_ADDR_MSG \
	"* MALI kbase_mmap_min_addr compiled to CONFIG_DEFAULT_MMAP_MIN_ADDR, no runtime update possible! *"
#endif

#else
#define kbase_mmap_min_addr (0UL)
#define KBASE_COMPILED_MMAP_MIN_ADDR_MSG \
	"* MALI kbase_mmap_min_addr compiled to (0UL), no runtime update possible! *"
#endif

static inline void kbase_timer_setup(struct timer_list *timer,
				     void (*callback)(struct timer_list *timer))
{
	timer_setup(timer, callback, 0);
}

#ifndef WRITE_ONCE
#ifdef ASSIGN_ONCE
#define WRITE_ONCE(x, val) ASSIGN_ONCE(val, x)
#else
#define WRITE_ONCE(x, val) (ACCESS_ONCE(x) = (val))
#endif
#endif

#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif

#ifndef CSTD_UNUSED
#define CSTD_UNUSED(x) ((void)(x))
#endif

static inline void *kbase_kmap(struct page *p)
{
	return kmap_local_page(p);
}

static inline void *kbase_kmap_atomic(struct page *p)
{
	return kmap_local_page(p);
}

static inline void kbase_kunmap(struct page *p, void *address)
{
	CSTD_UNUSED(p);
	kunmap_local(address);
}

static inline void kbase_kunmap_atomic(void *address)
{
	kunmap_local(address);
}

#include <linux/dma-fence.h>

static inline void dma_fence_set_error_helper(struct dma_fence *fence, int error)
{
	dma_fence_set_error(fence, error);
}

#include <linux/mm.h>

static inline void kbase_unpin_user_buf_page(struct page *page)
{
	unpin_user_page(page);
}

static inline long kbase_get_user_pages(unsigned long start, unsigned long nr_pages,
					unsigned int gup_flags, struct page **pages,
					struct vm_area_struct **vmas)
{
	return get_user_pages(start, nr_pages, gup_flags, pages);
}

static inline long kbase_pin_user_pages_remote(struct task_struct *tsk, struct mm_struct *mm,
					       unsigned long start, unsigned long nr_pages,
					       unsigned int gup_flags, struct page **pages,
					       struct vm_area_struct **vmas, int *locked)
{
	return pin_user_pages_remote(mm, start, nr_pages, gup_flags, pages, locked);
}

#define KBASE_CLASS_CREATE(owner, name) class_create(name)

#define kbase_totalram_pages() totalram_pages()

#define mali_read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, \
				      args...)                                                \
	({                                                                                    \
		u64 __timeout_us = (timeout_us);                                              \
		unsigned long __delay_us = (delay_us);                                        \
		ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us);                  \
		if (delay_before_read && __delay_us)                                          \
			udelay(__delay_us);                                                   \
		for (;;) {                                                                    \
			(val) = op(args);                                                     \
			if (cond)                                                             \
				break;                                                        \
			if (__timeout_us && ktime_compare(ktime_get(), __timeout) > 0) {      \
				(val) = op(args);                                             \
				break;                                                        \
			}                                                                     \
			if (__delay_us)                                                       \
				udelay(__delay_us);                                           \
		}                                                                             \
		(cond) ? 0 : -ETIMEDOUT;                                                      \
	})

#ifndef read_poll_timeout_atomic
#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, ...) \
	mali_read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, \
				      __VA_ARGS__)
#endif

#include <linux/refcount.h>

#define kbase_refcount_t refcount_t
#define kbase_refcount_read(x) refcount_read(x)
#define kbase_refcount_set(x, v) refcount_set(x, v)
#define kbase_refcount_dec_and_test(x) refcount_dec_and_test(x)
#define kbase_refcount_dec(x) refcount_dec(x)
#define kbase_refcount_inc_not_zero(x) refcount_inc_not_zero(x)
#define kbase_refcount_inc(x) refcount_inc(x)

#define KBASE_REGISTER_SHRINKER(reclaim, name, priv_data) \
	do {                                              \
		reclaim->private_data = priv_data;        \
		shrinker_register(reclaim);               \
	} while (0)

#define KBASE_UNREGISTER_SHRINKER(reclaim) shrinker_free(reclaim)
#define KBASE_GET_KBASE_DATA_FROM_SHRINKER(s, type, var) s->private_data
#define DEFINE_KBASE_SHRINKER struct shrinker *
#define KBASE_SHRINKER_ALLOC(name) shrinker_alloc(0, name)
#define KBASE_INIT_RECLAIM(var, attr, name) (KBASE_SHRINKER_ALLOC(name))
#define KBASE_SET_RECLAIM(var, attr, reclaim) ((var)->attr = reclaim)

#include <linux/compiler_attributes.h>
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

#define mali_kobj_type const struct kobj_type

static inline unsigned long
kbase_mm_get_unmapped_area_helper(struct mm_struct *mm, struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return mm_get_unmapped_area(mm, filp, addr, len, pgoff, flags);
}

#endif
