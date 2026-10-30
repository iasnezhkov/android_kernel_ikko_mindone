// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase.h"
#include "mali_kbase_kinstr_prfcnt.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "hwcnt/mali_kbase_hwcnt_gpu.h"
#include <uapi/gpu/arm/midgard/mali_kbase_ioctl.h>
#include "mali_malisw.h"
#include "mali_kbase_debug.h"

#include <linux/anon_inodes.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/version_compat_defs.h>
#include <linux/workqueue.h>

#if KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE
#include <uapi/linux/eventpoll.h>
#endif

#include <platform/mtk_mfg_counter.h>
#ifdef CONFIG_MTK_PERF_TRACKER
#include <perf_tracker.h>
#endif
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
#define CREATE_TRACE_POINTS
#include <platform/mtk_platform_common/mtk_gpu_trace.h>
#endif

#define DUMP_INTERVAL_MIN_NS (100 * NSEC_PER_USEC)

#define MAX_BUFFER_COUNT 32

#define PRINT_BUFFER_SIZE 1024

#define NS_PER_SEC (1E9)

#define KINSTR_PRFCNT_PERIOD_NS (100 * NSEC_PER_MSEC)
#define HWCNT_READER_API PRFCNT_READER_API_VERSION
#define POLL_TIMEOUT_MS (1000)

struct kbase_kinstr_prfcnt_context {
	struct kbase_hwcnt_virtualizer *hvirt;
	u32 info_item_count;
	const struct kbase_hwcnt_metadata *metadata;
	struct mutex lock;
	size_t suspend_count;
	size_t client_count;
	struct list_head clients;
	struct hrtimer dump_timer;
	struct work_struct dump_work;
};

struct kbase_kinstr_prfcnt_sample {
	struct prfcnt_metadata *sample_meta;
	struct kbase_hwcnt_dump_buffer dump_buf;
};

struct kbase_kinstr_prfcnt_sample_array {
	u8 *user_buf;
	size_t sample_count;
	blk_stt_t *blk_stt_scratch;
	struct kbase_kinstr_prfcnt_sample *samples;
};

struct kbase_kinstr_prfcnt_client_config {
	u8 prfcnt_mode;
	u8 counter_set;
	u8 scope;
	u16 buffer_count;
	u64 period_ns;
	struct kbase_hwcnt_enable_cm enable_cm;
};

enum kbase_kinstr_prfcnt_client_init_state {
	KINSTR_PRFCNT_UNINITIALISED,
	KINSTR_PRFCNT_PARSE_SETUP = KINSTR_PRFCNT_UNINITIALISED,
	KINSTR_PRFCNT_ENABLE_MAP,
	KINSTR_PRFCNT_DUMP_BUFFER,
	KINSTR_PRFCNT_SAMPLE_ARRAY,
	KINSTR_PRFCNT_VIRTUALIZER_CLIENT,
	KINSTR_PRFCNT_WAITQ_MUTEX,
	KINSTR_PRFCNT_INITIALISED
};

struct kbase_kinstr_prfcnt_client {
	struct kbase_kinstr_prfcnt_context *kinstr_ctx;
	struct kbase_hwcnt_virtualizer_client *hvcli;
	struct list_head node;
	struct mutex cmd_sync_lock;
	u64 next_dump_time_ns;
	u32 dump_interval_ns;
	u32 sample_flags;
	bool active;
	struct kbase_kinstr_prfcnt_client_config config;
	struct kbase_hwcnt_enable_map enable_map;
	struct kbase_hwcnt_dump_buffer tmp_buf;
	struct kbase_kinstr_prfcnt_sample_array sample_arr;
	atomic_t read_idx;
	atomic_t write_idx;
	atomic_t fetch_idx;
	wait_queue_head_t waitq;
	size_t sample_size;
	size_t sample_count;
	u64 user_data;
};

static struct prfcnt_enum_item kinstr_prfcnt_supported_requests[] = {
	{

		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_REQUEST,
				.item_version = PRFCNT_READER_API_VERSION,
		},
		.u.request = {
				.request_item_type = PRFCNT_REQUEST_MODE,
				.versions_mask = 0x1,
		},
	},
	{

		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_REQUEST,
				.item_version = PRFCNT_READER_API_VERSION,
		},
		.u.request = {
				.request_item_type = PRFCNT_REQUEST_ENABLE,
				.versions_mask = 0x1,
		},
	},
};

static __poll_t kbasep_kinstr_prfcnt_hwcnt_reader_poll(struct file *filp,
						       struct poll_table_struct *wait)
{
	struct kbase_kinstr_prfcnt_client *cli;

	if (!filp || !wait)
		return EPOLLHUP | EPOLLERR;

	cli = filp->private_data;

	if (!cli)
		return EPOLLHUP | EPOLLERR;

	poll_wait(filp, &cli->waitq, wait);

	if (atomic_read(&cli->write_idx) != atomic_read(&cli->fetch_idx))
		return EPOLLIN | EPOLLRDNORM;

	return (__poll_t)0;
}

u64 *kernel_dump;

int mtk_pm_tool = pm_non;
int ds5_used = 1;
static DEFINE_MUTEX(gpu_vinstr_mtk_lock);

static struct kbase_kinstr_prfcnt_client *mtk_cli = NULL;
struct mtk_gpu_perf{
	uint64_t counter[VINSTR_PERF_COUNTER_LAST];
};

static u64 kbasep_kinstr_prfcnt_next_dump_time_ns(u64 cur_ts_ns, u32 interval)
{

	if (interval == 0)
		return 0;

	do_div(cur_ts_ns, interval);

	return (cur_ts_ns + 1) * interval;
}

static u64 kbasep_kinstr_prfcnt_timestamp_ns(void)
{
	return ktime_get_raw_ns();
}

static void kbasep_kinstr_prfcnt_reschedule_worker(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	u64 cur_ts_ns;
	u64 shortest_period_ns = U64_MAX;
	struct kbase_kinstr_prfcnt_client *pos;

	WARN_ON(!kinstr_ctx);
	lockdep_assert_held(&kinstr_ctx->lock);
	cur_ts_ns = kbasep_kinstr_prfcnt_timestamp_ns();

	list_for_each_entry(pos, &kinstr_ctx->clients, node) {

		if (pos->active && pos->dump_interval_ns > 0) {
			shortest_period_ns = MIN(shortest_period_ns, pos->dump_interval_ns);

			if (pos->next_dump_time_ns < cur_ts_ns)
				pos->next_dump_time_ns =
					MAX(cur_ts_ns + 1,
					    pos->next_dump_time_ns + pos->dump_interval_ns);
		}
	}

	hrtimer_cancel(&kinstr_ctx->dump_timer);

	if ((shortest_period_ns != U64_MAX) && (kinstr_ctx->suspend_count == 0)) {
		u64 next_schedule_time_ns =
			kbasep_kinstr_prfcnt_next_dump_time_ns(cur_ts_ns, shortest_period_ns);
		hrtimer_start(&kinstr_ctx->dump_timer,
			      ns_to_ktime(next_schedule_time_ns - cur_ts_ns), HRTIMER_MODE_REL);
	}
}

static u32 kbase_hwcnt_block_state_to_prfcnt_block_state(blk_stt_t hw_blk_stt)
{
	u32 prfcnt_block_state = 0;
	unsigned long block_state_mask = hw_blk_stt;
	unsigned long bit_shift;

	for_each_set_bit(bit_shift, &block_state_mask, BITS_PER_TYPE(blk_stt_t)) {
		blk_stt_t set_bit = 1u << bit_shift;

		switch (set_bit) {
		case KBASE_HWCNT_STATE_UNKNOWN:
			prfcnt_block_state |= BLOCK_STATE_UNKNOWN;
			break;
		case KBASE_HWCNT_STATE_ON:
			prfcnt_block_state |= BLOCK_STATE_ON;
			break;
		case KBASE_HWCNT_STATE_OFF:
			prfcnt_block_state |= BLOCK_STATE_OFF;
			break;
		case KBASE_HWCNT_STATE_AVAILABLE:
			prfcnt_block_state |= BLOCK_STATE_AVAILABLE;
			break;
		case KBASE_HWCNT_STATE_UNAVAILABLE:
			prfcnt_block_state |= BLOCK_STATE_UNAVAILABLE;
			break;
		case KBASE_HWCNT_STATE_NORMAL:
			prfcnt_block_state |= BLOCK_STATE_NORMAL;
			break;
		case KBASE_HWCNT_STATE_PROTECTED:
			prfcnt_block_state |= BLOCK_STATE_PROTECTED;
			break;
		default:
			WARN(true, "Unknown block state bit set: 0x%x", set_bit);
			break;
		}
	}

	return prfcnt_block_state;
}

