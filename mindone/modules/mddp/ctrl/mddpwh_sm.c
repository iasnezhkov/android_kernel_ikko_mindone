/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/slab.h>
#include <linux/delay.h>

#include "mddp_ctrl.h"
#include "mddp_debug.h"
#include "mddp_dev.h"
#include "mddp_filter.h"
#include "mddp_sm.h"
#include "mddp_usage.h"

#define MDDP_RESET_READY_TIME_MS (100)
static struct workqueue_struct *mddp_workqueue;
static struct work_struct wfpm_reset_work;
static struct work_struct mddp_hook_work;
static struct work_struct mddp_unhook_work;

bool is_smem_layout_config;

static struct mddp_md_cfg_t mddpw_md_cfg_s = {
	MDFPM_AP_USER_ID,
	MDFPM_USER_ID_WFPM,
};

static void mddpwh_notify_state(struct mddp_app_t *app, enum mddp_state_e state)
{
	mutex_lock(&app->callback_lock);
	if (app->callbacks_ready && app->drv_hdlr.change_state)
		app->drv_hdlr.change_state(state, NULL, NULL);
	mutex_unlock(&app->callback_lock);
}

static void mddpwh_notify_info(struct mddp_app_t *app,
		struct mddpw_md_notify_info_t *info)
{
	mutex_lock(&app->callback_lock);
	if (app->callbacks_ready && app->drv_hdlr.wifi_handle &&
	    app->drv_hdlr.wifi_handle->notify_md_info)
		app->drv_hdlr.wifi_handle->notify_md_info(info);
	mutex_unlock(&app->callback_lock);
}

static void mddpwh_sm_enable(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                   *md_msg;
	struct wfpm_enable_md_func_req_t       *enable_req;
	struct wfpm_smem_info_t                *smem_info;
	uint32_t                                smem_num;

	mddpwh_notify_state(app, MDDP_STATE_ENABLING);

	if (wfpm_ipc_get_smem_list((void **)&smem_info, &smem_num)) {
		MDDP_S_LOG(MDDP_LL_NOTICE,
		"%s: Failed to get smem info!\n", __func__);
		smem_num = 0;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		app->ipc_send_status = -ENOMEM;
		return;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_ENABLE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_enable_md_func_req_t) +
				sizeof(struct wfpm_smem_info_t) * smem_num;
	enable_req = (struct wfpm_enable_md_func_req_t *)&md_msg->data;
	enable_req->mode = WFPM_FUNC_MODE_TETHER;
	enable_req->version = __MDDP_VERSION__;
	enable_req->smem_num = smem_num;

	memcpy(&enable_req->smem_info, smem_info,
			sizeof(struct wfpm_smem_info_t) * smem_num);
	app->ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

static void mddpwh_sm_rsp_enable_ok(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_enable_t            enable = {0};

	mddpwh_notify_state(app, app->state);

	mddp_dev_response(app->type, MDDP_CMCMD_ENABLE_RSP,
			true, (uint8_t *)&enable, sizeof(enable));
}

static void mddpwh_sm_rsp_enable_fail(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_enable_t    enable = {0};

	mddpwh_notify_state(app, app->state);

	mddp_dev_response(app->type, MDDP_CMCMD_ENABLE_RSP,
			false, (uint8_t *)&enable, sizeof(enable));
}

static void mddpwh_sm_disable(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                   *md_msg;
	struct wfpm_md_fast_path_common_req_t  *disable_req;

	mddpwh_notify_state(app, MDDP_STATE_DISABLING);

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);
	if (unlikely(!md_msg)) {
		app->ipc_send_status = -ENOMEM;
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to alloc md_msg bug!\n", __func__);
		return;
	}

	disable_req = (struct wfpm_md_fast_path_common_req_t *)&md_msg->data;
	disable_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_DISABLE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_md_fast_path_common_req_t);
	app->ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

static void mddpwh_sm_rsp_disable(struct mddp_app_t *app)
{

	mddpwh_notify_state(app, app->state);

}

static void mddpwh_sm_act(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                 *md_msg;
	struct wfpm_activate_md_func_req_t   *act_req;

	mddpwh_notify_state(app, MDDP_STATE_ACTIVATING);

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		app->ipc_send_status = -ENOMEM;
		return;
	}

	act_req = (struct wfpm_activate_md_func_req_t *)&md_msg->data;
	act_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_ACTIVATE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_activate_md_func_req_t);
	app->ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

