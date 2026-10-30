// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2016 MediaTek Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
/*
 ** Id: @(#) gl_p2p.c@@
 */

/*! \file   gl_p2p.c
 *    \brief  Main routines of Linux driver interface for Wi-Fi Direct
 *
 *    This file contains the main routines of Linux driver
 *    for MediaTek Inc. 802.11 Wireless LAN Adapters.
 */


/******************************************************************************
 *                         C O M P I L E R   F L A G S
 ******************************************************************************
 */

/******************************************************************************
 *                    E X T E R N A L   R E F E R E N C E S
 ******************************************************************************
 */

#include <linux/poll.h>

#include <linux/kmod.h>


#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"

/* #include <net/cfg80211.h> */
#include "gl_p2p_ioctl.h"

#include "precomp.h"
#include "gl_vendor.h"
#include "gl_cfg80211.h"
#include "mddp.h"
/******************************************************************************
 *                              C O N S T A N T S
 ******************************************************************************
 */
#define ARGV_MAX_NUM        (4)

/*For CFG80211 - wiphy parameters*/
#define MAX_SCAN_LIST_NUM   (1)
#define MAX_SCAN_IE_LEN     (512)

#if 0
#define RUNNING_P2P_MODE 0
#define RUNNING_AP_MODE 1
#define RUNNING_DUAL_AP_MODE 2
#endif
/******************************************************************************
 *                             D A T A   T Y P E S
 ******************************************************************************
 */

/******************************************************************************
 *                            P U B L I C   D A T A
 ******************************************************************************
 */


/******************************************************************************
 *                           P R I V A T E   D A T A
 ******************************************************************************
 */

struct net_device *g_P2pPrDev;
struct wireless_dev *gprP2pWdev[KAL_P2P_NUM];
struct wireless_dev *gprP2pRoleWdev[KAL_P2P_NUM];
struct net_device *gPrP2pDev[KAL_P2P_NUM];
uint32_t g_u4DevIdx[KAL_P2P_NUM];

#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
#if (CFG_ENABLE_UNIFY_WIPHY == 0)
static struct cfg80211_ops mtk_p2p_ops = {
#if (CFG_ENABLE_WIFI_DIRECT_CFG_80211 != 0)
	/* Froyo */
	.add_virtual_intf = mtk_p2p_cfg80211_add_iface,
	.change_virtual_intf = mtk_p2p_cfg80211_change_iface,	/* 1 st */
	.del_virtual_intf = mtk_p2p_cfg80211_del_iface,
	.change_bss = mtk_p2p_cfg80211_change_bss,
	.scan = mtk_p2p_cfg80211_scan,
	.abort_scan = mtk_p2p_cfg80211_abort_scan,
	.remain_on_channel = mtk_p2p_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = mtk_p2p_cfg80211_cancel_remain_on_channel,
	.mgmt_tx = mtk_p2p_cfg80211_mgmt_tx,
	.mgmt_tx_cancel_wait = mtk_p2p_cfg80211_mgmt_tx_cancel_wait,
	.connect = mtk_p2p_cfg80211_connect,
	.disconnect = mtk_p2p_cfg80211_disconnect,
	.deauth = mtk_p2p_cfg80211_deauth,
	.disassoc = mtk_p2p_cfg80211_disassoc,
	.start_ap = mtk_p2p_cfg80211_start_ap,
	.change_beacon = mtk_p2p_cfg80211_change_beacon,
	.stop_ap = mtk_p2p_cfg80211_stop_ap,
	.set_wiphy_params = mtk_p2p_cfg80211_set_wiphy_params,
	.del_station = mtk_p2p_cfg80211_del_station,
	.set_bitrate_mask = mtk_p2p_cfg80211_set_bitrate_mask,
	.mgmt_frame_register = mtk_p2p_cfg80211_mgmt_frame_register,
	.get_station = mtk_p2p_cfg80211_get_station,
	.add_key = mtk_p2p_cfg80211_add_key,
	.get_key = mtk_p2p_cfg80211_get_key,
	.del_key = mtk_p2p_cfg80211_del_key,
	.set_default_key = mtk_p2p_cfg80211_set_default_key,
	.set_default_mgmt_key = mtk_p2p_cfg80211_set_mgmt_key,
	.join_ibss = mtk_p2p_cfg80211_join_ibss,
	.leave_ibss = mtk_p2p_cfg80211_leave_ibss,
	.set_tx_power = mtk_p2p_cfg80211_set_txpower,
	.get_tx_power = mtk_p2p_cfg80211_get_txpower,
	.set_power_mgmt = mtk_p2p_cfg80211_set_power_mgmt,
#if (CFG_SUPPORT_DFS_MASTER == 1)
	.start_radar_detection = mtk_p2p_cfg80211_start_radar_detection,
	.channel_switch = mtk_p2p_cfg80211_channel_switch,
#endif
#ifdef CONFIG_NL80211_TESTMODE
	.testmode_cmd = mtk_p2p_cfg80211_testmode_cmd,
#endif
#endif
};

static const struct wiphy_vendor_command mtk_p2p_vendor_ops[] = {
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_SUBCMD_GET_CHANNEL_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = mtk_cfg80211_vendor_get_channel_list
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_SUBCMD_SET_COUNTRY_CODE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = mtk_cfg80211_vendor_set_country_code
	},
