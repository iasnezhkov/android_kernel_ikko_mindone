/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/build_bug.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "mddp_ctrl.h"
#include "mddp_if.h"
#include "mddp_debug.h"
#include "mddp_dev.h"
#include "mddp_filter.h"
#include "mddp_sm.h"
#include "mddp_usage.h"

#define MDDP_WIFI_NETIF_ID 0x500

static_assert(sizeof(struct mddp_dev_req_common_t) == 16);
static_assert(sizeof(struct mddp_dev_rsp_common_t) == 16);
static_assert(sizeof(struct mddp_dev_req_act_t) == 32);
static_assert(sizeof(struct mddp_dev_req_set_data_limit_t) == 24);
static_assert(sizeof(struct mddp_dev_req_set_warning_and_data_limit_t) == 32);
static_assert(sizeof(struct mddp_dev_req_set_ct_value_t) == 8);
static_assert(sizeof(struct mddp_dev_ipv4_conntrack_event_t) == 36);
static_assert(sizeof(struct mddp_dev_ipv6_conntrack_event_t) == 52);
static_assert(sizeof(struct mddp_u_data_stats_t) == 128);
static_assert(sizeof(struct mddp_ct_nat_table_t) == 28);

#define MDDP_CHECK_APP_TYPE(_type) ((unsigned int)(_type) < MDDP_APP_TYPE_CNT)

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
	if (!mddp_netdev_notifier_is_init &&
			(register_netdevice_notifier(&mddp_netdev_notifier) == 0)) {
		mddp_netdev_notifier_is_init = 1;
	}
}

void mddp_netdev_notifier_exit(void)
{
	if (mddp_netdev_notifier_is_init) {
		mddp_netdev_notifier_is_init = 0;
		unregister_netdevice_notifier(&mddp_netdev_notifier);
	}
}

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

void mddp_drv_set_ready(struct mddp_drv_conf_t *conf, bool ready)
{
	struct mddp_app_t *app;

	if (!MDDP_CHECK_APP_TYPE(conf->app_type))
		return;
	app = mddp_get_app_inst(conf->app_type);
	mutex_lock(&app->callback_lock);
	app->callbacks_ready = ready;
	mutex_unlock(&app->callback_lock);
}
EXPORT_SYMBOL(mddp_drv_set_ready);

int32_t mddp_on_enable(enum mddp_app_type_e in_type)
{
	struct mddp_app_t *app;
	uint32_t type, reset_seq;
	uint8_t idx;
	int ret;
	bool enabled = false;

	if (in_type != MDDP_APP_TYPE_ALL)
		return -EINVAL;
	mddpwh_sm_drain();
	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);
		if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
			continue;
		reset_seq = READ_ONCE(app->reset_seq);
		ret = mddp_sm_begin(app, MDDP_EVT_FUNC_ENABLE, reset_seq);
		if (ret)
			return ret;
		ret = mddp_sm_wait(app, MDDP_EVT_FUNC_ENABLE, reset_seq);
		if (ret)
			return ret;
		if (type == MDDP_APP_TYPE_WH && READ_ONCE(app->state) == MDDP_STATE_DEACTIVATED)
			enabled = true;
	}
	return enabled ? 0 : -ENODEV;
}

int32_t mddp_on_activate(enum mddp_app_type_e type,
		uint8_t *ul_dev_name, uint8_t *dl_dev_name)
{
	struct mddp_app_t *app;
	uint32_t reset_seq;
	int ret, work_ret;

	if (type == MDDP_APP_TYPE_RESERVED_1)
		return -EOPNOTSUPP;
	if (!MDDP_CHECK_APP_TYPE(type))
		return -EINVAL;
	app = mddp_get_app_inst(type);
	if (!app->is_config)
		return -EINVAL;
	if (!READ_ONCE(mddp_connection_base_activated_s))
		return -EOPNOTSUPP;
	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;
	mddpwh_sm_drain();
	reset_seq = READ_ONCE(app->reset_seq);
	mutex_lock(&app->resource_lock);
	if (mddp_f_dev_is_wan_lan_dev()) {
		ret = -EBUSY;
		goto unlock;
	}
	if (!mddp_f_dev_add_wan_dev(ul_dev_name)) {
		ret = -EINVAL;
		goto unlock;
	}
	if (!mddp_f_dev_add_lan_dev(dl_dev_name, MDDP_WIFI_NETIF_ID)) {
		mddp_f_dev_del_wan_dev(ul_dev_name);
		ret = -EINVAL;
		goto unlock;
	}
	strscpy(app->ap_cfg.ul_dev_name, ul_dev_name, sizeof(app->ap_cfg.ul_dev_name));
	strscpy(app->ap_cfg.dl_dev_name, dl_dev_name, sizeof(app->ap_cfg.dl_dev_name));
	mddp_netdev_notifier_init();
	ret = mddp_sm_begin(app, MDDP_EVT_FUNC_ACT, reset_seq);
	if (ret) {
		mddp_netdev_notifier_exit();
		mddp_f_dev_del_wan_dev(ul_dev_name);
		mddp_f_dev_del_lan_dev(dl_dev_name);
	}
unlock:
	mutex_unlock(&app->resource_lock);
	if (ret)
		return ret;
	ret = mddp_sm_wait(app, MDDP_EVT_FUNC_ACT, reset_seq);
	work_ret = mddp_sm_wait_work(app, reset_seq);
	if (!ret)
		ret = work_ret;
	if (!ret && READ_ONCE(app->state) != MDDP_STATE_ACTIVATED)
		ret = -EIO;
	if (ret) {
		mutex_lock(&app->resource_lock);
		mddp_netdev_notifier_exit();
		mutex_unlock(&app->resource_lock);
		return ret;
	}
	mddp_u_set_wan_iface(ul_dev_name);
	return 0;
}

