/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-cfg80211.h — cfg80211 pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_CFG80211_H__
#define __MINDONE_COMPAT_CFG80211_H__

#include <linux/version.h>
#include <net/cfg80211.h>

/* cfg80211_ops::change_beacon() takes a cfg80211_ap_update wrapper since 6.7. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_CFG_BEACON_PARAM		struct cfg80211_ap_update *info
#define MINDONE_CFG_BEACON_DATA(_info)		(&(_info)->beacon)
#else
#define MINDONE_CFG_BEACON_PARAM		struct cfg80211_beacon_data *info
#define MINDONE_CFG_BEACON_DATA(_info)		(_info)
#endif

/* tdls_mgmt() and start_radar_detection() carry a link_id in 6.12 (MLO); 6.1 has none. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_CFG_TDLS_LINK_ID		int link_id,
#define MINDONE_CFG_RADAR_LINK_ID		, int link_id
#else
#define MINDONE_CFG_TDLS_LINK_ID
#define MINDONE_CFG_RADAR_LINK_ID
#endif

/* cfg80211_ch_switch_notify() lost the punct_bitmap argument in 6.9. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_CH_SWITCH_NOTIFY(_dev, _chandef, _link)	cfg80211_ch_switch_notify((_dev), (_chandef), (_link))
#else
#define MINDONE_CH_SWITCH_NOTIFY(_dev, _chandef, _link)	cfg80211_ch_switch_notify((_dev), (_chandef), (_link), 0)
#endif

#endif /* __MINDONE_COMPAT_CFG80211_H__ */