static enum prfcnt_block_type kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(u64 type)
{
	enum prfcnt_block_type block_type;

	switch (type) {
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
		block_type = PRFCNT_BLOCK_TYPE_FE;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
		block_type = PRFCNT_BLOCK_TYPE_TILER;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
		block_type = PRFCNT_BLOCK_TYPE_SHADER_CORE;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
		block_type = PRFCNT_BLOCK_TYPE_MEMORY;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW3:
		block_type = PRFCNT_BLOCK_TYPE_FW;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG2:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG3:
		block_type = PRFCNT_BLOCK_TYPE_CSG;
		break;

	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FW_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_CSG_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER_UNDEFINED:
	case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS_UNDEFINED:
	default:
		block_type = PRFCNT_BLOCK_TYPE_RESERVED;
		break;
	}

	return block_type;
}

static bool kbase_kinstr_is_block_type_reserved(const struct kbase_hwcnt_metadata *metadata,
						size_t blk)
{
	enum prfcnt_block_type block_type = kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
		kbase_hwcnt_metadata_block_type(metadata, blk));

	return block_type == PRFCNT_BLOCK_TYPE_RESERVED;
}

int kbasep_kinstr_prfcnt_set_block_meta_items(struct kbase_hwcnt_enable_map *enable_map,
					      struct kbase_hwcnt_dump_buffer *dst,
					      struct prfcnt_metadata **block_meta_base,
					      u8 *base_addr, u8 counter_set)
{
	size_t blk, blk_inst;
	struct prfcnt_metadata **ptr_md = block_meta_base;
	const struct kbase_hwcnt_metadata *metadata;
	uint8_t block_idx = 0;

	if (!dst || !*block_meta_base)
		return -EINVAL;

	metadata = dst->metadata;
	kbase_hwcnt_metadata_for_each_block(metadata, blk, blk_inst) {
		u8 *dst_blk;
		blk_stt_t hw_blk_stt;

		if (blk_inst == 0)
			block_idx = 0;

		if (kbase_kinstr_is_block_type_reserved(metadata, blk) ||
		    !kbase_hwcnt_metadata_block_instance_avail(metadata, blk, blk_inst) ||
		    !kbase_hwcnt_enable_map_block_enabled(enable_map, blk, blk_inst))
			continue;

		dst_blk = (u8 *)kbase_hwcnt_dump_buffer_block_instance(dst, blk, blk_inst);
		hw_blk_stt = *kbase_hwcnt_dump_buffer_block_state_instance(dst, blk, blk_inst);
		(*ptr_md)->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_BLOCK;
		(*ptr_md)->hdr.item_version = PRFCNT_READER_API_VERSION;
		(*ptr_md)->u.block_md.block_type =
			kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
				kbase_hwcnt_metadata_block_type(metadata, blk));
		(*ptr_md)->u.block_md.block_idx = block_idx;
		(*ptr_md)->u.block_md.set = counter_set;
		(*ptr_md)->u.block_md.block_state =
			kbase_hwcnt_block_state_to_prfcnt_block_state(hw_blk_stt);
		(*ptr_md)->u.block_md.values_offset = (u32)(dst_blk - base_addr);

		(*ptr_md)++;
		block_idx++;
	}

	return 0;
}

static void kbasep_kinstr_prfcnt_set_sample_metadata(struct kbase_kinstr_prfcnt_client *cli,
						     struct kbase_hwcnt_dump_buffer *dump_buf,
						     struct prfcnt_metadata *ptr_md)
{
	u8 clk_cnt, i;

	clk_cnt = cli->kinstr_ctx->metadata->clk_cnt;

	ptr_md->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_SAMPLE;
	ptr_md->hdr.item_version = PRFCNT_READER_API_VERSION;
	ptr_md->u.sample_md.seq = (u64)atomic_read(&cli->write_idx);
	ptr_md->u.sample_md.flags = cli->sample_flags;

	ptr_md++;
	if (clk_cnt > MAX_REPORTED_DOMAINS)
		clk_cnt = MAX_REPORTED_DOMAINS;

	ptr_md->hdr.item_type = PRFCNT_SAMPLE_META_TYPE_CLOCK;
	ptr_md->hdr.item_version = PRFCNT_READER_API_VERSION;
	ptr_md->u.clock_md.num_domains = clk_cnt;
	for (i = 0; i < clk_cnt; i++)
		ptr_md->u.clock_md.cycles[i] = dump_buf->clk_cnt_buf[i];

	ptr_md++;
	if (WARN_ON(kbasep_kinstr_prfcnt_set_block_meta_items(&cli->enable_map, dump_buf, &ptr_md,
							      cli->sample_arr.user_buf,
							      cli->config.counter_set)))
		return;

	ptr_md->hdr.item_type = FLEX_LIST_TYPE_NONE;
	ptr_md->hdr.item_version = 0;
}

static void kbasep_kinstr_prfcnt_client_output_sample(struct kbase_kinstr_prfcnt_client *cli,
						      int buf_idx, u64 user_data, u64 ts_start_ns,
						      u64 ts_end_ns)
{
	struct kbase_hwcnt_dump_buffer *dump_buf;
	struct kbase_hwcnt_dump_buffer *tmp_buf = &cli->tmp_buf;
	struct prfcnt_metadata *ptr_md;

	if (WARN_ON(buf_idx >= cli->sample_arr.sample_count))
		return;

	dump_buf = &cli->sample_arr.samples[buf_idx].dump_buf;
	ptr_md = cli->sample_arr.samples[buf_idx].sample_meta;

	kbase_hwcnt_gpu_patch_dump_headers(tmp_buf, &cli->enable_map);

	kbase_hwcnt_dump_buffer_copy_strict(dump_buf, tmp_buf, &cli->enable_map);

	ptr_md->u.sample_md.timestamp_start = ts_start_ns;
	ptr_md->u.sample_md.timestamp_end = ts_end_ns;
	ptr_md->u.sample_md.user_data = user_data;

	kbasep_kinstr_prfcnt_set_sample_metadata(cli, dump_buf, ptr_md);
}

static int kbasep_kinstr_prfcnt_client_dump(struct kbase_kinstr_prfcnt_client *cli,
					    enum base_hwcnt_reader_event event_id, u64 user_data)
{
	int ret;
	u64 ts_start_ns = 0;
	u64 ts_end_ns = 0;
	int write_idx;
	int read_idx;
	size_t available_samples_count;

	struct kbase_hwcnt_dump_buffer *tmp_buf;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->kinstr_ctx->lock);

	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	available_samples_count = cli->sample_arr.sample_count;
	WARN_ON(available_samples_count < 1);

	available_samples_count -= 1;
	if ((size_t)(write_idx - read_idx) == available_samples_count && mtk_pm_tool!=pm_ltr) {

		if (event_id == BASE_HWCNT_READER_EVENT_PERIODIC)
			cli->sample_flags |= SAMPLE_FLAG_OVERFLOW;
		return -EBUSY;
	}

	write_idx %= cli->sample_arr.sample_count;

	if (ds5_used) {

		if (write_idx - read_idx == cli->sample_arr.sample_count) {
			return -EBUSY;
		}
		write_idx %= cli->sample_arr.sample_count;
		tmp_buf = &cli->tmp_buf;
	} else {
		tmp_buf = &cli->tmp_buf;
	}

	ret = kbase_hwcnt_virtualizer_client_dump(cli->hvcli, &ts_start_ns, &ts_end_ns,
						  &cli->tmp_buf);

	if (ret)
		cli->sample_flags |= SAMPLE_FLAG_ERROR;

	if (mtk_pm_tool == pm_ltr && ds5_used == 0) {
		kernel_dump = tmp_buf->dump_buf;
		MTK_update_gpu_LTR();
	}

	kbasep_kinstr_prfcnt_client_output_sample(cli, write_idx, user_data, ts_start_ns,
						  ts_end_ns);

	wmb();
	atomic_inc(&cli->write_idx);
	wake_up_interruptible(&cli->waitq);

	cli->sample_flags = 0;

	return 0;
}

