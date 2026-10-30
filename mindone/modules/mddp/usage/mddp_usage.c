/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/types.h>
#include <linux/mutex.h>

#include "mddp_ctrl.h"
#include "mddp_debug.h"
#include "mddp_dev.h"
#include "mddp_filter.h"
#include "mddp_ipc.h"
#include "mddp_usage.h"
#include "mtk_ccci_common.h"

static uint32_t mddp_u_iq_trans_id_s;
#define MDDP_U_WAN_COUNT 32

struct mddp_u_quota_state {
	uint32_t trans_id;
	bool valid;
	bool warning;
	bool warned;
	bool limited;
};

static DEFINE_MUTEX(mddp_u_lock);
static struct mddp_u_quota_state quota[MDDP_U_WAN_COUNT];
static int8_t wan_id = -1;

#define MDDP_U_GET_IQ_TRANS_ID()        (mddp_u_iq_trans_id_s++)

void mddp_u_get_data_stats(void *buf, uint32_t *buf_len)
{
	static struct mddp_u_data_stats_t       cur_stats[MDDP_U_WAN_COUNT];
	struct mddp_u_data_stats_t             *md_stats;
	struct mddp_u_data_stats_t             *usage;
	int                                     sm_len = 0;

	mutex_lock(&mddp_u_lock);
	*buf_len = 0;
	md_stats = get_smem_start_addr(MD_SYS1, SMEM_USER_RAW_NETD, &sm_len);
	if (md_stats && wan_id >= 0 &&
	    wan_id < MDDP_U_WAN_COUNT && sm_len > 0) {
		if ((size_t)wan_id < sm_len / sizeof(*md_stats)) {
			md_stats += wan_id;
			usage = (struct mddp_u_data_stats_t *)buf;
			*buf_len = sizeof(struct mddp_u_data_stats_t);
			#define DIFF_FROM_SMEM(x) (usage->x = \
			(md_stats->x > cur_stats[wan_id].x) ? (md_stats->x - cur_stats[wan_id].x) : 0)
			DIFF_FROM_SMEM(total_tx_pkts);
			DIFF_FROM_SMEM(total_tx_bytes);
			DIFF_FROM_SMEM(tx_tcp_bytes);
			DIFF_FROM_SMEM(tx_tcp_pkts);
			DIFF_FROM_SMEM(tx_udp_bytes);
			DIFF_FROM_SMEM(tx_udp_pkts);
			DIFF_FROM_SMEM(tx_others_bytes);
			DIFF_FROM_SMEM(tx_others_pkts);
			DIFF_FROM_SMEM(total_rx_pkts);
			DIFF_FROM_SMEM(total_rx_bytes);
			DIFF_FROM_SMEM(rx_tcp_bytes);
			DIFF_FROM_SMEM(rx_tcp_pkts);
			DIFF_FROM_SMEM(rx_udp_bytes);
			DIFF_FROM_SMEM(rx_udp_pkts);
			DIFF_FROM_SMEM(rx_others_bytes);
			DIFF_FROM_SMEM(rx_others_pkts);

			memcpy(&cur_stats[wan_id], md_stats, *buf_len);
		} else {
			MDDP_U_LOG(MDDP_LL_ERR,
					"%s: Failed to copy data stats, sm_len(%d), buf_len(%d)!\n",
					__func__, sm_len, *buf_len);
		}
	}
	mutex_unlock(&mddp_u_lock);
}

