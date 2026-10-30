/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>

#include "mddp_debug.h"
#include "mddp_ipc.h"
#include "mddp_sm.h"

#include "mtk_ccci_common.h"

#define LLS_SMEM_SIZE (44*1024)

uint8_t lls_mem_exist;

static uint8_t smem_user_num;
static int32_t mddp_ipc_tty_port_s = -1;
static struct task_struct *rx_task;
static struct wfpm_smem_info_t smem_info_s[] = {
	{MDDP_MD_SMEM_USER_RX_REORDER_TO_MD, 0, WFPM_SM_E_ATTRI_WO, 0, 0},
	{MDDP_MD_SMEM_USER_RX_REORDER_FROM_MD, 0, WFPM_SM_E_ATTRI_RO, 0, 0},
	{MDDP_MD_SMEM_USER_WIFI_STATISTICS, 0, WFPM_SM_E_ATTRI_RO, 0,
		sizeof(struct mddpw_net_stat_t)},
	{MDDP_MD_SMEM_USER_WIFI_STATISTICS_EXT, 0, WFPM_SM_E_ATTRI_RO, 0,
		sizeof(struct mddpw_net_stat_ext_t)},
	{MDDP_MD_SMEM_USER_SYS_STAT_SYNC, 0, WFPM_SM_E_ATTRI_RW, 0,
		sizeof(struct mddpw_sys_stat_t)},
	{MDDP_MD_SMEM_USER_RESERVE1, 0, WFPM_SM_E_ATTRI_RO, 0, 0},
	{MDDP_MD_SMEM_USER_RESERVE2, 0, WFPM_SM_E_ATTRI_RO, 0, 0},
	{MDDP_MD_SMEM_USER_LLS, 0, WFPM_SM_E_ATTRI_RO, 0,
		sizeof(struct wsvc_stat_lls_report_t)},
};

static struct mddp_ipc_rx_msg_entry_t mddp_rx_msg_table_s[] = {

	{ IPC_MSG_ID_WFPM_ENABLE_MD_FAST_PATH_RSP,
		sizeof(struct wfpm_enable_md_func_rsp_t) },
	{ IPC_MSG_ID_WFPM_DISABLE_MD_FAST_PATH_RSP,
		sizeof(struct wfpm_md_fast_path_common_rsp_t) },
	{ IPC_MSG_ID_WFPM_ACTIVATE_MD_FAST_PATH_RSP,
		sizeof(struct wfpm_md_fast_path_common_rsp_t) },
	{ IPC_MSG_ID_WFPM_MD_NOTIFY,
		sizeof(struct mddpw_md_notify_info_t) },
	{ IPC_MSG_ID_WFPM_DEACTIVATE_MD_FAST_PATH_RSP,
		sizeof(struct wfpm_deactivate_md_func_rsp_t) },
	{ IPC_MSG_ID_MDFPM_SUSPEND_TAG_IND,
		0 },
	{ IPC_MSG_ID_MDFPM_RESUME_TAG_IND,
		0 },
};
static uint32_t mddp_rx_msg_table_cnt = ARRAY_SIZE(mddp_rx_msg_table_s);

static int32_t mddp_ipc_md_smem_layout_config(void)
{
	struct wfpm_smem_info_t    *entry;
	uint32_t                    i;
	uint32_t                    total_len = 0;
	uint8_t                    *addr;
	int                         smem_total_len = 0;

	addr = get_smem_start_addr(MD_SYS1, SMEM_USER_RAW_USB, &smem_total_len);
	if (!addr || smem_total_len <= 0)
		return -EINVAL;

	smem_user_num = MDDP_MD_SMEM_USER_SYS_STAT_SYNC + 1;
	if (smem_total_len >= LLS_SMEM_SIZE) {
		smem_user_num = MDDP_MD_SMEM_USER_NUM;
		lls_mem_exist = 1;
	}

	for (i = 0; i < smem_user_num; i++) {
		entry = &smem_info_s[i];
		if (entry->size > smem_total_len - total_len)
			return -ENOSPC;
		entry->offset = total_len;
		total_len += entry->size;

		if (entry->size > 0)
			memset(addr + entry->offset, 0, entry->size);
	}

	MDDP_C_LOG(MDDP_LL_INFO,
			"%s: smem total_len(%d)!\n", __func__, total_len);
	return 0;
}

static int32_t mddp_ipc_open_port(void)
{
	int32_t                     ret;

	mddp_ipc_tty_port_s = mtk_ccci_request_port(MDDP_IPC_TTY_NAME);

	if (mddp_ipc_tty_port_s < 0) {
		MDDP_C_LOG(MDDP_LL_WARN,
				"%s: Failed to request port(%s, %d)!\n",
				__func__,
				MDDP_IPC_TTY_NAME, mddp_ipc_tty_port_s);
		return -ENODEV;
	}

	ret = mtk_ccci_open_port(mddp_ipc_tty_port_s);
	if (ret < 0) {
		MDDP_C_LOG(MDDP_LL_WARN,
				"%s: Failed to open port(%d)!\n",
				__func__, mddp_ipc_tty_port_s);
		mddp_ipc_tty_port_s = -1;
		return -ENODEV;
	}

	return 0;
}

