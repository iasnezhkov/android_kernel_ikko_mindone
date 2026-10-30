// SPDX-License-Identifier: GPL-2.0
#include "linux/mman.h"
#include <linux/version_compat_defs.h>
#include <mali_kbase.h>
#include <mali_kbase_reg_track.h>

static bool shader_code_align_and_check(unsigned long *gap_end, struct vm_unmapped_area_info *info)
{
	unsigned long align_adjust = (info->align_offset ? info->align_offset : info->length);
	unsigned long align_floor = info->low_limit + align_adjust;

	if (0 == (*gap_end & BASE_MEM_MASK_4GB) && *gap_end >= align_floor)
		(*gap_end) -= align_adjust;
	if (0 == ((*gap_end + info->length) & BASE_MEM_MASK_4GB) && *gap_end >= align_floor)
		(*gap_end) -= align_adjust;

	return ((*gap_end & BASE_MEM_MASK_4GB) && ((*gap_end + info->length) & BASE_MEM_MASK_4GB));
}

static bool align_4gb_no_straddle(unsigned long *gap_end, struct vm_unmapped_area_info *info)
{
	unsigned long start = *gap_end;
	unsigned long end = *gap_end + info->length;
	unsigned long mask = ~((unsigned long)U32_MAX);

	if ((start & mask) != ((end - 1) & mask)) {
		unsigned long offset = end - (end & mask);

		unsigned long rounded_offset = ALIGN(offset, info->align_mask + 1);

		start -= rounded_offset;
		end -= rounded_offset;

		*gap_end = start;

		if (WARN_ONCE((start & mask) != ((end - 1) & mask),
			      "Alignment unexpected straddles over 4GB boundary!"))
			return false;
	}

	return true;
}

#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE) || !defined(__ANDROID_COMMON_KERNEL__)

static bool align_and_check(unsigned long *gap_end, unsigned long gap_start,
			    struct vm_unmapped_area_info *info, bool is_shader_code,
			    bool is_same_4gb_page)
{

	*gap_end -= info->length;
	*gap_end -= (*gap_end - info->align_offset) & info->align_mask;

	if (is_shader_code) {
		if (!shader_code_align_and_check(gap_end, info))
			return false;
	} else if (is_same_4gb_page)
		if (!align_4gb_no_straddle(gap_end, info))
			return false;

	if ((*gap_end < info->low_limit) || (*gap_end < gap_start))
		return false;

	return true;
}
#endif

