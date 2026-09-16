// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <linux/wait.h>
#include <linux/file.h>
#include <linux/sched/clock.h>
#include <linux/sync_file.h>
#include <linux/workqueue.h>

#include <drm/mediatek_drm.h>

#include "mtk_sync.h"
#include <linux/moduleparam.h>
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_fence.h>
/* MINDONE F2887: how many times the present marker was substituted with ktime_get() */
unsigned long mindone_pf_time_fix;
module_param(mindone_pf_time_fix, ulong, 0444);
MODULE_PARM_DESC(mindone_pf_time_fix, "present fence timestamps replaced by ktime_get() (F2887)");
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_session.h"
#include "mtk_drm_plane.h"
#include "mtk_drm_mmp.h"
#include "mtk_drm_drv.h"
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_gem.h>
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_trace.h>

/************************* log*********************/
static bool mtk_fence_on;

#define MTK_FENCE_LOG(fmt, arg...)                                             \
	do {                                                                   \
		if (mtk_fence_on)                                              \
			pr_debug("DISP/fence " fmt, ##arg);                    \
	} while (0)
#define MTK_FENCE_PR_ERR(fmt, arg...)                                          \
	DDPPR_ERR("DISP/fence error(%d):" fmt, __LINE__, ##arg)
#define MTK_FENCE_LOG_D(fmt, arg...) DDPFENCE("DISP/fence " fmt, ##arg)
#define MTK_FENCE_LOG_D_IF(con, fmt, arg...)                                   \
	do {                                                                   \
		if (con)                                                       \
			DDPFENCE("DISP/fence " fmt, ##arg);                    \
	} while (0)

struct mtk_fence_session_sync_info _mtk_fence_context[MAX_SESSION_COUNT];

static LIST_HEAD(info_pool_head);
static DEFINE_MUTEX(_disp_fence_mutex);
static DEFINE_MUTEX(fence_buffer_mutex);

void mtk_fence_log_enable(bool enable)
{
	mtk_fence_on = enable;
	MTK_FENCE_LOG_D("mtk_fence log %s\n", enable ? "enabled" : "disabled");
}

char *mtk_fence_session_mode_spy(unsigned int session_id)
{
	switch (MTK_SESSION_TYPE(session_id)) {
	case MTK_SESSION_PRIMARY:
		return "P";
	case MTK_SESSION_EXTERNAL:
		return "E";
	case MTK_SESSION_MEMORY:
		return "M";
	case MTK_SESSION_SP:
		return "N";
	default:
		return "Unknown";
	}
}

static struct mtk_fence_session_sync_info *
_get_session_sync_info(unsigned int session_id)
{
	int i = 0;
	int j = 0;
	struct mtk_fence_session_sync_info *session_info = NULL;
	struct mtk_fence_info *layer_info = NULL;
	char name[32];

	if ((MTK_SESSION_TYPE(session_id) != MTK_SESSION_PRIMARY) &&
	    (MTK_SESSION_TYPE(session_id) != MTK_SESSION_EXTERNAL) &&
	    (MTK_SESSION_TYPE(session_id) != MTK_SESSION_MEMORY) &&
	    (MTK_SESSION_TYPE(session_id) != MTK_SESSION_SP)) {
		DDPFENCE("invalid session id:0x%08x\n", session_id);
		return NULL;
	}

	mutex_lock(&_disp_fence_mutex);
	for (i = 0; i < ARRAY_SIZE(_mtk_fence_context); i++) {
		if (session_id == _mtk_fence_context[i].session_id) {
			/* DDPPR_ERR("found session info for
			 * session_id:0x%08x,0x%08x\n",
			 * session_id, &(_mtk_fence_context[i]));
			 */
			session_info = &(_mtk_fence_context[i]);
			goto done;
		}
	}

	for (i = 0; i < ARRAY_SIZE(_mtk_fence_context); i++) {
		if (_mtk_fence_context[i].session_id == 0xffffffff) {
			DDPPR_ERR(
				"not found session info for session_id:0x%08x,insert %p to array index:%d\n",
				session_id, &(_mtk_fence_context[i]), i);
			_mtk_fence_context[i].session_id = session_id;
			session_info = &(_mtk_fence_context[i]);

			sprintf(name, "%s%d_prepare",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_frame_cfg",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_wait_fence",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_setinput",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_setoutput",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_trigger",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_findidx",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_release",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_waitvsync",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));
			sprintf(name, "%s%d_err",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id));

			for (j = 0;
			     j < (sizeof(session_info->session_layer_info) /
				  sizeof(session_info->session_layer_info[0]));
			     j++) {

				if (MTK_SESSION_TYPE(session_id) ==
				    MTK_SESSION_PRIMARY)
					sprintf(name, "-P_%d_%d-",
						MTK_SESSION_DEV(session_id), j);
				else if (MTK_SESSION_TYPE(session_id) ==
				    MTK_SESSION_EXTERNAL)
					sprintf(name, "-E_%d_%d-",
						MTK_SESSION_DEV(session_id), j);
				else if (MTK_SESSION_TYPE(session_id) ==
				    MTK_SESSION_MEMORY)
					sprintf(name, "-M_%d_%d-",
						MTK_SESSION_DEV(session_id), j);
				else if (MTK_SESSION_TYPE(session_id) ==
				    MTK_SESSION_SP)
					sprintf(name, "-N_%d_%d-",
						MTK_SESSION_DEV(session_id), j);
				else
					sprintf(name, "-NA_%d_%d-",
						MTK_SESSION_DEV(session_id), j);

				layer_info =
					&(session_info->session_layer_info[j]);
				mutex_init(&(layer_info->sync_lock));
				layer_info->layer_id = j;
				layer_info->fence_idx = 0;
				layer_info->timeline_idx = 0;
				layer_info->inc = 0;
				layer_info->cur_idx = 0;
				layer_info->inited = 1;
				layer_info->timeline =
					mtk_sync_timeline_create(name);
				if (layer_info->timeline) {
					DDPINFO("create timeline success\n");
					DDPINFO("%s=%p, layer_info=%p\n",
						name, layer_info->timeline,
						layer_info);
				}

				INIT_LIST_HEAD(&layer_info->buf_list);
			}

			goto done;
		}
	}

done:

	if (session_info == NULL)
		DDPPR_ERR("wrong session_id:%d, 0x%08x\n", session_id,
			  session_id);

	mutex_unlock(&_disp_fence_mutex);
	return session_info;
}

struct mtk_fence_session_sync_info *
disp_get_session_sync_info(unsigned int session_id)
{
	return _get_session_sync_info(session_id);
}

struct mtk_fence_info *_disp_sync_get_sync_info(unsigned int session_id,
						unsigned int timeline_id)
{
	struct mtk_fence_info *layer_info = NULL;
	struct mtk_fence_session_sync_info *session_info =
		_get_session_sync_info(session_id);

	mutex_lock(&_disp_fence_mutex);
	if (session_info) {
		if (timeline_id >=
		    sizeof(session_info->session_layer_info) /
			    sizeof(session_info->session_layer_info[0])) {

			DDPPR_ERR("invalid timeline_id:%d\n", timeline_id);
			goto done;
		} else {
			layer_info = &(
				session_info->session_layer_info[timeline_id]);
		}
	}

	if (layer_info == NULL || session_info == NULL) {
		DDPFENCE(
			"cant get sync info for session_id:0x%08x, timeline_id:%d\n",
			session_id, timeline_id);
		goto done;
	}

	if (layer_info->inited == 0) {
		DDPPR_ERR("layer_info[%d] not inited\n", timeline_id);
		goto done;
	}

done:
	mutex_unlock(&_disp_fence_mutex);
	return layer_info;
}

struct mtk_fence_info *mtk_fence_get_layer_info(unsigned int session,
						unsigned int timeline_id)
{
	return _disp_sync_get_sync_info(session, timeline_id);
}

/* ---------------------------------------------------------------------------
 */
/* local function declarations */
/* ---------------------------------------------------------------------------
 */
bool mtk_update_buf_info(unsigned int session_id, unsigned int layer_id,
			 unsigned int idx, unsigned int mva_offset,
			 unsigned int seq)
{
	struct mtk_fence_buf_info *buf;
	bool ret = false;
	struct mtk_fence_info *layer_info = NULL;

	layer_info = _disp_sync_get_sync_info(session_id, layer_id);

	if (layer_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return ret;
	}
	/* DDPINFO("U+/0x%08x/%d/%d/0x%08x\n", session_id, layer_id, idx,
	 * mva_offset);
	 */

	mutex_lock(&layer_info->sync_lock);
	list_for_each_entry(buf, &layer_info->buf_list, list) {
		if (buf->idx == idx) {
			buf->mva_offset = mva_offset;
			buf->seq = seq;
			ret = true;
			break;
		}
	}

	mutex_unlock(&layer_info->sync_lock);

	return ret;
}

unsigned int mtk_query_frm_seq_by_addr(unsigned int session_id,
				       unsigned int layer_id,
				       unsigned long phy_addr)
{
	struct mtk_fence_buf_info *buf;
	unsigned int frm_seq = 0x0;
	struct mtk_fence_session_sync_info *session_info;
	struct mtk_fence_info *layer_info;

	if (session_id <= 0)
		return 0;

	session_info = _get_session_sync_info(session_id);
	if (session_info == 0)
		return 0;
	layer_info = &(session_info->session_layer_info[layer_id]);

	if (layer_id != layer_info->layer_id) {
		MTK_FENCE_PR_ERR("wrong layer id %d(rt), %d(in)!\n",
				 layer_info->layer_id, layer_id);
		return 0;
	}

	mutex_lock(&layer_info->sync_lock);
	list_for_each_entry(buf, &layer_info->buf_list, list) {
		if (phy_addr > 0) {
			if ((buf->mva + buf->mva_offset) == phy_addr) {
				frm_seq = buf->seq;
				break;
			}
		} else { /* / get last buffer's seq */
			if (buf->seq < frm_seq)
				break;

			frm_seq = buf->seq;
		}
	}
	mutex_unlock(&layer_info->sync_lock);

	return frm_seq;
}

int mtk_fence_init(void)
{
	int i = 0;
	struct mtk_fence_session_sync_info *session_info = NULL;

	memset((void *)&_mtk_fence_context, 0, sizeof(_mtk_fence_context));
	/* for (i = 0; i < ARRAY_SIZE(_mtk_fence_context) /
	 * sizeof(_mtk_fence_context[0]); i++) {
	 */
	for (i = 0; i < ARRAY_SIZE(_mtk_fence_context); i++) { /* rogerhsu */
		session_info = &_mtk_fence_context[i];
		session_info->session_id = 0xffffffff;
	}

	return 0;
}

struct mtk_fence_buf_info *mtk_init_buf_info(struct mtk_fence_buf_info *buf)
{
	INIT_LIST_HEAD(&buf->list);
	buf->fence = MTK_INVALID_FENCE_FD;
	buf->buf_hnd = NULL;
	buf->idx = 0;
	buf->mva = 0;
	buf->layer_type = 0;

	return buf;
}

/**
 * Query a @mtk_fence_buf_info node from @info_pool_head, if empty create a new
 * one
 */
static struct mtk_fence_buf_info *mtk_get_buf_info(void)
{
	struct mtk_fence_buf_info *info;

	/* we must use another mutex for buffer list because it will be operated
	 * by ALL layer info.
	 */
	mutex_lock(&fence_buffer_mutex);
	if (!list_empty(&info_pool_head)) {
		info = list_first_entry(&info_pool_head,
					struct mtk_fence_buf_info, list);
		list_del_init(&info->list);
		mtk_init_buf_info(info);
	} else {
		info = kzalloc(sizeof(struct mtk_fence_buf_info), GFP_KERNEL);
		mtk_init_buf_info(info);
		MTK_FENCE_LOG("create new mtk_fence_buf_info node %p\n", info);
	}
	mutex_unlock(&fence_buffer_mutex);

	return info;
}

/**
 * signal fence and release buffer
 * layer: set layer
 * fence: signal fence which value is not bigger than this param
 */
void mtk_release_fence(unsigned int session_id, unsigned int layer_id,
		       int fence)
{
	struct mtk_fence_buf_info *buf;
	struct mtk_fence_buf_info *n;
	int num_fence = 0;
	int current_timeline_idx = 0;
	int ion_release_count = 0;
	struct mtk_fence_info *layer_info = NULL;
	struct mtk_fence_session_sync_info *session_info = NULL;

	session_info = _get_session_sync_info(session_id);
	layer_info = _disp_sync_get_sync_info(session_id, layer_id);

	if (layer_info == NULL) {
		DDPFENCE("%s:%d layer_info is null\n", __func__, __LINE__);
		return;
	}

	if (layer_info->timeline == NULL)
		return;

	mutex_lock(&layer_info->sync_lock);
	current_timeline_idx = layer_info->timeline_idx;
	num_fence = fence - layer_info->timeline_idx;
	if (num_fence > 0) {
		mtk_drm_trace_c("%d|layer_fence_release-%s-%d|%d",
			DRM_TRACE_FENCE_ID,
			mtk_fence_session_mode_spy(session_id),
			layer_id, fence);

		mtk_sync_timeline_inc(layer_info->timeline, num_fence, 0);
		layer_info->timeline_idx = fence;

		if (num_fence >= 2)
			DDPFENCE(
				"Warning, R/%s%d/L%d/timeline idx:%d/fence:%d\n",
				mtk_fence_session_mode_spy(session_id),
				MTK_SESSION_DEV(session_id), layer_id,
				current_timeline_idx, fence);

		mtk_drm_trace_c("%d|layer_fence_release-%s-%d|%d",
			DRM_TRACE_FENCE_ID,
			mtk_fence_session_mode_spy(session_id),
			layer_id, 0);
	} else {
		mutex_unlock(&layer_info->sync_lock);
		return;
	}


	list_for_each_entry_safe(buf, n, &layer_info->buf_list, list) {
		if (buf->idx > fence)
			continue;

		layer_info->fence_fd = buf->fence;

		if (buf->buf_hnd) {
			DDPFENCE("R+/%s%d/L%d/id%d/last%d/new%d/idx%d/hnd0x%8p\n",
				 mtk_fence_session_mode_spy(session_id),
				 MTK_SESSION_DEV(session_id), layer_id, fence,
				 current_timeline_idx, layer_info->fence_idx,
				 buf->idx, buf->buf_hnd);
		} else {
			DDPFENCE("R+/%s%d/L%d/id%d/last%d/new%d/idx%d\n",
				 mtk_fence_session_mode_spy(session_id),
				 MTK_SESSION_DEV(session_id), layer_id, fence,
				 current_timeline_idx, layer_info->fence_idx,
				 buf->idx);
		}

		list_del_init(&buf->list);

		if (buf->buf_hnd) {
			mtk_drm_gem_ion_free_handle(buf->buf_hnd,
					__func__, __LINE__);

			ion_release_count++;
		}

		/* we must use another mutex for buffer list*/
		/* because it will be operated by ALL layer info.*/

		mutex_lock(&fence_buffer_mutex);
		list_add_tail(&buf->list, &info_pool_head);
		mutex_unlock(&fence_buffer_mutex);
		buf->ts_period_keep = sched_clock() - buf->ts_create;
		/* DDPPR_ERR("buf->idx=%d,ts_create=%lld,
		 * ts_period_keep=%lld\n",
		 * buf->idx, buf->ts_create,
		 * buf->ts_period_keep);
		 */

		/* print mmp log for primary display */
		if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_PRIMARY)
			CRTC_MMP_MARK(0, release_fence, layer_id, buf->idx);
		if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_EXTERNAL)
			CRTC_MMP_MARK(1, release_fence, layer_id, buf->idx);
	}
	mutex_unlock(&layer_info->sync_lock);

	if (ion_release_count != num_fence)
		DDPFENCE("released %d fence but %d ion handle freed\n",
			  num_fence, ion_release_count);
}

