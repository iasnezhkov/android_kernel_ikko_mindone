/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "mddp_ctrl.h"
#include "mddp_debug.h"
#include "mddp_filter.h"
#include "mddp_sm.h"
#include "mddp_usage.h"
#include "mddp_dev.h"

typedef int32_t (*mddp_sm_init_func_t)(struct mddp_app_t *app);

struct feature_set {
	enum mddp_vc_mf_id_e type;
	uint32_t bm;
};

struct check_feature_rsp {
	uint16_t        major_version;
	uint16_t        minor_version;
	uint32_t        num;
	struct          feature_set fs[];
};

static const mddp_sm_init_func_t mddp_sm_init_func_list_s[] = {
	#undef MDDP_MODULE_ID
	#undef MDDP_MODULE_PREFIX
	#define MDDP_MODULE_ID(_id)
	#define MDDP_MODULE_PREFIX(_prefix) _prefix ## _sm_init,
	#include "mddp_app_config.h"
};

#define MDDP_SM_LOCK_FLAG unsigned long flags
#define MDDP_SM_LOCK_INIT(_locker) spin_lock_init(&(_locker))
#define MDDP_SM_LOCK(_locker) spin_lock_irqsave(&(_locker), flags)
#define MDDP_SM_UNLOCK(_locker) spin_unlock_irqrestore(&(_locker), flags)

static struct mddp_app_t mddp_app_inst_s[MDDP_APP_TYPE_CNT];
static struct mutex mddp_state_handler_mtx;
static bool mddp_stopping;
static enum mddp_state_e _mddp_sm_on_event(struct mddp_app_t *app, enum mddp_event_e event);
static void mddp_sm_timeout(struct work_struct *work);

static void _mddp_set_state(struct mddp_app_t *app, enum mddp_state_e new_state)
{
	MDDP_SM_LOCK_FLAG;

	MDDP_SM_LOCK(app->locker);
	app->state = new_state;
	MDDP_SM_UNLOCK(app->locker);
}

void mddp_check_feature(void)
{
	struct mddp_md_msg_t *md_msg;
	struct mddp_app_t *app;
	int32_t ipc_send_status = 0;
	struct check_feature_req_k66 ap_info_k66 = {
		.android_version = 16,
		.minor_version = 0,
		.wifi_feature = 0,
		.kernel_version = KERNEL_VERSION(6, 12, 0),
	};

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t),
			GFP_ATOMIC);
	if (unlikely(!md_msg)) {
		MDDP_F_LOG(MDDP_LL_NOTICE,
			"%s: failed to alloc md_msg bug!\n", __func__);
		return;
	}

	MDDP_S_LOG(MDDP_LL_DEBUG,
			"%s:, android_version [%d], wifi_feature [%d], kernel_version [%d]",
			__func__, ap_info_k66.android_version,
			ap_info_k66.wifi_feature, ap_info_k66.kernel_version);
	md_msg->msg_id = IPC_MSG_ID_MDFPM_CHECK_FEATURE_REQ;
	md_msg->data_len = sizeof(struct check_feature_req_k66);
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	app->abnormal_flags |= MDDP_ABNORMAL_CHECK_FEATURE_ABSENT;
	memcpy(&md_msg->data, &ap_info_k66, md_msg->data_len);
	ipc_send_status = mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_MDFPM);
	if (ipc_send_status != 0)
		MDDP_S_LOG(MDDP_LL_ERR,
			"%s: ipc_send_status = %d\n", __func__, ipc_send_status);
}