static int kbasep_kinstr_prfcnt_client_start(struct kbase_kinstr_prfcnt_client *cli, u64 user_data)
{
	int ret;
	u64 tm_start, tm_end;
	int write_idx;
	int read_idx;
	size_t available_samples_count;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	if (cli->active)
		return 0;

	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	available_samples_count = cli->sample_count - (size_t)(write_idx - read_idx);
	if (!available_samples_count)
		return -EBUSY;

	kbase_hwcnt_gpu_enable_map_from_cm(&cli->enable_map, &cli->config.enable_cm);

	cli->enable_map.clk_enable_map = (1ull << cli->kinstr_ctx->metadata->clk_cnt) - 1;

	mutex_lock(&cli->kinstr_ctx->lock);

	ret = kbase_hwcnt_virtualizer_client_set_counters(cli->hvcli, &cli->enable_map, &tm_start,
							  &tm_end, NULL);

	if (!ret) {
		cli->active = true;
		cli->user_data = user_data;
		cli->sample_flags = 0;

		if (cli->dump_interval_ns)
			kbasep_kinstr_prfcnt_reschedule_worker(cli->kinstr_ctx);
	}

	mutex_unlock(&cli->kinstr_ctx->lock);

	return ret;
}

static int kbasep_kinstr_prfcnt_client_stop(struct kbase_kinstr_prfcnt_client *cli, u64 user_data)
{
	int ret;
	u64 tm_start = 0;
	u64 tm_end = 0;
	size_t available_samples_count;
	int write_idx;
	int read_idx;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	if (!cli->active)
		return -EINVAL;

	mutex_lock(&cli->kinstr_ctx->lock);

	kbase_hwcnt_gpu_enable_map_from_cm(&cli->enable_map, &(struct kbase_hwcnt_enable_cm){});

	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	available_samples_count = cli->sample_count - (size_t)(write_idx - read_idx);

	ret = kbase_hwcnt_virtualizer_client_set_counters(cli->hvcli, &cli->enable_map, &tm_start,
							  &tm_end, &cli->tmp_buf);

	if (ret)
		cli->sample_flags |= SAMPLE_FLAG_ERROR;

	if (!WARN_ON(!available_samples_count)) {
		write_idx %= cli->sample_arr.sample_count;

		kbase_hwcnt_gpu_enable_map_from_cm(&cli->enable_map, &cli->config.enable_cm);

		kbasep_kinstr_prfcnt_client_output_sample(cli, write_idx, user_data, tm_start,
							  tm_end);

		wmb();
		atomic_inc(&cli->write_idx);
		wake_up_interruptible(&cli->waitq);
	}

	cli->active = false;
	cli->user_data = user_data;

	if (cli->dump_interval_ns)
		kbasep_kinstr_prfcnt_reschedule_worker(cli->kinstr_ctx);

	mutex_unlock(&cli->kinstr_ctx->lock);

	return 0;
}

static int kbasep_kinstr_prfcnt_client_sync_dump(struct kbase_kinstr_prfcnt_client *cli,
						 u64 user_data)
{
	int ret;

	lockdep_assert_held(&cli->cmd_sync_lock);

	if (!cli->active || cli->dump_interval_ns)
		return -EINVAL;

	mutex_lock(&cli->kinstr_ctx->lock);

	ret = kbasep_kinstr_prfcnt_client_dump(cli, BASE_HWCNT_READER_EVENT_MANUAL, user_data);

	mutex_unlock(&cli->kinstr_ctx->lock);

	return ret;
}

static int kbasep_kinstr_prfcnt_client_discard(struct kbase_kinstr_prfcnt_client *cli)
{
	int write_idx;

	WARN_ON(!cli);
	lockdep_assert_held(&cli->cmd_sync_lock);

	mutex_lock(&cli->kinstr_ctx->lock);

	write_idx = atomic_read(&cli->write_idx);

	if (atomic_read(&cli->fetch_idx) != atomic_read(&cli->read_idx)) {
		atomic_set(&cli->fetch_idx, write_idx);
	} else {
		atomic_set(&cli->fetch_idx, write_idx);
		atomic_set(&cli->read_idx, write_idx);
	}

	mutex_unlock(&cli->kinstr_ctx->lock);

	return 0;
}

int kbasep_kinstr_prfcnt_cmd(struct kbase_kinstr_prfcnt_client *cli,
			     struct prfcnt_control_cmd *control_cmd)
{
	int ret = 0;

	mutex_lock(&cli->cmd_sync_lock);

	switch (control_cmd->cmd) {
	case PRFCNT_CONTROL_CMD_START:
#if IS_ENABLED(CONFIG_MALI_MTK_HWCNT_HINT)
		hwcnt_hint(true);
#endif
		ret = kbasep_kinstr_prfcnt_client_start(cli, control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_STOP:
		ret = kbasep_kinstr_prfcnt_client_stop(cli, control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_SAMPLE_SYNC:
		ret = kbasep_kinstr_prfcnt_client_sync_dump(cli, control_cmd->user_data);
		break;
	case PRFCNT_CONTROL_CMD_DISCARD:
		ret = kbasep_kinstr_prfcnt_client_discard(cli);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&cli->cmd_sync_lock);

	return ret;
}

#if IS_ENABLED(CONFIG_MALI_MTK_HWCNT_HINT)
void hwcnt_hint(bool is_init)
{
	u32 csg_nr;
	bool need_delay = false;
	struct kbase_device *kbdev = kbase_find_device(-1);
	if(kbdev == NULL){
		return;
	}

	kbase_csf_scheduler_lock(kbdev);
	for (csg_nr = 0; csg_nr < kbdev->csf.global_iface.group_num; csg_nr++) {
		struct kbase_queue_group *const group =
		kbdev->csf.scheduler.csg_slots[csg_nr].resident_group;

		if (!group)
			continue;

		struct base_csf_notification const
			error = { .type = BASE_CSF_NOTIFICATION_HWCNT,
			  .payload = {
				  .csg_error = {
					  .handle = group->handle,
					  .error = {
						  .error_type =
							  BASE_NOTIFICATION_HWCNT,
					  } } } };
		kbase_csf_event_add_error(group->kctx, &group->error_fatal, &error);
		kbase_event_wakeup(group->kctx);
		need_delay = true;
	}
	kbase_csf_scheduler_unlock(kbdev);
	kbase_release_device(kbdev);
	if (is_init && need_delay)
		msleep(300);
}
#endif

static int kbasep_kinstr_prfcnt_get_sample(struct kbase_kinstr_prfcnt_client *cli,
					   struct prfcnt_sample_access *sample_access)
{
	int write_idx;
	int read_idx;
	int fetch_idx;
	u64 sample_offset_bytes;
	struct prfcnt_metadata *sample_meta;
	int err = 0;

	mutex_lock(&cli->cmd_sync_lock);
	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	if (write_idx == read_idx) {
		err = -EINVAL;
		goto error_out;
	}

	fetch_idx = atomic_read(&cli->fetch_idx);
	if (read_idx != fetch_idx) {
		err = -EBUSY;
		goto error_out;
	}

	read_idx %= cli->sample_arr.sample_count;
	sample_meta = cli->sample_arr.samples[read_idx].sample_meta;
	sample_offset_bytes = (u64)((u8 *)sample_meta - cli->sample_arr.user_buf);

	sample_access->sequence = sample_meta->u.sample_md.seq;
	sample_access->sample_offset_bytes = sample_offset_bytes;

	atomic_inc(&cli->fetch_idx);

error_out:
	mutex_unlock(&cli->cmd_sync_lock);
	return err;
}

static int kbasep_kinstr_prfcnt_put_sample(struct kbase_kinstr_prfcnt_client *cli,
					   struct prfcnt_sample_access *sample_access)
{
	int write_idx;
	int read_idx;
	int fetch_idx;
	u64 sample_offset_bytes;
	int err = 0;

	mutex_lock(&cli->cmd_sync_lock);
	write_idx = atomic_read(&cli->write_idx);
	read_idx = atomic_read(&cli->read_idx);

	if (write_idx == read_idx || sample_access->sequence != read_idx) {
		err = -EINVAL;
		goto error_out;
	}

	read_idx %= cli->sample_arr.sample_count;
	sample_offset_bytes = (u64)((u8 *)cli->sample_arr.samples[read_idx].sample_meta -
				    cli->sample_arr.user_buf);

	if (sample_access->sample_offset_bytes != sample_offset_bytes) {
		err = -EINVAL;
		goto error_out;
	}

	fetch_idx = atomic_read(&cli->fetch_idx);
	WARN_ON(read_idx == fetch_idx);

	atomic_set(&cli->read_idx, fetch_idx);

error_out:
	mutex_unlock(&cli->cmd_sync_lock);
	return err;
}

static long kbasep_kinstr_hwcnt_reader_ioctl_set_interval(
	struct kbase_kinstr_prfcnt_client *cli,
	u32 interval)
{
	mutex_lock(&cli->kinstr_ctx->lock);

	if ((interval != 0) && (interval < DUMP_INTERVAL_MIN_NS))
		interval = DUMP_INTERVAL_MIN_NS;

	cli->dump_interval_ns = interval;
	cli->next_dump_time_ns = 0;

	if ((interval != 0) && (cli->kinstr_ctx->suspend_count == 0))
		kbase_hwcnt_virtualizer_queue_work(cli->kinstr_ctx->hvirt,
						   &cli->kinstr_ctx->dump_work);

	mutex_unlock(&cli->kinstr_ctx->lock);

	return 0;
}

static long kbasep_kinstr_prfcnt_hwcnt_reader_ioctl(struct file *filp, unsigned int cmd,
						    unsigned long arg)
{
	long rcode = 0;
	struct kbase_kinstr_prfcnt_client *cli;
	void __user *uarg = (void __user *)arg;

	if (!filp)
		return -EINVAL;

	cli = filp->private_data;

	if (!cli)
		return -EINVAL;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_CMD): {
		struct prfcnt_control_cmd control_cmd;
		int err;

		err = copy_from_user(&control_cmd, uarg, sizeof(control_cmd));
		if (err)
			return -EFAULT;
		rcode = kbasep_kinstr_prfcnt_cmd(cli, &control_cmd);
		if (mtk_pm_tool != pm_non) {
			MTK_kbasep_vinstr_hwcnt_set_interval(0);
			ds5_used = 1;
		}
	} break;
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_GET_SAMPLE): {
		struct prfcnt_sample_access sample_access;
		int err;

		memset(&sample_access, 0, sizeof(sample_access));
		rcode = kbasep_kinstr_prfcnt_get_sample(cli, &sample_access);
		err = copy_to_user(uarg, &sample_access, sizeof(sample_access));
		if (err)
			return -EFAULT;
	} break;
	case _IOC_NR(KBASE_IOCTL_KINSTR_PRFCNT_PUT_SAMPLE): {
		struct prfcnt_sample_access sample_access;
		int err;

#if IS_ENABLED(CONFIG_MALI_MTK_HWCNT_HINT)
		hwcnt_hint(false);
#endif

		err = copy_from_user(&sample_access, uarg, sizeof(sample_access));
		if (err)
			return -EFAULT;
		rcode = kbasep_kinstr_prfcnt_put_sample(cli, &sample_access);
	} break;
	case _IOC_NR(KBASE_HWCNT_READER_SET_INTERVAL): {
		rcode = kbasep_kinstr_hwcnt_reader_ioctl_set_interval(
			cli, (u32)arg);
		if ((u32)arg == 0 && mtk_pm_tool != pm_non) {
			ds5_used = 0;
			if (mtk_pm_tool == pm_ltr)
				MTK_kbasep_vinstr_hwcnt_set_interval(8000000);
			else if (mtk_pm_tool == pm_swpm)
				MTK_kbasep_vinstr_hwcnt_set_interval(1000000);
                }
	} break;
	default:
		rcode = -EINVAL;
		break;
	}

	return rcode;
}