void mtk_release_layer_fence(unsigned int session_id, unsigned int layer_id)
{
	struct mtk_fence_info *layer_info = NULL;
	int fence = 0;

	layer_info = _disp_sync_get_sync_info(session_id, layer_id);
	if (layer_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return;
	}

	mutex_lock(&layer_info->sync_lock);
	fence = layer_info->fence_idx;
	mutex_unlock(&layer_info->sync_lock);

	DDPFENCE("RL+/%s%d/L%d/id%d\n", mtk_fence_session_mode_spy(session_id),
		 MTK_SESSION_DEV(session_id), layer_id, fence);
	/* DDPINFO("layer%d release all fence %d\n", layer_id, fence); */
	mtk_release_fence(session_id, layer_id, fence);
}

int mtk_release_present_fence(unsigned int session_id, unsigned int fence_idx, ktime_t time)
{
	struct mtk_fence_info *layer_info = NULL;
	unsigned int timeline_id = 0;
	int fence_increment = 0;
	int idx;

	timeline_id = mtk_fence_get_present_timeline_id(session_id);
	layer_info = _disp_sync_get_sync_info(session_id, timeline_id);
	if (layer_info == NULL) {
		DDPFENCE("%s:%d layer_info is null\n", __func__, __LINE__);
		return -1;
	}

	mutex_lock(&layer_info->sync_lock);

	fence_increment = fence_idx - layer_info->timeline->value;

	if (fence_increment <= 0)
		goto done;

	if (fence_increment >= 2)
		DDPFENCE("Warning, R/%s%d/L%d/timeline idx:%d/fence:%d\n",
			 mtk_fence_session_mode_spy(session_id),
			 MTK_SESSION_DEV(session_id), timeline_id,
			 layer_info->timeline->value, fence_idx);

	mtk_drm_trace_begin("present_fence_rel:%s-%d",
		mtk_fence_session_mode_spy(session_id), fence_idx);
	/* MINDONE F2887: the present timestamp comes from mtk_crtc->pf_time, written
	 * ONLY by RDMA frame-done. An empty frame commit doesn't write it - the
	 * timestamp stalls, present signals with a stale time, HWC sees "2 same signal
	 * time" and resets its vsync estimate (resetAvgVSyncPeriod) => HWC misses every
	 * vsync => SF never wakes the producer => a loop (F2886, +93/s while stuck).
	 * Guarantee monotonicity: 0 or a repeat => use ktime_get(). */
	{
		static ktime_t mindone_last_pf_time;

		if (time == 0 || time == mindone_last_pf_time) {
			time = ktime_get();
			mindone_pf_time_fix++;
		}
		mindone_last_pf_time = time;
	}
	mtk_sync_timeline_inc(layer_info->timeline, fence_increment, time);
	DDPFENCE("RL+/%s%d/T%d/id%d\n",
		 mtk_fence_session_mode_spy(session_id),
		 MTK_SESSION_DEV(session_id), timeline_id, fence_idx);

	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_PRIMARY)
		idx = 0;
	else if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_EXTERNAL)
		idx = 1;
	else if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_MEMORY)
		idx = 2;
	else
		idx = 3;

	CRTC_MMP_MARK(idx, release_present_fence, 0, fence_idx);

	mtk_drm_trace_end();

