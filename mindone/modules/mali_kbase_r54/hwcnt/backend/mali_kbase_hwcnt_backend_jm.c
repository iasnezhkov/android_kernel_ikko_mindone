// SPDX-License-Identifier: GPL-2.0
#include "hwcnt/backend/mali_kbase_hwcnt_backend_jm.h"
#include "hwcnt/mali_kbase_hwcnt_gpu.h"
#include "hwcnt/mali_kbase_hwcnt_types.h"
#include "mali_kbase.h"
#include "backend/gpu/mali_kbase_pm_ca.h"
#include "mali_kbase_hwaccess_instr.h"
#include "mali_kbase_hwaccess_time.h"
#include "mali_kbase_ccswe.h"
#include "backend/gpu/mali_kbase_model_linux.h"
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"

#include "backend/gpu/mali_kbase_pm_internal.h"

struct kbase_hwcnt_backend_jm_info {
	struct kbase_device *kbdev;
	enum kbase_hwcnt_set counter_set;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;
	struct kbase_hwcnt_gpu_info hwcnt_gpu_info;
};

struct kbase_hwcnt_jm_physical_layout {
	u8 fe_cnt;
	u8 tiler_cnt;
	u8 mmu_l2_cnt;
	u8 shader_cnt;
	u8 block_cnt;
	u64 shader_avail_mask;
	size_t enable_mask_offset;
	size_t headers_per_block;
	size_t counters_per_block;
	size_t values_per_block;
};

struct kbase_hwcnt_backend_jm {
	const struct kbase_hwcnt_backend_jm_info *info;
	struct kbase_context *kctx;
	u64 gpu_dump_va;
	void *cpu_dump_va;
	struct kbase_vmap_struct *vmap;
	u64 *to_user_buf;
	bool enabled;
	blk_stt_t accum_all_blk_stt;
	blk_stt_t sampled_all_blk_stt;
	u64 debug_core_mask;
	u64 pm_core_mask;
	struct kbase_hwcnt_curr_config curr_config;
	u64 max_core_mask;
	size_t max_l2_slices;
	u64 clk_enable_map;
	u64 cycle_count_elapsed[BASE_MAX_NR_CLOCKS_REGULATORS];
	u64 prev_cycle_count[BASE_MAX_NR_CLOCKS_REGULATORS];
	struct kbase_clk_rate_listener rate_listener;
	struct kbase_ccswe ccswe_shader_cores;
	struct kbase_hwcnt_jm_physical_layout phys_layout;
	u64 dump_time_ns;
};

static int kbasep_hwcnt_backend_jm_gpu_info_init(struct kbase_device *kbdev,
						 struct kbase_hwcnt_gpu_info *info)
{
	size_t clk, l2_count, core_mask;

	if (!kbdev || !info)
		return -EINVAL;

#if IS_ENABLED(CONFIG_MALI_NO_MALI)
	l2_count = KBASE_DUMMY_MODEL_MAX_MEMSYS_BLOCKS;
	core_mask = (1ull << KBASE_DUMMY_MODEL_MAX_SHADER_CORES) - 1;
#else
	l2_count = kbdev->gpu_props.num_l2_slices;
	core_mask = kbdev->gpu_props.coherency_info.group.core_mask;
#endif

	info->l2_count = l2_count;
	info->sc_core_mask = core_mask;
	info->prfcnt_values_per_block = KBASE_HWCNT_V5_DEFAULT_VALUES_PER_BLOCK;

	for (clk = 0; clk < BASE_MAX_NR_CLOCKS_REGULATORS; clk++) {
		if (kbdev->pm.clk_rtm.clks[clk] == NULL)
			break;
	}
	info->clk_cnt = clk;

	return 0;
}

static void kbasep_hwcnt_backend_jm_init_layout(const struct kbase_hwcnt_gpu_info *gpu_info,
						struct kbase_hwcnt_jm_physical_layout *phys_layout)
{
	u8 shader_core_cnt;

	WARN_ON(!gpu_info);
	WARN_ON(!phys_layout);