static int kbasep_kinstr_prfcnt_hwcnt_reader_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kbase_kinstr_prfcnt_client *cli;

	if (!filp || !vma)
		return -EINVAL;

	cli = filp->private_data;
	if (!cli)
		return -EINVAL;

	return remap_vmalloc_range(vma, cli->sample_arr.user_buf, 0);
}

static void
kbasep_kinstr_prfcnt_sample_array_free(struct kbase_kinstr_prfcnt_sample_array *sample_arr)
{
	if (!sample_arr)
		return;

	kfree(sample_arr->samples);
	kfree(sample_arr->blk_stt_scratch);
	vfree(sample_arr->user_buf);
	memset(sample_arr, 0, sizeof(*sample_arr));
}

static void
kbasep_kinstr_prfcnt_client_destroy_partial(struct kbase_kinstr_prfcnt_client *cli,
					    enum kbase_kinstr_prfcnt_client_init_state init_state)
{
	if (!cli)
		return;

	while (init_state-- > KINSTR_PRFCNT_UNINITIALISED) {
		switch (init_state) {
		case KINSTR_PRFCNT_INITIALISED:

			break;
		case KINSTR_PRFCNT_WAITQ_MUTEX:
			mutex_destroy(&cli->cmd_sync_lock);
			break;
		case KINSTR_PRFCNT_VIRTUALIZER_CLIENT:
			kbase_hwcnt_virtualizer_client_destroy(cli->hvcli);
			break;
		case KINSTR_PRFCNT_SAMPLE_ARRAY:
			kbasep_kinstr_prfcnt_sample_array_free(&cli->sample_arr);
			break;
		case KINSTR_PRFCNT_DUMP_BUFFER:
			kbase_hwcnt_dump_buffer_free(&cli->tmp_buf);
			break;
		case KINSTR_PRFCNT_ENABLE_MAP:
			kbase_hwcnt_enable_map_free(&cli->enable_map);
			break;
		case KINSTR_PRFCNT_PARSE_SETUP:

			break;
		}
	}
	kfree(cli);
}

void kbasep_kinstr_prfcnt_client_destroy(struct kbase_kinstr_prfcnt_client *cli)
{
	kbasep_kinstr_prfcnt_client_destroy_partial(cli, KINSTR_PRFCNT_INITIALISED);
}

static int kbasep_kinstr_prfcnt_hwcnt_reader_release(struct inode *inode, struct file *filp)
{
	struct kbase_kinstr_prfcnt_client *cli = filp->private_data;

	CSTD_UNUSED(inode);
	if (ds5_used) {
	mutex_lock(&cli->kinstr_ctx->lock);

	WARN_ON(cli->kinstr_ctx->client_count == 0);
	if (cli->kinstr_ctx->client_count > 0)
		cli->kinstr_ctx->client_count--;
	list_del(&cli->node);

	mutex_unlock(&cli->kinstr_ctx->lock);

	kbasep_kinstr_prfcnt_client_destroy(cli);
	}

	return 0;
}

static const struct file_operations kinstr_prfcnt_client_fops = {
	.owner = THIS_MODULE,
	.poll = kbasep_kinstr_prfcnt_hwcnt_reader_poll,
	.unlocked_ioctl = kbasep_kinstr_prfcnt_hwcnt_reader_ioctl,
	.compat_ioctl = kbasep_kinstr_prfcnt_hwcnt_reader_ioctl,
	.mmap = kbasep_kinstr_prfcnt_hwcnt_reader_mmap,
	.release = kbasep_kinstr_prfcnt_hwcnt_reader_release,
};

size_t kbasep_kinstr_prfcnt_get_sample_md_count(const struct kbase_hwcnt_metadata *metadata,
						struct kbase_hwcnt_enable_map *enable_map)
{
	size_t blk, blk_inst;
	size_t md_count = 0;

	if (!metadata)
		return 0;

	kbase_hwcnt_metadata_for_each_block(metadata, blk, blk_inst) {

		if (kbase_kinstr_is_block_type_reserved(metadata, blk) ||
		    !kbase_hwcnt_metadata_block_instance_avail(metadata, blk, blk_inst) ||
		    !kbase_hwcnt_enable_map_block_enabled(enable_map, blk, blk_inst))
			continue;

		md_count++;
	}

	md_count += 2;

	md_count++;

	return md_count;
}

static size_t kbasep_kinstr_prfcnt_get_sample_size(struct kbase_kinstr_prfcnt_client *cli,
						   const struct kbase_hwcnt_metadata *metadata)
{
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	struct kbase_hwcnt_dump_buffer *dump_buf = &cli->tmp_buf;
	size_t md_count = kbasep_kinstr_prfcnt_get_sample_md_count(metadata, &cli->enable_map);

	if (!metadata)
		return 0;

	sample_meta_bytes = sizeof(struct prfcnt_metadata) * md_count;
	dump_buf_bytes = metadata->dump_buf_bytes;
	clk_cnt_buf_bytes = sizeof(*dump_buf->clk_cnt_buf) * metadata->clk_cnt;

	return (sample_meta_bytes + dump_buf_bytes + clk_cnt_buf_bytes);
}