static void mddpwh_sm_rsp_act_ok(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_act_t       act = {0};

	mddpwh_notify_state(app, app->state);

	mddp_dev_response(app->type, MDDP_CMCMD_ACT_RSP,
			true, (uint8_t *)&act, sizeof(act));
	queue_work(mddp_workqueue, &mddp_hook_work);
}

static void mddpwh_sm_rsp_act_fail(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_act_t       act = {0};

	queue_work(mddp_workqueue, &mddp_unhook_work);

	mddpwh_notify_state(app, app->state);

	mddp_dev_response(app->type, MDDP_CMCMD_ACT_RSP,
			false, (uint8_t *)&act, sizeof(act));
}

static void mddpwh_sm_deact(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                 *md_msg;
	struct wfpm_activate_md_func_req_t   *deact_req;

	mddpwh_notify_state(app, MDDP_STATE_DEACTIVATING);

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		app->ipc_send_status = -ENOMEM;
		return;
	}

	deact_req = (struct wfpm_activate_md_func_req_t *)&md_msg->data;
	deact_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_DEACTIVATE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_activate_md_func_req_t);
	app->ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

static void mddpwh_sm_rsp_deact_result(struct mddp_app_t *app, bool success)
{
	struct mddp_dev_rsp_deact_t     deact = {0};

	queue_work(mddp_workqueue, &mddp_unhook_work);

	mddpwh_notify_state(app, app->state);

	mddp_dev_response(app->type, MDDP_CMCMD_DEACT_RSP,
			success, (uint8_t *)&deact, sizeof(deact));
}

static void mddpwh_sm_rsp_deact(struct mddp_app_t *app)
{
	mddpwh_sm_rsp_deact_result(app, true);
}

static void mddpwh_sm_rsp_deact_fail(struct mddp_app_t *app)
{
	mddpwh_sm_rsp_deact_result(app, false);
}

static void mddpwh_sm_md_reset(struct mddp_app_t *app)
{
	WRITE_ONCE(app->reset_seq, app->reset_seq + 1);
	app->reset_pending = true;
	cancel_delayed_work(&app->timeout_work);
	app->sync_lost = false;
	app->transaction_status = -ENETRESET;
	complete_all(&app->md_resp_comp);
	complete_all(&app->work_comp);
	mddp_u_clear_quota();
	queue_work(mddp_workqueue, &wfpm_reset_work);
}

static struct mddp_sm_entry_t mddpwh_uninit_state_machine_s[] = {

{MDDP_EVT_MD_RESET,       MDDP_STATE_DISABLED,     mddpwh_sm_md_reset},
{MDDP_EVT_DUMMY,          MDDP_STATE_UNINIT,       NULL}
};

static struct mddp_sm_entry_t mddpwh_disabled_state_machine_s[] = {

{MDDP_EVT_MD_RESET,       MDDP_STATE_DISABLED,     mddpwh_sm_md_reset},
{MDDP_EVT_FUNC_ENABLE,    MDDP_STATE_ENABLING,     mddpwh_sm_enable},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_DISABLED,     NULL},
{MDDP_EVT_DUMMY,          MDDP_STATE_DISABLED,     NULL}
};

static struct mddp_sm_entry_t mddpwh_enabling_state_machine_s[] = {

{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING, mddpwh_sm_disable},

{MDDP_EVT_MD_RESET,       MDDP_STATE_DISABLED,     mddpwh_sm_md_reset},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_enable_ok},
{MDDP_EVT_MD_RSP_TIMEOUT, MDDP_STATE_DISABLED,     mddpwh_sm_rsp_enable_fail},
{MDDP_EVT_DUMMY,          MDDP_STATE_ENABLING,     NULL}
};

static struct mddp_sm_entry_t mddpwh_disabling_state_machine_s[] = {

{MDDP_EVT_MD_RESET,       MDDP_STATE_DISABLED,     mddpwh_sm_md_reset},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_DISABLED,     mddpwh_sm_rsp_disable},
{MDDP_EVT_MD_RSP_TIMEOUT, MDDP_STATE_DISABLED,     mddpwh_sm_rsp_disable},
{MDDP_EVT_DUMMY,          MDDP_STATE_DISABLING,    NULL}
};