#if CFG_SUPPORT_P2P_PREFERRED_FREQ_LIST
	/* P2P get preferred freq list */
	{
		{
			.vendor_id = OUI_QCA,
			.subcmd = NL80211_VENDOR_SUBCMD_GET_PREFER_FREQ_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV
				| WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mtk_cfg80211_vendor_get_preferred_freq_list
	},
#endif /* CFG_SUPPORT_P2P_PREFERRED_FREQ_LIST */
#if CFG_AUTO_CHANNEL_SEL_SUPPORT
	{
		{
			.vendor_id = OUI_QCA,
			.subcmd = NL80211_VENDOR_SUBCMD_ACS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV
				| WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mtk_cfg80211_vendor_acs
	},
#endif
#if CFG_SUPPORT_DFS_MASTER
	{
		{
			.vendor_id = OUI_QCA,
			.subcmd = NL80211_VENDOR_SUBCMD_DFS_CAPABILITY
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = mtk_cfg80211_vendor_dfs_capability
	},
#endif
	{
		{
			.vendor_id = OUI_QCA,
			.subcmd = NL80211_VENDOR_SUBCMD_GET_FEATURES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV
				| WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = mtk_cfg80211_vendor_get_features
	},
};

static const struct nl80211_vendor_cmd_info mtk_p2p_vendor_events[] = {
#if CFG_AUTO_CHANNEL_SEL_SUPPORT
	{
		.vendor_id = OUI_QCA,
		.subcmd = NL80211_VENDOR_SUBCMD_ACS
	},
#endif
};



/* There isn't a lot of sense in it, but you can transmit anything you like */
static const struct ieee80211_txrx_stypes
mtk_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
			.tx = 0xffff,
			.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
			},
	[NL80211_IFTYPE_STATION] = {
			.tx = 0xffff,
			.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
				| BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
			},
	[NL80211_IFTYPE_AP] = {
			.tx = 0xffff,
			.rx = BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
				| BIT(IEEE80211_STYPE_ACTION >> 4)
#if CFG_SUPPORT_SOFTAP_WPA3
				| BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
				  BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
				  BIT(IEEE80211_STYPE_DISASSOC >> 4) |
				  BIT(IEEE80211_STYPE_AUTH >> 4) |
				  BIT(IEEE80211_STYPE_DEAUTH >> 4)
#endif
			},
	[NL80211_IFTYPE_AP_VLAN] = {
			/* copy AP */
			.tx = 0xffff,
		    .rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
				BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
				BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
				BIT(IEEE80211_STYPE_DISASSOC >> 4) |
				BIT(IEEE80211_STYPE_AUTH >> 4) |
				BIT(IEEE80211_STYPE_DEAUTH >> 4) |
				BIT(IEEE80211_STYPE_ACTION >> 4)
			},
	[NL80211_IFTYPE_P2P_CLIENT] = {
			.tx = 0xffff,
			.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
				| BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
			},
	[NL80211_IFTYPE_P2P_GO] = {
			.tx = 0xffff,
			.rx = BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
				| BIT(IEEE80211_STYPE_ACTION >> 4)
			}
};

#endif
#endif

static const struct iw_priv_args rP2PIwPrivTable[] = {
	{
	 .cmd = IOC_P2P_CFG_DEVICE,
	 .set_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_cfg_device_type),
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_CFG_DEVICE"}
	,
	{
	 .cmd = IOC_P2P_START_STOP_DISCOVERY,
	 .set_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_req_device_type),
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_DISCOVERY"}
	,
	{
	 .cmd = IOC_P2P_DISCOVERY_RESULTS,
	 .set_args = IW_PRIV_TYPE_NONE,
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_RESULT"}
	,
	{
	 .cmd = IOC_P2P_WSC_BEACON_PROBE_RSP_IE,
	 .set_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_hostapd_param),
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_WSC_IE"}
	,
	{
	 .cmd = IOC_P2P_CONNECT_DISCONNECT,
	 .set_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_connect_device),
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_CONNECT"}
	,
	{
	 .cmd = IOC_P2P_PASSWORD_READY,
	 .set_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_password_ready),
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_PASSWD_RDY"}
	,
	{
	 .cmd = IOC_P2P_GET_STRUCT,
	 .set_args = IW_PRIV_TYPE_NONE,
	 .get_args = 256,
	 .name = "P2P_GET_STRUCT"}
	,
	{
	 .cmd = IOC_P2P_SET_STRUCT,
	 .set_args = 256,
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "P2P_SET_STRUCT"}
	,
	{
	 .cmd = IOC_P2P_GET_REQ_DEVICE_INFO,
	 .set_args = IW_PRIV_TYPE_NONE,
	 .get_args = IW_PRIV_TYPE_BYTE
				| (__u16) sizeof(struct iw_p2p_device_req),
	 .name = "P2P_GET_REQDEV"}
	,
	{
	 /* SET STRUCT sub-ioctls commands */
	 .cmd = PRIV_CMD_OID,
	 .set_args = 256,
	 .get_args = IW_PRIV_TYPE_NONE,
	 .name = "set_oid"}
	,
	{
	 /* GET STRUCT sub-ioctls commands */
	 .cmd = PRIV_CMD_OID,
	 .set_args = IW_PRIV_TYPE_NONE,
	 .get_args = 256,
	 .name = "get_oid"}
};

#if 0
const struct iw_handler_def mtk_p2p_wext_handler_def = {
	.num_standard = (__u16) sizeof(rP2PIwStandardHandler)
					/ sizeof(iw_handler),
/* .num_private        = (__u16)sizeof(rP2PIwPrivHandler)/sizeof(iw_handler), */
	.num_private_args = (__u16) sizeof(rP2PIwPrivTable)
					/ sizeof(struct iw_priv_args),
	.standard = rP2PIwStandardHandler,
/* .private            = rP2PIwPrivHandler, */
	.private_args = rP2PIwPrivTable,
#if CFG_SUPPORT_P2P_RSSI_QUERY
	.get_wireless_stats = mtk_p2p_wext_get_wireless_stats,
#else
	.get_wireless_stats = NULL,
#endif
};
#endif

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support mtk_p2p_wowlan_support = {
	.flags = WIPHY_WOWLAN_DISCONNECT | WIPHY_WOWLAN_ANY,
};
#endif

static const struct ieee80211_iface_limit mtk_p2p_sta_go_limits[] = {
	{
		.max = 3,
		.types = BIT(NL80211_IFTYPE_STATION),
	},

	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_GO)
				| BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
};

#if (CFG_SUPPORT_DFS_MASTER == 1)
#endif

static const struct ieee80211_iface_combination
mtk_iface_combinations_sta[] = {
	{
#ifdef CFG_NUM_DIFFERENT_CHANNELS_STA
		.num_different_channels = CFG_NUM_DIFFERENT_CHANNELS_STA,
#else
		.num_different_channels = 2,
#endif /* CFG_NUM_DIFFERENT_CHANNELS_STA */
		.max_interfaces = 3,
		/*.beacon_int_infra_match = true,*/
		.limits = mtk_p2p_sta_go_limits,
		.n_limits = 1, /* include p2p */
	},
};

static const struct ieee80211_iface_combination
mtk_iface_combinations_p2p[] = {
	{
#if CFG_ENABLE_UNIFY_WIPHY
		/* The 2 MCC channels case has been verified */
		.num_different_channels = 2,
#elif defined(CFG_NUM_DIFFERENT_CHANNELS_P2P)
		.num_different_channels = CFG_NUM_DIFFERENT_CHANNELS_P2P,
#else
		.num_different_channels = 2,
#endif /* CFG_NUM_DIFFERENT_CHANNELS_P2P */
		.max_interfaces = 3,
		/*.beacon_int_infra_match = true,*/
		.limits = mtk_p2p_sta_go_limits,
		.n_limits = ARRAY_SIZE(mtk_p2p_sta_go_limits), /* include p2p */
	},
#if (CFG_SUPPORT_DFS_MASTER == 1)
#endif
};


const struct ieee80211_iface_combination
	*p_mtk_iface_combinations_sta = mtk_iface_combinations_sta;
const int32_t mtk_iface_combinations_sta_num =
	ARRAY_SIZE(mtk_iface_combinations_sta);

const struct ieee80211_iface_combination
	*p_mtk_iface_combinations_p2p = mtk_iface_combinations_p2p;
const int32_t mtk_iface_combinations_p2p_num =
	ARRAY_SIZE(mtk_iface_combinations_p2p);

/******************************************************************************
 *                                 M A C R O S
 ******************************************************************************
 */

/******************************************************************************
 *                   F U N C T I O N   D E C L A R A T I O N S
 ******************************************************************************
 */

/* Net Device Hooks */
static int p2pOpen(IN struct net_device *prDev);

static int p2pStop(IN struct net_device *prDev);

static struct net_device_stats *p2pGetStats(IN struct net_device *prDev);

static void p2pSetMulticastList(IN struct net_device *prDev);

static netdev_tx_t p2pHardStartXmit(IN struct sk_buff *prSkb,
		IN struct net_device *prDev);

static int p2pSetMACAddress(IN struct net_device *prDev, void *addr);

static int p2pDoIOCTL(struct net_device *prDev,
		struct ifreq *prIFReq,
		int i4Cmd);

#if CFG_SUPPORT_RX_GRO
/*----------------------------------------------------------------------------*/
/*!
 * \brief A method of callback function for napi struct
 *
 * It just return false because driver indicate Rx packet directly.
 *
 * \param[in] napi      Pointer to struct napi_struct.
 * \param[in] budget    Polling time interval.
 *
 * \return false
 */
/*----------------------------------------------------------------------------*/
static int p2p_napi_poll(struct napi_struct *napi, int budget)
{
	return 0;
}
#endif

/*---------------------------------------------------------------------------*/
/*!
 * \brief A function for prDev->init
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0         The execution of wlanInit succeeds.
 * \retval -ENXIO    No such device.
 */
/*---------------------------------------------------------------------------*/
static int p2pInit(struct net_device *prDev)
{
#if CFG_SUPPORT_RX_GRO
	struct NETDEV_PRIVATE_GLUE_INFO *prNetDevPrivate = NULL;
#endif
	if (!prDev)
		return -ENXIO;
#if CFG_SUPPORT_RX_GRO
	prNetDevPrivate = (struct NETDEV_PRIVATE_GLUE_INFO *)
		netdev_priv(prDev);
	prDev->features |= NETIF_F_GRO;
	prDev->hw_features |= NETIF_F_GRO;
	spin_lock_init(&prNetDevPrivate->napi_spinlock);
	prNetDevPrivate->napi.dev = prDev;
	netif_napi_add(prNetDevPrivate->napi.dev,
		&prNetDevPrivate->napi, p2p_napi_poll);
	DBGLOG(INIT, TRACE,
		"GRO interface added successfully:%p\n", prDev);
#endif
	return 0;		/* success */
}				/* end of p2pInit() */

/*---------------------------------------------------------------------------*/
/*!
 * \brief A function for prDev->uninit
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static void p2pUninit(IN struct net_device *prDev)
{
}				/* end of p2pUninit() */

static int p2p_siocdevprivate(struct net_device *dev, struct ifreq *ifr,
				  void __user *data, int cmd)
{
	return p2pDoIOCTL(dev, ifr, cmd);
}

const struct net_device_ops p2p_netdev_ops = {
	.ndo_open = p2pOpen,
	.ndo_stop = p2pStop,
	.ndo_set_mac_address = p2pSetMACAddress,
	.ndo_set_rx_mode = p2pSetMulticastList,
	.ndo_get_stats = p2pGetStats,
	.ndo_do_ioctl = p2pDoIOCTL,
	.ndo_siocdevprivate = p2p_siocdevprivate,
	.ndo_start_xmit = p2pHardStartXmit,
	/* .ndo_select_queue       = p2pSelectQueue, */
	.ndo_select_queue = wlanSelectQueue,
	.ndo_init = p2pInit,
	.ndo_uninit = p2pUninit,
};

/******************************************************************************
 *                              F U N C T I O N S
 ******************************************************************************
 */

/*---------------------------------------------------------------------------*/
/*!
 * \brief Allocate memory for P2P_INFO, GL_P2P_INFO, P2P_CONNECTION_SETTINGS
 *                                          P2P_SPECIFIC_BSS_INFO, P2P_FSM_INFO
 *
 * \param[in] prGlueInfo      Pointer to glue info
 *
 * \return   TRUE
 *           FALSE
 */
/*---------------------------------------------------------------------------*/
u_int8_t p2PAllocInfo(IN struct GLUE_INFO *prGlueInfo, IN uint8_t ucIdex, struct GL_P2P_INFO **pprInfo)
{
	struct ADAPTER *prAdapter;
	struct WIFI_VAR *prWifiVar;
	uint8_t ucRoleIdx;
	struct GL_P2P_INFO *prRole = NULL;
	struct GL_P2P_DEV_INFO *prNewDevice = NULL;
	struct P2P_INFO *prNewInfo = NULL;
	struct P2P_DEV_FSM_INFO *prNewDevFsm = NULL;
	struct P2P_CONNECTION_SETTINGS *prSettings = NULL;
	struct P2P_SPECIFIC_BSS_INFO *prSpecific = NULL;
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	struct PARAM_GET_STA_STATISTICS *prStatistics = NULL;
#endif

	if (!pprInfo)
		return FALSE;
	*pprInfo = NULL;
	if (!prGlueInfo || !prGlueInfo->prAdapter || ucIdex >= KAL_P2P_NUM)
		return FALSE;
	prAdapter = prGlueInfo->prAdapter;
	prWifiVar = &prAdapter->rWifiVar;

	for (ucRoleIdx = 0; ucRoleIdx < KAL_P2P_NUM; ucRoleIdx++) {
		if (!prGlueInfo->prP2PInfo[ucRoleIdx])
			continue;
		if (!prGlueInfo->prP2PDevInfo || !prAdapter->prP2pInfo ||
		    !prWifiVar->prP2pDevFsmInfo ||
		    !prWifiVar->prP2PConnSettings[ucRoleIdx] ||
		    !prWifiVar->prP2pSpecificBssInfo[ucRoleIdx]
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
		    || !prWifiVar->prP2pQueryStaStatistics[ucRoleIdx]
#endif
		   )
			return FALSE;
	}
	if (prGlueInfo->prP2PInfo[ucIdex]) {
		*pprInfo = prGlueInfo->prP2PInfo[ucIdex];
		return TRUE;
	}
	if (prWifiVar->prP2PConnSettings[ucIdex] ||
	    prWifiVar->prP2pSpecificBssInfo[ucIdex]
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	    || prWifiVar->prP2pQueryStaStatistics[ucIdex]
#endif
	   )
		return FALSE;

	prRole = kalMemAlloc(sizeof(*prRole), VIR_MEM_TYPE);
	if (!prRole)
		goto fail;
	if (!prGlueInfo->prP2PDevInfo) {
		prNewDevice = kalMemAlloc(sizeof(*prNewDevice), VIR_MEM_TYPE);
		if (!prNewDevice)
			goto fail;
	}
	if (!prAdapter->prP2pInfo) {
		prNewInfo = kalMemAlloc(sizeof(*prNewInfo), VIR_MEM_TYPE);
		if (!prNewInfo)
			goto fail;
	}
	if (!prWifiVar->prP2pDevFsmInfo) {
		prNewDevFsm = kalMemAlloc(sizeof(*prNewDevFsm), VIR_MEM_TYPE);
		if (!prNewDevFsm)
			goto fail;
	}
	prSettings = kalMemAlloc(sizeof(*prSettings), VIR_MEM_TYPE);
	if (!prSettings)
		goto fail;
	prSpecific = kalMemAlloc(sizeof(*prSpecific), VIR_MEM_TYPE);
	if (!prSpecific)
		goto fail;
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	prStatistics = kalMemAlloc(sizeof(*prStatistics), VIR_MEM_TYPE);
	if (!prStatistics)
		goto fail;
#endif

	kalMemZero(prRole, sizeof(*prRole));
	if (prNewDevice)
		kalMemZero(prNewDevice, sizeof(*prNewDevice));
	if (prNewInfo)
		kalMemZero(prNewInfo, sizeof(*prNewInfo));
	if (prNewDevFsm)
		kalMemZero(prNewDevFsm, sizeof(*prNewDevFsm));
	kalMemZero(prSettings, sizeof(*prSettings));
	kalMemZero(prSpecific, sizeof(*prSpecific));
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	kalMemZero(prStatistics, sizeof(*prStatistics));
#endif

	if (prNewDevice)
		prGlueInfo->prP2PDevInfo = prNewDevice;
	if (prNewInfo)
		prAdapter->prP2pInfo = prNewInfo;
	if (prNewDevFsm)
		prWifiVar->prP2pDevFsmInfo = prNewDevFsm;
	prWifiVar->prP2PConnSettings[ucIdex] = prSettings;
	prWifiVar->prP2pSpecificBssInfo[ucIdex] = prSpecific;
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	prWifiVar->prP2pQueryStaStatistics[ucIdex] = prStatistics;
#endif
	*pprInfo = prRole;
	return TRUE;

fail:
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	if (prStatistics)
		kalMemFree(prStatistics, VIR_MEM_TYPE, sizeof(*prStatistics));
#endif
	if (prSpecific)
		kalMemFree(prSpecific, VIR_MEM_TYPE, sizeof(*prSpecific));
	if (prSettings)
		kalMemFree(prSettings, VIR_MEM_TYPE, sizeof(*prSettings));
	if (prNewDevFsm)
		kalMemFree(prNewDevFsm, VIR_MEM_TYPE, sizeof(*prNewDevFsm));
	if (prNewInfo)
		kalMemFree(prNewInfo, VIR_MEM_TYPE, sizeof(*prNewInfo));
	if (prNewDevice)
		kalMemFree(prNewDevice, VIR_MEM_TYPE, sizeof(*prNewDevice));
	if (prRole)
		kalMemFree(prRole, VIR_MEM_TYPE, sizeof(*prRole));
	return FALSE;
}


/*---------------------------------------------------------------------------*/
/*!
 * \brief Free memory for P2P_INFO, GL_P2P_INFO, P2P_CONNECTION_SETTINGS
 *                                          P2P_SPECIFIC_BSS_INFO, P2P_FSM_INFO
 *
 * \param[in] prGlueInfo      Pointer to glue info
 *	[in] ucIdx	     The BSS with the idx will be freed.
 *			     "ucIdx == 0xff" will free all BSSs.
 *			     Only has meaning for "CFG_ENABLE_UNIFY_WIPHY == 1"
 *
 * \return   TRUE
 *           FALSE
 */
/*---------------------------------------------------------------------------*/
static void p2pFreeRoleMemory(struct GLUE_INFO *prGlueInfo, uint8_t ucIdx)
{
	struct WIFI_VAR *prWifiVar = &prGlueInfo->prAdapter->rWifiVar;

	if (prWifiVar->prP2PConnSettings[ucIdx])
		p2pFreeMemSafe(prGlueInfo, (void **)&prWifiVar->prP2PConnSettings[ucIdx], sizeof(struct P2P_CONNECTION_SETTINGS));
	if (prWifiVar->prP2pSpecificBssInfo[ucIdx])
		p2pFreeMemSafe(prGlueInfo, (void **)&prWifiVar->prP2pSpecificBssInfo[ucIdx], sizeof(struct P2P_SPECIFIC_BSS_INFO));
#if CFG_ENABLE_PER_STA_STATISTICS_LOG
	if (prWifiVar->prP2pQueryStaStatistics[ucIdx])
		p2pFreeMemSafe(prGlueInfo, (void **)&prWifiVar->prP2pQueryStaStatistics[ucIdx], sizeof(struct PARAM_GET_STA_STATISTICS));
#endif
	if (prGlueInfo->prP2PInfo[ucIdx])
		p2pFreeMemSafe(prGlueInfo, (void **)&prGlueInfo->prP2PInfo[ucIdx], sizeof(struct GL_P2P_INFO));
}

static void p2pFreeCommonMemory(struct GLUE_INFO *prGlueInfo)
{
	struct ADAPTER *prAdapter = prGlueInfo->prAdapter;

	if (prAdapter->rWifiVar.prP2pDevFsmInfo)
		p2pFreeMemSafe(prGlueInfo, (void **)&prAdapter->rWifiVar.prP2pDevFsmInfo, sizeof(struct P2P_DEV_FSM_INFO));
	if (prGlueInfo->prP2PDevInfo)
		p2pFreeMemSafe(prGlueInfo, (void **)&prGlueInfo->prP2PDevInfo, sizeof(struct GL_P2P_DEV_INFO));
	if (prAdapter->prP2pInfo)
		p2pFreeMemSafe(prGlueInfo, (void **)&prAdapter->prP2pInfo, sizeof(struct P2P_INFO));
}

u_int8_t p2PFreeInfo(struct GLUE_INFO *prGlueInfo, uint8_t ucIdx)
{
	struct ADAPTER *prAdapter;
	struct GL_P2P_INFO *prInfo;
	u_int8_t counted;
	uint8_t i;

	if (!prGlueInfo || !prGlueInfo->prAdapter || ucIdx >= KAL_P2P_NUM)
		return FALSE;
	prAdapter = prGlueInfo->prAdapter;
	if (!prAdapter->prP2pInfo)
		return FALSE;
	prInfo = prGlueInfo->prP2PInfo[ucIdx];
	if (!prInfo)
		return TRUE;
	if (P2P_ROLE_INDEX_2_ROLE_FSM_INFO(prAdapter, ucIdx))
		return FALSE;
	counted = prInfo->fgSetupCommitted;
	if (counted && !prAdapter->prP2pInfo->u4DeviceNum)
		return FALSE;
	if (prAdapter->prP2pInfo->u4DeviceNum == (counted ? 1 : 0) &&
	    prAdapter->rWifiVar.prP2pDevFsmInfo &&
	    prAdapter->rWifiVar.prP2pDevFsmInfo->fgIsInitialized)
		return FALSE;
	p2pFreeRoleMemory(prGlueInfo, ucIdx);
	if (counted)
		prAdapter->prP2pInfo->u4DeviceNum--;
	if (prAdapter->prP2pInfo->u4DeviceNum)
		return TRUE;
	for (i = 0; i < KAL_P2P_NUM; i++)
		if (prGlueInfo->prP2PInfo[i])
			return TRUE;
	p2pFreeCommonMemory(prGlueInfo);
	scanRemoveAllP2pBssDesc(prAdapter);
	return TRUE;
}


void p2pFreeMemSafe(struct GLUE_INFO *prGlueInfo,
		void **pprMemInfo, uint32_t size)
{
	void *prTmpMemInfo = NULL;

	GLUE_SPIN_LOCK_DECLARATION();

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	prTmpMemInfo = *pprMemInfo;
	*pprMemInfo = NULL;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	kalMemFree(prTmpMemInfo, VIR_MEM_TYPE, size);
}

u_int8_t p2pNetIsRegistered(struct GLUE_INFO *prGlueInfo)
{
	struct ADAPTER *adapter;
	u_int8_t ready = FALSE;

	GLUE_SPIN_LOCK_DECLARATION();

	ASSERT_RTNL();
	if (!prGlueInfo || !prGlueInfo->prAdapter)
		return FALSE;
	adapter = prGlueInfo->prAdapter;
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (adapter->fgIsP2PRegistered &&
	    adapter->rP2PRegState == ENUM_P2P_REG_STATE_REGISTERED &&
	    adapter->rP2PNetRegState == ENUM_NET_REG_STATE_REGISTERED &&
	    prGlueInfo->prP2PInfo[0] && gPrP2pDev[0] &&
	    prGlueInfo->prP2PInfo[0]->prDevHandler == gPrP2pDev[0] &&
	    gPrP2pDev[0]->reg_state == NETREG_REGISTERED)
		ready = TRUE;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	return ready;
}

u_int8_t p2pNetRegister(struct GLUE_INFO *prGlueInfo,
		u_int8_t fgIsRtnlLockAcquired)
{
	struct net_device *registered[KAL_P2P_NUM] = { NULL };
	struct net_device *prDevHandler = NULL;
	struct ADAPTER *prAdapter;
	uint32_t count, done = 0, i;
	u_int8_t result = FALSE;
	int ret;

	GLUE_SPIN_LOCK_DECLARATION();

	if (!prGlueInfo || !prGlueInfo->prAdapter)
		return FALSE;
	prAdapter = prGlueInfo->prAdapter;
	if (fgIsRtnlLockAcquired)
		ASSERT_RTNL();
	else
		rtnl_lock();

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (!prAdapter->fgIsP2PRegistered ||
	    prAdapter->rP2PRegState != ENUM_P2P_REG_STATE_REGISTERED ||
	    !prAdapter->prP2pInfo || !prAdapter->prP2pInfo->u4DeviceNum ||
	    prAdapter->prP2pInfo->u4DeviceNum > KAL_P2P_NUM) {
		goto leave_state;
	}
	count = prAdapter->prP2pInfo->u4DeviceNum;
	if (prAdapter->rP2PNetRegState == ENUM_NET_REG_STATE_REGISTERED) {
		result = TRUE;
		for (i = 0; i < count; i++) {
			prDevHandler = prGlueInfo->prP2PInfo[i] ?
				prGlueInfo->prP2PInfo[i]->prDevHandler : NULL;
			if (!prDevHandler ||
			    prDevHandler->reg_state != NETREG_REGISTERED ||
			    gPrP2pDev[i] != prDevHandler)
				result = FALSE;
		}
		goto leave_state;
	}
	if (prAdapter->rP2PNetRegState != ENUM_NET_REG_STATE_UNREGISTERED)
		goto leave_state;
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_REGISTERING;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	for (i = 0; i < count; i++) {
		GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		prDevHandler = prGlueInfo->prP2PInfo[i] ?
			prGlueInfo->prP2PInfo[i]->prDevHandler :
			NULL;

		if (!prDevHandler ||
		    prDevHandler->reg_state != NETREG_UNINITIALIZED) {
			GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
			goto fail;
		}

		netif_carrier_off(prDevHandler);
		netif_tx_stop_all_queues(prDevHandler);

		if (g_u4DevIdx[i])
			prDevHandler->ifindex = g_u4DevIdx[i];
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

		ret = register_netdevice(prDevHandler);
		if (ret < 0)
			goto fail;
		registered[done++] = prDevHandler;
	}

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	for (i = 0; i < count; i++) {
		gPrP2pDev[i] = registered[i];
		g_u4DevIdx[i] = 0;
	}
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_REGISTERED;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (!fgIsRtnlLockAcquired)
		rtnl_unlock();
	return TRUE;
fail:
	while (done) {
		prDevHandler = registered[--done];
		unregister_netdevice(prDevHandler);
	}
	rtnl_unlock();
	if (fgIsRtnlLockAcquired)
		rtnl_lock();
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	for (i = 0; i < count; i++)
		gPrP2pDev[i] = NULL;
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERED;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	return FALSE;
leave_state:
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (!fgIsRtnlLockAcquired)
		rtnl_unlock();
	return result;
}

u_int8_t p2pNetUnregister(struct GLUE_INFO *prGlueInfo,
		u_int8_t fgIsRtnlLockAcquired)
{
	u_int8_t result = FALSE;
	uint8_t ucRoleIdx;
	struct ADAPTER *prAdapter = NULL;
	struct NETDEV_PRIVATE_GLUE_INFO *prNetDevPriv = NULL;
	struct GL_P2P_INFO *prP2PInfo = NULL;
	struct BSS_INFO *prP2pBssInfo = NULL;
	struct cfg80211_scan_request *scan;
	int iftype = 0;
	struct net_device *prRoleDev = NULL;
	u_int8_t sharedAis;

	GLUE_SPIN_LOCK_DECLARATION();

	if (!prGlueInfo || !prGlueInfo->prAdapter)
		return FALSE;
	prAdapter = prGlueInfo->prAdapter;
	if (fgIsRtnlLockAcquired)
		ASSERT_RTNL();

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (prAdapter->rP2PRegState == ENUM_P2P_REG_STATE_REGISTERING ||
	    prAdapter->rP2PRegState == ENUM_P2P_REG_STATE_UNREGISTERING) {
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		return FALSE;
	}
	if (prAdapter->rP2PNetRegState == ENUM_NET_REG_STATE_UNREGISTERED) {
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		return TRUE;
	}
	if (prAdapter->rP2PNetRegState != ENUM_NET_REG_STATE_REGISTERED ||
	    prAdapter->rP2PRegState != ENUM_P2P_REG_STATE_REGISTERED) {
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		return FALSE;
	}
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	if (!fgIsRtnlLockAcquired)
		rtnl_lock();
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (prAdapter->rP2PNetRegState == ENUM_NET_REG_STATE_UNREGISTERED &&
	    (prAdapter->rP2PRegState == ENUM_P2P_REG_STATE_UNREGISTERED ||
	     prAdapter->rP2PRegState == ENUM_P2P_REG_STATE_REGISTERED)) {
		result = TRUE;
		goto leave_state;
	}
	if (prAdapter->rP2PNetRegState != ENUM_NET_REG_STATE_REGISTERED ||
	    prAdapter->rP2PRegState != ENUM_P2P_REG_STATE_REGISTERED)
		goto leave_state;
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERING;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	KAL_ACQUIRE_MUTEX(prAdapter, MUTEX_DEL_INF);
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	scan = prGlueInfo->prP2PDevInfo ?
		prGlueInfo->prP2PDevInfo->prScanRequest : NULL;
	if (scan)
		prGlueInfo->prP2PDevInfo->prScanRequest = NULL;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (scan)
		kalCfg80211ScanDone(scan, TRUE);
	KAL_RELEASE_MUTEX(prAdapter, MUTEX_DEL_INF);

	for (ucRoleIdx = 0; ucRoleIdx < KAL_P2P_NUM; ucRoleIdx++) {
		GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		prP2PInfo = prGlueInfo->prP2PInfo[ucRoleIdx];
		gPrP2pDev[ucRoleIdx] = NULL;
		if (prP2PInfo == NULL) {
			GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
			continue;
		}

#if CFG_ENABLE_UNIFY_WIPHY
		sharedAis = wlanIsAisDev(prP2PInfo->prDevHandler);
#else
		sharedAis = FALSE;
#endif

		prRoleDev = prP2PInfo->aprRoleHandler;
		gPrP2pDev[ucRoleIdx] = NULL;
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		if (prRoleDev != NULL) {
			/* info cfg80211 disconnect */
			prNetDevPriv = (struct NETDEV_PRIVATE_GLUE_INFO *)
				netdev_priv(prRoleDev);
			iftype = prRoleDev->ieee80211_ptr->iftype;
			prP2pBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter,
							prNetDevPriv->ucBssIdx);

			/* FIXME: The p2pRoleFsmUninit may call the
			 * cfg80211_disconnected.
			 * p2pRemove()->glUnregisterP2P->p2pRoleFsmUninit(),
			 * it may be too late.
			 */
			if ((prP2pBssInfo != NULL) &&
			    (prP2pBssInfo->eConnectionState ==
				MEDIA_STATE_CONNECTED) &&
			    ((iftype == NL80211_IFTYPE_P2P_CLIENT) ||
			     (iftype == NL80211_IFTYPE_STATION))) {
				p2pChangeMediaState(prAdapter,
					prP2pBssInfo,
					MEDIA_STATE_DISCONNECTED);

				cfg80211_disconnected(prRoleDev, 0, NULL, 0,
							TRUE, GFP_KERNEL);
			}

			if (prRoleDev != prP2PInfo->prDevHandler) {
				if (netif_carrier_ok(prRoleDev))
					netif_carrier_off(prRoleDev);

				netif_tx_stop_all_queues(prRoleDev);
				wlanBindBssIdxToNetInterface(prGlueInfo,
					prNetDevPriv->ucBssIdx,
					prP2PInfo->prDevHandler);
				GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo,
					SPIN_LOCK_NET_DEV);
				prP2PInfo->aprRoleHandler =
					prP2PInfo->prDevHandler;
				prP2PInfo->prWdev =
					prP2PInfo->prDevHandler->ieee80211_ptr;
				gprP2pRoleWdev[ucRoleIdx] = prP2PInfo->prWdev;
				GLUE_RELEASE_SPIN_LOCK(prGlueInfo,
					SPIN_LOCK_NET_DEV);
			}
		}

		if (!sharedAis) {
			if (netif_carrier_ok(prP2PInfo->prDevHandler))
				netif_carrier_off(prP2PInfo->prDevHandler);
			netif_tx_stop_all_queues(prP2PInfo->prDevHandler);
		}

		/* Here are the functions which need rtnl_lock */
		if ((prRoleDev) && (prP2PInfo->prDevHandler != prRoleDev)) {
			DBGLOG(INIT, INFO,
				"unregister p2p[%d], net_dev=%p, wdev=%px\n",
				ucRoleIdx, prRoleDev,
				prRoleDev->ieee80211_ptr);
			if (prRoleDev->reg_state == NETREG_REGISTERED)
				unregister_netdevice(prRoleDev);

			/* This ndev is created in mtk_p2p_cfg80211_add_iface(),
			 * and unregister_netdev will also free the ndev.
			 */
		}

		DBGLOG(INIT, INFO, "unregister p2pdev[%d], net_dev=%p, wdev=%px\n",
			ucRoleIdx, prP2PInfo->prDevHandler,
			prP2PInfo->prDevHandler->ieee80211_ptr);
		if (!sharedAis &&
		    prP2PInfo->prDevHandler->reg_state == NETREG_REGISTERED)
			unregister_netdevice(prP2PInfo->prDevHandler);
	}

	rtnl_unlock();
	if (fgIsRtnlLockAcquired)
		rtnl_lock();
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERED;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	return TRUE;
leave_state:
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if (!fgIsRtnlLockAcquired)
		rtnl_unlock();
	return result;
}