static void kbasep_kinstr_prfcnt_dump_worker(struct work_struct *work)
{
	struct kbase_kinstr_prfcnt_context *kinstr_ctx =
		container_of(work, struct kbase_kinstr_prfcnt_context, dump_work);
	struct kbase_kinstr_prfcnt_client *pos;
	u64 cur_time_ns;

	mutex_lock(&kinstr_ctx->lock);

	cur_time_ns = kbasep_kinstr_prfcnt_timestamp_ns();

	list_for_each_entry(pos, &kinstr_ctx->clients, node) {
		if (pos->active && (pos->next_dump_time_ns != 0) &&
		    (pos->next_dump_time_ns < cur_time_ns))
			kbasep_kinstr_prfcnt_client_dump(pos, BASE_HWCNT_READER_EVENT_PERIODIC,
							 pos->user_data);
	}

	kbasep_kinstr_prfcnt_reschedule_worker(kinstr_ctx);

	mutex_unlock(&kinstr_ctx->lock);
}

static enum hrtimer_restart kbasep_kinstr_prfcnt_dump_timer(struct hrtimer *timer)
{
	struct kbase_kinstr_prfcnt_context *kinstr_ctx =
		container_of(timer, struct kbase_kinstr_prfcnt_context, dump_timer);

	kbase_hwcnt_virtualizer_queue_work(kinstr_ctx->hvirt, &kinstr_ctx->dump_work);

	return HRTIMER_NORESTART;
}

int kbase_kinstr_prfcnt_init(struct kbase_hwcnt_virtualizer *hvirt,
			     struct kbase_kinstr_prfcnt_context **out_kinstr_ctx)
{
	struct kbase_kinstr_prfcnt_context *kinstr_ctx;
	const struct kbase_hwcnt_metadata *metadata;

	if (!hvirt || !out_kinstr_ctx)
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(hvirt);

	if (!metadata)
		return -EINVAL;

	kinstr_ctx = kzalloc(sizeof(*kinstr_ctx), GFP_KERNEL);

	if (!kinstr_ctx)
		return -ENOMEM;

	kinstr_ctx->hvirt = hvirt;
	kinstr_ctx->metadata = metadata;

	mutex_init(&kinstr_ctx->lock);
	INIT_LIST_HEAD(&kinstr_ctx->clients);
	hrtimer_init(&kinstr_ctx->dump_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kinstr_ctx->dump_timer.function = kbasep_kinstr_prfcnt_dump_timer;
	INIT_WORK(&kinstr_ctx->dump_work, kbasep_kinstr_prfcnt_dump_worker);

	*out_kinstr_ctx = kinstr_ctx;
	return 0;
}

void kbase_kinstr_prfcnt_term(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (!kinstr_ctx)
		return;

	if (WARN_ON(kinstr_ctx->client_count > 0)) {
		struct kbase_kinstr_prfcnt_client *pos, *n;

		list_for_each_entry_safe(pos, n, &kinstr_ctx->clients, node) {
			list_del(&pos->node);
			kinstr_ctx->client_count--;
			kbasep_kinstr_prfcnt_client_destroy(pos);
		}
	}

	cancel_work_sync(&kinstr_ctx->dump_work);

	WARN_ON(kinstr_ctx->client_count > 0);
	kfree(kinstr_ctx);
}

void kbase_kinstr_prfcnt_suspend(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (!kinstr_ctx) {
		pr_warn("%s: kinstr_ctx is NULL\n", __func__);
		return;
	}

	mutex_lock(&kinstr_ctx->lock);

	if (!WARN_ON(kinstr_ctx->suspend_count == SIZE_MAX))
		kinstr_ctx->suspend_count++;

	mutex_unlock(&kinstr_ctx->lock);

	hrtimer_cancel(&kinstr_ctx->dump_timer);
	cancel_work_sync(&kinstr_ctx->dump_work);
}

void kbase_kinstr_prfcnt_resume(struct kbase_kinstr_prfcnt_context *kinstr_ctx)
{
	if (!kinstr_ctx) {
		pr_warn("%s: kinstr_ctx is NULL\n", __func__);
		return;
	}

	mutex_lock(&kinstr_ctx->lock);

	if (!WARN_ON(kinstr_ctx->suspend_count == 0)) {
		kinstr_ctx->suspend_count--;

		if (kinstr_ctx->suspend_count == 0) {
			struct kbase_kinstr_prfcnt_client *pos;
			bool has_periodic_clients = false;

			list_for_each_entry(pos, &kinstr_ctx->clients, node) {
				if (pos->dump_interval_ns != 0) {
					has_periodic_clients = true;
					break;
				}
			}

			if (has_periodic_clients)
				kbase_hwcnt_virtualizer_queue_work(kinstr_ctx->hvirt,
								   &kinstr_ctx->dump_work);
		}
	}

	mutex_unlock(&kinstr_ctx->lock);
}

static int kbasep_kinstr_prfcnt_sample_array_alloc(struct kbase_kinstr_prfcnt_client *cli,
						   const struct kbase_hwcnt_metadata *metadata)
{
	struct kbase_kinstr_prfcnt_sample_array *sample_arr = &cli->sample_arr;
	struct kbase_kinstr_prfcnt_sample *samples;
	blk_stt_t *scratch_buffer;
	size_t sample_idx;
	size_t dump_buf_bytes;
	size_t clk_cnt_buf_bytes;
	size_t sample_meta_bytes;
	size_t scratch_buffer_bytes;
	size_t md_count;
	size_t sample_size;
	size_t buffer_count = cli->config.buffer_count;

	if (!metadata || !sample_arr)
		return -EINVAL;

	md_count = kbasep_kinstr_prfcnt_get_sample_md_count(metadata, &cli->enable_map);
	sample_meta_bytes = sizeof(struct prfcnt_metadata) * md_count;
	dump_buf_bytes = metadata->dump_buf_bytes;
	clk_cnt_buf_bytes = sizeof(*samples->dump_buf.clk_cnt_buf) * metadata->clk_cnt;
	sample_size = sample_meta_bytes + dump_buf_bytes + clk_cnt_buf_bytes;

	scratch_buffer_bytes = metadata->blk_stt_bytes;

	samples = kmalloc_array(buffer_count, sizeof(*samples), GFP_KERNEL);

	if (!samples)
		return -ENOMEM;

	sample_arr->user_buf = vmalloc_user(sample_size * buffer_count);

	if (!sample_arr->user_buf) {
		kfree(samples);
		return -ENOMEM;
	}

	scratch_buffer = kmalloc(scratch_buffer_bytes * sizeof(*scratch_buffer), GFP_KERNEL);
	if (!scratch_buffer) {
		vfree(sample_arr->user_buf);
		kfree(samples);
		return -ENOMEM;
	}

	sample_arr->blk_stt_scratch = scratch_buffer;
	sample_arr->sample_count = buffer_count;
	sample_arr->samples = samples;

	for (sample_idx = 0; sample_idx < buffer_count; sample_idx++) {
		const size_t sample_meta_offset = sample_size * sample_idx;
		const size_t dump_buf_offset = sample_meta_offset + sample_meta_bytes;
		const size_t clk_cnt_buf_offset = dump_buf_offset + dump_buf_bytes;

		samples[sample_idx].dump_buf.metadata = metadata;
		samples[sample_idx].sample_meta =
			(struct prfcnt_metadata *)(sample_arr->user_buf + sample_meta_offset);
		samples[sample_idx].dump_buf.dump_buf =
			(u64 *)(sample_arr->user_buf + dump_buf_offset);
		samples[sample_idx].dump_buf.clk_cnt_buf =
			(u64 *)(sample_arr->user_buf + clk_cnt_buf_offset);
		samples[sample_idx].dump_buf.blk_stt_buf = scratch_buffer;
	}

	return 0;
}

static bool prfcnt_mode_supported(u8 mode)
{
	return (mode == PRFCNT_MODE_MANUAL) || (mode == PRFCNT_MODE_PERIODIC);
}

static bool prfcnt_block_supported(const struct kbase_hwcnt_metadata *metadata,
				   const struct prfcnt_request_enable *req_enable)
{
	size_t blk, blk_inst;

	kbase_hwcnt_metadata_for_each_block(metadata, blk, blk_inst) {
		const enum kbase_hwcnt_gpu_v5_block_type blk_type =
			kbase_hwcnt_metadata_block_type(metadata, blk);
		const enum prfcnt_block_type prfcnt_block_type =
			kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(blk_type);

		if (prfcnt_block_type == req_enable->block_type)
			return true;
	}
	return false;
}

static void kbasep_kinstr_prfcnt_block_enable_req_to_cfg(uint64_t *cfg_cm,
							 const uint64_t *enable_mask)
{

	cfg_cm[0] |= (0xF | enable_mask[0]);
	cfg_cm[1] |= enable_mask[1];
}

static int
kbasep_kinstr_prfcnt_parse_request_enable(const struct prfcnt_request_enable *req_enable,
					  struct kbase_kinstr_prfcnt_client_config *config)
{
	int err = 0;
	u8 req_set = KBASE_HWCNT_SET_UNDEFINED, default_set;