done:
	mutex_unlock(&layer_info->sync_lock);
	return 0;
}

int mtk_release_sf_present_fence(unsigned int session_id,
				 unsigned int fence_idx)
{
	struct mtk_fence_info *layer_info = NULL;
	unsigned int timeline_id = 0;
	int fence_increment = 0;
	int idx;

	timeline_id = mtk_fence_get_sf_present_timeline_id(session_id);
	layer_info = _disp_sync_get_sync_info(session_id, timeline_id);
	if (layer_info == NULL) {
		DDPFENCE("%s:%d layer_info is null\n", __func__, __LINE__);
		return -1;
	}

	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_PRIMARY)
		idx = 0;
	else if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_EXTERNAL)
		idx = 1;
	else if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_MEMORY)
		idx = 2;
	else
		idx = 3;

	mutex_lock(&layer_info->sync_lock);

	fence_increment = fence_idx - layer_info->timeline->value;

	if (fence_increment <= 0) {
		CRTC_MMP_MARK(idx, warn_sf_pf_0, fence_idx, fence_increment);
		goto done;
	}

	if (fence_increment >= 2) {
		CRTC_MMP_MARK(idx, warn_sf_pf_2, fence_idx, fence_increment);
		DDPFENCE("Warning, R/%s%d/L%d/timeline idx:%d/fence:%d\n",
			 mtk_fence_session_mode_spy(session_id),
			 MTK_SESSION_DEV(session_id), timeline_id,
			 layer_info->timeline->value, fence_idx);
	}

	mtk_drm_trace_begin("sf_present_fence_rel:%s-%d",
		mtk_fence_session_mode_spy(session_id), fence_idx);

	mtk_sync_timeline_inc(layer_info->timeline, fence_increment, 0);
	DDPFENCE("RL+/%s%d/T%d/id%d\n",
		 mtk_fence_session_mode_spy(session_id),
		 MTK_SESSION_DEV(session_id), timeline_id, fence_idx);

	CRTC_MMP_MARK(idx, release_sf_present_fence, 0, fence_idx);

	mtk_drm_trace_end();