/*---------------------------------------------------------------------------*/
/*!
 * \brief Setup the P2P device information
 *
 * \param[in] prGlueInfo      Pointer to glue info
 *       [in] prP2pWdev       Pointer to the wireless device
 *       [in] prP2pDev        Pointer to the net device
 *       [in] u4Idx           The P2P Role index (max : (KAL_P2P_NUM-1))
 *       [in] fgIsApMode      Indicate that this device is AP Role or not
 *
 * \return    0	Success
 *           -1	Failure
 */
/*---------------------------------------------------------------------------*/
int glSetupP2P(struct GLUE_INFO *prGlueInfo, struct wireless_dev *prP2pWdev,
	struct net_device *prP2pDev, uint8_t u4Idx, u_int8_t fgIsApMode)
{
	struct ADAPTER *prAdapter;
	struct WIFI_VAR *prWifiVar;
	struct GL_P2P_INFO *prP2PInfo;
	struct P2P_ROLE_FSM_INFO *prPreparedRole = NULL;
	struct NETDEV_PRIVATE_GLUE_INFO *prNetDevPriv;
	struct GL_HIF_INFO *prHif;
	u_int8_t newCommon;
	uint8_t peer;
	uint32_t committed = 0;
	int ret;

	GLUE_SPIN_LOCK_DECLARATION();

