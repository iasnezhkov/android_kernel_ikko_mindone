// SPDX-License-Identifier: GPL-2.0
/*
 * mddp_if.c - Interface API between MDDP and other kernel module.
 *
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include "mddp_ctrl.h"
#include "mddp_debug.h"
#include "mddp_dev.h"
#include "mddp_filter.h"
#include "mddp_sm.h"
#include "mddp_usage.h"

#define MDDP_WIFI_NETIF_ID 0x500 /* copy from MD IPC_NETIF_ID_MCIF_BEGIN */

//------------------------------------------------------------------------------
// Struct definition.
// -----------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Private helper macro.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Private variables.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Private helper macro.
//------------------------------------------------------------------------------
#define MDDP_CHECK_APP_TYPE(_type) ((_type < MDDP_APP_TYPE_CNT) ? (1) : (0))

//------------------------------------------------------------------------------
// Private functions.
//------------------------------------------------------------------------------
static uint32_t mddp_netdev_notifier_is_init;
static int mddp_netdev_notify_cb(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct mddp_app_t *app;
	struct net_device *dev = netdev_notifier_info_to_dev(data);

	if (!mddp_netdev_notifier_is_init)
		return NOTIFY_DONE;

	if (event == NETDEV_UNREGISTER) {
		if (mddp_f_is_support_lan_dev(dev->ifindex) ||
				mddp_f_is_support_wan_dev(dev->ifindex)) {
			app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
			mddp_sm_on_event(app, MDDP_EVT_FUNC_DEACT);
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block mddp_netdev_notifier __read_mostly = {
	.notifier_call = mddp_netdev_notify_cb,
};

void mddp_netdev_notifier_init(void)
{
	if (register_netdevice_notifier(&mddp_netdev_notifier) == 0)
		mddp_netdev_notifier_is_init = 1;
}

void mddp_netdev_notifier_exit(void)
{
	if (mddp_netdev_notifier_is_init) {
		mddp_netdev_notifier_is_init = 0;
		unregister_netdevice_notifier(&mddp_netdev_notifier);
	}
}
//------------------------------------------------------------------------------
// Public functions.
//------------------------------------------------------------------------------
int32_t mddp_drv_attach(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle)
{
	if (MDDP_CHECK_APP_TYPE(conf->app_type) && handle)
		return mddp_sm_reg_callback(conf, handle);

	MDDP_C_LOG(MDDP_LL_WARN,
			"%s: Failed to drv_attach, type(%d), handle(%p)!\n",
			__func__, conf->app_type, handle);

	return -EINVAL;
}
EXPORT_SYMBOL(mddp_drv_attach);

void mddp_drv_detach(
	struct mddp_drv_conf_t *conf,
	struct mddp_drv_handle_t *handle)
{
	if (MDDP_CHECK_APP_TYPE(conf->app_type))
		mddp_sm_dereg_callback(conf, handle);
}
EXPORT_SYMBOL(mddp_drv_detach);

int32_t mddp_on_enable(enum mddp_app_type_e in_type)
{
	struct mddp_app_t      *app;
	uint32_t                type;
	uint8_t                 idx;

	if (in_type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	/*
	 * MDDP ENABLE command.
	 */
	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);
		mddp_sm_wait_pre(app);
		mddp_sm_on_event(app, MDDP_EVT_FUNC_ENABLE);
		mddp_sm_wait(app, MDDP_EVT_FUNC_ENABLE);
	}

	return 0;
}

int32_t mddp_on_disable(enum mddp_app_type_e in_type)
{
	struct mddp_app_t      *app;
	uint32_t                type;
	uint8_t                 idx;

	if (in_type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	/*
	 * MDDP DISABLE command.
	 */
	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);
		mddp_sm_wait_pre(app);
		mddp_sm_on_event(app, MDDP_EVT_FUNC_DISABLE);
		mddp_sm_wait(app, MDDP_EVT_FUNC_DISABLE);
	}

	return 0;
}

int32_t mddp_on_activate(enum mddp_app_type_e type,
		uint8_t *ul_dev_name, uint8_t *dl_dev_name)
{
	struct mddp_app_t      *app;

	// NG. app_type is unknown!
	if (!MDDP_CHECK_APP_TYPE(type))
		return -EINVAL;

	// NG. app is not configured!
	app = mddp_get_app_inst(type);
	if (!app->is_config)
		return -EINVAL;

	if (!mddp_f_dev_add_wan_dev(ul_dev_name))
		return -EINVAL;
	if (!mddp_f_dev_add_lan_dev(dl_dev_name, MDDP_WIFI_NETIF_ID)) {
		mddp_f_dev_del_wan_dev(ul_dev_name);
		return -EINVAL;
	}
	mddp_netdev_notifier_init();

	/*
	 * MDDP ACTIVATE command.
	 */
	strscpy(app->ap_cfg.ul_dev_name, ul_dev_name,
			sizeof(app->ap_cfg.ul_dev_name));
	strscpy(app->ap_cfg.dl_dev_name, dl_dev_name,
			sizeof(app->ap_cfg.dl_dev_name));
	MDDP_C_LOG(MDDP_LL_INFO,
			"%s: type(%d), app(%p), ul(%s), dl(%s).\n",
			__func__, type, app,
			app->ap_cfg.ul_dev_name, app->ap_cfg.dl_dev_name);

	mddp_sm_wait_pre(app);
	mddp_sm_on_event(app, MDDP_EVT_FUNC_ACT);
	mddp_sm_wait(app, MDDP_EVT_FUNC_ACT);
	mddp_u_set_wan_iface(ul_dev_name);

	return 0;
}

int32_t mddp_on_deactivate(enum mddp_app_type_e type)
{
	struct mddp_app_t      *app;

	// NG. app_type is unknown!
	if (!MDDP_CHECK_APP_TYPE(type))
		return -EINVAL;

	// NG. app is not configured!
	app = mddp_get_app_inst(type);
	if (!app->is_config)
		return -EINVAL;

	mddp_netdev_notifier_exit();
	/*
	 * MDDP DEACTIVATE command.
	 */
	mddp_sm_wait_pre(app);
	mddp_sm_on_event(app, MDDP_EVT_FUNC_DEACT);
	mddp_sm_wait(app, MDDP_EVT_FUNC_DEACT);

	return 0;
}

int32_t mddp_on_get_offload_stats(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t *buf_len)
{
	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	/*
	 * MDDP GET_OFFLOAD_STATISTICS command.
	 */
	mddp_u_get_data_stats(buf, buf_len);

	return 0;
}

int32_t mddp_on_set_data_limit(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	ret = mddp_u_set_data_limit(buf, buf_len);

	return ret;
}

int32_t mddp_on_set_warning_and_data_limit(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	ret = mddp_u_set_warning_and_data_limit(buf, buf_len);

	return ret;
}

int32_t mddp_on_set_ct_value(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	/*
	 * MDDP GET_OFFLOAD_STATISTICS command.
	 */
	ret = mddp_f_set_ct_value(buf, buf_len);

	return ret;
}

//------------------------------------------------------------------------------
// Kernel functions.
//------------------------------------------------------------------------------
static int mddp_init_steps;

/*
 * MINDONE mind_one 08.09 (F3970): the modem becomes ready at ~9.4 s, while the module
 * loads at ~3.7 s, so mtk_ccci_request_port("ccci_0_200") is guaranteed to fail at boot.
 * The stock code would then tear itself down entirely and return 0 -- the module stayed
 * in lsmod but was dead forever: /dev/mddp was never created, filters were never set up,
 * and there was no retry on any event. A retry via delayed work is added here.
 */
#define MDDP_IPC_RETRY_MAX       60     /* 60 x 500 ms = up to 30 s waiting for the modem */
#define MDDP_IPC_RETRY_DELAY_MS  500

static struct delayed_work      mddp_ipc_retry_work;
static int                      mddp_ipc_retry_cnt;

/* Teardown WITHOUT cancelling the delayed work -- also fine to call from within the work itself. */
static void mddp_teardown(void)
{
	synchronize_net();

	switch (mddp_init_steps) {
	case 4:
		mddp_filter_uninit();
		fallthrough;
	case 3:
		mddp_dev_uninit();
		fallthrough;
	case 2:
		mddp_ipc_uninit();
		fallthrough;
	case 1:
		mddp_sm_uninit();
		fallthrough;
	default:
		break;
	}
}

/*
 * Cancel the work + tear down. MUST NOT be called from inside mddp_ipc_retry_fn():
 * cancel_delayed_work_sync() from within its own work is a self-deadlock.
 */
static void mddp_exit(void)
{
	cancel_delayed_work_sync(&mddp_ipc_retry_work);
	mddp_teardown();
}

/* Steps 3 and 4 -- shared between the normal path and the retry path. */
static int32_t mddp_init_tail(void)
{
	int32_t         ret;

	ret = mddp_dev_init();
	if (ret < 0)
		return ret;
	mddp_init_steps++;

	ret = mddp_filter_init();
	if (ret < 0)
		return ret;
	mddp_init_steps++;

	return 0;
}

static void mddp_ipc_retry_fn(struct work_struct *work)
{
	int32_t         ret;

	ret = mddp_ipc_init();
	if (ret < 0) {
		if (++mddp_ipc_retry_cnt < MDDP_IPC_RETRY_MAX) {
			schedule_delayed_work(&mddp_ipc_retry_work,
					msecs_to_jiffies(MDDP_IPC_RETRY_DELAY_MS));
			return;
		}
		pr_warn("mddp: ipc did not come up after %d attempts -- offload disabled\n",
				mddp_ipc_retry_cnt);
		mddp_teardown();
		return;
	}

	mddp_init_steps++;      /* ipc ready */
	pr_info("mddp: ipc came up on attempt %d\n", mddp_ipc_retry_cnt + 1);

	if (mddp_init_tail() < 0)
		mddp_teardown();
}

static int __init mddp_init(void)
{
	int32_t         ret = 0;

	/* MINDONE: set up the work item BEFORE any failure branch: mddp_exit() cancels it. */
	INIT_DELAYED_WORK(&mddp_ipc_retry_work, mddp_ipc_retry_fn);

	ret = mddp_sm_init();
	if (ret < 0)
		goto _init_fail;

	mddp_init_steps++;
	ret = mddp_ipc_init();
	if (ret < 0) {
		/* The modem has not come up yet -- do not give up, retry instead (F3970). */
		pr_info("mddp: ipc not available yet, retrying in %d ms\n",
				MDDP_IPC_RETRY_DELAY_MS);
		schedule_delayed_work(&mddp_ipc_retry_work,
				msecs_to_jiffies(MDDP_IPC_RETRY_DELAY_MS));
		return 0;
	}

	mddp_init_steps++;
	if (mddp_init_tail() < 0)
		goto _init_fail;

	return 0;

_init_fail:
	mddp_exit();
	return 0;
}
module_init(mddp_init);
module_exit(mddp_exit);

MODULE_LICENSE("GPL v2");