int32_t mddp_u_set_data_limit(uint8_t *buf, uint32_t buf_len)
{
	uint32_t                                md_status;
	struct mddp_md_msg_t                   *md_msg;
	struct mddp_dev_req_set_data_limit_t   *in_req;
	struct mddp_u_data_limit_t              limit;
	int8_t                                  id;
	struct mddp_app_t *app;
	int32_t ret;

	if (buf_len != sizeof(struct mddp_dev_req_set_data_limit_t)) {
		MDDP_U_LOG(MDDP_LL_ERR,
				"%s: Invalid parameter, buf_len(%d)!\n",
				__func__, buf_len);
		return -EINVAL;
	}

	md_status = exec_ccci_kern_func_by_md_id(MD_SYS1, ID_GET_MD_STATE, NULL, 0);

	if (md_status != MD_STATE_READY) {
		MDDP_U_LOG(MDDP_LL_NOTICE,
				"%s: Invalid state, md_status(%d)!\n",
				__func__, md_status);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t),
			GFP_ATOMIC);
	if (unlikely(!md_msg)) {
		return -EAGAIN;
	}

	in_req = (struct mddp_dev_req_set_data_limit_t *)buf;
	id = mddp_f_data_usage_wan_dev_name_to_id(in_req->ul_dev_name);
	if (unlikely(id < 0 || id >= MDDP_U_WAN_COUNT)) {
		MDDP_U_LOG(MDDP_LL_ERR,
				"%s: Invalid dev_name, dev_name(%s)!\n",
				__func__, in_req->ul_dev_name);
		kfree(md_msg);
		return -EINVAL;
	}

	mutex_lock(&mddp_u_lock);
	if (id != wan_id) {
		mutex_unlock(&mddp_u_lock);
		kfree(md_msg);
		return -EINVAL;
	}

	memset(&limit, 0, sizeof(limit));
	limit.cmd = MSG_ID_DPFM_SET_IQUOTA_REQ;
	limit.trans_id = MDDP_U_GET_IQ_TRANS_ID();
	limit.limit_buffer_size = in_req->limit_size;
	limit.id = id;
	MDDP_U_LOG(MDDP_LL_NOTICE,
			"%s: Send cmd(%d)/id(%d)/name(%s) limit(%llx) to MD.\n",
			__func__, limit.cmd, limit.id, in_req->ul_dev_name,
			limit.limit_buffer_size);

	md_msg->msg_id = IPC_MSG_ID_DPFM_DATA_USAGE_CMD;
	md_msg->data_len = sizeof(limit);
	memcpy(&md_msg->data, &limit, sizeof(limit));
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	ret = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_DPFM);
	if (!ret) {
		mddp_dev_purge_quota_notifications();
		quota[id] = (struct mddp_u_quota_state) {
			.trans_id = limit.trans_id,
			.valid = true,
			.warning = false,
		};
	}
	mutex_unlock(&mddp_u_lock);

	return ret;
}

int32_t mddp_u_set_warning_and_data_limit(uint8_t *buf, uint32_t buf_len)
{
	uint32_t                                md_status;
	struct mddp_md_msg_t                   *md_msg;
	struct mddp_dev_req_set_warning_and_data_limit_t   *in_req;
	struct mddp_u_warning_and_data_limit_t  limit;
	int8_t                                  id;
	struct mddp_app_t *app;
	int32_t ret;

	if (buf_len != sizeof(struct mddp_dev_req_set_warning_and_data_limit_t)) {
		MDDP_U_LOG(MDDP_LL_ERR,
				"%s: Invalid parameter, buf_len(%d)!\n",
				__func__, buf_len);
		return -EINVAL;
	}

	md_status = exec_ccci_kern_func_by_md_id(MD_SYS1, ID_GET_MD_STATE, NULL, 0);

	if (md_status != MD_STATE_READY) {
		MDDP_U_LOG(MDDP_LL_NOTICE,
				"%s: Invalid state, md_status(%d)!\n",
				__func__, md_status);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t),
			GFP_ATOMIC);
	if (unlikely(!md_msg))
		return -EAGAIN;

	in_req = (struct mddp_dev_req_set_warning_and_data_limit_t *)buf;
	id = mddp_f_data_usage_wan_dev_name_to_id(in_req->ul_dev_name);
	if (unlikely(id < 0 || id >= MDDP_U_WAN_COUNT)) {
		MDDP_U_LOG(MDDP_LL_ERR,
				"%s: Invalid dev_name, dev_name(%s)!\n",
				__func__, in_req->ul_dev_name);
		kfree(md_msg);
		return -EINVAL;
	}

	mutex_lock(&mddp_u_lock);
	if (id != wan_id) {
		mutex_unlock(&mddp_u_lock);
		kfree(md_msg);
		return -EINVAL;
	}

	memset(&limit, 0, sizeof(limit));
	limit.cmd = MSG_ID_DPFM_SET_IQUOTA_V2_REQ;
	limit.trans_id = MDDP_U_GET_IQ_TRANS_ID();
	limit.limit_buffer_size = in_req->limit_size;
	limit.warning_buffer_size = in_req->warning_size;
	limit.id = id;
	MDDP_U_LOG(MDDP_LL_NOTICE,
			"%s: Send cmd(%d)/id(%d)/name(%s) limit(%lld) warning(%lld) to MD.\n",
			__func__, limit.cmd, limit.id, in_req->ul_dev_name,
			limit.limit_buffer_size, limit.warning_buffer_size);

	md_msg->msg_id = IPC_MSG_ID_DPFM_DATA_USAGE_CMD;
	md_msg->data_len = sizeof(limit);
	memcpy(&md_msg->data, &limit, sizeof(limit));
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	ret = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_DPFM);
	if (!ret) {
		mddp_dev_purge_quota_notifications();
		quota[id] = (struct mddp_u_quota_state) {
			.trans_id = limit.trans_id,
			.valid = true,
			.warning = true,
		};
	}
	mutex_unlock(&mddp_u_lock);

	return ret;
}