	if (!prGlueInfo || !prGlueInfo->prAdapter || !prP2pWdev ||
	    !prP2pWdev->wiphy || !prP2pDev || u4Idx >= KAL_P2P_NUM)
		return -EINVAL;
	prAdapter = prGlueInfo->prAdapter;
	prWifiVar = &prAdapter->rWifiVar;
	prHif = &prGlueInfo->rHifInfo;
	if (!prAdapter->chip_info)
		return -EINVAL;
	if (prGlueInfo->prP2PInfo[u4Idx] ||
	    P2P_ROLE_INDEX_2_ROLE_FSM_INFO(prAdapter, u4Idx))
		return -EALREADY;
	newCommon = !prGlueInfo->prP2PDevInfo && !prAdapter->prP2pInfo &&
		!prWifiVar->prP2pDevFsmInfo;
	for (peer = 0; peer < KAL_P2P_NUM; peer++) {
		if (!prGlueInfo->prP2PInfo[peer])
			continue;
		if (!prGlueInfo->prP2PInfo[peer]->fgSetupCommitted)
			return -EINVAL;
		committed++;
	}
	if ((newCommon && (committed || prAdapter->fgIsP2PRegistered)) ||
	    (!newCommon && prAdapter->prP2pInfo &&
	     prAdapter->prP2pInfo->u4DeviceNum != committed))
		return -EINVAL;
	if (!newCommon && (!prGlueInfo->prP2PDevInfo || !prAdapter->prP2pInfo ||
	    !prAdapter->prP2pInfo->u4DeviceNum ||
	    !prWifiVar->prP2pDevFsmInfo ||
	    !prWifiVar->prP2pDevFsmInfo->fgIsInitialized))
		return -EINVAL;
	if (!p2PAllocInfo(prGlueInfo, u4Idx, &prP2PInfo))
		return -ENOMEM;
	p2pFuncInitConnectionSettings(prAdapter,
		prWifiVar->prP2PConnSettings[u4Idx], fgIsApMode);
	ret = p2pDevFsmInit(prAdapter);
	if (ret < 0)
		goto fail;
	if (ret != prAdapter->ucP2PDevBssIdx) {
		ret = -EINVAL;
		goto fail_device;
	}
	ret = p2pRoleFsmPrepare(prAdapter, u4Idx, prP2pDev->dev_addr, fgIsApMode, &prPreparedRole);
	if (ret < 0)
		goto fail_device;
	if (ret >= prAdapter->ucHwBssIdNum) {
		p2pRoleFsmReleasePrepared(prAdapter, prPreparedRole);
		ret = -EINVAL;
		goto fail_device;
	}

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	prNetDevPriv = netdev_priv(prP2pDev);
	prNetDevPriv->prGlueInfo = prGlueInfo;
	prNetDevPriv->ucBssIdx = ret;
	prNetDevPriv->ucMddpSupport = FALSE;
	if (fgIsApMode) {
		prP2pWdev->iftype = NL80211_IFTYPE_AP;
#if CFG_ENABLE_UNIFY_WIPHY
		prNetDevPriv->ucIsP2p = FALSE;
#endif
#if CFG_MTK_MDDP_SUPPORT
		prNetDevPriv->ucMddpSupport = TRUE;
#endif
	} else {
		prP2pWdev->iftype = NL80211_IFTYPE_P2P_CLIENT;
#if CFG_ENABLE_UNIFY_WIPHY
		prNetDevPriv->ucIsP2p = TRUE;
#endif
	}
	prP2pDev->needed_headroom = NIC_TX_DESC_AND_PADDING_LENGTH +
		prAdapter->chip_info->txd_append_size;
	prP2pDev->netdev_ops = &p2p_netdev_ops;
#if defined(_HIF_SDIO) && (MTK_WCN_HIF_SDIO == 0)
	SET_NETDEV_DEV(prP2pDev, &prHif->func->dev);
#endif
	(void)prHif;
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
	prP2pDev->ieee80211_ptr = prP2pWdev;
	prP2pWdev->netdev = prP2pDev;
	prP2PInfo->prWdev = prP2pWdev;
	if (!prAdapter->fgEnable5GBand)
		prP2pWdev->wiphy->bands[BAND_5G] = NULL;
#endif
#if CFG_SUPPORT_WIFI_6G
	if (!prAdapter->fgIsHwSupport6G)
		prP2pWdev->wiphy->bands[BAND_6G] = NULL;
#endif
#if CFG_TCP_IP_CHKSUM_OFFLOAD
	if (prAdapter->fgIsSupportCsumOffload)
		prP2pDev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;
#endif
	kalResetStats(prP2pDev);
	prP2PInfo->prDevHandler = prP2pDev;
	prP2PInfo->aprRoleHandler = prP2pDev;
	init_completion(&prP2PInfo->rStopApComp);
	wlanBindBssIdxToNetInterface(prGlueInfo, prNetDevPriv->ucBssIdx, prP2pDev);
	prP2PInfo->fgSetupCommitted = TRUE;
	P2P_ROLE_INDEX_2_ROLE_FSM_INFO(prAdapter, u4Idx) = prPreparedRole;
	prGlueInfo->prP2PInfo[u4Idx] = prP2PInfo;
	prAdapter->prP2pInfo->u4DeviceNum++;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	return 0;
fail_device:
	if (newCommon)
		p2pDevFsmAbortInit(prAdapter);
fail:
	p2pFreeRoleMemory(prGlueInfo, u4Idx);
	kalMemFree(prP2PInfo, VIR_MEM_TYPE, sizeof(*prP2PInfo));
	if (newCommon)
		p2pFreeCommonMemory(prGlueInfo);
	return ret;
}