static struct mddp_sm_entry_t mddpwh_deactivated_state_machine_s[] = {

{MDDP_EVT_MD_RESET,       MDDP_STATE_DEACTIVATED,  mddpwh_sm_md_reset},
{MDDP_EVT_FUNC_ENABLE,    MDDP_STATE_ENABLING,     mddpwh_sm_enable},
{MDDP_EVT_FUNC_DISABLE,   MDDP_STATE_DISABLING,    mddpwh_sm_disable},
{MDDP_EVT_FUNC_ACT,       MDDP_STATE_ACTIVATING,   mddpwh_sm_act},
{MDDP_EVT_FUNC_DEACT,     MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_deact},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_DEACTIVATED,  NULL},
{MDDP_EVT_DUMMY,          MDDP_STATE_DEACTIVATED,  NULL}
};

static struct mddp_sm_entry_t mddpwh_activating_state_machine_s[] = {

{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING, mddpwh_sm_disable},

{MDDP_EVT_MD_RESET,       MDDP_STATE_DEACTIVATED,  mddpwh_sm_md_reset},
{MDDP_EVT_FUNC_DEACT,     MDDP_STATE_DEACTIVATING, mddpwh_sm_deact},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_ACTIVATED,    mddpwh_sm_rsp_act_ok},
{MDDP_EVT_MD_RSP_TIMEOUT, MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_act_fail},
{MDDP_EVT_DUMMY,          MDDP_STATE_ACTIVATING,   NULL}
};

static struct mddp_sm_entry_t mddpwh_activated_state_machine_s[] = {

{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING, mddpwh_sm_disable},

{MDDP_EVT_MD_RESET,       MDDP_STATE_ACTIVATED,    mddpwh_sm_md_reset},
{MDDP_EVT_FUNC_ENABLE,    MDDP_STATE_ENABLING,     mddpwh_sm_enable},
{MDDP_EVT_FUNC_DEACT,     MDDP_STATE_DEACTIVATING, mddpwh_sm_deact},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_ACTIVATED,    NULL},
{MDDP_EVT_DUMMY,          MDDP_STATE_ACTIVATED,    NULL}
};

static struct mddp_sm_entry_t mddpwh_deactivating_state_machine_s[] = {

{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING, mddpwh_sm_disable},

{MDDP_EVT_MD_RESET,       MDDP_STATE_DEACTIVATED,  mddpwh_sm_md_reset},
{MDDP_EVT_MD_RSP_OK,      MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_deact},
{MDDP_EVT_MD_RSP_TIMEOUT, MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_deact_fail},
{MDDP_EVT_DUMMY,          MDDP_STATE_DEACTIVATING, NULL}
};

struct mddp_sm_entry_t *mddpwh_state_machines_s[MDDP_STATE_CNT] = {
	mddpwh_uninit_state_machine_s,
	mddpwh_enabling_state_machine_s,
	mddpwh_deactivated_state_machine_s,
	mddpwh_activating_state_machine_s,
	mddpwh_activated_state_machine_s,
	mddpwh_deactivating_state_machine_s,
	mddpwh_disabling_state_machine_s,
	mddpwh_disabled_state_machine_s,
};

static void mddpw_wfpm_send_smem_layout(void)
{
	struct mddp_app_t                *app;
	struct mddp_md_msg_t             *md_msg;
	struct wfpm_enable_md_func_req_t *enable_req;
	struct wfpm_smem_info_t          *smem_info;
	uint32_t                          smem_num;
	int32_t                           ipc_send_status = 0;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
			__func__);
		return;
	}

	if (wfpm_ipc_get_smem_list((void **)&smem_info, &smem_num)) {
		MDDP_S_LOG(MDDP_LL_NOTICE,
				"%s: Failed to get smem info!\n",
				__func__);
		smem_num = 0;
	}
	MDDP_S_LOG(MDDP_LL_INFO,
			"%s: smem_info(%llx), smem_num(%u)\n",
			__func__, (unsigned long long)smem_info, smem_num);

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		return;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_SEND_SMEM_LAYOUT_NOTIFY;
	md_msg->data_len = sizeof(struct wfpm_enable_md_func_req_t) +
			smem_num * sizeof(struct wfpm_smem_info_t);
	enable_req = (struct wfpm_enable_md_func_req_t *)&md_msg->data;
	enable_req->mode = WFPM_FUNC_MODE_TETHER;
	enable_req->version = __MDDP_VERSION__;
	enable_req->smem_num = smem_num;
	memcpy(&enable_req->smem_info, smem_info,
			smem_num * sizeof(struct wfpm_smem_info_t));

	ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
	if (ipc_send_status != 0) {
		is_smem_layout_config = false;
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: ipc_send_status = %d\n", __func__, ipc_send_status);
		return;
	}

	is_smem_layout_config = true;
}