struct mddp_md_msg_t rx_ctrl_msg;
static int32_t mddp_md_msg_hdlr(void *arg)
{
	int32_t                     rx_count;

	allow_signal(SIGTERM);

	while (!kthread_should_stop()) {
		if (mddp_ipc_tty_port_s < 0) {
			MDDP_C_LOG(MDDP_LL_WARN,
					"%s: ipc_tty_port is invalid(%d)!\n",
					__func__, mddp_ipc_tty_port_s);
			msleep(5000);
			continue;
		}

		rx_count = mtk_ccci_read_data(mddp_ipc_tty_port_s,
				(char *)&(rx_ctrl_msg), sizeof(rx_ctrl_msg));

		if (signal_pending(current))
			break;

		if (rx_count >= (int)MDFPM_CTRL_MSG_HEADER_SZ &&
		    rx_ctrl_msg.data_len <= sizeof(rx_ctrl_msg.data) &&
		    rx_ctrl_msg.data_len <= rx_count - MDFPM_CTRL_MSG_HEADER_SZ) {

			mddp_sm_msg_hdlr(rx_ctrl_msg.dest_user_id, rx_ctrl_msg.msg_id,
					&rx_ctrl_msg.data, rx_ctrl_msg.data_len);
		} else {

			MDDP_C_LOG(MDDP_LL_DEBUG,
					"%s: Failed to read TTY (%d), count(%d)!\n",
					__func__,
					mddp_ipc_tty_port_s, rx_count);
			msleep(1000);
			continue;
		}
	}

	return 0;
}

int32_t mddp_ipc_send_md(
	void *in_app,
	struct mddp_md_msg_t *msg,
	enum mdfpm_user_id_e dest_user)
{
	struct mddp_app_t      *app;
	int32_t                 ret;
	int32_t                 size = MDFPM_CTRL_MSG_HEADER_SZ + msg->data_len;
	int32_t buf_sz = MDFPM_TTY_BUF_SZ_OLD;

	if (!in_app)
		app = mddp_get_default_app_inst();
	else
		app = (struct mddp_app_t *) in_app;

	if (app->state == MDDP_STATE_UNINIT) {
		kfree(msg);
		return -ENODEV;
	}

	if (mddp_check_subfeature(MF_ID_COMMON, COM_LEN2B))
		buf_sz = MDFPM_TTY_BUF_SZ;

	if (msg->data_len > buf_sz) {
		kfree(msg);
		return -EFAULT;
	}
	msg->dest_user_id = (dest_user == MDFPM_USER_ID_NULL)
		? (app->md_cfg.ipc_md_user_id) : (dest_user);

	ret = mtk_ccci_send_data(mddp_ipc_tty_port_s, (char *)msg, size);

	kfree(msg);

	if (unlikely(ret != size)) {
		MDDP_C_LOG(MDDP_LL_WARN,
				"%s: mtk_ccci_send_data error(%d)!\n",
				__func__, ret);
		app->abnormal_flags |= MDDP_ABNORMAL_CCCI_SEND_FAILED;
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

int32_t wfpm_ipc_get_smem_list(void **smem_info_base, uint32_t *smem_num)
{
	*smem_info_base = &smem_info_s;
	*smem_num = smem_user_num;

	return 0;
}

int32_t mddp_ipc_get_md_smem_by_id(enum mddp_md_smem_user_id_e app_id,
		void **smem_addr, uint8_t *smem_attr, uint32_t *smem_size)
{
	struct wfpm_smem_info_t    *smem_entry;
	int                         smem_total_len = 0;

	if ((unsigned int)app_id >= smem_user_num)
		return -EINVAL;

	smem_entry = &smem_info_s[app_id];
	if (!smem_entry->size)
		return -ENOENT;
	*smem_attr = smem_entry->attribute;
	*smem_size = smem_entry->size;

	*smem_addr = (uint8_t *)get_smem_start_addr(MD_SYS1, SMEM_USER_RAW_USB, &smem_total_len);

	if (!*smem_addr || smem_total_len <= 0 ||
	    smem_entry->offset > smem_total_len ||
	    smem_entry->size > smem_total_len - smem_entry->offset)
		return -EINVAL;

	*smem_addr += smem_entry->offset;

	return 0;
}

int32_t mddp_ipc_init(void)
{
	int32_t ret = 0;

	ret = mddp_ipc_md_smem_layout_config();
	if (ret)
		return ret;

	ret = mddp_ipc_open_port();
	if (ret)
		return ret;

	rx_task = kthread_run(mddp_md_msg_hdlr, NULL, "mddp_rx");

	if (IS_ERR(rx_task)) {
		MDDP_C_LOG(MDDP_LL_ERR,
				"%s: kthread_run fail(%li)!\n",
				__func__, PTR_ERR(rx_task));

		rx_task = NULL;
		ret = -ECHILD;
		mtk_ccci_release_port(mddp_ipc_tty_port_s);
		mddp_ipc_tty_port_s = -1;
	}

	return ret;
}

void mddp_ipc_stop(void)
{
	if (rx_task) {
		send_sig(SIGTERM, rx_task, 1);
		kthread_stop(rx_task);
		rx_task = NULL;
	}
}

void mddp_ipc_uninit(void)
{
	mddp_ipc_stop();
	if (mddp_ipc_tty_port_s >= 0) {
		mtk_ccci_release_port(mddp_ipc_tty_port_s);
		mddp_ipc_tty_port_s = -1;
	}
}

bool mddp_ipc_rx_msg_validation(enum MDDP_MDFPM_MSG_ID_CODE msg_id,
		uint32_t msg_len)
{
	uint32_t                i;

	for (i = 0; i < mddp_rx_msg_table_cnt; i++)
		if (mddp_rx_msg_table_s[i].msg_id == msg_id)
			return (mddp_rx_msg_table_s[i].rx_msg_len <= msg_len)
				? (true) : (false);

	return true;
}