/*---------------------------------------------------------------------------*/
/*!
 * \brief Register for cfg80211 for Wi-Fi Direct
 *
 * \param[in] prGlueInfo      Pointer to glue info
 *
 * \return   TRUE
 *           FALSE
 */
/*---------------------------------------------------------------------------*/
u_int8_t glRegisterP2P(struct GLUE_INFO *prGlueInfo, const char *prDevName,
	const char *prDevName2, uint8_t ucApMode)
{
	struct ADAPTER *prAdapter;
	struct wireless_dev *oldRole[KAL_P2P_NUM], *oldBase[KAL_P2P_NUM];
	struct wireless_dev *created[KAL_P2P_NUM] = {NULL};
	struct net_device *made[KAL_P2P_NUM] = {NULL};
	struct ieee80211_supported_band *oldBands[KAL_P2P_NUM][NUM_NL80211_BANDS];
	enum nl80211_iftype oldType[KAL_P2P_NUM];
	struct wireless_dev *wdev;
	struct net_device *ndev = NULL;
	struct GL_P2P_INFO *info;
	const char *name;
	uint8_t count, done = 0, i, j;
	u_int8_t ap;
#if CFG_ENABLE_UNIFY_WIPHY == 0
	struct device *parent;
#endif
	GLUE_SPIN_LOCK_DECLARATION();

	if (!prGlueInfo || !prGlueInfo->prAdapter || !prDevName ||
	    ucApMode > RUNNING_DUAL_P2P_MODE)
		return FALSE;
	prAdapter = prGlueInfo->prAdapter;
	if (prAdapter->fgIsP2PRegistered || prAdapter->prP2pInfo ||
	    prGlueInfo->prP2PDevInfo || prAdapter->rWifiVar.prP2pDevFsmInfo)
		return FALSE;
	count = ucApMode == RUNNING_DUAL_AP_MODE ||
		ucApMode == RUNNING_P2P_AP_MODE ||
		ucApMode == RUNNING_DUAL_P2P_MODE ? 2 : 1;
	if (count > KAL_P2P_NUM || (ucApMode == RUNNING_P2P_AP_MODE && !prDevName2))
		return FALSE;
	for (i = 0; i < KAL_P2P_NUM; i++) {
		if (prGlueInfo->prP2PInfo[i] ||
		    P2P_ROLE_INDEX_2_ROLE_FSM_INFO(prAdapter, i))
			return FALSE;
		oldRole[i] = gprP2pRoleWdev[i];
		oldBase[i] = gprP2pWdev[i];
	}
#if CFG_ENABLE_UNIFY_WIPHY == 0
	glGetHifDev(&prGlueInfo->rHifInfo, &parent);
	if (!parent)
		return FALSE;
#endif
	for (i = 0; i < count; i++) {
		if (!gprP2pRoleWdev[i] && gprP2pWdev[i])
			gprP2pRoleWdev[i] = gprP2pWdev[i];
		if (!gprP2pRoleWdev[i]) {
			if (!glP2pCreateWirelessDevice(prGlueInfo))
				goto fail;
			created[i] = gprP2pRoleWdev[i];
		}
		wdev = gprP2pRoleWdev[i];
		if (!wdev || !wdev->wiphy || wdev->netdev)
			goto fail;
		oldType[i] = wdev->iftype;
		for (j = 0; j < NUM_NL80211_BANDS; j++)
			oldBands[i][j] = wdev->wiphy->bands[j];
		ap = ucApMode != RUNNING_P2P_MODE && ucApMode != RUNNING_DUAL_P2P_MODE;
		name = prDevName;
		if (ucApMode == RUNNING_P2P_AP_MODE) {
			ap = i != 0;
			if (i)
				name = prDevName2;
		}
		ndev = alloc_netdev_mq(sizeof(struct NETDEV_PRIVATE_GLUE_INFO), name,
			NET_NAME_PREDICTABLE, ether_setup, CFG_MAX_TXQ_NUM);
		if (!ndev)
			goto fail;
		dev_addr_set(ndev, prAdapter->rWifiVar.aucInterfaceAddress[i]);
		kalMemCopy(ndev->perm_addr, ndev->dev_addr, ETH_ALEN);
		if (glSetupP2P(prGlueInfo, wdev, ndev, i, ap))
			goto fail;
		made[i] = ndev;
		ndev = NULL;
		done++;
	}
#if CFG_ENABLE_UNIFY_WIPHY == 0
	for (i = 0; i < count; i++)
		set_wiphy_dev(gprP2pRoleWdev[i]->wiphy, parent);
#endif
#if (CFG_SUPPORT_DFS_MASTER == 1)
	p2pFuncRadarInfoInit();
#endif
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERED;
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	return TRUE;
fail:
	if (ndev)
		free_netdev(ndev);
	p2pDevFsmAbortInit(prAdapter);
	while (done) {
		i = --done;
		wdev = made[i]->ieee80211_ptr;
		info = prGlueInfo->prP2PInfo[i];
		prGlueInfo->prP2PInfo[i] = NULL;
		p2pRoleFsmAbortInit(prAdapter, i);
		if (info->fgSetupCommitted) {
			info->fgSetupCommitted = FALSE;
			prAdapter->prP2pInfo->u4DeviceNum--;
		}
		if (wdev && wdev->netdev == made[i])
			wdev->netdev = NULL;
		made[i]->ieee80211_ptr = NULL;
		p2pFreeRoleMemory(prGlueInfo, i);
		kalMemFree(info, VIR_MEM_TYPE, sizeof(*info));
		if (!wlanIsAisDev(made[i]))
			free_netdev(made[i]);
		wdev->iftype = oldType[i];
		for (j = 0; j < NUM_NL80211_BANDS; j++)
			wdev->wiphy->bands[j] = oldBands[i][j];
	}
	if (prAdapter->prP2pInfo && !prAdapter->prP2pInfo->u4DeviceNum)
		p2pFreeCommonMemory(prGlueInfo);
	for (i = 0; i < KAL_P2P_NUM; i++) {
		gprP2pRoleWdev[i] = oldRole[i];
		gprP2pWdev[i] = oldBase[i];
		if (!created[i])
			continue;
#if CFG_ENABLE_UNIFY_WIPHY == 0
		wiphy_unregister(created[i]->wiphy);
		wiphy_free(created[i]->wiphy);
#endif
		kfree(created[i]);
	}
	return FALSE;
}


#if CFG_ENABLE_UNIFY_WIPHY
u_int8_t glP2pCreateWirelessDevice(struct GLUE_INFO *prGlueInfo)
{
	struct wiphy *prWiphy = wlanGetWiphy();
	struct wireless_dev *prWdev = NULL;
	uint8_t	i = 0;

#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
	if (!prWiphy) {
		DBGLOG(P2P, ERROR, "unable to allocate wiphy for p2p\n");
		return FALSE;
	}

	for (i = 0 ; i < KAL_P2P_NUM; i++) {
		if (!gprP2pRoleWdev[i])
			break;
	}

	if (i >= KAL_P2P_NUM) {
		DBGLOG(INIT, WARN, "fail to register wiphy to driver\n");
		return FALSE;
	}

	prWdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!prWdev) {
		DBGLOG(P2P, ERROR, "allocate p2p wdev fail, no memory\n");
		return FALSE;
	}

	/* set priv as pointer to glue structure */
	prWdev->wiphy = prWiphy;

	gprP2pRoleWdev[i] = prWdev;
	DBGLOG(INIT, TRACE, "glP2pCreateWirelessDevice (%p)\n",
			gprP2pRoleWdev[i]->wiphy);

	/* P2PDev and P2PRole[0] share the same Wdev */
	gprP2pWdev[i] = gprP2pRoleWdev[i];

	return TRUE;