static int32_t mddpw_wfpm_msg_hdlr(uint32_t msg_id, void *buf, uint32_t buf_len)
{
	struct mddp_app_t                      *app;
	struct wfpm_md_fast_path_common_rsp_t  *rsp;
	struct wfpm_enable_md_func_rsp_t       *enable_rsp;
	struct mddpw_md_notify_info_t          *md_info;

	if (!mddp_ipc_rx_msg_validation(msg_id, buf_len))
		return -EINVAL;

	rsp = (struct wfpm_md_fast_path_common_rsp_t *) buf;
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	switch (msg_id) {
	case IPC_MSG_ID_WFPM_ENABLE_MD_FAST_PATH_RSP:
		enable_rsp = (struct wfpm_enable_md_func_rsp_t *) buf;
		MDDP_S_LOG(MDDP_LL_INFO,
				"%s: set (%u), (%u), MD version(%u), (%u).\n",
				__func__, enable_rsp->mode, enable_rsp->result,
		enable_rsp->version, enable_rsp->reserved);

		if (rsp->result) {

			MDDP_S_LOG(MDDP_LL_INFO,
					"%s: ENABLE RSP OK, result(%d).\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ENABLING, true);
		} else {

			MDDP_S_LOG(MDDP_LL_NOTICE,
					"%s: ENABLE RSP FAIL, result(%d)!\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ENABLING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_DISABLE_MD_FAST_PATH_RSP:
		if (rsp->result) {

			MDDP_S_LOG(MDDP_LL_INFO,
					"%s: DISABLE RSP OK, result(%d).\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DISABLING, true);
		} else {

			MDDP_S_LOG(MDDP_LL_NOTICE,
					"%s: DISABLE RSP FAIL, result(%d)!\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DISABLING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_ACTIVATE_MD_FAST_PATH_RSP:
		if (rsp->result) {

			MDDP_S_LOG(MDDP_LL_INFO,
					"%s: ACT RSP OK, result(%d).\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ACTIVATING, true);
		} else {

			MDDP_S_LOG(MDDP_LL_NOTICE,
					"%s: ACT RSP FAIL, result(%d)!\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ACTIVATING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_DEACTIVATE_MD_FAST_PATH_RSP:
		if (rsp->result) {

			MDDP_S_LOG(MDDP_LL_INFO,
					"%s: DEACT RSP OK, result(%d)\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DEACTIVATING, true);

		} else {

			MDDP_S_LOG(MDDP_LL_NOTICE,
					"%s: DEACT RSP FAIL, result(%d)\n",
					__func__, rsp->result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DEACTIVATING, false);

		}
		break;

	case IPC_MSG_ID_WFPM_RESET_IND:
		MDDP_S_LOG(MDDP_LL_WARN,
				"%s: Received WFPM RESET IND\n", __func__);

		is_smem_layout_config = false;

		msleep(MDDP_RESET_READY_TIME_MS);
		mddp_sm_on_event(app, MDDP_EVT_MD_RESET);
		break;
	case IPC_MSG_ID_WFPM_MD_NOTIFY:
		MDDP_S_LOG(MDDP_LL_DEBUG,
				"%s: Received WFPM MD NOTIFY\n", __func__);
		md_info = (struct mddpw_md_notify_info_t *) buf;
		if (md_info->buf_len > buf_len - sizeof(*md_info))
			return -EINVAL;
		mddpwh_notify_info(app, md_info);
		break;

	default:
		MDDP_S_LOG(MDDP_LL_NOTICE,
				"%s: Unsupported RSP MSG_ID[%d] from WFPM.\n",
				__func__, msg_id);
		break;
	}

	return 0;
}

static int32_t mddpw_drv_add_txd(struct mddpw_txd_t *txd)
{
	struct mddp_md_msg_t    *md_msg;
	struct mddp_app_t       *app;

	if (!txd || txd->txd_length > MDFPM_TTY_BUF_SZ - sizeof(*txd))
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
			__func__);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		return -ENOMEM;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_SEND_MD_TXD_NOTIFY;
	md_msg->data_len = sizeof(struct mddpw_txd_t) + txd->txd_length;
	memcpy(&md_msg->data, txd, md_msg->data_len);
	return mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

static int32_t mddpw_drv_get_net_stat(struct mddpw_net_stat_t *usage)
{

	static struct mddpw_net_stat_t     cur_stats = {0};
	struct mddpw_net_stat_t           *md_stats;
	uint8_t                            smem_attr;
	uint32_t                           smem_size;

	if (!usage) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: usage is NULL!\n", __func__);
		return -EINVAL;
	}
	memset(usage, 0, sizeof(struct mddpw_net_stat_t));

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_WIFI_STATISTICS,
				(void **)&md_stats, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_WIFI_STATISTICS);
		return -EFAULT;
	}

	if (md_stats && smem_size > 0) {
		#define DIFF_FROM_SMEM(x) (usage->x = \
		(md_stats->x > cur_stats.x) ? (md_stats->x - cur_stats.x) : 0)
		DIFF_FROM_SMEM(tx_packets);
		DIFF_FROM_SMEM(rx_packets);
		DIFF_FROM_SMEM(tx_bytes);
		DIFF_FROM_SMEM(rx_bytes);
		DIFF_FROM_SMEM(tx_errors);
		DIFF_FROM_SMEM(rx_errors);

		memcpy(&cur_stats, md_stats, sizeof(struct mddpw_net_stat_t));
	}

	return 0;
}

static int32_t mddpw_drv_get_net_stat_ext(struct mddpw_net_stat_ext_t *usage)
{
	struct mddpw_net_stat_ext_t       *md_stats = NULL;
	uint8_t                            smem_attr;
	uint32_t                           smem_size;

	if (!usage) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: usage is NULL!\n", __func__);
		return -EINVAL;
	}
	memset(usage, 0, sizeof(struct mddpw_net_stat_ext_t));

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_WIFI_STATISTICS_EXT,
				(void **)&md_stats, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to get smem_id (%d)!\n",
				__func__,
				MDDP_MD_SMEM_USER_WIFI_STATISTICS_EXT);
		return -EFAULT;
	}

	if (!md_stats || smem_size != sizeof(struct mddpw_net_stat_ext_t)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Invalid share memory data, md_stats(%llx), smem_size(%u)!\n",
				__func__, (unsigned long long)md_stats, smem_size);
		return -EFAULT;
	}

	memcpy(usage, md_stats, smem_size);
	return 0;
}

static int32_t mddpw_drv_get_sys_stat(struct mddpw_sys_stat_t **sys_stat)
{
	uint8_t                  smem_attr;
	uint32_t                 smem_size;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_SYS_STAT_SYNC,
				(void **)sys_stat, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_SYS_STAT_SYNC);
		return -EINVAL;
	}

	return 0;
}