	shader_core_cnt = fls64(gpu_info->sc_core_mask);

	*phys_layout = (struct kbase_hwcnt_jm_physical_layout){
		.fe_cnt = KBASE_HWCNT_V5_FE_BLOCK_COUNT,
		.tiler_cnt = KBASE_HWCNT_V5_TILER_BLOCK_COUNT,
		.mmu_l2_cnt = gpu_info->l2_count,
		.shader_cnt = shader_core_cnt,
		.block_cnt = KBASE_HWCNT_V5_FE_BLOCK_COUNT + KBASE_HWCNT_V5_TILER_BLOCK_COUNT +
			     gpu_info->l2_count + shader_core_cnt,
		.shader_avail_mask = gpu_info->sc_core_mask,
		.headers_per_block = KBASE_HWCNT_V5_HEADERS_PER_BLOCK,
		.values_per_block = gpu_info->prfcnt_values_per_block,
		.counters_per_block =
			gpu_info->prfcnt_values_per_block - KBASE_HWCNT_V5_HEADERS_PER_BLOCK,
		.enable_mask_offset = KBASE_HWCNT_V5_PRFCNT_EN_HEADER,
	};
}

static void
kbasep_hwcnt_backend_jm_dump_sample(const struct kbase_hwcnt_backend_jm *const backend_jm)
{
	size_t block_idx;
	const u32 *new_sample_buf = backend_jm->cpu_dump_va;
	const u32 *new_block = new_sample_buf;
	u64 *dst_buf = backend_jm->to_user_buf;
	u64 *dst_block = dst_buf;
	const size_t values_per_block = backend_jm->phys_layout.values_per_block;
	const size_t dump_bytes = backend_jm->info->dump_bytes;

	for (block_idx = 0; block_idx < backend_jm->phys_layout.block_cnt; block_idx++) {
		size_t ctr_idx;

		for (ctr_idx = 0; ctr_idx < values_per_block; ctr_idx++)
			dst_block[ctr_idx] = new_block[ctr_idx];

		new_block += values_per_block;
		dst_block += values_per_block;
	}

	WARN_ON(new_block != new_sample_buf + (dump_bytes / KBASE_HWCNT_VALUE_HW_BYTES));
	WARN_ON(dst_block != dst_buf + (dump_bytes / KBASE_HWCNT_VALUE_HW_BYTES));
}

static void kbasep_hwcnt_backend_jm_on_freq_change(struct kbase_clk_rate_listener *rate_listener,
						   u32 clk_index, u32 clk_rate_hz)
{
	struct kbase_hwcnt_backend_jm *backend_jm =
		container_of(rate_listener, struct kbase_hwcnt_backend_jm, rate_listener);
	u64 timestamp_ns;

	if (clk_index != KBASE_CLOCK_DOMAIN_SHADER_CORES)
		return;

	timestamp_ns = ktime_get_raw_ns();
	kbase_ccswe_freq_change(&backend_jm->ccswe_shader_cores, timestamp_ns, clk_rate_hz);
}