#else
	return FALSE;
#endif
}
#else	/* (CFG_ENABLE_UNIFY_WIPHY == 0) */
u_int8_t glP2pCreateWirelessDevice(struct GLUE_INFO *prGlueInfo)
{
	struct wiphy *prWiphy = NULL;
	struct wireless_dev *prWdev = NULL;
	uint8_t	i = 0;
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
	prWdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!prWdev) {
		DBGLOG(P2P, ERROR,
			"allocate p2p wireless device fail, no memory\n");
		return FALSE;
	}
	/* 1. allocate WIPHY */
	prWiphy = wiphy_new(&mtk_p2p_ops, sizeof(struct GLUE_INFO *));
	if (!prWiphy) {
		DBGLOG(P2P, ERROR, "unable to allocate wiphy for p2p\n");
		goto free_wdev;
	}

	prWiphy->interface_modes = BIT(NL80211_IFTYPE_AP)
	    | BIT(NL80211_IFTYPE_P2P_CLIENT)
	    | BIT(NL80211_IFTYPE_P2P_GO)
	    | BIT(NL80211_IFTYPE_STATION);

	prWiphy->software_iftypes |= BIT(NL80211_IFTYPE_P2P_DEVICE);

	prWiphy->iface_combinations = p_mtk_iface_combinations_p2p;
	prWiphy->n_iface_combinations = mtk_iface_combinations_p2p_num;

	prWiphy->bands[KAL_BAND_2GHZ] = &mtk_band_2ghz;
	prWiphy->bands[KAL_BAND_5GHZ] = &mtk_band_5ghz;
#if (CFG_SUPPORT_WIFI_6G == 1)
	prWiphy->bands[KAL_BAND_6GHZ] = &mtk_band_6ghz;
	DBGLOG(INIT, INFO, "P2P support 6G\n");
#endif

	prWiphy->mgmt_stypes = mtk_cfg80211_default_mgmt_stypes;
	prWiphy->max_remain_on_channel_duration = 5000;
	prWiphy->n_cipher_suites = 5;
	prWiphy->cipher_suites = mtk_cipher_suites;
#if (CFG_SUPPORT_DFS_MASTER == 1)
	prWiphy->flags = WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL
				| WIPHY_FLAG_HAVE_AP_SME
				| WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	prWiphy->max_num_csa_counters = 2;
#else
	prWiphy->flags = WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL
				| WIPHY_FLAG_HAVE_AP_SME;
#endif
	prWiphy->regulatory_flags = REGULATORY_CUSTOM_REG;
	prWiphy->ap_sme_capa = 1;

#if CFG_ENABLE_OFFCHANNEL_TX
	prWiphy->flags |= WIPHY_FLAG_OFFCHAN_TX;
#endif /* CFG_ENABLE_OFFCHANNEL_TX */
	prWiphy->features |= NL80211_FEATURE_INACTIVITY_TIMER;

	prWiphy->max_scan_ssids = MAX_SCAN_LIST_NUM;
	prWiphy->max_scan_ie_len = MAX_SCAN_IE_LEN;
	prWiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	prWiphy->vendor_commands = mtk_p2p_vendor_ops;
	prWiphy->n_vendor_commands = sizeof(mtk_p2p_vendor_ops)
		/ sizeof(struct wiphy_vendor_command);
	prWiphy->vendor_events = mtk_p2p_vendor_events;
	prWiphy->n_vendor_events = ARRAY_SIZE(mtk_p2p_vendor_events);

#ifdef CONFIG_PM
#endif

		prWiphy->max_ap_assoc_sta = P2P_MAXIMUM_CLIENT_COUNT;

	cfg80211_regd_set_wiphy(prWiphy);

	/* 2.1 set priv as pointer to glue structure */
	*((struct GLUE_INFO **) wiphy_priv(prWiphy)) = prGlueInfo;
	/* Here are functions which need rtnl_lock */
	if (wiphy_register(prWiphy) < 0) {
		DBGLOG(INIT, WARN, "fail to register wiphy for p2p\n");
		goto free_wiphy;
	}
	prWdev->wiphy = prWiphy;

	for (i = 0 ; i < KAL_P2P_NUM; i++) {
		if (!gprP2pRoleWdev[i]) {
			gprP2pRoleWdev[i] = prWdev;
			DBGLOG(INIT, INFO,
				"glP2pCreateWirelessDevice (%x)\n",
				gprP2pRoleWdev[i]->wiphy);
			break;
		}
	}

	if (i == KAL_P2P_NUM)
		DBGLOG(INIT, WARN, "fail to register wiphy to driver\n");

	return TRUE;

free_wiphy:
	wiphy_free(prWiphy);
free_wdev:
	kfree(prWdev);
#endif
	return FALSE;
}
#endif	/* CFG_ENABLE_UNIFY_WIPHY */

/*---------------------------------------------------------------------------*/
/*!
 * \brief Unregister Net Device for Wi-Fi Direct
 *
 * \param[in] prGlueInfo      Pointer to glue info
 *	[in] ucIdx	     The BSS with the idx will be freed.
 *			     "ucIdx == 0xff" will free all BSSs.
 *			     Only has meaning for "CFG_ENABLE_UNIFY_WIPHY == 1"
 *
 * \return   TRUE
 *           FALSE
 */
/*---------------------------------------------------------------------------*/
u_int8_t glUnregisterP2P(struct GLUE_INFO *prGlueInfo, uint8_t ucIdx)
{
	uint8_t ucRoleIdx;
	uint32_t counted = 0;
	u_int8_t fgDoUnregister = FALSE;
	struct ADAPTER *prAdapter;
	struct GL_P2P_INFO *prP2PInfo = NULL;
	int i4Start = 0, i4End = 0;
	GLUE_SPIN_LOCK_DECLARATION();

	if (!prGlueInfo || !prGlueInfo->prAdapter)
		return FALSE;

	if (ucIdx == 0xff) {
		i4Start = 0;
		i4End = BSS_P2P_NUM;
	} else if (ucIdx < BSS_P2P_NUM) {
		i4Start = ucIdx;
		i4End = ucIdx + 1;
	} else {
		DBGLOG(INIT, WARN, "The ucIdx (%d) is a wrong value\n", ucIdx);
		return FALSE;
	}

	prAdapter = prGlueInfo->prAdapter;

	for (ucRoleIdx = i4Start; ucRoleIdx < i4End; ucRoleIdx++)
		if (prGlueInfo->prP2PInfo[ucRoleIdx] &&
		    prGlueInfo->prP2PInfo[ucRoleIdx]->fgSetupCommitted)
			counted++;
	if (ucIdx == 0xff || (prAdapter->prP2pInfo && counted &&
	    counted == prAdapter->prP2pInfo->u4DeviceNum))
		p2pDevFsmUninit(prAdapter);

	for (ucRoleIdx = i4Start; ucRoleIdx < i4End; ucRoleIdx++) {
		if (P2P_ROLE_INDEX_2_ROLE_FSM_INFO(prAdapter, ucRoleIdx)) {

			p2pRoleFsmUninit(prGlueInfo->prAdapter, ucRoleIdx);
		}
	}

	for (ucRoleIdx = i4Start; ucRoleIdx < i4End; ucRoleIdx++) {
		GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		prP2PInfo = prGlueInfo->prP2PInfo[ucRoleIdx];

		if (prP2PInfo == NULL) {
			GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
			continue;
		}

		if ((prP2PInfo->aprRoleHandler != NULL) &&
		    (prP2PInfo->aprRoleHandler != prP2PInfo->prDevHandler)) {

			prP2PInfo->aprRoleHandler = NULL;
			DBGLOG(P2P, INFO, "aprRoleHandler idx %d set NULL\n",
					ucRoleIdx);

		}
		if (prP2PInfo->prDevHandler) {
			gPrP2pDev[ucRoleIdx] = NULL;

			if (wlanIsAisDev(prP2PInfo->prDevHandler))
				gprP2pRoleWdev[ucRoleIdx] = NULL;
			else {
				if (prAdapter->rP2PNetRegState ==
				    ENUM_NET_REG_STATE_REGISTERED) {
					prAdapter->rP2PNetRegState =
						ENUM_NET_REG_STATE_UNREGISTERING;
					fgDoUnregister = TRUE;
				}
				GLUE_RELEASE_SPIN_LOCK(prGlueInfo,
					SPIN_LOCK_NET_DEV);
				if (prP2PInfo->prDevHandler->reg_state ==
					NETREG_REGISTERED && fgDoUnregister) {
					DBGLOG(INIT, WARN,
						"Force unregister netdev\n");
					unregister_netdev(
						prP2PInfo->prDevHandler);
				}
				if (prP2PInfo->prDevHandler->ieee80211_ptr &&
				    prP2PInfo->prDevHandler->ieee80211_ptr->netdev ==
				    prP2PInfo->prDevHandler)
					prP2PInfo->prDevHandler->ieee80211_ptr->netdev = NULL;
				prP2PInfo->prDevHandler->ieee80211_ptr = NULL;
				free_netdev(prP2PInfo->prDevHandler);
				GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo,
					SPIN_LOCK_NET_DEV);
				prAdapter->rP2PNetRegState =
					ENUM_NET_REG_STATE_UNREGISTERED;
			}
			prP2PInfo->prDevHandler = NULL;
		}
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

		if (!p2PFreeInfo(prGlueInfo, ucRoleIdx)) {

			DBGLOG(INIT, ERROR, "p2PFreeInfo FAILED\n");
			return FALSE;
		}

	}

	return TRUE;
}