static int32_t mddpw_drv_get_ap_rx_reorder_buf(
	struct mddpw_ap_reorder_sync_table_t **ap_table)
{
	uint8_t      smem_attr;
	uint32_t     smem_size;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_RX_REORDER_TO_MD,
				(void **)ap_table, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_RX_REORDER_TO_MD);
		return -EINVAL;
	}

	return 0;
}

static int32_t mddpw_drv_get_md_rx_reorder_buf(
	struct mddpw_md_reorder_sync_table_t **md_table)
{
	uint8_t      smem_attr;
	uint32_t     smem_size;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_RX_REORDER_FROM_MD,
				(void **)md_table, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_RX_REORDER_FROM_MD);
		return -EINVAL;
	}

	return 0;
}

static int32_t mddpw_drv_get_lls_stat(struct wsvc_stat_lls_report_t *stat)
{
	struct wsvc_stat_lls_report_t *lls = NULL;
	uint8_t                       smem_attr;
	uint32_t                      smem_size;

	if (!stat) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: stat is NULL!\n", __func__);
		return -EINVAL;
	}
	memset(stat, 0, sizeof(struct wsvc_stat_lls_report_t));

	if (!mddp_check_subfeature(MF_ID_COMMON, COM_V1_LLS))
		return -ENOENT;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_LLS,
				(void **)&lls, &smem_attr, &smem_size)) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_LLS);
		return -EFAULT;
	}
	if (!lls || smem_size != sizeof(struct wsvc_stat_lls_report_t)) {
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Invalid share memory data, lls(%llx), smem_size(%u)!\n",
				__func__, (unsigned long long)lls, smem_size);
		return -EFAULT;
	}

	memcpy(stat, lls, smem_size);
	return 0;
}