int32_t mddp_u_msg_hdlr(uint32_t msg_id, void *buf, uint32_t buf_len)
{
	struct mddp_u_iquota_ind_t ind;
	struct mddp_u_quota_state *quota_state;
	uint8_t id;
	int32_t ret = -EINVAL;

	if (msg_id != IPC_MSG_ID_DPFM_DATA_USAGE_CMD ||
	    !buf || buf_len != sizeof(ind))
		return -EINVAL;
	memcpy(&ind, buf, sizeof(ind));
	id = ind.iq.dpfm_id;
	if (id >= MDDP_U_WAN_COUNT || ind.iq.status != 0)
		return -EINVAL;

	mutex_lock(&mddp_u_lock);
	quota_state = &quota[id];
	if (id != wan_id || !quota_state->valid ||
	    ind.iq.trans_id != quota_state->trans_id) {
		ret = -ESTALE;
		goto out;
	}

	switch (ind.cmd) {
	case MSG_ID_DPFM_ALERT_IQUOTA_IND:
		if (quota_state->limited) {
			ret = -EALREADY;
			break;
		}
		quota_state->limited = true;
		mddp_dev_response(MDDP_APP_TYPE_ALL,
			MDDP_CMCMD_LIMIT_IND, true, NULL, 0);
		ret = 0;
		break;
	case MSG_ID_DPFM_ALERT_IQUOTA_WARNING_IND:
		if (!quota_state->warning || quota_state->warned || quota_state->limited) {
			ret = -EALREADY;
			break;
		}
		quota_state->warned = true;
		mddp_dev_response(MDDP_APP_TYPE_ALL,
			MDDP_CMCMD_WARNING_IND, true, NULL, 0);
		ret = 0;
		break;
	default:
		break;
	}

out:
	mutex_unlock(&mddp_u_lock);
	return ret;
}

void mddp_u_clear_quota(void)
{
	mutex_lock(&mddp_u_lock);
	memset(quota, 0, sizeof(quota));
	mddp_dev_purge_quota_notifications();
	mutex_unlock(&mddp_u_lock);
}

void mddp_u_set_wan_iface(uint8_t *devname)
{
	int8_t id = mddp_f_data_usage_wan_dev_name_to_id(devname);

	mutex_lock(&mddp_u_lock);
	if (id != wan_id) {
		memset(quota, 0, sizeof(quota));
		mddp_dev_purge_quota_notifications();
	}
	wan_id = id;
	mutex_unlock(&mddp_u_lock);
}