/* Net Device Hooks */
/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device open (ifup)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int p2pOpen(IN struct net_device *prDev)
{
/* P_GLUE_INFO_T prGlueInfo = NULL; */
/* P_ADAPTER_T prAdapter = NULL; */
/* P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch; */

	ASSERT(prDev);

#if 0 /* Move after device name set. (mtk_p2p_set_local_dev_info) */
	prGlueInfo = *((struct GLUE_INFO **) netdev_priv(prDev));
	ASSERT(prGlueInfo);

	prAdapter = prGlueInfo->prAdapter;
	ASSERT(prAdapter);

	/* 1. switch P2P-FSM on */
	/* 1.1 allocate for message */
	prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(prAdapter,
			RAM_TYPE_MSG, sizeof(MSG_P2P_FUNCTION_SWITCH_T));

	if (!prFuncSwitch) {
		ASSERT(0);	/* Can't trigger P2P FSM */
		return -ENOMEM;
	}

	/* 1.2 fill message */
	prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
	prFuncSwitch->fgIsFuncOn = TRUE;

	/* 1.3 send message */
	mboxSendMsg(prAdapter,
		MBOX_ID_0,
		(struct MSG_HDR *) prFuncSwitch,
		MSG_SEND_METHOD_BUF);
#endif

	/* 2. carrier on & start TX queue */
	/*DFS todo 20161220_DFS*/
#if (CFG_SUPPORT_DFS_MASTER == 1)
	if (prDev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		/*netif_carrier_on(prDev);*/
		netif_tx_start_all_queues(prDev);
	}
#else
	/*netif_carrier_on(prDev);*/
	netif_tx_start_all_queues(prDev);
#endif

#ifdef CONFIG_WIRELESS_EXT
	prDev->wireless_handlers = &wext_handler_def;
#endif

	return 0;		/* success */
}				/* end of p2pOpen() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device stop (ifdown)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int p2pStop(IN struct net_device *prDev)
{
	struct GLUE_INFO *prGlueInfo = NULL;
	struct ADAPTER *prAdapter = NULL;
	struct GL_P2P_DEV_INFO *prP2pGlueDevInfo = NULL;
/* P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch; */

	GLUE_SPIN_LOCK_DECLARATION();

	ASSERT(prDev);

	prGlueInfo = *((struct GLUE_INFO **) netdev_priv(prDev));
	ASSERT(prGlueInfo);

	prAdapter = prGlueInfo->prAdapter;
	ASSERT(prAdapter);

	/* XXX: The p2pStop may be triggered after the wlanRemove.	*/
	/*      And prGlueInfo->prP2PDevInfo is freed in p2PFreeInfo.	*/
	if (!prAdapter || !prAdapter->fgIsP2PRegistered)
		return -EFAULT;

	prP2pGlueDevInfo = prGlueInfo->prP2PDevInfo;
	ASSERT(prP2pGlueDevInfo);

	/* 0. Do the scan done and set parameter to abort if the scan pending */
	/*DBGLOG(INIT, INFO, "p2pStop and ucRoleIdx = %u\n", ucRoleIdx);*/

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	if ((prP2pGlueDevInfo->prScanRequest != NULL) &&
	    (prP2pGlueDevInfo->prScanRequest->wdev == prDev->ieee80211_ptr)) {
		DBGLOG(INIT, INFO, "p2pStop and abort scan!!\n");
		kalCfg80211ScanDone(prP2pGlueDevInfo->prScanRequest, TRUE);
		prP2pGlueDevInfo->prScanRequest = NULL;
	}
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	/* zero clear old acs information */
	kalMemZero(&(prGlueInfo->prAdapter->rWifiVar.rChnLoadInfo),
		sizeof(prGlueInfo->prAdapter->rWifiVar.rChnLoadInfo));
	wlanInitChnLoadInfoChannelList(prGlueInfo->prAdapter);

	/* 1. stop TX queue */
	netif_tx_stop_all_queues(prDev);
#if 0
	/* 2. switch P2P-FSM off */
	/* 2.1 allocate for message */
	prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(prAdapter,
			RAM_TYPE_MSG, sizeof(MSG_P2P_FUNCTION_SWITCH_T));

	if (!prFuncSwitch) {
		ASSERT(0);	/* Can't trigger P2P FSM */
		return -ENOMEM;
	}

	/* 2.2 fill message */
	prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
	prFuncSwitch->fgIsFuncOn = FALSE;

	/* 2.3 send message */
	mboxSendMsg(prAdapter,
		MBOX_ID_0,
		(struct MSG_HDR *) prFuncSwitch,
		MSG_SEND_METHOD_BUF);
#endif
	/* 3. stop queue and turn off carrier */
	/* TH3 multiple P2P */
	/*prGlueInfo->prP2PInfo[0]->eState = MEDIA_STATE_DISCONNECTED;*/

	netif_tx_stop_all_queues(prDev);
	if (netif_carrier_ok(prDev))
		netif_carrier_off(prDev);

#ifdef CONFIG_WIRELESS_EXT
	prDev->wireless_handlers = NULL;
#endif

	return 0;
}				/* end of p2pStop() */

/*---------------------------------------------------------------------------*/
/*!
 * \brief A method of struct net_device,
 *        to get the network interface statistical
 *        information.
 *
 * Whenever an application needs to get statistics for the interface,
 * this method is called.
 * This happens, for example, when ifconfig or netstat -i is run.
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \return net_device_stats buffer pointer.
 */
/*---------------------------------------------------------------------------*/
struct net_device_stats *p2pGetStats(IN struct net_device *prDev)
{
	struct NETDEV_PRIVATE_GLUE_INFO *prNetDevPrivate;

	prNetDevPrivate = (struct NETDEV_PRIVATE_GLUE_INFO *)
			netdev_priv(prDev);
	kalMemCopy(&prNetDevPrivate->stats, &prDev->stats,
			sizeof(struct net_device_stats));
#if CFG_MTK_MDDP_SUPPORT
	mddpGetMdStats(prDev);
#endif

	return (struct net_device_stats *) &prNetDevPrivate->stats;
}				/* end of p2pGetStats() */

static void p2pSetMulticastList(IN struct net_device *prDev)
{
	struct GLUE_INFO *prGlueInfo = (struct GLUE_INFO *) NULL;

	prGlueInfo = (prDev != NULL)
		? *((struct GLUE_INFO **) netdev_priv(prDev))
		: NULL;

	if (!prDev || !prGlueInfo) {
		DBGLOG(INIT, WARN,
			" abnormal dev or skb: prDev(0x%p), prGlueInfo(0x%p)\n",
			prDev, prGlueInfo);
		return;
	}

	g_P2pPrDev = prDev;

	/* 4  Mark HALT, notify main thread to finish current job */
	set_bit(GLUE_FLAG_SUB_MOD_MULTICAST_BIT, &prGlueInfo->ulFlag);
	/* wake up main thread */
	wake_up_interruptible(&prGlueInfo->waitq);

}				/* p2pSetMulticastList */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function is to set multicast list and set rx mode.
 *
 * \param[in] prDev  Pointer to struct net_device
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
void mtk_p2p_wext_set_Multicastlist(struct GLUE_INFO *prGlueInfo)
{
	uint32_t u4SetInfoLen = 0;
	uint32_t u4McCount;
	struct net_device *prDev = NULL;

	GLUE_SPIN_LOCK_DECLARATION();
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	prDev = g_P2pPrDev;

	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

	prGlueInfo = (prDev != NULL)
		? *((struct GLUE_INFO **) netdev_priv(prDev))
		: NULL;

	if (!prDev || !prGlueInfo) {
		DBGLOG(INIT, WARN,
			" abnormal dev or skb: prDev(0x%p), prGlueInfo(0x%p)\n",
			prDev, prGlueInfo);
		return;
	}

	if (prDev->flags & IFF_PROMISC)
		prGlueInfo->prP2PDevInfo->u4PacketFilter
			|= PARAM_PACKET_FILTER_PROMISCUOUS;

	if (prDev->flags & IFF_BROADCAST)
		prGlueInfo->prP2PDevInfo->u4PacketFilter
			|= PARAM_PACKET_FILTER_BROADCAST;
	u4McCount = netdev_mc_count(prDev);

	if (prDev->flags & IFF_MULTICAST) {
		if ((prDev->flags & IFF_ALLMULTI)
			|| (u4McCount > MAX_NUM_GROUP_ADDR))
			prGlueInfo->prP2PDevInfo->u4PacketFilter
				|= PARAM_PACKET_FILTER_ALL_MULTICAST;
		else
			prGlueInfo->prP2PDevInfo->u4PacketFilter
				|= PARAM_PACKET_FILTER_MULTICAST;
	}

	if (prGlueInfo->prP2PDevInfo->u4PacketFilter
		& PARAM_PACKET_FILTER_MULTICAST) {
		/* Prepare multicast address list */
		struct netdev_hw_addr *ha;
		uint32_t i = 0;

		/* Avoid race condition with kernel net subsystem */
		netif_addr_lock_bh(prDev);

		netdev_for_each_mc_addr(ha, prDev) {
			/* If ha is null, it will break the loop. */
			/* Check mc count before accessing to ha to
			 * prevent from kernel crash.
			 */
			if (i == u4McCount || !ha)
				break;
			if (i < MAX_NUM_GROUP_ADDR) {
				COPY_MAC_ADDR(
					&(prGlueInfo->prP2PDevInfo
						->aucMCAddrList[i]),
					GET_ADDR(ha));
				i++;
			}
		}

		netif_addr_unlock_bh(prDev);

		DBGLOG(P2P, TRACE, "SEt Multicast Address List\n");

		if (i >= MAX_NUM_GROUP_ADDR)
			return;
		wlanoidSetP2PMulticastList(prGlueInfo->prAdapter,
			&(prGlueInfo->prP2PDevInfo->aucMCAddrList[0]),
			(i * ETH_ALEN), &u4SetInfoLen);

	}

}				/* end of p2pSetMulticastList() */

/*---------------------------------------------------------------------------*/
/*!
 *  \brief This function is TX entry point of NET DEVICE.
 *
 *  \param[in] prSkb  Pointer of the sk_buff to be sent
 *  \param[in] prDev  Pointer to struct net_device
 *
 * \retval NETDEV_TX_OK - on success.
 * \retval NETDEV_TX_BUSY - on failure, packet will be discarded by upper layer.
 */