static void kbasep_hwcnt_backend_jm_cc_enable(struct kbase_hwcnt_backend_jm *backend_jm,
					      const struct kbase_hwcnt_enable_map *enable_map,
					      u64 timestamp_ns)
{
	struct kbase_device *kbdev = backend_jm->kctx->kbdev;
	u64 clk_enable_map = enable_map->clk_enable_map;
	u64 cycle_count;

	if (kbase_hwcnt_clk_enable_map_enabled(clk_enable_map, KBASE_CLOCK_DOMAIN_TOP)) {

		kbase_pm_request_gpu_cycle_counter_l2_is_on(kbdev);

		kbase_backend_get_gpu_time_norequest(kbdev, &cycle_count, NULL, NULL);

		backend_jm->prev_cycle_count[KBASE_CLOCK_DOMAIN_TOP] = cycle_count;
	}

	if (kbase_hwcnt_clk_enable_map_enabled(clk_enable_map, KBASE_CLOCK_DOMAIN_SHADER_CORES)) {

		struct kbase_clk_rate_trace_manager *rtm = &kbdev->pm.clk_rtm;
		const struct kbase_clk_data *clk_data = rtm->clks[KBASE_CLOCK_DOMAIN_SHADER_CORES];
		u32 cur_freq;
		unsigned long flags;

		spin_lock_irqsave(&rtm->lock, flags);

		cur_freq = (u32)clk_data->clock_val;
		kbase_ccswe_reset(&backend_jm->ccswe_shader_cores);
		kbase_ccswe_freq_change(&backend_jm->ccswe_shader_cores, timestamp_ns, cur_freq);

		kbase_clk_rate_trace_manager_subscribe_no_lock(rtm, &backend_jm->rate_listener);

		spin_unlock_irqrestore(&rtm->lock, flags);

		backend_jm->prev_cycle_count[KBASE_CLOCK_DOMAIN_SHADER_CORES] = 0;
	}

	backend_jm->clk_enable_map = clk_enable_map;
}

static void kbasep_hwcnt_backend_jm_cc_disable(struct kbase_hwcnt_backend_jm *backend_jm)
{
	struct kbase_device *kbdev = backend_jm->kctx->kbdev;
	struct kbase_clk_rate_trace_manager *rtm = &kbdev->pm.clk_rtm;
	u64 clk_enable_map = backend_jm->clk_enable_map;

	if (kbase_hwcnt_clk_enable_map_enabled(clk_enable_map, KBASE_CLOCK_DOMAIN_TOP)) {

		kbase_pm_release_gpu_cycle_counter(kbdev);
	}

	if (kbase_hwcnt_clk_enable_map_enabled(clk_enable_map, KBASE_CLOCK_DOMAIN_SHADER_CORES)) {
		kbase_clk_rate_trace_manager_unsubscribe(rtm, &backend_jm->rate_listener);
	}
}

static int kbasep_hwcnt_gpu_update_curr_config(struct kbase_device *kbdev,
					       struct kbase_hwcnt_curr_config *curr_config)
{
	if (WARN_ON(!kbdev) || WARN_ON(!curr_config))
		return -EINVAL;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	curr_config->num_l2_slices = kbdev->gpu_props.curr_config.l2_slices;
	curr_config->shader_present = kbdev->gpu_props.curr_config.shader_present;
	return 0;
}

static u64 kbasep_hwcnt_backend_jm_timestamp_ns(struct kbase_hwcnt_backend *backend)
{
	(void)backend;
	return ktime_get_raw_ns();
}

static int
kbasep_hwcnt_backend_jm_dump_enable_nolock(struct kbase_hwcnt_backend *backend,
					   const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_physical_enable_map phys_enable_map = { 0 };
	enum kbase_hwcnt_physical_set phys_counter_set;
	struct kbase_instr_hwcnt_enable enable = { 0 };
	u64 timestamp_ns;

	if (!backend_jm || !enable_map || backend_jm->enabled ||
	    (enable_map->metadata != backend_jm->info->metadata))
		return -EINVAL;

	kctx = backend_jm->kctx;
	kbdev = backend_jm->kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_hwcnt_gpu_enable_map_to_physical(&phys_enable_map, enable_map);

	kbase_hwcnt_gpu_set_to_physical(&phys_counter_set, backend_jm->info->counter_set);

	enable = (struct kbase_instr_hwcnt_enable)
	{
		.fe_bm = phys_enable_map.fe_bm, .shader_bm = phys_enable_map.shader_bm,
		.tiler_bm = phys_enable_map.tiler_bm, .mmu_l2_bm = phys_enable_map.mmu_l2_bm,
		.counter_set = phys_counter_set,
#if IS_ENABLED(CONFIG_MALI_NO_MALI)

			.dump_buffer = (uintptr_t)backend_jm->cpu_dump_va,
#else
		.dump_buffer = backend_jm->gpu_dump_va,
#endif
		.dump_buffer_bytes = backend_jm->info->dump_bytes,
	};

	timestamp_ns = kbasep_hwcnt_backend_jm_timestamp_ns(backend);

	errcode = kbasep_hwcnt_gpu_update_curr_config(kbdev, &backend_jm->curr_config);
	if (errcode)
		goto error;

	errcode = kbase_instr_hwcnt_enable_internal(kbdev, kctx, &enable);
	if (errcode)
		goto error;

	backend_jm->debug_core_mask = kbase_pm_ca_get_debug_core_mask(kbdev);
	backend_jm->max_l2_slices = backend_jm->info->hwcnt_gpu_info.l2_count;
	backend_jm->max_core_mask = backend_jm->info->hwcnt_gpu_info.sc_core_mask;

	backend_jm->pm_core_mask = kbase_pm_ca_get_instr_core_mask(kbdev);

	backend_jm->enabled = true;

	kbase_hwcnt_block_state_append(&backend_jm->accum_all_blk_stt, KBASE_HWCNT_STATE_OFF);

	kbasep_hwcnt_backend_jm_cc_enable(backend_jm, enable_map, timestamp_ns);

	return 0;
error:
	return errcode;
}