	switch (req_enable->set) {
	case PRFCNT_SET_PRIMARY:
		req_set = KBASE_HWCNT_SET_PRIMARY;
		break;
	case PRFCNT_SET_SECONDARY:
		req_set = KBASE_HWCNT_SET_SECONDARY;
		break;
	case PRFCNT_SET_TERTIARY:
		req_set = KBASE_HWCNT_SET_TERTIARY;
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (!err) {
		if (config->counter_set == KBASE_HWCNT_SET_UNDEFINED)
			config->counter_set = req_set;
		else if (config->counter_set != req_set)
			err = -EINVAL;
	}

#if defined(CONFIG_MALI_PRFCNT_SET_SECONDARY)
	default_set = KBASE_HWCNT_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
	default_set = KBASE_HWCNT_SET_TERTIARY;
#else

	default_set = KBASE_HWCNT_SET_PRIMARY;
#endif

	if (req_set != default_set)
		err = -EINVAL;

	if (err < 0)
		return err;

	switch (req_enable->block_type) {
	case PRFCNT_BLOCK_TYPE_FE:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.fe_bm,
							     req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_TILER:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.tiler_bm,
							     req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_MEMORY:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.mmu_l2_bm,
							     req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_SHADER_CORE:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.shader_bm,
							     req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_FW:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.fw_bm,
							     req_enable->enable_mask);
		break;
	case PRFCNT_BLOCK_TYPE_CSG:
		kbasep_kinstr_prfcnt_block_enable_req_to_cfg(config->enable_cm.csg_bm,
							     req_enable->enable_mask);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int
kbasep_kinstr_prfcnt_parse_request_scope(const struct prfcnt_request_scope *req_scope,
					 struct kbase_kinstr_prfcnt_client_config *config)
{
	int err = 0;

	if (config->scope == PRFCNT_SCOPE_RESERVED)
		config->scope = req_scope->scope;
	else if (config->scope != req_scope->scope)
		err = -EINVAL;

	return err;
}

static int kbasep_kinstr_prfcnt_parse_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
					    union kbase_ioctl_kinstr_prfcnt_setup *setup,
					    struct kbase_kinstr_prfcnt_client_config *config,
					    struct prfcnt_request_item *req_arr)
{
	uint32_t i;
	unsigned int item_count = setup->in.request_item_count;
	int err = 0;

	CSTD_UNUSED(kinstr_ctx);

	if (req_arr[item_count - 1].hdr.item_type != FLEX_LIST_TYPE_NONE ||
	    req_arr[item_count - 1].hdr.item_version != 0) {
		return -EINVAL;
	}

	config->prfcnt_mode = PRFCNT_MODE_RESERVED;
	config->counter_set = KBASE_HWCNT_SET_UNDEFINED;
	config->scope = PRFCNT_SCOPE_RESERVED;

	for (i = 0; i < item_count - 1; i++) {
		if (req_arr[i].hdr.item_version > PRFCNT_READER_API_VERSION) {
			err = -EINVAL;
			break;
		}

		switch (req_arr[i].hdr.item_type) {

		case PRFCNT_REQUEST_TYPE_MODE:
			if (!prfcnt_mode_supported(req_arr[i].u.req_mode.mode))
				err = -EINVAL;
			else if (config->prfcnt_mode == PRFCNT_MODE_RESERVED)
				config->prfcnt_mode = req_arr[i].u.req_mode.mode;
			else if (req_arr[i].u.req_mode.mode != config->prfcnt_mode)
				err = -EINVAL;

			if (err < 0)
				break;

			if (config->prfcnt_mode == PRFCNT_MODE_PERIODIC) {
				config->period_ns =
					req_arr[i].u.req_mode.mode_config.periodic.period_ns;

				if ((config->period_ns != 0) &&
				    (config->period_ns < DUMP_INTERVAL_MIN_NS)) {
					config->period_ns = DUMP_INTERVAL_MIN_NS;
				}

				if (config->period_ns == 0)
					err = -EINVAL;
			}
			break;

		case PRFCNT_REQUEST_TYPE_ENABLE:
			if (!prfcnt_block_supported(kinstr_ctx->metadata,
						    &req_arr[i].u.req_enable)) {
				err = -EINVAL;
				break;
			}

			err = kbasep_kinstr_prfcnt_parse_request_enable(&req_arr[i].u.req_enable,
									config);
			break;

		case PRFCNT_REQUEST_TYPE_SCOPE:
			err = kbasep_kinstr_prfcnt_parse_request_scope(&req_arr[i].u.req_scope,
								       config);
			break;

		default:
			err = -EINVAL;
			break;
		}

		if (err < 0)
			break;
	}

	if (!err) {

		if (config->prfcnt_mode == PRFCNT_MODE_RESERVED)
			err = -EINVAL;

		if (config->counter_set == KBASE_HWCNT_SET_UNDEFINED)
			err = -EINVAL;
	}

	return err;
}

int kbasep_kinstr_prfcnt_client_create(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				       union kbase_ioctl_kinstr_prfcnt_setup *setup,
				       struct kbase_kinstr_prfcnt_client **out_vcli,
				       struct prfcnt_request_item *req_arr)
{
	int err;
	struct kbase_kinstr_prfcnt_client *cli;
	enum kbase_kinstr_prfcnt_client_init_state init_state;

	if (WARN_ON(!kinstr_ctx))
		return -EINVAL;

	if (WARN_ON(!setup))
		return -EINVAL;

	if (WARN_ON(!req_arr))
		return -EINVAL;

	cli = kzalloc(sizeof(*cli), GFP_KERNEL);

	if (!cli)
		return -ENOMEM;

	for (init_state = KINSTR_PRFCNT_UNINITIALISED; init_state < KINSTR_PRFCNT_INITIALISED;
	     init_state++) {
		err = 0;
		switch (init_state) {
		case KINSTR_PRFCNT_PARSE_SETUP:
			cli->kinstr_ctx = kinstr_ctx;
			err = kbasep_kinstr_prfcnt_parse_setup(kinstr_ctx, setup, &cli->config,
							       req_arr);

			break;

		case KINSTR_PRFCNT_ENABLE_MAP:
			cli->config.buffer_count = MAX_BUFFER_COUNT;
			cli->dump_interval_ns = cli->config.period_ns;
			cli->next_dump_time_ns = 0;
			cli->active = false;
			atomic_set(&cli->write_idx, 0);
			atomic_set(&cli->read_idx, 0);
			atomic_set(&cli->fetch_idx, 0);

			err = kbase_hwcnt_enable_map_alloc(kinstr_ctx->metadata, &cli->enable_map);
			break;

		case KINSTR_PRFCNT_DUMP_BUFFER:
			kbase_hwcnt_gpu_enable_map_from_cm(&cli->enable_map,
							   &cli->config.enable_cm);
			if(mtk_pm_tool==pm_ltr) {
				cli->config.enable_cm.fe_bm[0] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.fe_bm[1] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.shader_bm[0] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.shader_bm[1] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.tiler_bm[0] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.tiler_bm[1] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.mmu_l2_bm[0] = 0xFFFFFFFFFFFFFFFF;
				cli->config.enable_cm.mmu_l2_bm[1] = 0xFFFFFFFFFFFFFFFF;
			}
			cli->sample_count = cli->config.buffer_count;
			cli->sample_size =
				kbasep_kinstr_prfcnt_get_sample_size(cli, kinstr_ctx->metadata);

			err = kbase_hwcnt_dump_buffer_alloc(kinstr_ctx->metadata, &cli->tmp_buf);
			break;

		case KINSTR_PRFCNT_SAMPLE_ARRAY:

			cli->enable_map.clk_enable_map = 0;

			err = kbasep_kinstr_prfcnt_sample_array_alloc(cli, kinstr_ctx->metadata);

			break;

		case KINSTR_PRFCNT_VIRTUALIZER_CLIENT:

			kbase_hwcnt_gpu_enable_map_from_cm(&cli->enable_map,
							   &(struct kbase_hwcnt_enable_cm){});

			err = kbase_hwcnt_virtualizer_client_create(kinstr_ctx->hvirt,
								    &cli->enable_map, &cli->hvcli);
			break;

		case KINSTR_PRFCNT_WAITQ_MUTEX:
			init_waitqueue_head(&cli->waitq);
			mutex_init(&cli->cmd_sync_lock);
			break;

		case KINSTR_PRFCNT_INITIALISED:

			break;
		}

		if (err < 0) {
			kbasep_kinstr_prfcnt_client_destroy_partial(cli, init_state);
			return err;
		}
	}
	*out_vcli = cli;