int32_t mddp_on_deactivate(enum mddp_app_type_e type)
{
	struct mddp_app_t *app;
	uint32_t reset_seq;
	int ret, work_ret;

	if (type == MDDP_APP_TYPE_RESERVED_1)
		return -EOPNOTSUPP;
	if (!MDDP_CHECK_APP_TYPE(type))
		return -EINVAL;
	app = mddp_get_app_inst(type);
	if (!app->is_config)
		return -EINVAL;
	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;
	mddpwh_sm_drain();
	reset_seq = READ_ONCE(app->reset_seq);
	mddp_u_clear_quota();
	mutex_lock(&app->resource_lock);
	mddp_netdev_notifier_exit();
	ret = mddp_sm_begin(app, MDDP_EVT_FUNC_DEACT, reset_seq);
	mutex_unlock(&app->resource_lock);
	if (ret)
		return ret;
	ret = mddp_sm_wait(app, MDDP_EVT_FUNC_DEACT, reset_seq);
	work_ret = mddp_sm_wait_work(app, reset_seq);
	return ret ? ret : work_ret;
}

int32_t mddp_on_disable(enum mddp_app_type_e in_type)
{
	struct mddp_app_t *app;
	uint32_t type, reset_seq;
	uint8_t idx;
	int ret = 0, step;

	if (in_type != MDDP_APP_TYPE_ALL)
		return -EINVAL;
	for (idx = 0; idx < MDDP_MOD_CNT; idx++)
		mddp_sm_disable_requested(mddp_get_app_inst(mddp_sm_module_list_s[idx]));
	mddpwh_sm_drain();
	mddp_u_clear_quota();
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (READ_ONCE(app->state) == MDDP_STATE_ACTIVATED ||
	    READ_ONCE(app->state) == MDDP_STATE_DEACTIVATED)
		ret = mddp_on_deactivate(MDDP_APP_TYPE_WH);
	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);
		reset_seq = READ_ONCE(app->reset_seq);
		step = mddp_sm_begin(app, MDDP_EVT_FUNC_DISABLE, reset_seq);
		if (!step)
			step = mddp_sm_wait(app, MDDP_EVT_FUNC_DISABLE, reset_seq);
		if (!ret)
			ret = step;
	}
	mddpwh_sm_drain();
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	mutex_lock(&app->resource_lock);
	mddp_netdev_notifier_exit();
	mddp_f_dev_del_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_del_lan_dev(app->ap_cfg.dl_dev_name);
	mutex_unlock(&app->resource_lock);
	return ret;
}

int32_t mddp_on_get_offload_stats(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t *buf_len)
{
	struct mddp_app_t      *app;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (!app->is_config)
		return -EINVAL;

	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;

	mddp_u_get_data_stats(buf, buf_len);

	return 0;
}

int32_t mddp_on_set_data_limit(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	struct mddp_app_t      *app;
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (!app->is_config)
		return -EINVAL;

	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;

	ret = mddp_u_set_data_limit(buf, buf_len);

	return ret;
}

int32_t mddp_on_set_warning_and_data_limit(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	struct mddp_app_t      *app;
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (!app->is_config)
		return -EINVAL;

	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;

	ret = mddp_u_set_warning_and_data_limit(buf, buf_len);

	return ret;
}

int32_t mddp_on_set_ct_value(
		enum mddp_app_type_e type,
		uint8_t *buf,
		uint32_t buf_len)
{
	struct mddp_app_t      *app;
	int32_t                 ret;

	if (type != MDDP_APP_TYPE_ALL)
		return -EINVAL;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);
	if (!app->is_config)
		return -EINVAL;

	if (!(app->feature & MDDP_FEATURE_MDDP_WH) || !app->drv_reg)
		return -ENOENT;

	ret = mddp_f_set_ct_value(buf, buf_len);

	return ret;
}

static int mddp_init_steps;
static void mddp_exit(void)
{
	if (mddp_init_steps >= 5) {
		mddp_dev_unpublish();
		mddp_on_disable(MDDP_APP_TYPE_ALL);
	}
	if (mddp_init_steps >= 1)
		mddp_sm_begin_shutdown();
	mddp_netdev_notifier_exit();
	if (mddp_init_steps >= 4)
		mddp_ipc_stop();
	if (mddp_init_steps >= 1)
		mddpwh_sm_quiesce();
	if (mddp_init_steps >= 4)
		mddp_ipc_uninit();
	if (mddp_init_steps >= 3)
		mddp_filter_uninit();
	if (mddp_init_steps >= 2)
		mddp_dev_uninit();
	if (mddp_init_steps >= 1)
		mddp_sm_uninit();
	mddp_init_steps = 0;
}

static int __init mddp_init(void)
{
	int ret;

	ret = mddp_sm_init();
	if (ret)
		return ret;
	mddp_init_steps = 1;
	mddp_dev_prepare();
	mddp_init_steps = 2;
	ret = mddp_filter_init();
	if (ret)
		goto fail;
	mddp_init_steps = 3;
	ret = mddp_ipc_init();
	if (ret)
		goto fail;
	mddp_init_steps = 4;
	ret = mddp_dev_init();
	if (ret)
		goto fail;
	mddp_init_steps = 5;
	return 0;

fail:
	mddp_exit();
	return ret;
}
module_init(mddp_init);
module_exit(mddp_exit);

MODULE_LICENSE("GPL v2");
