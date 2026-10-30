/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#ifndef __MDDP_SM_H
#define __MDDP_SM_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "mddp_export.h"
#include "mddp_ipc.h"

#define MDDP_ABNORMAL_CCCI_SEND_FAILED                      (1U << 0)
#define MDDP_ABNORMAL_CHECK_FEATURE_ABSENT                  (1U << 1)
#define MDDP_ABNORMAL_WIFI_DRV_GET_FEATURE_BEFORE_MD_READY  (1U << 2)
#define MDFPM_SEND_LOG_HEADER 3
#define MDFPM_SEND_LOG_BUF_SZ (MDFPM_TTY_BUF_SZ - MDFPM_SEND_LOG_HEADER)

enum mddp_event_e {
	MDDP_EVT_FUNC_ENABLE,
	MDDP_EVT_FUNC_DISABLE,
	MDDP_EVT_FUNC_ACT,
	MDDP_EVT_FUNC_DEACT,

	MDDP_EVT_MD_RSP_OK,
	MDDP_EVT_MD_RSP_TIMEOUT,

	MDDP_EVT_MD_RESET,

	MDDP_EVT_CNT,
	MDDP_EVT_DUMMY = 0x7fff
};

enum mddp_sysfs_cmd_e {
	MDDP_SYSFS_CMD_STATISTIC_READ,

#ifdef MDDP_EM_SUPPORT
	MDDP_SYSFS_EM_CMD_TEST_READ,
	MDDP_SYSFS_EM_CMD_TEST_WRITE,
#endif
};

struct mddp_app_t;
typedef void (*mddp_sm_action_t)(struct mddp_app_t *);
typedef int32_t (*mddp_md_recv_msg_hdlr_t)(uint32_t msg_id,
		void *buf, uint32_t buf_len);
typedef int32_t (*mddp_reg_drv_cbf_t)(struct mddp_drv_handle_t *);
typedef ssize_t (*mddp_sysfs_cbf_t)(struct mddp_app_t *app,
				    enum mddp_sysfs_cmd_e,
				    char *buf, size_t buf_len);

struct mddp_sm_entry_t {
	enum mddp_event_e       event;
	enum mddp_state_e       new_state;
	mddp_sm_action_t        action;
};

struct mddp_ap_cfg_t {
	uint8_t         ul_dev_name[IFNAMSIZ];
	uint8_t         dl_dev_name[IFNAMSIZ];
};

struct mddp_md_cfg_t {
	uint32_t        ipc_ap_mod_id;
	enum mdfpm_user_id_e ipc_md_user_id;
};

struct mddp_app_t {
	uint32_t                    is_config;
	enum mddp_app_type_e        type;
	enum mddp_state_e           state;
	spinlock_t                  locker;

	struct mddp_ap_cfg_t        ap_cfg;
	struct mddp_md_cfg_t        md_cfg;

	mddp_md_recv_msg_hdlr_t     md_recv_msg_hdlr;

	mddp_reg_drv_cbf_t          reg_drv_callback;
	mddp_reg_drv_cbf_t          dereg_drv_callback;
	struct mddp_drv_handle_t    drv_hdlr;

	mddp_sysfs_cbf_t            sysfs_callback;

	struct mddp_sm_entry_t     *state_machines[MDDP_STATE_CNT];
	uint32_t                    drv_reg;
	uint32_t                    feature;
	struct mddp_feature         mddp_feat;
	uint32_t                    abnormal_flags;
	uint32_t                    reset_cnt;
	uint32_t                    reset_seq;
	bool                        reset_pending;
	bool                        sync_lost;
	bool                        desired_enabled;
	bool                        callbacks_ready;
	struct mutex                resource_lock;
	struct mutex                callback_lock;
	struct delayed_work         timeout_work;
	enum mddp_state_e            timeout_state;
	uint32_t                    timeout_seq;
	unsigned long               timeout_deadline;
	int32_t                     ipc_send_status;
	int32_t                     transaction_status;
	struct completion           md_resp_comp;
	struct completion           work_comp;
};

typedef uint16_t MDDP_MOD_TYPE;

static const MDDP_MOD_TYPE mddp_sm_module_list_s[] = {
	#undef MDDP_MODULE_ID
	#undef MDDP_MODULE_PREFIX
	#define MDDP_MODULE_ID(_id) _id,
	#define MDDP_MODULE_PREFIX(_prefix)
	#include "mddp_app_config.h"
};

#define MDDP_MOD_CNT    (sizeof(mddp_sm_module_list_s) / sizeof(MDDP_MOD_TYPE))

int32_t mddpu_sm_init(struct mddp_app_t *app);
int32_t mddpwh_sm_init(struct mddp_app_t *app);
void mddpw_notify_wlan_mdinfo(void);

int32_t mddp_sm_init(void);
void mddp_sm_uninit(void);
void mddp_sm_begin_shutdown(void);
void mddpwh_sm_quiesce(void);
void mddpwh_sm_drain(void);
void mddp_sm_disable_requested(struct mddp_app_t *app);
void mddp_sm_finish_reset(struct mddp_app_t *app, uint32_t reset_seq);
struct mddp_app_t *mddp_get_default_app_inst(void);
enum mddp_state_e mddp_sm_set_state_by_md_rsp(struct mddp_app_t *app,
		enum mddp_state_e new_state, bool md_rsp_result);
#ifdef __MDDP_DEBUG__
void mddp_dump_sm_table(struct mddp_app_t *app);
#else
#define mddp_dump_sm_table(...)
#endif
enum mddp_state_e mddp_sm_on_event(struct mddp_app_t *app, enum mddp_event_e event);
int mddp_sm_begin(struct mddp_app_t *app, enum mddp_event_e event,
		uint32_t reset_seq);
int mddp_sm_wait(struct mddp_app_t *app, enum mddp_event_e event,
		uint32_t reset_seq);
int mddp_sm_wait_work(struct mddp_app_t *app, uint32_t reset_seq);

void mddp_check_feature(void);
bool mddp_check_subfeature(int type, int feat);

int32_t mddp_sm_msg_hdlr(uint32_t user_id,
		uint32_t msg_id, void *buf, uint32_t buf_len);
int32_t mddp_sm_reg_callback(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle);
void mddp_sm_dereg_callback(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle);
void mddp_netdev_notifier_exit(void);

struct mdfpm_log {
	uint16_t action_id;
	uint8_t buffer_len;
	uint8_t buf[MDFPM_SEND_LOG_BUF_SZ];
};

enum mdfpm_log_action_id {
	MDFPM_LOG_NONE,
	MDFPM_LOG_MDDP_WH_RUN,
	MDFPM_LOG_MDDP_WH_STOP,
	MDFPM_LOG_MDDP_EM_TEST,
	MDFPM_LOG_MDDP_WH_LOCK_MD,
	MDFPM_LOG_MDDP_WH_RM_BY_REQ,
	MDFPM_LOG_MDDP_WH_RM_BY_ASSIGN,
	MDFPM_LOG_MDDP_WH_RM_BY_SCORE,
	MDFPM_LOG_MD_ADD_FILTER_V4,
	MDFPM_LOG_MD_DEL_FILTER_V4,
	MDFPM_LOG_CS_ADD_FILTER_V4,
	MDFPM_LOG_CS_DEL_FILTER_V4,
	MDFPM_LOG_MD_ADD_FILTER_V6,
	MDFPM_LOG_MD_DEL_FILTER_V6,
	MDFPM_LOG_CS_ADD_FILTER_V6,
	MDFPM_LOG_CS_DEL_FILTER_V6,
	MDFPM_LOG_NUM,
};
#endif