static int32_t mddpw_drv_notify_info(
	struct mddpw_drv_notify_info_t *wifi_notify)
{
	struct mddp_md_msg_t    *md_msg;
	struct mddp_app_t       *app;
	int32_t                 ipc_send_status = 0;

	if (!wifi_notify || wifi_notify->buf_len > MDFPM_TTY_BUF_SZ - sizeof(*wifi_notify))
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config || !is_smem_layout_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured! smem status - %d]\n",
				__func__, is_smem_layout_config);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg)) {
		return -ENOMEM;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_DRV_NOTIFY;
	md_msg->data_len = sizeof(struct mddpw_drv_notify_info_t) +
		wifi_notify->buf_len;
	memcpy(&md_msg->data, wifi_notify, md_msg->data_len);
	ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
	if (ipc_send_status != 0) {

		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: ipc_send_status = %d\n",__func__, ipc_send_status);
		return ipc_send_status;
	}

	return 0;
}

static int32_t mddpw_drv_notify_info_v1(
	struct mddpw_drv_notify_info_t_v1 *wifi_notify_v1)
{
	struct mddp_md_msg_t    *md_msg;
	struct mddp_app_t       *app;
	int32_t                 ipc_send_status = 0;

	if (!wifi_notify_v1 || wifi_notify_v1->buf_len > MDFPM_TTY_BUF_SZ - sizeof(*wifi_notify_v1))
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config || !is_smem_layout_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured! smem status - %d]\n",
				__func__, is_smem_layout_config);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);

	if (unlikely(!md_msg))
		return -ENOMEM;

	md_msg->msg_id = IPC_MSG_ID_WFPM_DRV_NOTIFY;
	md_msg->data_len = sizeof(struct mddpw_drv_notify_info_t_v1) +
		wifi_notify_v1->buf_len;
	memcpy(&md_msg->data, wifi_notify_v1, md_msg->data_len);
	ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
	if (ipc_send_status != 0) {

		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: ipc_send_status = %d\n",__func__, ipc_send_status);
		return ipc_send_status;
	}

	return 0;
}

static int32_t mddpw_drv_get_mddp_feature(void)
{
	struct mddp_app_t       *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
			__func__);
		return -ENODEV;
	}

	if (!app->reset_cnt) {
		pr_notice_once("%s before MD ready!\n", __func__);
		app->abnormal_flags |= MDDP_ABNORMAL_WIFI_DRV_GET_FEATURE_BEFORE_MD_READY;
	}
	return app->feature;
}

static int32_t mddpw_drv_get_mddp_featset(struct mddp_feature *info)
{
	struct mddp_app_t       *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
			__func__);
		return -ENODEV;
	}

	if (!app->reset_cnt) {
		pr_notice_once("%s before MD ready!\n", __func__);
		app->abnormal_flags |= MDDP_ABNORMAL_WIFI_DRV_GET_FEATURE_BEFORE_MD_READY;
	}

	if (!(app->feature & MDDP_FEATURE_NEW_INFO))
		return -ENOENT;

	*info = app->mddp_feat;
	return 0;
}