done:
	mutex_unlock(&layer_info->sync_lock);
	return 0;
}

void mtk_release_session_fence(unsigned int session_id)
{
	struct mtk_fence_session_sync_info *session_sync_info = NULL;
	int i;

	session_sync_info = _get_session_sync_info(session_id);
	if (session_sync_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return;
	}
	for (i = 0; i < ARRAY_SIZE(session_sync_info->session_layer_info); i++)
		mtk_release_layer_fence(session_id, i);
}

int mtk_fence_get_ovl_timeline_id(int layer_id)
{
	return MTK_PLANE_OVL_TIMELINE_ID(layer_id);
}

int mtk_fence_get_present_timeline_id(unsigned int session_id)
{
	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_PRIMARY)
		return MTK_TIMELINE_PRIMARY_PRESENT_TIMELINE_ID;
	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_EXTERNAL)
		return MTK_TIMELINE_SECONDARY_PRESENT_TIMELINE_ID;
	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_SP)
		return MTK_TIMELINE_SP_PRESENT_TIMELINE_ID;

	DDPFENCE("session id is wrong, session=0x%x!!\n", session_id);
	return -1;
}

int mtk_fence_get_sf_present_timeline_id(unsigned int session_id)
{
	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_PRIMARY)
		return MTK_TIMELINE_SF_PRIMARY_PRESENT_TIMELINE_ID;
	if (MTK_SESSION_TYPE(session_id) == MTK_SESSION_EXTERNAL)
		return MTK_TIMELINE_SECONDARY_PRESENT_TIMELINE_ID;

	DDPFENCE("session id is wrong, session=0x%x!!\n", session_id);
	return -1;
}