static int kbasep_hwcnt_backend_jm_dump_enable(struct kbase_hwcnt_backend *backend,
					       const struct kbase_hwcnt_enable_map *enable_map)
{
	unsigned long flags;
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_device *kbdev;

	if (!backend_jm)
		return -EINVAL;

	kbdev = backend_jm->kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	errcode = kbasep_hwcnt_backend_jm_dump_enable_nolock(backend, enable_map);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return errcode;
}

static void kbasep_hwcnt_backend_jm_dump_disable(struct kbase_hwcnt_backend *backend,
						 struct kbase_hwcnt_dump_buffer *dump_buffer,
						 const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;

	if (WARN_ON(!backend_jm ||
		    (dump_buffer && (backend_jm->info->metadata != dump_buffer->metadata)) ||
		    (enable_map && (backend_jm->info->metadata != enable_map->metadata)) ||
		    (dump_buffer && !enable_map)))
		return;

	if (!backend_jm->enabled)
		return;

	kbasep_hwcnt_backend_jm_cc_disable(backend_jm);

	errcode = kbase_instr_hwcnt_disable_internal(backend_jm->kctx);
	WARN_ON(errcode);

	kbase_hwcnt_block_state_append(&backend_jm->accum_all_blk_stt, KBASE_HWCNT_STATE_OFF);

	if (dump_buffer) {

		kbase_hwcnt_block_state_accumulate(&backend_jm->sampled_all_blk_stt,
						   &backend_jm->accum_all_blk_stt);
		kbase_hwcnt_dump_buffer_block_state_update(dump_buffer, enable_map,
							   backend_jm->sampled_all_blk_stt);

		kbase_hwcnt_block_state_set(&backend_jm->sampled_all_blk_stt,
					    KBASE_HWCNT_STATE_UNKNOWN);
		kbase_hwcnt_block_state_set(&backend_jm->accum_all_blk_stt,
					    KBASE_HWCNT_STATE_UNKNOWN);
	}

	backend_jm->enabled = false;
}

static int kbasep_hwcnt_backend_jm_dump_clear(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_clear(backend_jm->kctx);
}

static int kbasep_hwcnt_backend_jm_dump_request(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_device *kbdev;
	const struct kbase_hwcnt_metadata *metadata;
	u64 current_cycle_count;
	size_t clk;
	int ret;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	kbdev = backend_jm->kctx->kbdev;
	metadata = backend_jm->info->metadata;

	preempt_disable();
	{
		backend_jm->dump_time_ns = kbasep_hwcnt_backend_jm_timestamp_ns(backend);
		ret = kbase_instr_hwcnt_request_dump(backend_jm->kctx);

		kbase_hwcnt_metadata_for_each_clock(metadata, clk) {
			if (!kbase_hwcnt_clk_enable_map_enabled(backend_jm->clk_enable_map, clk))
				continue;

			if (clk == KBASE_CLOCK_DOMAIN_TOP) {

				kbase_backend_get_gpu_time_norequest(kbdev, &current_cycle_count,
								     NULL, NULL);
			} else {

				current_cycle_count = kbase_ccswe_cycle_at(
					&backend_jm->ccswe_shader_cores, backend_jm->dump_time_ns);
			}
			backend_jm->cycle_count_elapsed[clk] =
				current_cycle_count - backend_jm->prev_cycle_count[clk];

			backend_jm->prev_cycle_count[clk] = current_cycle_count;
		}
	}
	preempt_enable();

	return ret;
}