	return 0;
}

static size_t kbasep_kinstr_prfcnt_get_block_info_count(const struct kbase_hwcnt_metadata *metadata)
{
	size_t blk;
	size_t block_info_count = 0;

	if (!metadata)
		return 0;

	for (blk = 0; blk < kbase_hwcnt_metadata_block_count(metadata); blk++) {
		if (!kbase_kinstr_is_block_type_reserved(metadata, blk))
			block_info_count++;
	}

	return block_info_count;
}

static size_t kbasep_kinstr_prfcnt_get_enum_info_count(const struct kbase_hwcnt_metadata *metadata)
{

	uint32_t count = ARRAY_SIZE(kinstr_prfcnt_supported_requests);

	count += kbasep_kinstr_prfcnt_get_block_info_count(metadata);

	return count + 2;
}

static void kbasep_kinstr_prfcnt_get_request_info_list(struct prfcnt_enum_item *item_arr,
						       size_t *arr_idx)
{
	memcpy(&item_arr[*arr_idx], kinstr_prfcnt_supported_requests,
	       sizeof(kinstr_prfcnt_supported_requests));
	*arr_idx += ARRAY_SIZE(kinstr_prfcnt_supported_requests);
}

static void kbasep_kinstr_prfcnt_get_sample_info_item(const struct kbase_hwcnt_metadata *metadata,
						      struct prfcnt_enum_item *item_arr,
						      size_t *arr_idx)
{
	struct prfcnt_enum_item sample_info = {
		.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_SAMPLE_INFO,
				.item_version = PRFCNT_READER_API_VERSION,
			},
		.u.sample_info = {
				.num_clock_domains = metadata->clk_cnt,
			},
	};

	item_arr[*arr_idx] = sample_info;
	*arr_idx += 1;
}

int kbasep_kinstr_prfcnt_get_block_info_list(const struct kbase_hwcnt_metadata *metadata,
					     size_t block_set, struct prfcnt_enum_item *item_arr,
					     size_t *arr_idx)
{
	size_t blk;

	if (!metadata || !item_arr || !arr_idx)
		return -EINVAL;

	for (blk = 0; blk < kbase_hwcnt_metadata_block_count(metadata); blk++) {
		size_t blk_inst;
		size_t unused_blk_inst_count = 0;
		size_t blk_inst_count = kbase_hwcnt_metadata_block_instance_count(metadata, blk);
		enum prfcnt_block_type block_type =
			kbase_hwcnt_metadata_block_type_to_prfcnt_block_type(
				kbase_hwcnt_metadata_block_type(metadata, blk));

		if (block_type == PRFCNT_BLOCK_TYPE_RESERVED)
			continue;

		for (blk_inst = 0; blk_inst < blk_inst_count; blk_inst++) {
			if (!kbase_hwcnt_metadata_block_instance_avail(metadata, blk, blk_inst))
				unused_blk_inst_count++;
		}

		item_arr[(*arr_idx)++] = (struct prfcnt_enum_item){
			.hdr = {
				.item_type = PRFCNT_ENUM_TYPE_BLOCK,
				.item_version = PRFCNT_READER_API_VERSION,
			},
				.u.block_counter = {
					.set = block_set,
					.block_type = block_type,
					.num_instances = blk_inst_count - unused_blk_inst_count,
					.num_values = kbase_hwcnt_metadata_block_values_count(
							metadata, blk),

					.counter_mask = {U64_MAX, U64_MAX},
				},
		};
	}

	return 0;
}

static int
kbasep_kinstr_prfcnt_enum_info_count(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				     struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	const struct kbase_hwcnt_metadata *metadata;

	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);

	enum_info->info_item_count = kbasep_kinstr_prfcnt_get_enum_info_count(metadata);
	enum_info->info_item_size = sizeof(struct prfcnt_enum_item);

	return 0;
}

int kbasep_kinstr_populate_prfcnt_enum_list(const struct kbase_hwcnt_metadata *metadata,
					    struct prfcnt_enum_item *prfcnt_item_arr,
					    size_t prfcnt_item_array_size)
{
	size_t arr_idx = 0;
	int err = 0;
	size_t block_info_count;

	if (!metadata)
		return -EINVAL;

	block_info_count = kbasep_kinstr_prfcnt_get_block_info_count(metadata);

	if (block_info_count >= prfcnt_item_array_size)
		return -EINVAL;

	kbasep_kinstr_prfcnt_get_request_info_list(prfcnt_item_arr, &arr_idx);

	kbasep_kinstr_prfcnt_get_sample_info_item(metadata, prfcnt_item_arr, &arr_idx);

	if (arr_idx + block_info_count >= prfcnt_item_array_size)
		err = -EINVAL;

	if (!err) {
		size_t counter_set;

#if defined(CONFIG_MALI_PRFCNT_SET_SECONDARY)
		counter_set = KBASE_HWCNT_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
		counter_set = KBASE_HWCNT_SET_TERTIARY;
#else

		counter_set = KBASE_HWCNT_SET_PRIMARY;
#endif
		kbasep_kinstr_prfcnt_get_block_info_list(metadata, counter_set, prfcnt_item_arr,
							 &arr_idx);
		if (arr_idx != prfcnt_item_array_size - 1)
			err = -EINVAL;
	}

	prfcnt_item_arr[prfcnt_item_array_size - 1].hdr.item_type = FLEX_LIST_TYPE_NONE;
	prfcnt_item_arr[prfcnt_item_array_size - 1].hdr.item_version = 0;

	return err;
}

static int
kbasep_kinstr_prfcnt_enum_info_list(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				    struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	struct prfcnt_enum_item *prfcnt_item_arr;
	int err = 0;
	const struct kbase_hwcnt_metadata *metadata;

	if ((enum_info->info_item_size == 0) || (enum_info->info_item_count == 0) ||
	    !enum_info->info_list_ptr)
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);

	if (enum_info->info_item_count != kbasep_kinstr_prfcnt_get_enum_info_count(metadata))
		return -EINVAL;

	prfcnt_item_arr = kcalloc(enum_info->info_item_count, sizeof(*prfcnt_item_arr), GFP_KERNEL);
	if (!prfcnt_item_arr)
		return -ENOMEM;

	err = kbasep_kinstr_populate_prfcnt_enum_list(metadata, prfcnt_item_arr,
						      enum_info->info_item_count);

	if (!err) {
		unsigned long bytes = enum_info->info_item_count * sizeof(*prfcnt_item_arr);

		if (copy_to_user(u64_to_user_ptr(enum_info->info_list_ptr), prfcnt_item_arr, bytes))
			err = -EFAULT;
	}

	kfree(prfcnt_item_arr);
	return err;
}

int kbase_kinstr_prfcnt_enum_info(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				  struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info)
{
	int err;

	if (!kinstr_ctx || !enum_info)
		return -EINVAL;

	if (!enum_info->info_list_ptr)
		err = kbasep_kinstr_prfcnt_enum_info_count(kinstr_ctx, enum_info);
	else
		err = kbasep_kinstr_prfcnt_enum_info_list(kinstr_ctx, enum_info);

	return err;
}

int kbase_kinstr_prfcnt_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
			      union kbase_ioctl_kinstr_prfcnt_setup *setup)
{
	int err;
	size_t item_count, max_item_count;
	size_t bytes;
	struct prfcnt_request_item *req_arr = NULL;
	struct kbase_kinstr_prfcnt_client *cli = NULL;
	const size_t max_bytes = 32 * sizeof(*req_arr);
	const struct kbase_hwcnt_metadata *metadata;

	if (!kinstr_ctx || !setup)
		return -EINVAL;

	item_count = setup->in.request_item_count;