int mtk_fence_get_output_timeline_id(void)
{
	return MTK_TIMELINE_OUTPUT_TIMELINE_ID;
}

int mtk_fence_get_interface_timeline_id(void)
{
	return MTK_TIMELINE_OUTPUT_INTERFACE_TIMELINE_ID;
}

int mtk_fence_get_cached_layer_info(unsigned int session_id,
				    unsigned int timeline_idx,
				    unsigned int *layer_en, unsigned long *addr,
				    unsigned int *fence_idx)
{
	int ret = -1;
	struct mtk_fence_info *layer_info = NULL;

	layer_info = _disp_sync_get_sync_info(session_id, timeline_idx);

	if (layer_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return 0;
	}

	mutex_lock(&(layer_info->sync_lock));

	if (layer_en && addr && fence_idx) {
		*layer_en = layer_info->cached_config.layer_en;
		*addr = layer_info->cached_config.addr;
		*fence_idx = layer_info->cached_config.buff_idx;
		ret = 0;
		goto done;
	} else {
		ret = -1;
		goto done;
	}

done:
	mutex_unlock(&(layer_info->sync_lock));

	return ret;
}

int mtk_fence_put_cached_layer_info(unsigned int session_id,
				    unsigned int timeline_idx,
				    struct mtk_plane_input_config *src,
				    unsigned long mva)
{
	int ret = -1;
	struct mtk_fence_info *layer_info = NULL;

	layer_info = _disp_sync_get_sync_info(session_id, timeline_idx);

	if (layer_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return -1;
	}

	mutex_lock(&(layer_info->sync_lock));

	if (src) {
		mtk_fence_convert_input_to_fence_layer_info(
			src, &(layer_info->cached_config), mva);

		ret = 0;
		goto done;
	} else {
		ret = -1;
		goto done;
	}

done:
	mutex_unlock(&(layer_info->sync_lock));

	return ret;
}

