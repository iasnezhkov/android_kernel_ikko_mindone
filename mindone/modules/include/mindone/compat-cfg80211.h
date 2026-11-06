/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_CFG80211_H
#define MINDONE_COMPAT_CFG80211_H

#include <net/cfg80211.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_CFG80211_OP_DEV struct wireless_dev
#define MINDONE_CFG80211_NETDEV(dev) ((dev)->netdev)
#define MINDONE_CFG80211_RADIO_IDX int radio_idx,
#define MINDONE_CFG80211_TXPOWER_LINK unsigned int link_id,
#define MINDONE_CFG80211_ROC_RX_ADDR , const u8 *rx_addr
#define MINDONE_CFG80211_STA_EVENT_DEV(ndev) ((ndev)->ieee80211_ptr)
#else
#define MINDONE_CFG80211_OP_DEV struct net_device
#define MINDONE_CFG80211_NETDEV(dev) (dev)
#define MINDONE_CFG80211_RADIO_IDX
#define MINDONE_CFG80211_TXPOWER_LINK
#define MINDONE_CFG80211_ROC_RX_ADDR
#define MINDONE_CFG80211_STA_EVENT_DEV(ndev) (ndev)
#endif

#endif