/*---------------------------------------------------------------------------*/
netdev_tx_t p2pHardStartXmit(IN struct sk_buff *prSkb,
		IN struct net_device *prDev)
{
	struct NETDEV_PRIVATE_GLUE_INFO *prNetDevPrivate =
		(struct NETDEV_PRIVATE_GLUE_INFO *) NULL;
	struct GLUE_INFO *prGlueInfo = NULL;
	uint8_t ucBssIndex;
	struct BSS_INFO *prP2pBssInfo = NULL;

	ASSERT(prSkb);
	ASSERT(prDev);

	prNetDevPrivate = (struct NETDEV_PRIVATE_GLUE_INFO *)
		netdev_priv(prDev);
	prGlueInfo = prNetDevPrivate->prGlueInfo;
	ucBssIndex = prNetDevPrivate->ucBssIdx;

	kalResetPacket(prGlueInfo, (void *) prSkb);

	kalHardStartXmit(prSkb, prDev, prGlueInfo, ucBssIndex);
	prP2pBssInfo = GET_BSS_INFO_BY_INDEX(prGlueInfo->prAdapter, ucBssIndex);
	if (!prP2pBssInfo)
		return NETDEV_TX_BUSY;
	if ((prP2pBssInfo->eConnectionState == MEDIA_STATE_CONNECTED) ||
		(prP2pBssInfo->rStaRecOfClientList.u4NumElem > 0))
		kalPerMonStart(prGlueInfo);

	return NETDEV_TX_OK;
}				/* end of p2pHardStartXmit() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief A method of struct net_device, a primary SOCKET interface to configure
 *        the interface lively. Handle an ioctl call on one of our devices.
 *        Everything Linux ioctl specific is done here.
 *        Then we pass the contents
 *        of the ifr->data to the request message handler.
 *
 * \param[in] prDev      Linux kernel netdevice
 *
 * \param[in] prIFReq    Our private ioctl request structure,
 *                       typed for the generic struct ifreq
 *                       so we can use ptr to function
 *
 * \param[in] cmd        Command ID
 *
 * \retval WLAN_STATUS_SUCCESS The IOCTL command is executed successfully.
 * \retval OTHER The execution of IOCTL command is failed.
 */
/*----------------------------------------------------------------------------*/

int p2pDoIOCTL(struct net_device *prDev, struct ifreq *prIfReq, int i4Cmd)
{
	struct GLUE_INFO *prGlueInfo = NULL;
	int ret = 0;
	/* char *prExtraBuf = NULL; */
	/* UINT_32 u4ExtraSize = 0; */
	/* struct iwreq *prIwReq = (struct iwreq *)prIfReq; */
	/* struct iw_request_info rIwReqInfo; */

	ASSERT(prDev && prIfReq);

	prGlueInfo = *((struct GLUE_INFO **) netdev_priv(prDev));
	if (!prGlueInfo) {
		DBGLOG(P2P, ERROR, "prGlueInfo is NULL\n");
		return -EFAULT;
	}

	if (prGlueInfo->u4ReadyFlag == 0) {
		DBGLOG(P2P, ERROR, "Adapter is not ready\n");
		return -EINVAL;
	}

	if (i4Cmd == SIOCGIWPRIV) {
		ret = wext_support_ioctl(prDev, prIfReq, i4Cmd);
	} else if ((i4Cmd >= SIOCIWFIRSTPRIV) && (i4Cmd < SIOCIWLASTPRIV)) {
		/* 0x8BE0 ~ 0x8BFF, private ioctl region */
		ret = priv_support_ioctl(prDev, prIfReq, i4Cmd);
	} else if (i4Cmd == SIOCDEVPRIVATE + 1) {
#ifdef CFG_ANDROID_AOSP_PRIV_CMD
		ret = android_private_support_driver_cmd(prDev, prIfReq, i4Cmd);
#else
		ret = priv_support_driver_cmd(prDev, prIfReq, i4Cmd);
#endif /* CFG_ANDROID_AOSP_PRIV_CMD */
	} else {
		DBGLOG(INIT, WARN, "Unexpected ioctl command: 0x%04x\n", i4Cmd);
		ret = -1;
	}

#if 0
	/* fill rIwReqInfo */
	rIwReqInfo.cmd = (__u16) i4Cmd;
	rIwReqInfo.flags = 0;

	switch (i4Cmd) {
	case SIOCSIWENCODEEXT:
		/* Set Encryption Material after 4-way handshaking is done */
		if (prIwReq->u.encoding.pointer) {
			u4ExtraSize = prIwReq->u.encoding.length;
			prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);

			if (!prExtraBuf) {
				ret = -ENOMEM;
				break;
			}

			if (copy_from_user(prExtraBuf,
				prIwReq->u.encoding.pointer,
				prIwReq->u.encoding.length))
				ret = -EFAULT;
		} else if (prIwReq->u.encoding.length != 0) {
			ret = -EINVAL;
			break;
		}

		if (ret == 0)
			ret = mtk_p2p_wext_set_key(prDev,
				&rIwReqInfo,
				&(prIwReq->u), prExtraBuf);

		kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
		prExtraBuf = NULL;
		break;

	case SIOCSIWMLME:
		/* IW_MLME_DISASSOC used for disconnection */
		if (prIwReq->u.data.length != sizeof(struct iw_mlme)) {
			DBGLOG(INIT, INFO,
				"MLME buffer strange:%d\n",
				prIwReq->u.data.length);
			ret = -EINVAL;
			break;
		}

		if (!prIwReq->u.data.pointer) {
			ret = -EINVAL;
			break;
		}

		prExtraBuf = kalMemAlloc(sizeof(struct iw_mlme), VIR_MEM_TYPE);
		if (!prExtraBuf) {
			ret = -ENOMEM;
			break;
		}

		if (copy_from_user(prExtraBuf,
			prIwReq->u.data.pointer, sizeof(struct iw_mlme)))
			ret = -EFAULT;
		else
			ret = mtk_p2p_wext_mlme_handler(prDev,
				&rIwReqInfo, &(prIwReq->u), prExtraBuf);

		kalMemFree(prExtraBuf, VIR_MEM_TYPE, sizeof(struct iw_mlme));
		prExtraBuf = NULL;
		break;

	case SIOCGIWPRIV:
		/* This ioctl is used to list all IW privilege ioctls */
		ret = mtk_p2p_wext_get_priv(prDev,
			&rIwReqInfo, &(prIwReq->u), NULL);
		break;

	case SIOCGIWSCAN:
		ret = mtk_p2p_wext_discovery_results(prDev,
			&rIwReqInfo, &(prIwReq->u), NULL);
		break;

	case SIOCSIWAUTH:
		ret = mtk_p2p_wext_set_auth(prDev,
			&rIwReqInfo, &(prIwReq->u), NULL);
		break;

	case IOC_P2P_CFG_DEVICE:
	case IOC_P2P_PROVISION_COMPLETE:
	case IOC_P2P_START_STOP_DISCOVERY:
	case IOC_P2P_DISCOVERY_RESULTS:
	case IOC_P2P_WSC_BEACON_PROBE_RSP_IE:
	case IOC_P2P_CONNECT_DISCONNECT:
	case IOC_P2P_PASSWORD_READY:
	case IOC_P2P_GET_STRUCT:
	case IOC_P2P_SET_STRUCT:
	case IOC_P2P_GET_REQ_DEVICE_INFO:
#if 0
		ret = rP2PIwPrivHandler[i4Cmd - SIOCIWFIRSTPRIV](prDev,
				&rIwReqInfo,
				&(prIwReq->u),
				(char *)&(prIwReq->u));
#endif
		break;
#if CFG_SUPPORT_P2P_RSSI_QUERY
	case SIOCGIWSTATS:
		ret = mtk_p2p_wext_get_rssi(prDev,
			&rIwReqInfo, &(prIwReq->u), NULL);
		break;
#endif
	default:
		ret = -ENOTTY;
	}
#endif /* 0 */

	return ret;
}				/* end of p2pDoIOCTL() */


/*---------------------------------------------------------------------------*/
/*!
 * \brief To override p2p interface address
 *
 * \param[in] prDev Net device requested.
 * \param[in] addr  Pointer to address
 *
 * \retval 0 For success.
 * \retval -E2BIG For user's buffer size is too small.
 * \retval -EFAULT For fail.
 *
 */
/*---------------------------------------------------------------------------*/
int p2pSetMACAddress(IN struct net_device *prDev, void *addr)
{
	struct ADAPTER *prAdapter = NULL;
	struct GLUE_INFO *prGlueInfo = NULL;
	struct sockaddr *sa = NULL;
	struct BSS_INFO *prBssInfo = NULL;
	struct BSS_INFO *prDevBssInfo = NULL;
	uint8_t ucRoleIdx = 0, ucBssIdx = 0;
	struct GL_P2P_INFO *prP2pInfo = NULL;

	prGlueInfo = *((struct GLUE_INFO **) netdev_priv(prDev));
	ASSERT(prGlueInfo);

	prAdapter = prGlueInfo->prAdapter;
	ASSERT(prAdapter);

	if (!prDev || !addr) {
		DBGLOG(INIT, ERROR, "Set macaddr with ndev(%d) and addr(%d)\n",
		       (prDev == NULL) ? 0 : 1, (addr == NULL) ? 0 : 1);
		return -EINVAL;
	}

	sa = (struct sockaddr *)addr;

	if (mtk_Netdev_To_RoleIdx(prGlueInfo, prDev, &ucRoleIdx) != 0) {
		DBGLOG(INIT, ERROR, "can't find the matched dev");
		goto skip_role;
	}

	if (p2pFuncRoleToBssIdx(prGlueInfo->prAdapter,
		ucRoleIdx, &ucBssIdx) != WLAN_STATUS_SUCCESS) {
		DBGLOG(INIT, ERROR, "can't find the matched bss");
		goto skip_role;
	}

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIdx);
	if (!prBssInfo) {
		DBGLOG(INIT, ERROR, "bss is not active\n");
		goto skip_role;
	} else {
		COPY_MAC_ADDR(prBssInfo->aucOwnMacAddr,
			sa->sa_data);
		COPY_MAC_ADDR(
			prAdapter->rWifiVar.aucInterfaceAddress[ucRoleIdx],
			sa->sa_data);
	}

skip_role:

	prDevBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter,
		prAdapter->ucP2PDevBssIdx);
	if (!prDevBssInfo) {
		DBGLOG(INIT, ERROR, "dev bss is not active\n");
		return -EINVAL;
	}

	prP2pInfo = prGlueInfo->prP2PInfo[0];
	if (!prP2pInfo) {
		DBGLOG(INIT, ERROR, "p2p info is null\n");
		return -EINVAL;
	}

	dev_addr_set(prDev, sa->sa_data);

	if ((prP2pInfo->prDevHandler == prDev)
		&& mtk_IsP2PNetDevice(prGlueInfo, prDev)) {
		COPY_MAC_ADDR(prAdapter->rWifiVar.aucDeviceAddress,
			sa->sa_data);
		COPY_MAC_ADDR(prDevBssInfo->aucOwnMacAddr, sa->sa_data);
		DBGLOG(INIT, INFO,
			"[%d][%d] Set random macaddr to " MACSTR ".\n",
			ucBssIdx,
			prDevBssInfo->ucBssIndex,
			MAC2STR(prDevBssInfo->aucOwnMacAddr));
	} else if (prBssInfo) {
		DBGLOG(INIT, INFO,
			"[%d] Set random macaddr to " MACSTR ".\n",
			ucBssIdx,
			MAC2STR(prBssInfo->aucOwnMacAddr));
	}

	return WLAN_STATUS_SUCCESS;
}