int mtk_fence_convert_input_to_fence_layer_info(
	struct mtk_plane_input_config *src, struct FENCE_LAYER_INFO *dst,
	unsigned long dst_mva)
{
	if (src && dst) {
		dst->layer = src->layer_id;
		dst->addr = dst_mva;
		/* no fence */
		if (src->next_buff_idx == 0) {
			dst->layer_en = 0;
		} else {
			dst->layer_en = src->layer_enable;
			dst->buff_idx = src->next_buff_idx;
		}

		return 0;
	} else {
		return -1;
	}
}

/**
 * 1. query a @mtk_fence_buf_info list node
 * 2. create fence object
 * 3. save fence fd, mva to @mtk_fence_buf_info node
 * 4. add @mtk_fence_buf_info node to @mtk_fence_sync_info.buf_list
 * @buf struct @fb_overlay_buffer
 * @return struct @mtk_fence_buf_info
 */
/* MINDONE F2899: a layer fence is created AHEAD (++fence_idx) on MTK_GEM_SUBMIT and
 * signals only once the frame with that index reaches the callback. HWC prepares
 * buffers ~20ms AFTER the last empty packet and sends nothing more (F2899 window:
 * S+ up to 1095, release up to 1100, then P+ 1101..1103 and silence) - those fds
 * hang forever in Fence::waitForever. Can't fix it in the callback by construction.
 * Kernel-side timeout instead: every prepare_buf restarts the layer's delayed work;
 * if the timeline hasn't reached fence_idx after 500ms, force-release it (like
 * sw_sync timeout). On the normal path release already happened, so it's a no-op. */
unsigned long mindone_fence_timeout_fired;
module_param(mindone_fence_timeout_fired, ulong, 0444);
MODULE_PARM_DESC(mindone_fence_timeout_fired, "layer fences force-released by kernel timeout (F2899)");
unsigned int mindone_fence_timeout_ms = 500;
module_param(mindone_fence_timeout_ms, uint, 0644);
MODULE_PARM_DESC(mindone_fence_timeout_ms, "layer fence timeout in ms, 0 = disabled");