static int kbasep_hwcnt_backend_jm_dump_wait(struct kbase_hwcnt_backend *backend)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	errcode = kbase_instr_hwcnt_wait_for_dump(backend_jm->kctx);
	if (errcode)
		return errcode;

	kbase_hwcnt_block_state_accumulate(&backend_jm->sampled_all_blk_stt,
					   &backend_jm->accum_all_blk_stt);
	kbase_hwcnt_block_state_set(&backend_jm->accum_all_blk_stt, KBASE_HWCNT_STATE_UNKNOWN);

	return errcode;
}

static int kbasep_hwcnt_backend_jm_dump_get(struct kbase_hwcnt_backend *backend,
					    struct kbase_hwcnt_dump_buffer *dst,
					    const struct kbase_hwcnt_enable_map *dst_enable_map,
					    bool accumulate, u64 *dump_time_ns)
{
	struct kbase_hwcnt_backend_jm *backend_jm = (struct kbase_hwcnt_backend_jm *)backend;
	size_t clk;
#if IS_ENABLED(CONFIG_MALI_NO_MALI)
	struct kbase_device *kbdev;
	unsigned long flags;
#endif
	int errcode;

	if (!backend_jm || !dst || !dst_enable_map || !dump_time_ns ||
	    (backend_jm->info->metadata != dst->metadata) ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	kbase_sync_mem_regions(backend_jm->kctx, backend_jm->vmap, KBASE_SYNC_TO_CPU);

	kbasep_hwcnt_backend_jm_dump_sample(backend_jm);

	kbase_hwcnt_metadata_for_each_clock(dst_enable_map->metadata, clk) {
		if (!kbase_hwcnt_clk_enable_map_enabled(dst_enable_map->clk_enable_map, clk))
			continue;

		if (!accumulate)
			dst->clk_cnt_buf[clk] = 0;
		dst->clk_cnt_buf[clk] += backend_jm->cycle_count_elapsed[clk];
	}

#if IS_ENABLED(CONFIG_MALI_NO_MALI)
	kbdev = backend_jm->kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	errcode = kbasep_hwcnt_gpu_update_curr_config(kbdev, &backend_jm->curr_config);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (errcode)
		return errcode;
#endif
	errcode = kbase_hwcnt_jm_dump_get(dst, backend_jm->to_user_buf, dst_enable_map,
					  backend_jm->pm_core_mask, backend_jm->debug_core_mask,
					  backend_jm->max_l2_slices, &backend_jm->curr_config,
					  accumulate);

	if (errcode)
		return errcode;

	*dump_time_ns = backend_jm->dump_time_ns;
	kbase_hwcnt_dump_buffer_block_state_update(dst, dst_enable_map,
						   backend_jm->sampled_all_blk_stt);
	kbase_hwcnt_block_state_set(&backend_jm->sampled_all_blk_stt, KBASE_HWCNT_STATE_UNKNOWN);
	return errcode;
}

static int kbasep_hwcnt_backend_jm_dump_alloc(const struct kbase_hwcnt_backend_jm_info *info,
					      struct kbase_context *kctx, u64 *gpu_dump_va)
{
	struct kbase_va_region *reg;
	u64 flags;
	u64 nr_pages;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	WARN_ON(!info);
	WARN_ON(!kctx);
	WARN_ON(!gpu_dump_va);

	flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_WR | BASEP_MEM_PERMANENT_KERNEL_MAPPING |
		BASE_MEM_CACHED_CPU | BASE_MEM_UNCACHED_GPU;

	nr_pages = PFN_UP(info->dump_bytes);

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	reg = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags, gpu_dump_va, mmu_sync_info, KBASE_MEM_JM);
#else
	reg = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags, gpu_dump_va, mmu_sync_info);