static void mddp_handshake_done(void *buf, uint32_t buf_len)
{
	struct mddp_app_t *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	app->abnormal_flags &= ~MDDP_ABNORMAL_CHECK_FEATURE_ABSENT;

	if (buf_len == 4) {
		app->feature = *(uint32_t *)buf;
	} else {
		struct check_feature_rsp *rsp;
		int i;

		if (buf_len < sizeof(*rsp))
			return;
		rsp = (struct check_feature_rsp *)buf;
		if (rsp->num <= (buf_len - sizeof(*rsp)) / sizeof(struct feature_set) &&
		    buf_len == sizeof(*rsp) + rsp->num * sizeof(struct feature_set)) {
			app->feature |= MDDP_FEATURE_NEW_INFO;
			app->mddp_feat.major_version = rsp->major_version;
			app->mddp_feat.minor_version = rsp->minor_version;
			for (i = 0; i < rsp->num; i++) {
				switch (rsp->fs[i].type) {
				case MF_ID_COMMON:
					app->mddp_feat.common = rsp->fs[i].bm;
					app->feature |= MDDP_FEATURE_MCIF_WIFI;
					break;
				case MF_ID_WFC:
					app->mddp_feat.wfc = rsp->fs[i].bm;
					app->feature |= MDDP_FEATURE_MCIF_WIFI;
					break;
				case MF_ID_MDDP_WH:
					app->mddp_feat.wh = rsp->fs[i].bm;
					app->feature |= MDDP_FEATURE_MDDP_WH;
					break;
				default:
					break;
				}
				MDDP_S_LOG(MDDP_LL_DEBUG,
					"%s[%d] Sub featureset: common[%d], wfc[%d], wh[%d]",
					__func__, i, app->mddp_feat.common,
					app->mddp_feat.wfc, app->mddp_feat.wh);
			}
		} else
			MDDP_S_LOG(MDDP_LL_ERR, "MD response size(%d) error, num(%d)",
					buf_len, rsp->num);
	}
	mddp_connection_base_activated_s =
		mddp_check_subfeature(MF_ID_MDDP_WH, MDDP_WH_CONNECTION_BASED);

	mddpw_notify_wlan_mdinfo();
}

bool mddp_check_subfeature(int type, int feat)
{
	struct mddp_app_t *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	switch (type) {
	case MF_ID_COMMON:
		return app->mddp_feat.common & (1 << feat);
	case MF_ID_WFC:
		return app->mddp_feat.wfc & (1 << feat);
	case MF_ID_MDDP_WH:
		return app->mddp_feat.wh & (1 << feat);
	default:
		return 0;
	}
}
EXPORT_SYMBOL(mddp_check_subfeature);

char operate[4][14] = {
	"Add dl filter",
	"Del dl filter",
	"Add ul filter",
	"Del ul filter"
};

uint32_t printV4Msg(struct mdfpm_log *mdfpm_log_buf, int action_id_t, char *str_dstate)
{
	uint32_t size;
	uint16_t port[2];
	char SrcAddr[16];
	char DstAddr[16];

	snprintf(SrcAddr, 16, "%3u.%3u.%3u.%3u",
			mdfpm_log_buf->buf[0], mdfpm_log_buf->buf[1],
			mdfpm_log_buf->buf[2], mdfpm_log_buf->buf[3]);
	snprintf(DstAddr, 16, "%3u.%3u.%3u.%3u",
			mdfpm_log_buf->buf[4], mdfpm_log_buf->buf[5],
			mdfpm_log_buf->buf[6], mdfpm_log_buf->buf[7]);
	port[0] = mdfpm_log_buf->buf[9] << 8 | mdfpm_log_buf->buf[8];
	port[1] = mdfpm_log_buf->buf[11] << 8 | mdfpm_log_buf->buf[10];
	size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
			"[MDDP_WH] %s, src_addr(%s), dst_addr(%s), src_port(%5u), dst_port(%5u)",
			operate[action_id_t%4], SrcAddr, DstAddr, port[0], port[1]);
	MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
	return size;
}

uint32_t printV6Msg(struct mdfpm_log *mdfpm_log_buf, int action_id_t, char *str_dstate)
{
	uint32_t idx, size;
	uint16_t d16Addr[16], v6Port[2];
	char v6SrcAddr[40];
	char v6DstAddr[40];

	for (idx = 0; idx < 16; idx++)
		d16Addr[idx] = mdfpm_log_buf->buf[2*idx+1] << 8 | mdfpm_log_buf->buf[2*idx];
	snprintf(v6SrcAddr, 40,	"%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x",
			d16Addr[0], d16Addr[1], d16Addr[2], d16Addr[3],
			d16Addr[4], d16Addr[5], d16Addr[6], d16Addr[7]);
	snprintf(v6DstAddr, 40, "%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x",
			d16Addr[8], d16Addr[9], d16Addr[10], d16Addr[11],
			d16Addr[12], d16Addr[13], d16Addr[14], d16Addr[15]);
	v6Port[0] = mdfpm_log_buf->buf[33] << 8 | mdfpm_log_buf->buf[32];
	v6Port[1] = mdfpm_log_buf->buf[35] << 8 | mdfpm_log_buf->buf[34];
	size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
			"[MDDP_WH] %s, src_addr(%s), dst_addr(%s), src_port(%5u), dst_port(%5u)",
			operate[action_id_t%4], v6SrcAddr, v6DstAddr, v6Port[0], v6Port[1]);
	MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
	return size;
}