struct mindone_fence_wd {
	struct delayed_work work;
	unsigned int session_id;
	unsigned int layer_id;
	bool inited;
};
static struct mindone_fence_wd mindone_fence_wd_tbl[MTK_TIMELINE_COUNT];
/* MINDONE F2909: timers only fire after an EMPTY commit (LYE_IDX=0) - normal
 * (idle, interactive) traffic never has one, but the hang is always preceded by
 * one. Without this gate the timer released pipeline buffers before they were
 * shown => black flashes. */
unsigned long mindone_empty_commit_jiffies;
unsigned int mindone_wd_window_ms = 3000;
module_param(mindone_wd_window_ms, uint, 0644);
MODULE_PARM_DESC(mindone_wd_window_ms, "fence timeouts armed only within this window after an empty commit");
unsigned long mindone_wd_gated;
module_param(mindone_wd_gated, ulong, 0444);
MODULE_PARM_DESC(mindone_wd_gated, "fence timeout firings suppressed by the empty-commit gate");

static inline bool mindone_wd_armed(void)
{
	return mindone_empty_commit_jiffies &&
	       time_before(jiffies, mindone_empty_commit_jiffies +
				    msecs_to_jiffies(mindone_wd_window_ms));
}

static void mindone_fence_wd_fn(struct work_struct *w)
{
	struct mindone_fence_wd *wd = container_of(to_delayed_work(w),
						    struct mindone_fence_wd, work);
	struct mtk_fence_info *li = _disp_sync_get_sync_info(wd->session_id, wd->layer_id);
	unsigned int target, cur;

	if (!li || !li->timeline)
		return;
	if (!mindone_wd_armed()) {
		mindone_wd_gated++;
		return;
	}
	mutex_lock(&li->sync_lock);
	target = li->fence_idx;
	cur = li->timeline->value;
	mutex_unlock(&li->sync_lock);
	if (cur < target) {
		/* F2911: the pathology outlasts the window - extend it while we still find stuck ones */
		mindone_empty_commit_jiffies = jiffies;
		mindone_fence_timeout_fired++;
		if ((mindone_fence_timeout_fired & 0xf) == 1)
			pr_notice("MINDONE-FENCEWD: L%u timeline %u < fence_idx %u — force release (n=%lu)\n",
				  wd->layer_id, cur, target, mindone_fence_timeout_fired);
		mtk_release_fence(wd->session_id, wd->layer_id, target);
	}
}

static void mindone_fence_wd_kick(unsigned int session_id, unsigned int layer_id);
void mindone_fence_wd_kick_all(unsigned int session_id)
{
	unsigned int i;

	mindone_empty_commit_jiffies = jiffies;
	for (i = 0; i < MTK_TIMELINE_OUTPUT_TIMELINE_ID; i++)
		mindone_fence_wd_kick(session_id, i);
}

static void mindone_fence_wd_kick(unsigned int session_id, unsigned int layer_id)
{
	struct mindone_fence_wd *wd;

	if (!mindone_fence_timeout_ms || layer_id >= MTK_TIMELINE_COUNT)
		return;
	wd = &mindone_fence_wd_tbl[layer_id];
	if (!wd->inited) {
		INIT_DELAYED_WORK(&wd->work, mindone_fence_wd_fn);
		wd->inited = true;
	}
	wd->session_id = session_id;
	wd->layer_id = layer_id;
	mod_delayed_work(system_wq, &wd->work, msecs_to_jiffies(mindone_fence_timeout_ms));
}

struct mtk_fence_buf_info *mtk_fence_prepare_buf(struct drm_device *dev,
						 struct drm_mtk_gem_submit *buf)
{
	int ret = 0;
	unsigned int session_id = 0;
	unsigned int timeline_id = 0;
	struct mtk_fence_buf_info *buf_info = NULL;
	struct fence_data data;
	struct mtk_fence_info *layer_info = NULL;
	struct mtk_fence_session_sync_info *session_info = NULL;
	struct dma_fence *fence = NULL;

	if (buf == NULL) {
		DDPPR_ERR("Prepare Buffer, buf is NULL!!\n");
		return NULL;
	}

	session_id = buf->session_id;
	timeline_id = buf->layer_id;
	session_info = _get_session_sync_info(session_id);
	layer_info = _disp_sync_get_sync_info(session_id, timeline_id);

	if (layer_info == NULL) {
		DDPPR_ERR("%s:%d layer_info is null\n", __func__, __LINE__);
		return NULL;
	}