#endif

	if (!reg)
		return -ENOMEM;

	return 0;
}

static void kbasep_hwcnt_backend_jm_dump_free(struct kbase_context *kctx, u64 gpu_dump_va)
{
	WARN_ON(!kctx);
	if (gpu_dump_va)
		kbase_mem_free(kctx, gpu_dump_va);
}

static void kbasep_hwcnt_backend_jm_destroy(struct kbase_hwcnt_backend_jm *backend)
{
	if (!backend)
		return;

	if (backend->kctx) {
		struct kbase_context *kctx = backend->kctx;
		struct kbase_device *kbdev = kctx->kbdev;

		if (backend->cpu_dump_va)
			kbase_phy_alloc_mapping_put(kctx, backend->vmap);

		if (backend->gpu_dump_va)
			kbasep_hwcnt_backend_jm_dump_free(kctx, backend->gpu_dump_va);

		kbasep_js_release_privileged_ctx(kbdev, kctx);
		kbase_destroy_context(kctx);
	}

	kfree(backend->to_user_buf);

	kfree(backend);
}

static int kbasep_hwcnt_backend_jm_create(const struct kbase_hwcnt_backend_jm_info *info,
					  struct kbase_hwcnt_backend_jm **out_backend)
{
	int errcode;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_backend_jm *backend = NULL;

	WARN_ON(!info);
	WARN_ON(!out_backend);

	kbdev = info->kbdev;

	backend = kzalloc(sizeof(*backend), GFP_KERNEL);
	if (!backend)
		goto alloc_error;

	backend->info = info;
	kbasep_hwcnt_backend_jm_init_layout(&info->hwcnt_gpu_info, &backend->phys_layout);

	backend->kctx = kbase_create_context(kbdev, true,
					     BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED, 0, NULL);
	if (!backend->kctx)
		goto alloc_error;

	kbasep_js_schedule_privileged_ctx(kbdev, backend->kctx);

	errcode = kbasep_hwcnt_backend_jm_dump_alloc(info, backend->kctx, &backend->gpu_dump_va);
	if (errcode)
		goto error;

	backend->cpu_dump_va =
		kbase_phy_alloc_mapping_get(backend->kctx, backend->gpu_dump_va, &backend->vmap);
	if (!backend->cpu_dump_va || !backend->vmap)
		goto alloc_error;

	backend->to_user_buf = kzalloc(info->metadata->dump_buf_bytes, GFP_KERNEL);
	if (!backend->to_user_buf)
		goto alloc_error;

	kbase_ccswe_init(&backend->ccswe_shader_cores);
	backend->rate_listener.notify = kbasep_hwcnt_backend_jm_on_freq_change;
	kbase_hwcnt_block_state_set(&backend->accum_all_blk_stt, KBASE_HWCNT_STATE_UNKNOWN);
	kbase_hwcnt_block_state_set(&backend->sampled_all_blk_stt, KBASE_HWCNT_STATE_UNKNOWN);

	*out_backend = backend;
	return 0;

alloc_error:
	errcode = -ENOMEM;
error:
	kbasep_hwcnt_backend_jm_destroy(backend);
	return errcode;
}

static const struct kbase_hwcnt_metadata *
kbasep_hwcnt_backend_jm_metadata(const struct kbase_hwcnt_backend_info *info)
{
	if (!info)
		return NULL;

	return ((const struct kbase_hwcnt_backend_jm_info *)info)->metadata;
}