static int32_t mddpw_drv_reg_callback(struct mddp_drv_handle_t *handle)
{
	struct mddpw_drv_handle_t         *wifi_handle;

	if (handle->wifi_handle == NULL) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: handle NULL\n", __func__);
		return -EINVAL;
	}

	wifi_handle = handle->wifi_handle;

	wifi_handle->add_txd = mddpw_drv_add_txd;
	wifi_handle->get_net_stat = mddpw_drv_get_net_stat;
	wifi_handle->get_ap_rx_reorder_buf = mddpw_drv_get_ap_rx_reorder_buf;
	wifi_handle->get_md_rx_reorder_buf = mddpw_drv_get_md_rx_reorder_buf;
	wifi_handle->notify_drv_info = mddpw_drv_notify_info;
	wifi_handle->notify_drv_info_v1 = mddpw_drv_notify_info_v1;
	wifi_handle->get_net_stat_ext = mddpw_drv_get_net_stat_ext;
	wifi_handle->get_sys_stat = mddpw_drv_get_sys_stat;
	wifi_handle->get_mddp_feature = mddpw_drv_get_mddp_feature;
	wifi_handle->get_mddp_featset = mddpw_drv_get_mddp_featset;
	if (lls_mem_exist)
		wifi_handle->get_lls_stat = mddpw_drv_get_lls_stat;
	else
		wifi_handle->get_lls_stat = NULL;

	return 0;
}

static int32_t mddpw_drv_dereg_callback(struct mddp_drv_handle_t *handle)
{
	struct mddpw_drv_handle_t         *wifi_handle;

	if (handle->wifi_handle == NULL) {
		MDDP_S_LOG(MDDP_LL_ERR, "%s: handle NULL\n", __func__);
		return -EINVAL;
	}

	wifi_handle = handle->wifi_handle;

	wifi_handle->add_txd = NULL;
	wifi_handle->get_net_stat = NULL;
	wifi_handle->get_ap_rx_reorder_buf = NULL;
	wifi_handle->get_md_rx_reorder_buf = NULL;
	wifi_handle->notify_drv_info = NULL;
	wifi_handle->notify_drv_info_v1 = NULL;
	wifi_handle->get_net_stat_ext = NULL;
	wifi_handle->get_sys_stat = NULL;
	wifi_handle->get_mddp_feature = NULL;
	wifi_handle->get_mddp_featset = NULL;
	wifi_handle->get_lls_stat = NULL;

	return 0;
}

static ssize_t mddpwh_sysfs_callback(
	struct mddp_app_t *app,
	enum mddp_sysfs_cmd_e cmd,
	char *buf,
	size_t buf_len)
{
	struct mddpw_net_stat_t        *md_stats;
	uint8_t                         smem_attr;
	uint32_t                        smem_size;
	uint32_t                        show_cnt = 0;
#ifdef MDDP_EM_SUPPORT
	struct mddp_md_msg_t           *md_msg;
	int32_t                         ipc_send_status = 0;
#endif

	if (cmd == MDDP_SYSFS_CMD_STATISTIC_READ) {
		if (mddp_ipc_get_md_smem_by_id(
				MDDP_MD_SMEM_USER_WIFI_STATISTICS,
				(void **)&md_stats, &smem_attr, &smem_size)) {
			MDDP_S_LOG(MDDP_LL_NOTICE,
					"%s: Failed to get smem_id (%d)!\n",
					__func__,
					MDDP_MD_SMEM_USER_WIFI_STATISTICS);
			return -EINVAL;
		}

		show_cnt += scnprintf(buf + show_cnt, PAGE_SIZE - show_cnt,
					"[MDDP-WH Statistics]\n");
		show_cnt += scnprintf(buf + show_cnt, PAGE_SIZE - show_cnt,
			"%s\t\t%s\t\t%s\t%s\t%s\t%s\n",
			"tx_pkts", "rx_pkts",
			"tx_bytes", "rx_bytes",
			"tx_error", "rx_error");
		show_cnt += scnprintf(buf + show_cnt, PAGE_SIZE - show_cnt,
			"%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
			md_stats->tx_packets, md_stats->rx_packets,
			md_stats->tx_bytes, md_stats->rx_bytes,
			md_stats->tx_errors, md_stats->rx_errors);
		return show_cnt;
	}
#ifdef MDDP_EM_SUPPORT
	if (cmd == MDDP_SYSFS_EM_CMD_TEST_WRITE) {
		md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_ATOMIC);
		if (md_msg) {
			md_msg->msg_id = IPC_MSG_ID_MDFPM_EM_TEST_REQ;
			md_msg->data_len = buf_len;
			memcpy(&md_msg->data, buf, buf_len);
			ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_MDFPM);
			if (ipc_send_status != 0)
				return ipc_send_status;
		}

		return buf_len;
	}