static unsigned long kbase_unmapped_area_topdown(struct vm_unmapped_area_info *info,
						 bool is_shader_code, bool is_same_4gb_page)
{
#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE)
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	gap_end = info->high_limit;
	if (gap_end < length)
		return -ENOMEM;
	high_limit = gap_end - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	gap_start = mm->highest_vm_end;
	if (gap_start <= high_limit) {
		if (align_and_check(&gap_end, gap_start, info, is_shader_code, is_same_4gb_page))
			return gap_end;
	}

	if (RB_EMPTY_ROOT(&mm->mm_rb))
		return -ENOMEM;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		return -ENOMEM;

	while (true) {

		gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
		if (gap_start <= high_limit && vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right, struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

check_current:

		gap_end = vma->vm_start;
		if (gap_end < low_limit)
			return -ENOMEM;
		if (gap_start <= high_limit && gap_end - gap_start >= length) {

			if (gap_end > info->high_limit)
				gap_end = info->high_limit;

			if (align_and_check(&gap_end, gap_start, info, is_shader_code,
					    is_same_4gb_page))
				return gap_end;
		}

		if (vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left, struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		while (true) {
			struct rb_node *prev = &vma->vm_rb;

			if (!rb_parent(prev))
				return -ENOMEM;
			vma = rb_entry(rb_parent(prev), struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_right) {
				gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
				goto check_current;
			}
		}
	}
#else
#ifdef __ANDROID_COMMON_KERNEL__
	struct vm_unmapped_area_info tmp_info = *info;
	unsigned long length;

	tmp_info.flags |= VM_UNMAPPED_AREA_TOPDOWN;
	if (!(is_shader_code || is_same_4gb_page))
		return vm_unmapped_area(&tmp_info);

	length = info->length + info->align_mask;

	while (true) {
		unsigned long saved_high_lmt = tmp_info.high_limit;
		unsigned long gap_end, start, rev_high_limit;

		gap_end = vm_unmapped_area(&tmp_info);
		if (IS_ERR_VALUE(gap_end))
			return gap_end;

		start = gap_end;
		if (is_shader_code) {
			bool shader_code_aligned;
			unsigned long align_cmp_ref;

			while (true) {

				align_cmp_ref = gap_end;

				shader_code_aligned =
					shader_code_align_and_check(&gap_end, &tmp_info);
				if (shader_code_aligned || (align_cmp_ref == gap_end))
					break;
			}

			if (shader_code_aligned) {
				if (start == gap_end)
					return gap_end;

				rev_high_limit = gap_end + length;
			} else
				break;
		} else {

			if (likely(align_4gb_no_straddle(&gap_end, &tmp_info))) {
				if (start == gap_end)
					return gap_end;

				rev_high_limit = gap_end + length;
			} else
				break;
		}

		if (rev_high_limit < info->low_limit)
			break;

		if (WARN_ONCE(rev_high_limit >= saved_high_lmt,
			      "Unexpected recurring high_limit in search, %lx => %lx\n"
			      "\tinfo-input: limit=[%lx, %lx], mask=%lx, len=%lx\n",
			      saved_high_lmt, rev_high_limit, info->low_limit, info->high_limit,
			      info->align_mask, info->length))
			rev_high_limit = saved_high_lmt -
					 (info->align_offset ? info->align_offset : info->length);

		tmp_info.high_limit = rev_high_limit;
	}
#else
	unsigned long length, high_limit;

	MA_STATE(mas, &current->mm->mm_mt, 0, 0);

	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	high_limit = info->high_limit;
	if ((high_limit - info->low_limit) < length)
		return -ENOMEM;

	while (true) {
		unsigned long gap_start, gap_end;
		unsigned long saved_high_lmt = high_limit;

		if (mas_empty_area_rev(&mas, info->low_limit, high_limit - 1, length))
			return -ENOMEM;

		gap_end = mas.last + 1;
		gap_start = mas.index;

		if (align_and_check(&gap_end, gap_start, info, is_shader_code, is_same_4gb_page))
			return gap_end;

		if (gap_end < info->low_limit)
			return -ENOMEM;

		high_limit = gap_end + length;

		if (WARN_ONCE(high_limit >= saved_high_lmt,
			      "Unexpected recurring high_limit in search, %lx => %lx\n"
			      "\tinfo-input: limit=[%lx, %lx], mask=%lx, len=%lx\n",
			      saved_high_lmt, high_limit, info->low_limit, info->high_limit,
			      info->align_mask, info->length))
			high_limit = saved_high_lmt -
				     (info->align_offset ? info->align_offset : info->length);
		mas_reset(&mas);
	}
#endif
#endif
	return -ENOMEM;
}

unsigned long kbase_context_get_unmapped_area(struct kbase_context *const kctx,
					      const unsigned long addr, const unsigned long len,
					      const unsigned long pgoff, const unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;
	unsigned long align_offset = 0;
	unsigned long align_mask = 0;
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	unsigned long high_limit = arch_get_mmap_base(addr, mm->mmap_base);
	unsigned long low_limit = max_t(unsigned long, PAGE_SIZE, kbase_mmap_min_addr);
#else
	unsigned long high_limit = mm->mmap_base;
	unsigned long low_limit = PAGE_SIZE;
#endif
	unsigned int cpu_va_bits = BITS_PER_LONG;
	unsigned int gpu_pc_bits = kctx->kbdev->gpu_props.log2_program_counter_size;
	bool is_shader_code = false;
	bool is_same_4gb_page = false;
	unsigned long ret;

	struct kbase_reg_zone *zone = kbase_ctx_reg_zone_get_nolock(kctx, SAME_VA_ZONE);
	u64 same_va_end_addr = kbase_reg_zone_end_pfn(zone) << PAGE_SHIFT;
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	const unsigned long mmap_end = arch_get_mmap_end(addr, len, flags);

	if (len > mmap_end - kbase_mmap_min_addr)
		return -ENOMEM;
#endif

	if ((flags & MAP_FIXED) || addr)
		return -EINVAL;

#if IS_ENABLED(CONFIG_64BIT)

	if (len > TASK_SIZE - SZ_2M)
		return -ENOMEM;

	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		high_limit = min_t(unsigned long, high_limit, same_va_end_addr);

		if (kctx->kbdev->gpu_props.mmu.va_bits > 33) {
			if (len >= SZ_2M) {
				align_offset = SZ_2M;
				align_mask = SZ_2M - 1;
			}
		}

		low_limit = SZ_2M;
	} else {
		cpu_va_bits = 32;
	}
#endif
	if ((PFN_DOWN(BASE_MEM_COOKIE_BASE) <= pgoff) &&
	    (PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) > pgoff)) {
		int cookie = pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);
		struct kbase_va_region *reg;

		kbase_gpu_vm_lock(kctx);
		reg = kctx->pending_regions[cookie];
		if (!reg) {
			kbase_gpu_vm_unlock(kctx);
			return -EINVAL;
		}
		if (!(reg->flags & KBASE_REG_GPU_NX)) {
			if (cpu_va_bits > gpu_pc_bits) {
				align_offset = 1ULL << gpu_pc_bits;
				align_mask = align_offset - 1;
				is_shader_code = true;
			}
#if !MALI_USE_CSF
		} else if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
			unsigned long extension_bytes =
				(unsigned long)(reg->extension << PAGE_SHIFT);

			WARN_ON(reg->extension > (ULONG_MAX >> PAGE_SHIFT));
			WARN_ON(reg->initial_commit > (ULONG_MAX >> PAGE_SHIFT));
			WARN_ON(!is_power_of_2(extension_bytes));
			align_mask = extension_bytes - 1;
			align_offset = extension_bytes - (reg->initial_commit << PAGE_SHIFT);
#endif
		} else if (reg->flags & KBASE_REG_GPU_VA_SAME_4GB_PAGE) {
			is_same_4gb_page = true;
		}
		kbase_gpu_vm_unlock(kctx);
	} else if (!IS_ENABLED(CONFIG_64BIT)) {
		return kbase_mm_get_unmapped_area_helper(mm, kctx->filp, addr, len, pgoff, flags);
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = low_limit;
	info.high_limit = high_limit;
	info.align_offset = align_offset;
	info.align_mask = align_mask;

	ret = kbase_unmapped_area_topdown(&info, is_shader_code, is_same_4gb_page);

	if (IS_ERR_VALUE(ret) && high_limit == mm->mmap_base && high_limit < same_va_end_addr) {
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)

		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = min_t(u64, mmap_end, same_va_end_addr);
#else

		info.low_limit = mm->mmap_base;
		info.high_limit = min_t(u64, TASK_SIZE, same_va_end_addr);
#endif

		ret = kbase_unmapped_area_topdown(&info, is_shader_code, is_same_4gb_page);
	}

	return ret;
}