char buffer[MDFPM_SEND_LOG_BUF_SZ];
uint32_t print_unexpected_id(struct mdfpm_log *mdfpm_log_buf, uint32_t buf_len, char *str_dstate)
{
	uint32_t i, size, idx = 0;

	buffer[0] = 0;
	for (i = 0; i < buf_len && idx < sizeof(buffer) - 1; i++)
		idx += scnprintf(&buffer[idx], sizeof(buffer) - idx,
				 "%u", mdfpm_log_buf->buf[i]);
	size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
			"[MDDP_WH] Unexpected id, buffer:%s", buffer);
	MDDP_S_LOG(MDDP_LL_DEBUG, "%s\n", str_dstate);
	return size;
}

char str_dstate[MDFPM_SEND_LOG_BUF_SZ];
static void mddp_print_mdfpm_log(struct mdfpm_log *buf, uint32_t buf_len)
{
	static const uint8_t minimum[] = {0, 0, 0, 1, 1, 2, 1, 1, 12, 12, 12, 12, 36, 36, 36, 36};
	uint32_t size = 0;
	struct mdfpm_log *mdfpm_log_buf;

	if (buf_len >= MDFPM_SEND_LOG_HEADER) {
		mdfpm_log_buf = (struct mdfpm_log *)buf;
		buf_len -= MDFPM_SEND_LOG_HEADER;
		if (mdfpm_log_buf->buffer_len > buf_len)
			return;
		buf_len = mdfpm_log_buf->buffer_len;
		if (mdfpm_log_buf->action_id < ARRAY_SIZE(minimum) &&
		    buf_len < minimum[mdfpm_log_buf->action_id])
			return;
		switch (mdfpm_log_buf->action_id) {
		case MDFPM_LOG_NONE:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ, "[MDDP_WH] None");
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_RUN:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ, "[MDDP_WH] Running");
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_STOP:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ, "[MDDP_WH] Stopped");
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_EM_TEST:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
					"[MDDP_WH] em_test_cmd_id(%u)",
					mdfpm_log_buf->buf[0]);
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_LOCK_MD:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
					"[MDDP_WH] Lock MD path(%u)",
					mdfpm_log_buf->buf[0]);
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_RM_BY_REQ:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
					"[MDDP_WH] Del filter by req, task_id(%u), level(%u)",
					mdfpm_log_buf->buf[0], mdfpm_log_buf->buf[1]);
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_RM_BY_ASSIGN:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
					"[MDDP_WH] Del filter by assign, level(%u)",
					mdfpm_log_buf->buf[0]);
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MDDP_WH_RM_BY_SCORE:
			size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
					"[MDDP_WH] Del filter by score, level(%u)",
					mdfpm_log_buf->buf[0]);
			MDDP_S_LOG(MDDP_LL_DEBUG, "%s", str_dstate);
			break;
		case MDFPM_LOG_MD_ADD_FILTER_V4:
		case MDFPM_LOG_MD_DEL_FILTER_V4:
		case MDFPM_LOG_CS_ADD_FILTER_V4:
		case MDFPM_LOG_CS_DEL_FILTER_V4:
			size = printV4Msg(mdfpm_log_buf, mdfpm_log_buf->action_id, &str_dstate[0]);
			break;
		case MDFPM_LOG_MD_ADD_FILTER_V6:
		case MDFPM_LOG_MD_DEL_FILTER_V6:
		case MDFPM_LOG_CS_ADD_FILTER_V6:
		case MDFPM_LOG_CS_DEL_FILTER_V6:
			size = printV6Msg(mdfpm_log_buf, mdfpm_log_buf->action_id, &str_dstate[0]);
			break;
		default:
			break;
		}
		if (mdfpm_log_buf->action_id >= MDFPM_LOG_NUM)
			size = print_unexpected_id(mdfpm_log_buf, buf_len, &str_dstate[0]);
	} else {
		size = scnprintf(str_dstate, MDFPM_SEND_LOG_BUF_SZ,
				"[MDDP_WH] error, buf_len(%u) should large than %u",
				buf_len, MDFPM_SEND_LOG_HEADER);
		MDDP_S_LOG(MDDP_LL_INFO, "%s", str_dstate);
	}
	mddp_enqueue_md_log(MDDP_MD_LOG_ID_GET_LOG, size, str_dstate);
}