	if (!setup->in.requests_ptr || (item_count < 2) || (setup->in.request_item_size == 0))
		return -EINVAL;

	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);

	max_item_count = kbasep_kinstr_prfcnt_get_enum_info_count(metadata);

	if (item_count > 2 * max_item_count)
		return -EINVAL;

	kinstr_ctx->info_item_count = max_item_count;

	if (check_mul_overflow(item_count, sizeof(*req_arr), &bytes))
		return -EINVAL;

	if (bytes > max_bytes)
		return -EINVAL;

	req_arr = memdup_user(u64_to_user_ptr(setup->in.requests_ptr), bytes);

	if (IS_ERR(req_arr))
		return PTR_ERR(req_arr);

	err = kbasep_kinstr_prfcnt_client_create(kinstr_ctx, setup, &cli, req_arr);

	if (err < 0)
		goto error;

	mutex_lock(&kinstr_ctx->lock);
	kinstr_ctx->client_count++;
	list_add(&cli->node, &kinstr_ctx->clients);
	mutex_unlock(&kinstr_ctx->lock);

	setup->out.prfcnt_metadata_item_size = sizeof(struct prfcnt_metadata);
	setup->out.prfcnt_mmap_size_bytes = cli->sample_size * cli->sample_count;

	err = anon_inode_getfd("[mali_kinstr_prfcnt_desc]", &kinstr_prfcnt_client_fops, cli,
			       O_RDONLY | O_CLOEXEC);

	if (err < 0)
		goto client_installed_error;

	goto free_buf;

client_installed_error:
	mutex_lock(&kinstr_ctx->lock);
	kinstr_ctx->client_count--;
	list_del(&cli->node);
	mutex_unlock(&kinstr_ctx->lock);
error:
	kbasep_kinstr_prfcnt_client_destroy(cli);
free_buf:
	kfree(req_arr);
	return err;
}

void kbasep_vinstr_dump_worker(struct work_struct *work)
{
	return;
}

void MTK_update_mtk_pm(int flag)
{
	mtk_pm_tool = flag;
}

int MTK_get_mtk_pm(void)
{
	return mtk_pm_tool;
}

int MTK_kbase_vinstr_hwcnt_reader_setup(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	union kbase_ioctl_kinstr_prfcnt_setup *setup)
{
	int err, ret;
	const uint64_t user_data_sample = 0xDEADBEEF;
	int fd;
	size_t item_count, max_item_count;

	struct prfcnt_request_item *req_arr = NULL;
	struct kbase_kinstr_prfcnt_client *cli = NULL;

	const struct kbase_hwcnt_metadata *metadata;
	struct prfcnt_request_item req_arr_[] = {
		{
			.hdr.item_type = PRFCNT_REQUEST_TYPE_MODE,
			.hdr.item_version = HWCNT_READER_API,
			.u.req_mode.mode = PRFCNT_MODE_PERIODIC,
			.u.req_mode.mode_config.periodic.period_ns = KINSTR_PRFCNT_PERIOD_NS,
		},
		{
			.hdr.item_type = PRFCNT_REQUEST_TYPE_ENABLE,
			.hdr.item_version = HWCNT_READER_API,
			.u.req_enable.block_type = PRFCNT_BLOCK_TYPE_FE,
			.u.req_enable.set = PRFCNT_SET_PRIMARY,
			.u.req_enable.enable_mask[0] = 0xFFFFFFFFFFFFFFFF,
			.u.req_enable.enable_mask[1] = 0x0000000000000000,
		},
		{
			.hdr.item_type = FLEX_LIST_TYPE_NONE,
			.hdr.item_version = 0,
		}
	};

	if (!kinstr_ctx || !setup)
	{
		return -EINVAL;
	}
	item_count = setup->in.request_item_count;
	metadata = kbase_hwcnt_virtualizer_metadata(kinstr_ctx->hvirt);
	max_item_count = kbasep_kinstr_prfcnt_get_enum_info_count(metadata);

	kinstr_ctx->info_item_count = max_item_count;

	err = kbasep_kinstr_prfcnt_client_create(kinstr_ctx, setup, &cli, req_arr_);

	if (err)
	{
		goto error;
	}
	else {

		ret = kbasep_kinstr_prfcnt_client_start(cli, (__u64)user_data_sample);
	}

	fd = err;

	mutex_lock(&kinstr_ctx->lock);
	mutex_lock(&gpu_vinstr_mtk_lock);

	kinstr_ctx->client_count++;
	list_add(&cli->node, &kinstr_ctx->clients);
	mtk_cli = cli;
	ds5_used = 0;
	mutex_unlock(&kinstr_ctx->lock);
	mutex_unlock(&gpu_vinstr_mtk_lock);
	kfree(req_arr);
	return fd;
error:
	kbasep_kinstr_prfcnt_client_destroy(cli);
	kfree(req_arr);
	return err;
}

void MTK_kbasep_vinstr_hwcnt_set_interval(unsigned int interval)
{
	mutex_lock(&gpu_vinstr_mtk_lock);
	if (mtk_cli != NULL) {
		kbasep_kinstr_hwcnt_reader_ioctl_set_interval(mtk_cli, interval);
	}
	mutex_unlock(&gpu_vinstr_mtk_lock);
	return;
}

void MTK_kbasep_vinstr_hwcnt_release(void)
{
	mtk_pm_tool = pm_non;
	ds5_used = 1;
	mutex_lock(&gpu_vinstr_mtk_lock);
	if (mtk_cli != NULL) {
		mutex_lock(&mtk_cli->kinstr_ctx->lock);
		mtk_cli->kinstr_ctx->suspend_count = 0;
		mtk_cli->kinstr_ctx->client_count--;
		list_del(&mtk_cli->node);
		mutex_unlock(&mtk_cli->kinstr_ctx->lock);

		kbasep_kinstr_prfcnt_client_destroy(mtk_cli);
		mtk_cli = NULL;
	}
	mutex_unlock(&gpu_vinstr_mtk_lock);
}

static inline void format_gpu_data(char *buf, u64 size, u64 *gpu_data, u32 lens)
{
	char *ptr = buf;
	char *buffer_end = buf + size;
	int i;

	ptr += snprintf(ptr, buffer_end - ptr, "ARRAY[");
	for (i = 0; i < lens; i++) {
		ptr += snprintf(ptr, buffer_end - ptr, "%02llx, %02llx, %02llx, %02llx, %02llx, %02llx, %02llx, %02llx, ",
				(*(gpu_data+i)) & 0xff, (*(gpu_data+i) >> 8) & 0xff,
				(*(gpu_data+i) >> 16) & 0xff, (*(gpu_data+i) >> 24) & 0xff,
				(*(gpu_data+i) >> 32) & 0xff, (*(gpu_data+i) >> 40) & 0xff,
				(*(gpu_data+i) >> 48) & 0xff, (*(gpu_data+i) >> 56) & 0xff);
	}
	ptr -= 2;
	ptr += snprintf(ptr, buffer_end - ptr, "]");
}

void MTK_update_gpu_LTR(void)
{
	unsigned int pm_gpu_loading = 0;
	struct mtk_gpu_perf gpu_perf_counter;
	unsigned int stall_counter[4] = {0};
	int i = 0;

	mtk_get_gpu_loading(&pm_gpu_loading);
	gpu_perf_counter.counter[VINSTR_GPU_FREQ] = gpufreq_get_cur_freq(TARGET_DEFAULT);
	gpu_perf_counter.counter[VINSTR_GPU_VOLT] = gpufreq_get_cur_volt(TARGET_DEFAULT);
	gpu_perf_counter.counter[VINSTR_GPU_LOADING] = pm_gpu_loading;

	for (i = VINSTR_GPU_ACTIVE; i <= VINSTR_LS_MEM_ATOMIC; i++) {
		int pmu_index = gpu_pmu_index[i];
		int j = 0;
		int stall_index =0;

		if (i >= VINSTR_STALL0 && i <= VINSTR_STALL3) {
			gpu_perf_counter.counter[i] = stall_counter[stall_index];
			stall_index ++;
			continue;
		} else if  (i >= VINSTR_L2_EXT_WRITE_BEATS &&i <= VINSTR_L2_ANY_LOOKUP) {
			for (j = 0; j < L2_CNT; j++) {
				gpu_perf_counter.counter[i] += kernel_dump[pmu_index];
				pmu_index += MALI_COUNTERS_PER_BLOCK;
			}
		} else {
			gpu_perf_counter.counter[i] += kernel_dump[pmu_index];
		}
	}

#if IS_ENABLED(CONFIG_MTK_PERF_TRACKER) && IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)

	trace_perf_index_gpu(gpu_perf_counter.counter, VINSTR_PERF_COUNTER_LAST);
#endif
}