	if (layer_info->inited == 0) {
		DDPPR_ERR(
			"FATAL ERROR, sync info not inited, session_id=0x%08x|layer_id=%d\n",
			session_id, timeline_id);
		return NULL;
	}

	buf_info = mtk_get_buf_info();
	mutex_lock(&layer_info->sync_lock);
	data.fence = MTK_INVALID_FENCE_FD;
	data.value = ++(layer_info->fence_idx);
	mutex_unlock(&(layer_info->sync_lock));

	ret = mtk_sync_fence_create(layer_info->timeline, &data);
	mindone_fence_wd_kick(session_id, timeline_id);
	if (ret != 0) {
		/* Does this really happened? */
		DDPPR_ERR("%s%d,layer%d create Fence Object failed ret=%d!\n",
			  mtk_fence_session_mode_spy(session_id),
			  MTK_SESSION_DEV(session_id), timeline_id, ret);
	}
	buf_info->fence = data.fence;
	buf_info->idx = data.value;

	if (buf->ion_fd >= 0)
		buf_info->buf_hnd = mtk_drm_gem_ion_import_handle(buf->ion_fd);
	if (buf_info->buf_hnd == NULL) {
		DDPPR_ERR("import buf handle fail\n");
		return NULL;
	}

	buf_info->mva_offset = 0;
	buf_info->trigger_ticket = 0;
	buf_info->buf_state = create;
	mutex_lock(&layer_info->sync_lock);
	list_add_tail(&buf_info->list, &layer_info->buf_list);
	mutex_unlock(&layer_info->sync_lock);

	fence = sync_file_get_fence(buf_info->fence);
	if (buf_info->buf_hnd)
		DDPFENCE("P+/%s%d/L%d/id%d/fd%d/hnd0x%8p/pt0x%lx\n",
			 mtk_fence_session_mode_spy(session_id),
			 MTK_SESSION_DEV(session_id), timeline_id, buf_info->idx,
			 buf_info->fence, buf_info->buf_hnd,
			 IS_ERR_OR_NULL(fence) ? 0x0 : (unsigned long)fence);
	else
		DDPFENCE("P+/%s%d/L%d/id%d/fd%d/pt0x%lx\n",
			 mtk_fence_session_mode_spy(session_id),
			 MTK_SESSION_DEV(session_id), timeline_id, buf_info->idx,
			 buf_info->fence,
			 IS_ERR_OR_NULL(fence) ? 0x0 : (unsigned long)fence);
	if (!IS_ERR_OR_NULL(fence))
		dma_fence_put(fence);

	return buf_info;
}

unsigned int mtk_fence_query_buf_info(unsigned int session_id,
				      unsigned int timeline_id,
				      unsigned int idx, unsigned long *mva,
				      unsigned int *size, void **va,
				      int need_sync)
{
	struct mtk_fence_buf_info *buf = NULL;
	unsigned long dst_mva = 0;
	uint32_t dst_size = 0;
	struct mtk_fence_info *layer_info = NULL;

	layer_info = _disp_sync_get_sync_info(session_id, timeline_id);
	if (layer_info == NULL || mva == NULL || size == NULL) {
		DDPPR_ERR(
			"layer_info is null, layer_info=%p, mva=%p, size=%p\n",
			layer_info, mva, size);
		return 0;
	}

	mutex_lock(&layer_info->sync_lock);
	list_for_each_entry(buf, &layer_info->buf_list, list) {
		if (buf->idx == idx) {
			/* use local variable here to avoid polluted pointer */
			dst_mva = buf->mva;
			dst_size = buf->size;

			break;
		}
	}
	mutex_unlock(&layer_info->sync_lock);

	if (dst_mva != 0x0) {
		*mva = dst_mva;
		*size = dst_size;
		buf->ts_create = sched_clock();

		MTK_FENCE_LOG("query buf mva: layer=%d, idx=%d, mva=0x%lx\n",
			      timeline_id, idx, buf->mva);
	} else {
		/* FIXME: non-ion buffer need cache sync here? */
		DDPPR_ERR("cannot find this buf, session:%s%d, layer=%d,\n",
			  mtk_fence_session_mode_spy(session_id),
			  MTK_SESSION_DEV(session_id), timeline_id);
		DDPPR_ERR(
			"idx=%d, fence_idx=%d, timeline_idx=%d, cur_idx=%d!\n",
			idx, layer_info->fence_idx, layer_info->timeline_idx,
			layer_info->cur_idx);
	}

	/* DDPPR_ERR("mva query:session_id=0x%08x, layer_id=%d, mva=0x%08x\n",
	 * session_id, layer_id, mva);
	 */

	return 0;
}