static int32_t mddp_sm_ctrl_msg_hdlr(
		uint32_t msg_id,
		void *buf,
		uint32_t buf_len)
{
	int32_t                 ret = 0;

	switch (msg_id) {
	case IPC_MSG_ID_MDFPM_CHECK_FEATURE_RSP:
		mddp_handshake_done(buf, buf_len);
		break;
	case IPC_MSG_ID_MDFPM_LOG:
		mddp_print_mdfpm_log(buf, buf_len);
		break;
	default:
		MDDP_S_LOG(MDDP_LL_ERR,
				"%s: Unaccepted msg_id(%d)!\n",
				__func__, msg_id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int32_t mddp_sm_init(void)
{
	struct mddp_app_t              *app;
	uint32_t                        type;
	uint32_t                        idx;
	int ret;

	memset(&mddp_app_inst_s, 0, sizeof(mddp_app_inst_s));
	mddp_stopping = false;
	mutex_init(&mddp_state_handler_mtx);

	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);

		app->type = type;
		MDDP_SM_LOCK_INIT(app->locker);
		mutex_init(&app->resource_lock);
		mutex_init(&app->callback_lock);
		INIT_DELAYED_WORK(&app->timeout_work, mddp_sm_timeout);
		init_completion(&app->md_resp_comp);
		init_completion(&app->work_comp);
		_mddp_set_state(app, MDDP_STATE_UNINIT);

		ret = mddp_sm_init_func_list_s[idx](app);
		if (ret) {
			mddpwh_sm_quiesce();
			return ret;
		}
	}

	return 0;
}

void mddp_sm_begin_shutdown(void)
{
	uint32_t idx;

	mutex_lock(&mddp_state_handler_mtx);
	WRITE_ONCE(mddp_stopping, true);
	mutex_unlock(&mddp_state_handler_mtx);
	for (idx = 0; idx < MDDP_MOD_CNT; idx++)
		cancel_delayed_work_sync(&mddp_get_app_inst(mddp_sm_module_list_s[idx])->timeout_work);
}

void mddp_sm_uninit(void)
{
	memset(mddp_app_inst_s, 0, sizeof(mddp_app_inst_s));
}

struct mddp_app_t *mddp_get_default_app_inst(void)
{
	return &mddp_app_inst_s[0];
}

struct mddp_app_t *mddp_get_app_inst(enum mddp_app_type_e type)
{
	struct mddp_app_t      *app;

	app = &mddp_app_inst_s[type];
	return app;
}

static enum mddp_state_e mddp_get_state(struct mddp_app_t *app)
{
	return app->state;
}

enum mddp_state_e mddp_sm_set_state_by_md_rsp(struct mddp_app_t *app,
	enum mddp_state_e prev_state, bool md_rsp_result)
{
	enum mddp_state_e new_state = MDDP_STATE_DUMMY;

	mutex_lock(&mddp_state_handler_mtx);
	if (!mddp_stopping && !app->reset_pending && !app->sync_lost &&
	    mddp_get_state(app) == prev_state) {
		cancel_delayed_work(&app->timeout_work);
		if (!md_rsp_result)
			app->transaction_status = -EIO;
		new_state = _mddp_sm_on_event(app,
			md_rsp_result ? MDDP_EVT_MD_RSP_OK : MDDP_EVT_MD_RSP_TIMEOUT);
		complete(&app->md_resp_comp);
	}
	mutex_unlock(&mddp_state_handler_mtx);
	return new_state;
}

#ifdef __MDDP_DEBUG__
void mddp_dump_sm_table(struct mddp_app_t *app)
{
	uint32_t                        i, j;
	struct mddp_sm_entry_t         *state_machine;
	struct mddp_sm_entry_t         *entry;

	MDDP_S_LOG(MDDP_LL_DEBUG,
			"\n\n\t%s:\n==============================\n",
			__func__);

	for (i = 0; i < MDDP_STATE_CNT; i++) {
		state_machine = app->state_machines[i];
		MDDP_S_LOG(MDDP_LL_DEBUG, "\n=====(%d)=====\n", i);

		for (j = 0; j < MDDP_EVT_CNT; j++) {
			entry = state_machine + j;

			if (entry->event == MDDP_EVT_DUMMY)
				break;

			MDDP_S_LOG(MDDP_LL_DEBUG,
					"\tevt(%d), new_state(%d)\n",
					entry->event, entry->new_state);
		}
	}

	MDDP_S_LOG(MDDP_LL_DEBUG,
			"\n ==============================\n\n");
}
#endif

static enum mddp_state_e _mddp_sm_on_event(struct mddp_app_t *app,
		enum mddp_event_e event)
{
	uint32_t                        idx;
	enum mddp_state_e               old_state;
	enum mddp_state_e               new_state;
	struct mddp_sm_entry_t         *state_machine;
	struct mddp_sm_entry_t         *entry;

	new_state = old_state = mddp_get_state(app);
	state_machine = app->state_machines[old_state];

	for (idx = 0; idx < MDDP_EVT_CNT; idx++) {
		entry = state_machine + idx;
		if (event == entry->event) {

			new_state = entry->new_state;

			if (new_state != MDDP_STATE_DUMMY)
				_mddp_set_state(app, new_state);

			mddp_dump_sm_table(app);
			MDDP_S_LOG(MDDP_LL_WARN,
					"%s: event(%d), old_state(%d) -> new_state(%d).\n",
					__func__, event, old_state, new_state);

			if (entry->action)
				entry->action(app);

			break;
		} else if (entry->event == MDDP_EVT_DUMMY) {

			MDDP_S_LOG(MDDP_LL_WARN,
					"%s: Invalid event(%d) for current state(%d)!\n",
					__func__, event, old_state);

			break;
		}
	}

	return new_state;
}

static enum mddp_state_e mddp_sm_dispatch(struct mddp_app_t *app,
		enum mddp_event_e event)
{
	enum mddp_state_e new_state;
	bool is_func_event = event >= MDDP_EVT_FUNC_ENABLE &&
			event <= MDDP_EVT_FUNC_DEACT;

	if (is_func_event) {
		app->ipc_send_status = 0;
		app->transaction_status = 0;
	}
	if (event == MDDP_EVT_MD_RSP_TIMEOUT && !app->transaction_status)
		app->transaction_status = -ETIMEDOUT;
	new_state = _mddp_sm_on_event(app, event);
	if (is_func_event && app->ipc_send_status) {
		app->transaction_status = app->ipc_send_status;
		new_state = _mddp_sm_on_event(app, MDDP_EVT_MD_RSP_TIMEOUT);
		complete(&app->md_resp_comp);
	} else if (is_func_event && (new_state == MDDP_STATE_ENABLING ||
		   new_state == MDDP_STATE_DISABLING || new_state == MDDP_STATE_ACTIVATING ||
		   new_state == MDDP_STATE_DEACTIVATING)) {
		app->timeout_state = new_state;
		app->timeout_seq = app->reset_seq;
		app->timeout_deadline = jiffies + msecs_to_jiffies(150);
		mod_delayed_work(system_wq, &app->timeout_work, msecs_to_jiffies(150));
	}
	return new_state;
}

static void mddp_sm_timeout(struct work_struct *work)
{
	struct mddp_app_t *app = container_of(to_delayed_work(work),
			struct mddp_app_t, timeout_work);

	mutex_lock(&mddp_state_handler_mtx);
	if (!mddp_stopping && !app->reset_pending && !app->sync_lost &&
	    app->reset_seq == app->timeout_seq && app->state == app->timeout_state &&
	    time_after_eq(jiffies, app->timeout_deadline)) {
		app->sync_lost = true;
		mddp_sm_dispatch(app, MDDP_EVT_MD_RSP_TIMEOUT);
		complete(&app->md_resp_comp);
	}
	mutex_unlock(&mddp_state_handler_mtx);
}

enum mddp_state_e mddp_sm_on_event(struct mddp_app_t *app,
		enum mddp_event_e event)
{
	enum mddp_state_e state = MDDP_STATE_DUMMY;

	mutex_lock(&mddp_state_handler_mtx);
	if (!mddp_stopping && (!app->reset_pending || event == MDDP_EVT_MD_RESET) &&
	    (!app->sync_lost || event == MDDP_EVT_MD_RESET))
		state = mddp_sm_dispatch(app, event);
	mutex_unlock(&mddp_state_handler_mtx);
	return state;
}

void mddp_sm_disable_requested(struct mddp_app_t *app)
{
	mutex_lock(&mddp_state_handler_mtx);
	app->desired_enabled = false;
	mutex_unlock(&mddp_state_handler_mtx);
}

void mddp_sm_finish_reset(struct mddp_app_t *app, uint32_t reset_seq)
{
	mutex_lock(&mddp_state_handler_mtx);
	if (app->reset_seq == reset_seq && app->reset_pending) {
		app->reset_pending = false;
		if (!mddp_stopping && app->desired_enabled &&
		    app->state != MDDP_STATE_DISABLED)
			mddp_sm_dispatch(app, MDDP_EVT_FUNC_ENABLE);
		else
			_mddp_set_state(app, MDDP_STATE_DISABLED);
	}
	mutex_unlock(&mddp_state_handler_mtx);
}

int mddp_sm_begin(struct mddp_app_t *app, enum mddp_event_e event,
		uint32_t reset_seq)
{
	bool legal = false;
	int ret = 0;

	mutex_lock(&mddp_state_handler_mtx);
	if (mddp_stopping) {
		ret = -ESHUTDOWN;
		goto out;
	}
	if (app->reset_pending || app->reset_seq != reset_seq) {
		ret = -ENETRESET;
		goto out;
	}
	if (app->sync_lost && event != MDDP_EVT_FUNC_DISABLE) {
		ret = -EPIPE;
		goto out;
	}
	switch (event) {
	case MDDP_EVT_FUNC_ENABLE:
		legal = app->state == MDDP_STATE_DISABLED ||
			app->state == MDDP_STATE_DEACTIVATED;
		break;
	case MDDP_EVT_FUNC_ACT:
		legal = app->desired_enabled && app->state == MDDP_STATE_DEACTIVATED;
		break;
	case MDDP_EVT_FUNC_DEACT:
		legal = app->state == MDDP_STATE_ACTIVATED ||
			app->state == MDDP_STATE_DEACTIVATED;
		break;
	case MDDP_EVT_FUNC_DISABLE:
		legal = app->state != MDDP_STATE_UNINIT;
		break;
	default:
		break;
	}
	if (!legal) {
		ret = -EBUSY;
		goto out;
	}
	reinit_completion(&app->md_resp_comp);
	reinit_completion(&app->work_comp);
	if (event == MDDP_EVT_FUNC_ENABLE)
		app->desired_enabled = true;
	if (event == MDDP_EVT_FUNC_DISABLE && app->state == MDDP_STATE_DISABLED) {
		app->transaction_status = 0;
		complete(&app->md_resp_comp);
		goto out;
	}
	mddp_sm_dispatch(app, event);
	if (event == MDDP_EVT_FUNC_DEACT && app->state == MDDP_STATE_DEACTIVATED)
		complete(&app->md_resp_comp);
out:
	mutex_unlock(&mddp_state_handler_mtx);
	return ret;
}

int mddp_sm_wait(struct mddp_app_t *app, enum mddp_event_e event,
		uint32_t reset_seq)
{
	enum mddp_state_e waiting_state;
	unsigned long completed;
	int ret;

	switch (event) {
	case MDDP_EVT_FUNC_ENABLE: waiting_state = MDDP_STATE_ENABLING; break;
	case MDDP_EVT_FUNC_DISABLE: waiting_state = MDDP_STATE_DISABLING; break;
	case MDDP_EVT_FUNC_ACT: waiting_state = MDDP_STATE_ACTIVATING; break;
	case MDDP_EVT_FUNC_DEACT: waiting_state = MDDP_STATE_DEACTIVATING; break;
	default: return -EINVAL;
	}
	completed = wait_for_completion_timeout(&app->md_resp_comp, msecs_to_jiffies(150));
	mutex_lock(&mddp_state_handler_mtx);
	if (app->reset_seq != reset_seq || app->reset_pending)
		ret = -ENETRESET;
	else if (app->sync_lost)
		ret = -EPIPE;
	else {
		if (!completed && app->state == waiting_state) {
			app->sync_lost = true;
			mddp_sm_dispatch(app, MDDP_EVT_MD_RSP_TIMEOUT);
		}
		ret = app->transaction_status;
	}
	mutex_unlock(&mddp_state_handler_mtx);
	return ret;
}

int mddp_sm_wait_work(struct mddp_app_t *app, uint32_t reset_seq)
{
	unsigned long completed;
	int ret;

	completed = wait_for_completion_timeout(&app->work_comp, msecs_to_jiffies(1000));
	mutex_lock(&mddp_state_handler_mtx);
	if (app->reset_seq != reset_seq || app->reset_pending)
		ret = -ENETRESET;
	else if (!completed)
		ret = -ETIMEDOUT;
	else
		ret = app->transaction_status;
	mutex_unlock(&mddp_state_handler_mtx);
	return ret;
}

int32_t mddp_sm_msg_hdlr(
		uint32_t user_id,
		uint32_t msg_id,
		void *buf,
		uint32_t buf_len)
{
	struct mddp_app_t      *app = NULL;
	int32_t                 ret = -ENODEV;

	if (READ_ONCE(mddp_stopping))
		return -ESHUTDOWN;
	switch (user_id) {
	case MDFPM_USER_ID_MDFPM:
		ret = mddp_sm_ctrl_msg_hdlr(msg_id, buf, buf_len);
		goto _done;

	case MDFPM_USER_ID_WFPM:
		app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
		break;

	case MDFPM_USER_ID_DPFM:
		ret = mddp_f_msg_hdlr(msg_id, buf, buf_len);
		if (ret)
			ret = mddp_u_msg_hdlr(msg_id, buf, buf_len);

		goto _done;

	default:

		MDDP_S_LOG(MDDP_LL_WARN,
				"%s: Unaccepted user_id(%d)!\n",
				__func__, user_id);
		ret = -EINVAL;
		goto _done;
	}

	if (app && app->is_config) {
		ret = app->md_recv_msg_hdlr(msg_id, buf, buf_len);
		goto _done;
	}

	MDDP_S_LOG(MDDP_LL_ERR,
			"%s: app is not configured, app(%p), user_id(%d), msg_id(%d)!\n",
			__func__, app, user_id, msg_id);

_done:
	return ret;
}

int32_t mddp_sm_reg_callback(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle)
{
	struct mddp_app_t *app = mddp_get_app_inst(conf->app_type);
	int ret = -EPERM;

	mutex_lock(&app->callback_lock);
	if (app->drv_reg)
		ret = -EBUSY;
	else if (app->is_config && app->reg_drv_callback) {
		ret = app->reg_drv_callback(handle);
		if (!ret) {
			memcpy(&app->drv_hdlr, handle, sizeof(app->drv_hdlr));
			WRITE_ONCE(app->drv_reg, 1);
			app->abnormal_flags &= ~MDDP_ABNORMAL_WIFI_DRV_GET_FEATURE_BEFORE_MD_READY;
		}
	}
	mutex_unlock(&app->callback_lock);
	return ret;
}

void mddp_sm_dereg_callback(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle)
{
	struct mddp_app_t *app = mddp_get_app_inst(conf->app_type);

	mutex_lock(&app->callback_lock);
	if (app->is_config && app->drv_reg && app->dereg_drv_callback) {
		app->callbacks_ready = false;
		WRITE_ONCE(app->drv_reg, 0);
		memset(&app->drv_hdlr, 0, sizeof(app->drv_hdlr));
		app->dereg_drv_callback(handle);
	}
	mutex_unlock(&app->callback_lock);
}