static int kbasep_hwcnt_backend_jm_init(const struct kbase_hwcnt_backend_info *info,
					struct kbase_hwcnt_backend **out_backend)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend = NULL;

	if (!info || !out_backend)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_jm_create((const struct kbase_hwcnt_backend_jm_info *)info,
						 &backend);
	if (errcode)
		return errcode;

	*out_backend = (struct kbase_hwcnt_backend *)backend;

	return 0;
}

static void kbasep_hwcnt_backend_jm_term(struct kbase_hwcnt_backend *backend)
{
	if (!backend)
		return;

	kbasep_hwcnt_backend_jm_dump_disable(backend, NULL, NULL);
	kbasep_hwcnt_backend_jm_destroy((struct kbase_hwcnt_backend_jm *)backend);
}

static void kbasep_hwcnt_backend_jm_acquire(const struct kbase_hwcnt_backend *backend)
{
}

static void kbasep_hwcnt_backend_jm_release(const struct kbase_hwcnt_backend *backend)
{
}

static void kbasep_hwcnt_backend_jm_info_destroy(const struct kbase_hwcnt_backend_jm_info *info)
{
	if (!info)
		return;

	kbase_hwcnt_metadata_destroy(info->metadata);
	kfree(info);
}

static int kbasep_hwcnt_backend_jm_info_create(struct kbase_device *kbdev,
					       const struct kbase_hwcnt_backend_jm_info **out_info)
{
	int errcode = -ENOMEM;
	struct kbase_hwcnt_backend_jm_info *info = NULL;

	WARN_ON(!kbdev);
	WARN_ON(!out_info);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return errcode;

	info->kbdev = kbdev;

#if defined(CONFIG_MALI_PRFCNT_SET_SECONDARY)
	info->counter_set = KBASE_HWCNT_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
	info->counter_set = KBASE_HWCNT_SET_TERTIARY;
#else

	info->counter_set = KBASE_HWCNT_SET_PRIMARY;
#endif

	errcode = kbasep_hwcnt_backend_jm_gpu_info_init(kbdev, &info->hwcnt_gpu_info);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_jm_metadata_create(&info->hwcnt_gpu_info, info->counter_set,
						 &info->metadata, &info->dump_bytes);
	if (errcode)
		goto error;

	*out_info = info;

	return 0;
error:
	kbasep_hwcnt_backend_jm_info_destroy(info);
	return errcode;
}

int kbase_hwcnt_backend_jm_create(struct kbase_device *kbdev,
				  struct kbase_hwcnt_backend_interface *iface)
{
	int errcode;
	const struct kbase_hwcnt_backend_jm_info *info = NULL;

	if (!kbdev || !iface)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_jm_info_create(kbdev, &info);

	if (errcode)
		return errcode;

	iface->info = (struct kbase_hwcnt_backend_info *)info;
	iface->metadata = kbasep_hwcnt_backend_jm_metadata;
	iface->init = kbasep_hwcnt_backend_jm_init;
	iface->term = kbasep_hwcnt_backend_jm_term;
	iface->acquire = kbasep_hwcnt_backend_jm_acquire;
	iface->release = kbasep_hwcnt_backend_jm_release;
	iface->timestamp_ns = kbasep_hwcnt_backend_jm_timestamp_ns;
	iface->dump_enable = kbasep_hwcnt_backend_jm_dump_enable;
	iface->dump_enable_nolock = kbasep_hwcnt_backend_jm_dump_enable_nolock;
	iface->dump_disable = kbasep_hwcnt_backend_jm_dump_disable;
	iface->dump_clear = kbasep_hwcnt_backend_jm_dump_clear;
	iface->dump_request = kbasep_hwcnt_backend_jm_dump_request;
	iface->dump_wait = kbasep_hwcnt_backend_jm_dump_wait;
	iface->dump_get = kbasep_hwcnt_backend_jm_dump_get;

	return 0;
}

void kbase_hwcnt_backend_jm_destroy(struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface)
		return;

	kbasep_hwcnt_backend_jm_info_destroy(
		(const struct kbase_hwcnt_backend_jm_info *)iface->info);
	memset(iface, 0, sizeof(*iface));
}
