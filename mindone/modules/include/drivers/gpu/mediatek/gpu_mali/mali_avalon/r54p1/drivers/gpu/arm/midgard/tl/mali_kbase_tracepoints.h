/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_TRACEPOINTS_H)
#define _KBASE_TRACEPOINTS_H

#include "mali_kbase.h"
#include "mali_kbase_gator.h"

#include <linux/types.h>
#include <linux/atomic.h>

struct kbase_tlstream;

extern const size_t __obj_stream_offset;
extern const size_t __aux_stream_offset;

#define __TL_DISPATCH_STREAM(kbdev, stype)	\
	((struct kbase_tlstream *)	\
	 ((u8 *)kbdev->timeline + __ ## stype ## _stream_offset))

struct tp_desc;

extern const char   *obj_desc_header;
extern const size_t  obj_desc_header_size;

extern const char   *aux_desc_header;
extern const size_t  aux_desc_header_size;

#define TL_ATOM_STATE_IDLE 0
#define TL_ATOM_STATE_READY 1
#define TL_ATOM_STATE_DONE 2
#define TL_ATOM_STATE_POSTED 3

#define TL_JS_EVENT_START     GATOR_JOB_SLOT_START
#define TL_JS_EVENT_STOP      GATOR_JOB_SLOT_STOP
#define TL_JS_EVENT_SOFT_STOP GATOR_JOB_SLOT_SOFT_STOPPED

#define TL_PM_STATE_SHADER 0x100
#define TL_PM_STATE_TILER 0x110
#define TL_PM_STATE_L2 0x120
#define TL_PM_STATE_STACK 0xE00

#define TLSTREAM_ENABLED (1u << 31)

void __kbase_tlstream_tl_new_ctx(
	struct kbase_tlstream *stream,
	const void *ctx,
	u32 ctx_nr,
	u32 tgid
);

void __kbase_tlstream_tl_new_gpu(
	struct kbase_tlstream *stream,
	const void *gpu,
	u32 gpu_id,
	u32 core_count
);

void __kbase_tlstream_tl_new_lpu(
	struct kbase_tlstream *stream,
	const void *lpu,
	u32 lpu_nr,
	u32 lpu_fn
);

void __kbase_tlstream_tl_new_atom(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 atom_nr
);

void __kbase_tlstream_tl_new_as(
	struct kbase_tlstream *stream,
	const void *address_space,
	u32 as_nr
);

void __kbase_tlstream_tl_del_ctx(
	struct kbase_tlstream *stream,
	const void *ctx
);

void __kbase_tlstream_tl_del_atom(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_lifelink_lpu_gpu(
	struct kbase_tlstream *stream,
	const void *lpu,
	const void *gpu
);

void __kbase_tlstream_tl_lifelink_as_gpu(
	struct kbase_tlstream *stream,
	const void *address_space,
	const void *gpu
);

void __kbase_tlstream_tl_ret_ctx_lpu(
	struct kbase_tlstream *stream,
	const void *ctx,
	const void *lpu
);

void __kbase_tlstream_tl_ret_atom_ctx(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *ctx
);

void __kbase_tlstream_tl_ret_atom_lpu(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *lpu,
	const char *attrib_match_list
);

void __kbase_tlstream_tl_nret_ctx_lpu(
	struct kbase_tlstream *stream,
	const void *ctx,
	const void *lpu
);

void __kbase_tlstream_tl_nret_atom_ctx(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *ctx
);

void __kbase_tlstream_tl_nret_atom_lpu(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *lpu
);

void __kbase_tlstream_tl_ret_as_ctx(
	struct kbase_tlstream *stream,
	const void *address_space,
	const void *ctx
);

void __kbase_tlstream_tl_nret_as_ctx(
	struct kbase_tlstream *stream,
	const void *address_space,
	const void *ctx
);

void __kbase_tlstream_tl_ret_atom_as(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *address_space
);

void __kbase_tlstream_tl_nret_atom_as(
	struct kbase_tlstream *stream,
	const void *atom,
	const void *address_space
);

void __kbase_tlstream_tl_attrib_atom_config(
	struct kbase_tlstream *stream,
	const void *atom,
	u64 descriptor,
	u64 affinity,
	u32 config
);

void __kbase_tlstream_tl_jit_usedpages(
	struct kbase_tlstream *stream,
	u64 used_pages,
	u32 j_id
);

void __kbase_tlstream_tl_attrib_atom_jitallocinfo(
	struct kbase_tlstream *stream,
	const void *atom,
	u64 va_pgs,
	u64 com_pgs,
	u64 extent,
	u32 j_id,
	u32 bin_id,
	u32 max_allocs,
	u32 jit_flags,
	u32 usg_id
);

void __kbase_tlstream_tl_attrib_atom_jitfreeinfo(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 j_id
);

void __kbase_tlstream_tl_attrib_as_config(
	struct kbase_tlstream *stream,
	const void *address_space,
	u64 transtab,
	u64 memattr,
	u64 transcfg
);

void __kbase_tlstream_tl_event_lpu_softstop(
	struct kbase_tlstream *stream,
	const void *lpu
);

void __kbase_tlstream_tl_event_atom_softstop_ex(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_event_atom_softstop_issue(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_event_atom_softjob_start(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_event_atom_softjob_end(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_arbiter_granted(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_tl_arbiter_started(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_tl_arbiter_stop_requested(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_tl_arbiter_stopped(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_tl_arbiter_requested(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_jd_gpu_soft_reset(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_jd_tiler_heap_chunk_alloc(
	struct kbase_tlstream *stream,
	u32 ctx_nr,
	u64 heap_id,
	u64 chunk_va
);

void __kbase_tlstream_tl_js_sched_start(
	struct kbase_tlstream *stream,
	u32 dummy
);

void __kbase_tlstream_tl_js_sched_end(
	struct kbase_tlstream *stream,
	u32 dummy
);

void __kbase_tlstream_tl_jd_submit_atom_start(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_submit_atom_end(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_done_no_lock_start(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_done_no_lock_end(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_done_start(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_done_end(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_jd_atom_complete(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_run_atom_start(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 atom_nr
);

void __kbase_tlstream_tl_run_atom_end(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 atom_nr
);

void __kbase_tlstream_tl_attrib_atom_priority(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 prio
);

void __kbase_tlstream_tl_attrib_atom_state(
	struct kbase_tlstream *stream,
	const void *atom,
	u32 state
);

void __kbase_tlstream_tl_attrib_atom_prioritized(
	struct kbase_tlstream *stream,
	const void *atom
);

void __kbase_tlstream_tl_attrib_atom_jit(
	struct kbase_tlstream *stream,
	const void *atom,
	u64 edit_addr,
	u64 new_addr,
	u32 jit_flags,
	u64 mem_flags,
	u32 j_id,
	u64 com_pgs,
	u64 extent,
	u64 va_pgs
);

void __kbase_tlstream_tl_kbase_new_device(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kbase_device_gpu_core_count,
	u32 kbase_device_max_num_csgs,
	u32 kbase_device_as_count,
	u32 kbase_device_sb_entry_count,
	u32 kbase_device_has_cross_stream_sync,
	u32 kbase_device_supports_gpu_sleep,
	u32 kbase_device_has_vd54d34dbb40917c8cea48cca407a8789413be0db
);

void __kbase_tlstream_tl_kbase_gpucmdqueue_kick(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id,
	u64 buffer_gpu_addr
);

void __kbase_tlstream_tl_kbase_device_program_csg(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kernel_ctx_id,
	u32 gpu_cmdq_grp_handle,
	u32 kbase_device_csg_slot_index,
	u32 kbase_device_csg_slot_resuming
);

void __kbase_tlstream_tl_kbase_device_deprogram_csg(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kbase_device_csg_slot_index
);

void __kbase_tlstream_tl_kbase_device_halting_csg(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kbase_device_csg_slot_index,
	u32 kbase_device_csg_slot_suspending
);

void __kbase_tlstream_tl_kbase_device_suspend_csg(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kbase_device_csg_slot_index
);

void __kbase_tlstream_tl_kbase_device_csg_idle(
	struct kbase_tlstream *stream,
	u32 kbase_device_id,
	u32 kbase_device_csg_slot_index
);

void __kbase_tlstream_tl_kbase_new_ctx(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id,
	u32 kbase_device_id
);

void __kbase_tlstream_tl_kbase_del_ctx(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id
);

void __kbase_tlstream_tl_kbase_ctx_assign_as(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id,
	u32 kbase_device_as_index
);

void __kbase_tlstream_tl_kbase_ctx_unassign_as(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id
);

void __kbase_tlstream_tl_kbase_new_kcpuqueue(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 kcpu_queue_id,
	u32 kernel_ctx_id,
	u32 kcpuq_num_pending_cmds
);

void __kbase_tlstream_tl_kbase_del_kcpuqueue(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_fence_signal(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	const void *fence
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_fence_wait(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	const void *fence
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_wait(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 cqs_obj_gpu_addr,
	u32 compare_value,
	u32 inherit_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_set(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 cqs_obj_gpu_addr
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_wait_operation(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 cqs_obj_gpu_addr,
	u64 compare_value,
	u32 condition,
	u32 data_type,
	u32 inherit_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_set_operation(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 cqs_obj_gpu_addr,
	u64 value,
	u32 operation,
	u32 data_type
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_map_import(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 map_import_buf_gpu_addr
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_unmap_import(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 map_import_buf_gpu_addr
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_unmap_import_force(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 map_import_buf_gpu_addr
);

void __kbase_tlstream_tl_kbase_array_begin_kcpuqueue_enqueue_jit_alloc(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_item_kcpuqueue_enqueue_jit_alloc(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u64 jit_alloc_gpu_alloc_addr_dest,
	u64 jit_alloc_va_pages,
	u64 jit_alloc_commit_pages,
	u64 jit_alloc_extent,
	u32 jit_alloc_jit_id,
	u32 jit_alloc_bin_id,
	u32 jit_alloc_max_allocations,
	u32 jit_alloc_flags,
	u32 jit_alloc_usage_id
);

void __kbase_tlstream_tl_kbase_array_end_kcpuqueue_enqueue_jit_alloc(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_begin_kcpuqueue_enqueue_jit_free(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_item_kcpuqueue_enqueue_jit_free(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 jit_alloc_jit_id
);

void __kbase_tlstream_tl_kbase_array_end_kcpuqueue_enqueue_jit_free(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_error_barrier(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_enqueue_group_suspend(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	const void *group_suspend_buf,
	u32 gpu_cmdq_grp_handle
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_signal_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_signal_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_wait_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_wait_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_set(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_operation_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_operation_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_set_operation(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_map_import_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_map_import_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_force_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_force_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_jit_alloc_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_begin_kcpuqueue_execute_jit_alloc_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_item_kcpuqueue_execute_jit_alloc_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error,
	u64 jit_alloc_gpu_alloc_addr,
	u64 jit_alloc_mmu_flags
);

void __kbase_tlstream_tl_kbase_array_end_kcpuqueue_execute_jit_alloc_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_jit_free_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_begin_kcpuqueue_execute_jit_free_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_array_item_kcpuqueue_execute_jit_free_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error,
	u64 jit_free_pages_used
);

void __kbase_tlstream_tl_kbase_array_end_kcpuqueue_execute_jit_free_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_error_barrier(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_group_suspend_start(
	struct kbase_tlstream *stream,
	const void *kcpu_queue
);

void __kbase_tlstream_tl_kbase_kcpuqueue_execute_group_suspend_end(
	struct kbase_tlstream *stream,
	const void *kcpu_queue,
	u32 execute_error
);

void __kbase_tlstream_tl_kbase_csffw_fw_reloading(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_enabling(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_request_sleep(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_request_wakeup(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_request_halt(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_disabling(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_fw_off(
	struct kbase_tlstream *stream,
	u64 csffw_cycle
);

void __kbase_tlstream_tl_kbase_csffw_tlstream_overflow(
	struct kbase_tlstream *stream,
	u64 csffw_timestamp,
	u64 csffw_cycle
);

void __kbase_tlstream_aux_pm_state(
	struct kbase_tlstream *stream,
	u32 core_type,
	u64 core_state_bitset
);

void __kbase_tlstream_aux_pagefault(
	struct kbase_tlstream *stream,
	u32 ctx_nr,
	u32 as_nr,
	u64 page_cnt_change
);

void __kbase_tlstream_aux_pagesalloc(
	struct kbase_tlstream *stream,
	u32 ctx_nr,
	u64 page_cnt
);

void __kbase_tlstream_aux_devfreq_target(
	struct kbase_tlstream *stream,
	u64 target_freq
);

void __kbase_tlstream_aux_jit_stats(
	struct kbase_tlstream *stream,
	u32 ctx_nr,
	u32 bid,
	u32 max_allocs,
	u32 allocs,
	u32 va_pages,
	u32 ph_pages
);

void __kbase_tlstream_aux_tiler_heap_stats(
	struct kbase_tlstream *stream,
	u32 ctx_nr,
	u64 heap_id,
	u32 va_pages,
	u32 ph_pages,
	u32 max_chunks,
	u32 chunk_size,
	u32 chunk_count,
	u32 target_in_flight,
	u32 nr_in_flight
);

void __kbase_tlstream_aux_event_job_slot(
	struct kbase_tlstream *stream,
	const void *ctx,
	u32 slot_nr,
	u32 atom_nr,
	u32 event
);

void __kbase_tlstream_aux_protected_enter_start(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_aux_protected_enter_end(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_aux_mmu_command(
	struct kbase_tlstream *stream,
	u32 kernel_ctx_id,
	u32 mmu_cmd_id,
	u32 mmu_synchronicity,
	u64 mmu_lock_addr,
	u32 mmu_lock_page_num
);

void __kbase_tlstream_aux_protected_leave_start(
	struct kbase_tlstream *stream,
	const void *gpu
);

void __kbase_tlstream_aux_protected_leave_end(
	struct kbase_tlstream *stream,
	const void *gpu
);

struct kbase_tlstream;

#define KBASE_TLSTREAM_TL_NEW_CTX(	\
	kbdev,	\
	ctx,	\
	ctx_nr,	\
	tgid	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_new_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				ctx,	\
				ctx_nr,	\
				tgid	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NEW_GPU(	\
	kbdev,	\
	gpu,	\
	gpu_id,	\
	core_count	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_new_gpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu,	\
				gpu_id,	\
				core_count	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NEW_LPU(	\
	kbdev,	\
	lpu,	\
	lpu_nr,	\
	lpu_fn	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_new_lpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				lpu,	\
				lpu_nr,	\
				lpu_fn	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NEW_ATOM(	\
	kbdev,	\
	atom,	\
	atom_nr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_new_atom(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				atom_nr	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NEW_AS(	\
	kbdev,	\
	address_space,	\
	as_nr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_new_as(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				address_space,	\
				as_nr	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_DEL_CTX(	\
	kbdev,	\
	ctx	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_del_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				ctx	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_DEL_ATOM(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_del_atom(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_LIFELINK_LPU_GPU(	\
	kbdev,	\
	lpu,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_lifelink_lpu_gpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				lpu,	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_LIFELINK_AS_GPU(	\
	kbdev,	\
	address_space,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_lifelink_as_gpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				address_space,	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RET_CTX_LPU(	\
	kbdev,	\
	ctx,	\
	lpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_ret_ctx_lpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				ctx,	\
				lpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RET_ATOM_CTX(	\
	kbdev,	\
	atom,	\
	ctx	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_ret_atom_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				ctx	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RET_ATOM_LPU(	\
	kbdev,	\
	atom,	\
	lpu,	\
	attrib_match_list	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_ret_atom_lpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				lpu,	\
				attrib_match_list	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NRET_CTX_LPU(	\
	kbdev,	\
	ctx,	\
	lpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_nret_ctx_lpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				ctx,	\
				lpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NRET_ATOM_CTX(	\
	kbdev,	\
	atom,	\
	ctx	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_nret_atom_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				ctx	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NRET_ATOM_LPU(	\
	kbdev,	\
	atom,	\
	lpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_nret_atom_lpu(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				lpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RET_AS_CTX(	\
	kbdev,	\
	address_space,	\
	ctx	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_ret_as_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				address_space,	\
				ctx	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NRET_AS_CTX(	\
	kbdev,	\
	address_space,	\
	ctx	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_nret_as_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				address_space,	\
				ctx	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RET_ATOM_AS(	\
	kbdev,	\
	atom,	\
	address_space	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_ret_atom_as(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				address_space	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_NRET_ATOM_AS(	\
	kbdev,	\
	atom,	\
	address_space	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_nret_atom_as(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				address_space	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_CONFIG(	\
	kbdev,	\
	atom,	\
	descriptor,	\
	affinity,	\
	config	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_attrib_atom_config(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				descriptor,	\
				affinity,	\
				config	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JIT_USEDPAGES(	\
	kbdev,	\
	used_pages,	\
	j_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jit_usedpages(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				used_pages,	\
				j_id	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_JITALLOCINFO(	\
	kbdev,	\
	atom,	\
	va_pgs,	\
	com_pgs,	\
	extent,	\
	j_id,	\
	bin_id,	\
	max_allocs,	\
	jit_flags,	\
	usg_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_attrib_atom_jitallocinfo(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				va_pgs,	\
				com_pgs,	\
				extent,	\
				j_id,	\
				bin_id,	\
				max_allocs,	\
				jit_flags,	\
				usg_id	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_JITFREEINFO(	\
	kbdev,	\
	atom,	\
	j_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_attrib_atom_jitfreeinfo(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				j_id	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_AS_CONFIG(	\
	kbdev,	\
	address_space,	\
	transtab,	\
	memattr,	\
	transcfg	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_attrib_as_config(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				address_space,	\
				transtab,	\
				memattr,	\
				transcfg	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_EVENT_LPU_SOFTSTOP(	\
	kbdev,	\
	lpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_event_lpu_softstop(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				lpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTSTOP_EX(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_event_atom_softstop_ex(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTSTOP_ISSUE(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_event_atom_softstop_issue(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_START(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_event_atom_softjob_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTJOB_END(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_event_atom_softjob_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ARBITER_GRANTED(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_arbiter_granted(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ARBITER_STARTED(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_arbiter_started(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ARBITER_STOP_REQUESTED(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_arbiter_stop_requested(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ARBITER_STOPPED(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_arbiter_stopped(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ARBITER_REQUESTED(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_arbiter_requested(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_JD_GPU_SOFT_RESET(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_jd_gpu_soft_reset(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_JD_TILER_HEAP_CHUNK_ALLOC(	\
	kbdev,	\
	ctx_nr,	\
	heap_id,	\
	chunk_va	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_jd_tiler_heap_chunk_alloc(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				ctx_nr,	\
				heap_id,	\
				chunk_va	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JS_SCHED_START(	\
	kbdev,	\
	dummy	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_js_sched_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				dummy	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JS_SCHED_END(	\
	kbdev,	\
	dummy	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_js_sched_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				dummy	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_SUBMIT_ATOM_START(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_submit_atom_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_SUBMIT_ATOM_END(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_submit_atom_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_DONE_NO_LOCK_START(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_done_no_lock_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_DONE_NO_LOCK_END(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_done_no_lock_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_DONE_START(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_done_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_DONE_END(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_done_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_JD_ATOM_COMPLETE(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_jd_atom_complete(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RUN_ATOM_START(	\
	kbdev,	\
	atom,	\
	atom_nr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_run_atom_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				atom_nr	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_RUN_ATOM_END(	\
	kbdev,	\
	atom,	\
	atom_nr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_tl_run_atom_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				atom_nr	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_PRIORITY(	\
	kbdev,	\
	atom,	\
	prio	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS)	\
			__kbase_tlstream_tl_attrib_atom_priority(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				prio	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(	\
	kbdev,	\
	atom,	\
	state	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS)	\
			__kbase_tlstream_tl_attrib_atom_state(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				state	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_PRIORITIZED(	\
	kbdev,	\
	atom	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS)	\
			__kbase_tlstream_tl_attrib_atom_prioritized(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_TL_ATTRIB_ATOM_JIT(	\
	kbdev,	\
	atom,	\
	edit_addr,	\
	new_addr,	\
	jit_flags,	\
	mem_flags,	\
	j_id,	\
	com_pgs,	\
	extent,	\
	va_pgs	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_JOB_DUMPING_ENABLED)	\
			__kbase_tlstream_tl_attrib_atom_jit(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				atom,	\
				edit_addr,	\
				new_addr,	\
				jit_flags,	\
				mem_flags,	\
				j_id,	\
				com_pgs,	\
				extent,	\
				va_pgs	\
				);	\
	} while (0)

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_NEW_DEVICE(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_gpu_core_count,	\
	kbase_device_max_num_csgs,	\
	kbase_device_as_count,	\
	kbase_device_sb_entry_count,	\
	kbase_device_has_cross_stream_sync,	\
	kbase_device_supports_gpu_sleep,	\
	kbase_device_has_vd54d34dbb40917c8cea48cca407a8789413be0db	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_new_device(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kbase_device_gpu_core_count,	\
				kbase_device_max_num_csgs,	\
				kbase_device_as_count,	\
				kbase_device_sb_entry_count,	\
				kbase_device_has_cross_stream_sync,	\
				kbase_device_supports_gpu_sleep,	\
				kbase_device_has_vd54d34dbb40917c8cea48cca407a8789413be0db	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_NEW_DEVICE(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_gpu_core_count,	\
	kbase_device_max_num_csgs,	\
	kbase_device_as_count,	\
	kbase_device_sb_entry_count,	\
	kbase_device_has_cross_stream_sync,	\
	kbase_device_supports_gpu_sleep,	\
	kbase_device_has_vd54d34dbb40917c8cea48cca407a8789413be0db	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_GPUCMDQUEUE_KICK(	\
	kbdev,	\
	kernel_ctx_id,	\
	buffer_gpu_addr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_gpucmdqueue_kick(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kernel_ctx_id,	\
				buffer_gpu_addr	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_GPUCMDQUEUE_KICK(	\
	kbdev,	\
	kernel_ctx_id,	\
	buffer_gpu_addr	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_PROGRAM_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kernel_ctx_id,	\
	gpu_cmdq_grp_handle,	\
	kbase_device_csg_slot_index,	\
	kbase_device_csg_slot_resuming	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_device_program_csg(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kernel_ctx_id,	\
				gpu_cmdq_grp_handle,	\
				kbase_device_csg_slot_index,	\
				kbase_device_csg_slot_resuming	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_PROGRAM_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kernel_ctx_id,	\
	gpu_cmdq_grp_handle,	\
	kbase_device_csg_slot_index,	\
	kbase_device_csg_slot_resuming	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_DEPROGRAM_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_device_deprogram_csg(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kbase_device_csg_slot_index	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_DEPROGRAM_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_HALTING_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index,	\
	kbase_device_csg_slot_suspending	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_device_halting_csg(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kbase_device_csg_slot_index,	\
				kbase_device_csg_slot_suspending	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_HALTING_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index,	\
	kbase_device_csg_slot_suspending	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_SUSPEND_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_device_suspend_csg(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kbase_device_csg_slot_index	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_SUSPEND_CSG(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_CSG_IDLE(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_device_csg_idle(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kbase_device_id,	\
				kbase_device_csg_slot_index	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEVICE_CSG_IDLE(	\
	kbdev,	\
	kbase_device_id,	\
	kbase_device_csg_slot_index	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_NEW_CTX(	\
	kbdev,	\
	kernel_ctx_id,	\
	kbase_device_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_new_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kernel_ctx_id,	\
				kbase_device_id	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_NEW_CTX(	\
	kbdev,	\
	kernel_ctx_id,	\
	kbase_device_id	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEL_CTX(	\
	kbdev,	\
	kernel_ctx_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_del_ctx(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kernel_ctx_id	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEL_CTX(	\
	kbdev,	\
	kernel_ctx_id	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CTX_ASSIGN_AS(	\
	kbdev,	\
	kernel_ctx_id,	\
	kbase_device_as_index	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_ctx_assign_as(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kernel_ctx_id,	\
				kbase_device_as_index	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CTX_ASSIGN_AS(	\
	kbdev,	\
	kernel_ctx_id,	\
	kbase_device_as_index	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(	\
	kbdev,	\
	kernel_ctx_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_ctx_unassign_as(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kernel_ctx_id	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(	\
	kbdev,	\
	kernel_ctx_id	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_NEW_KCPUQUEUE(	\
	kbdev,	\
	kcpu_queue,	\
	kcpu_queue_id,	\
	kernel_ctx_id,	\
	kcpuq_num_pending_cmds	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_new_kcpuqueue(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				kcpu_queue_id,	\
				kernel_ctx_id,	\
				kcpuq_num_pending_cmds	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_NEW_KCPUQUEUE(	\
	kbdev,	\
	kcpu_queue,	\
	kcpu_queue_id,	\
	kernel_ctx_id,	\
	kcpuq_num_pending_cmds	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_DEL_KCPUQUEUE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_del_kcpuqueue(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_DEL_KCPUQUEUE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_FENCE_SIGNAL(	\
	kbdev,	\
	kcpu_queue,	\
	fence	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_fence_signal(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				fence	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_FENCE_SIGNAL(	\
	kbdev,	\
	kcpu_queue,	\
	fence	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_FENCE_WAIT(	\
	kbdev,	\
	kcpu_queue,	\
	fence	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_fence_wait(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				fence	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_FENCE_WAIT(	\
	kbdev,	\
	kcpu_queue,	\
	fence	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_WAIT(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	compare_value,	\
	inherit_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_wait(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				cqs_obj_gpu_addr,	\
				compare_value,	\
				inherit_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_WAIT(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	compare_value,	\
	inherit_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_SET(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_set(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				cqs_obj_gpu_addr	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_SET(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_WAIT_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	compare_value,	\
	condition,	\
	data_type,	\
	inherit_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_wait_operation(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				cqs_obj_gpu_addr,	\
				compare_value,	\
				condition,	\
				data_type,	\
				inherit_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_WAIT_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	compare_value,	\
	condition,	\
	data_type,	\
	inherit_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_SET_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	value,	\
	operation,	\
	data_type	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_cqs_set_operation(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				cqs_obj_gpu_addr,	\
				value,	\
				operation,	\
				data_type	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_CQS_SET_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	cqs_obj_gpu_addr,	\
	value,	\
	operation,	\
	data_type	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_MAP_IMPORT(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_map_import(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				map_import_buf_gpu_addr	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_MAP_IMPORT(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_UNMAP_IMPORT(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_unmap_import(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				map_import_buf_gpu_addr	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_UNMAP_IMPORT(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_UNMAP_IMPORT_FORCE(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_unmap_import_force(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				map_import_buf_gpu_addr	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_UNMAP_IMPORT_FORCE(	\
	kbdev,	\
	kcpu_queue,	\
	map_import_buf_gpu_addr	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_begin_kcpuqueue_enqueue_jit_alloc(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue,	\
	jit_alloc_gpu_alloc_addr_dest,	\
	jit_alloc_va_pages,	\
	jit_alloc_commit_pages,	\
	jit_alloc_extent,	\
	jit_alloc_jit_id,	\
	jit_alloc_bin_id,	\
	jit_alloc_max_allocations,	\
	jit_alloc_flags,	\
	jit_alloc_usage_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_item_kcpuqueue_enqueue_jit_alloc(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				jit_alloc_gpu_alloc_addr_dest,	\
				jit_alloc_va_pages,	\
				jit_alloc_commit_pages,	\
				jit_alloc_extent,	\
				jit_alloc_jit_id,	\
				jit_alloc_bin_id,	\
				jit_alloc_max_allocations,	\
				jit_alloc_flags,	\
				jit_alloc_usage_id	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue,	\
	jit_alloc_gpu_alloc_addr_dest,	\
	jit_alloc_va_pages,	\
	jit_alloc_commit_pages,	\
	jit_alloc_extent,	\
	jit_alloc_jit_id,	\
	jit_alloc_bin_id,	\
	jit_alloc_max_allocations,	\
	jit_alloc_flags,	\
	jit_alloc_usage_id	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_end_kcpuqueue_enqueue_jit_alloc(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_ENQUEUE_JIT_ALLOC(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_begin_kcpuqueue_enqueue_jit_free(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue,	\
	jit_alloc_jit_id	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_item_kcpuqueue_enqueue_jit_free(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				jit_alloc_jit_id	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue,	\
	jit_alloc_jit_id	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_end_kcpuqueue_enqueue_jit_free(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_ENQUEUE_JIT_FREE(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_ERROR_BARRIER(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_error_barrier(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_ERROR_BARRIER(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_GROUP_SUSPEND(	\
	kbdev,	\
	kcpu_queue,	\
	group_suspend_buf,	\
	gpu_cmdq_grp_handle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_enqueue_group_suspend(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				group_suspend_buf,	\
				gpu_cmdq_grp_handle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_ENQUEUE_GROUP_SUSPEND(	\
	kbdev,	\
	kcpu_queue,	\
	group_suspend_buf,	\
	gpu_cmdq_grp_handle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_SIGNAL_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_signal_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_SIGNAL_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_SIGNAL_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_signal_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_SIGNAL_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_WAIT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_wait_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_WAIT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_WAIT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_fence_wait_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_FENCE_WAIT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_SET(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_set(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_SET(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_OPERATION_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_operation_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_OPERATION_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_OPERATION_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_wait_operation_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_WAIT_OPERATION_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_SET_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_cqs_set_operation(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_CQS_SET_OPERATION(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_MAP_IMPORT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_map_import_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_MAP_IMPORT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_MAP_IMPORT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_map_import_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_MAP_IMPORT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_FORCE_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_force_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_FORCE_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_FORCE_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_unmap_import_force_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_UNMAP_IMPORT_FORCE_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_JIT_ALLOC_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_jit_alloc_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_JIT_ALLOC_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_begin_kcpuqueue_execute_jit_alloc_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error,	\
	jit_alloc_gpu_alloc_addr,	\
	jit_alloc_mmu_flags	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_item_kcpuqueue_execute_jit_alloc_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error,	\
				jit_alloc_gpu_alloc_addr,	\
				jit_alloc_mmu_flags	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error,	\
	jit_alloc_gpu_alloc_addr,	\
	jit_alloc_mmu_flags	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_end_kcpuqueue_execute_jit_alloc_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_EXECUTE_JIT_ALLOC_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_JIT_FREE_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_jit_free_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_JIT_FREE_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_begin_kcpuqueue_execute_jit_free_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_BEGIN_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error,	\
	jit_free_pages_used	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_item_kcpuqueue_execute_jit_free_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error,	\
				jit_free_pages_used	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_ITEM_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error,	\
	jit_free_pages_used	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_array_end_kcpuqueue_execute_jit_free_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_ARRAY_END_KCPUQUEUE_EXECUTE_JIT_FREE_END(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_ERROR_BARRIER(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_error_barrier(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_ERROR_BARRIER(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_GROUP_SUSPEND_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_group_suspend_start(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_GROUP_SUSPEND_START(	\
	kbdev,	\
	kcpu_queue	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_GROUP_SUSPEND_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_kcpuqueue_execute_group_suspend_end(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				kcpu_queue,	\
				execute_error	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_KCPUQUEUE_EXECUTE_GROUP_SUSPEND_END(	\
	kbdev,	\
	kcpu_queue,	\
	execute_error	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_RELOADING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_reloading(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_RELOADING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_ENABLING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_enabling(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_ENABLING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_SLEEP(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_request_sleep(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_SLEEP(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_WAKEUP(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_request_wakeup(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_WAKEUP(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_HALT(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_request_halt(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_REQUEST_HALT(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_DISABLING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_disabling(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_DISABLING(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_OFF(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_fw_off(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_FW_OFF(	\
	kbdev,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#if MALI_USE_CSF
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_TLSTREAM_OVERFLOW(	\
	kbdev,	\
	csffw_timestamp,	\
	csffw_cycle	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)	\
			__kbase_tlstream_tl_kbase_csffw_tlstream_overflow(	\
				__TL_DISPATCH_STREAM(kbdev, obj),	\
				csffw_timestamp,	\
				csffw_cycle	\
				);	\
	} while (0)
#else
#define KBASE_TLSTREAM_TL_KBASE_CSFFW_TLSTREAM_OVERFLOW(	\
	kbdev,	\
	csffw_timestamp,	\
	csffw_cycle	\
	)	\
	do { } while (0)
#endif

#define KBASE_TLSTREAM_AUX_PM_STATE(	\
	kbdev,	\
	core_type,	\
	core_state_bitset	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pm_state(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				core_type,	\
				core_state_bitset	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PAGEFAULT(	\
	kbdev,	\
	ctx_nr,	\
	as_nr,	\
	page_cnt_change	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pagefault(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr,	\
				as_nr,	\
				page_cnt_change	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PAGESALLOC(	\
	kbdev,	\
	ctx_nr,	\
	page_cnt	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pagesalloc(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr,	\
				page_cnt	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_DEVFREQ_TARGET(	\
	kbdev,	\
	target_freq	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_devfreq_target(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				target_freq	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_JIT_STATS(	\
	kbdev,	\
	ctx_nr,	\
	bid,	\
	max_allocs,	\
	allocs,	\
	va_pages,	\
	ph_pages	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_jit_stats(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr,	\
				bid,	\
				max_allocs,	\
				allocs,	\
				va_pages,	\
				ph_pages	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(	\
	kbdev,	\
	ctx_nr,	\
	heap_id,	\
	va_pages,	\
	ph_pages,	\
	max_chunks,	\
	chunk_size,	\
	chunk_count,	\
	target_in_flight,	\
	nr_in_flight	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_tiler_heap_stats(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr,	\
				heap_id,	\
				va_pages,	\
				ph_pages,	\
				max_chunks,	\
				chunk_size,	\
				chunk_count,	\
				target_in_flight,	\
				nr_in_flight	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(	\
	kbdev,	\
	ctx,	\
	slot_nr,	\
	atom_nr,	\
	event	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_event_job_slot(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx,	\
				slot_nr,	\
				atom_nr,	\
				event	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PROTECTED_ENTER_START(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_protected_enter_start(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PROTECTED_ENTER_END(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_protected_enter_end(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_MMU_COMMAND(	\
	kbdev,	\
	kernel_ctx_id,	\
	mmu_cmd_id,	\
	mmu_synchronicity,	\
	mmu_lock_addr,	\
	mmu_lock_page_num	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_mmu_command(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				kernel_ctx_id,	\
				mmu_cmd_id,	\
				mmu_synchronicity,	\
				mmu_lock_addr,	\
				mmu_lock_page_num	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_START(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS)	\
			__kbase_tlstream_aux_protected_leave_start(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				gpu	\
				);	\
	} while (0)

#define KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_END(	\
	kbdev,	\
	gpu	\
	)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		if (enabled & BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS)	\
			__kbase_tlstream_aux_protected_leave_end(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				gpu	\
				);	\
	} while (0)

#if defined(CONFIG_MALI_GATOR_SUPPORT)

#undef KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT
#define KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(kbdev,	\
	context, slot_nr, atom_nr, event)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		kbase_trace_mali_job_slots_event(kbdev->id,	\
			GATOR_MAKE_EVENT(event, slot_nr),	\
			context, (u8) atom_nr);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_event_job_slot(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				context, slot_nr, atom_nr, event);	\
	} while (0)

#undef KBASE_TLSTREAM_AUX_PM_STATE
#define KBASE_TLSTREAM_AUX_PM_STATE(kbdev, core_type, state)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		kbase_trace_mali_pm_status(kbdev->id,	\
			core_type, state);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pm_state(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				core_type, state);	\
	} while (0)

#undef KBASE_TLSTREAM_AUX_PAGEFAULT
#define KBASE_TLSTREAM_AUX_PAGEFAULT(kbdev,	\
	ctx_nr, as_nr, page_cnt_change)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		kbase_trace_mali_page_fault_insert_pages(kbdev->id,	\
			(int)as_nr,	\
			page_cnt_change);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pagefault(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr, as_nr, page_cnt_change);	\
	} while (0)

#undef KBASE_TLSTREAM_AUX_PAGESALLOC
#define KBASE_TLSTREAM_AUX_PAGESALLOC(kbdev, ctx_nr, page_cnt)	\
	do {	\
		u32 enabled = (u32)atomic_read(&kbdev->timeline_flags);	\
		u32 global_pages_count =	\
			(u32)atomic_read(&kbdev->memdev.used_pages);	\
			\
		kbase_trace_mali_total_alloc_pages_change(kbdev->id,	\
			global_pages_count);	\
		if (enabled & TLSTREAM_ENABLED)	\
			__kbase_tlstream_aux_pagesalloc(	\
				__TL_DISPATCH_STREAM(kbdev, aux),	\
				ctx_nr, page_cnt);	\
	} while (0)
#endif

#endif