#endif

	return 0;
}

static void wfpm_reset_work_func(struct work_struct *work)
{
	struct mddp_app_t *app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	uint32_t reset_seq = READ_ONCE(app->reset_seq);

	app->abnormal_flags &= ~MDDP_ABNORMAL_CCCI_SEND_FAILED;
	mddpw_wfpm_send_smem_layout();
	if (!app->reset_cnt)
		mddp_check_feature();
	else
		mddpw_notify_wlan_mdinfo();
	app->reset_cnt++;
	mutex_lock(&app->resource_lock);
	mddp_netdev_notifier_exit();
	mddp_netfilter_unhook();
	mddp_f_dev_del_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_del_lan_dev(app->ap_cfg.dl_dev_name);
	mddp_sm_finish_reset(app, reset_seq);
	mutex_unlock(&app->resource_lock);
}

static void mddp_hook_work_func(struct work_struct *work)
{
	struct mddp_app_t       *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (!mddp_connection_base_activated_s)
		mddp_netfilter_hook();
	complete(&app->work_comp);
}

static void mddp_unhook_work_func(struct work_struct *work)
{
	struct mddp_app_t *app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	mutex_lock(&app->resource_lock);
	if (!mddp_connection_base_activated_s)
		mddp_netfilter_unhook();
	mddp_f_dev_del_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_del_lan_dev(app->ap_cfg.dl_dev_name);
	complete(&app->work_comp);
	mutex_unlock(&app->resource_lock);
}

void mddpw_notify_wlan_mdinfo(void)
{
	struct mddp_app_t       *app;
	struct mddpw_md_notify_info_t     md_info;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	md_info.version = 0;
	md_info.info_type = 1;
	md_info.buf_len = 0;
	mddpwh_notify_info(app, &md_info);
}

void mddpwh_sm_drain(void)
{
	if (mddp_workqueue)
		flush_workqueue(mddp_workqueue);
}

void mddpwh_sm_quiesce(void)
{
	struct mddp_app_t *app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!mddp_workqueue)
		return;
	cancel_work_sync(&wfpm_reset_work);
	cancel_work_sync(&mddp_hook_work);
	cancel_work_sync(&mddp_unhook_work);
	destroy_workqueue(mddp_workqueue);
	mddp_workqueue = NULL;
	mutex_lock(&app->resource_lock);
	mddp_netfilter_unhook();
	mddp_f_dev_del_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_del_lan_dev(app->ap_cfg.dl_dev_name);
	mutex_unlock(&app->resource_lock);
}

int32_t mddpwh_sm_init(struct mddp_app_t *app)
{
	mddp_workqueue = alloc_ordered_workqueue("mddp_ctrl", WQ_MEM_RECLAIM);
	if (!mddp_workqueue)
		return -ENOMEM;

	memcpy(&app->state_machines,
		&mddpwh_state_machines_s,
		sizeof(mddpwh_state_machines_s));

	MDDP_S_LOG(MDDP_LL_INFO,
			"%s: %p, %p\n",
			__func__,
			&(app->state_machines), &mddpwh_state_machines_s);
	mddp_dump_sm_table(app);

	app->md_recv_msg_hdlr = mddpw_wfpm_msg_hdlr;
	app->reg_drv_callback = mddpw_drv_reg_callback;
	app->dereg_drv_callback = mddpw_drv_dereg_callback;
	app->sysfs_callback = mddpwh_sysfs_callback;
	memcpy(&app->md_cfg, &mddpw_md_cfg_s, sizeof(struct mddp_md_cfg_t));
	app->is_config = 1;

	INIT_WORK(&wfpm_reset_work, wfpm_reset_work_func);
	INIT_WORK(&mddp_hook_work, mddp_hook_work_func);
	INIT_WORK(&mddp_unhook_work, mddp_unhook_work_func);

	return 0;
}
