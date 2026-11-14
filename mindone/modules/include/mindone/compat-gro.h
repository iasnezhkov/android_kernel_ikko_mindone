/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_GRO_H
#define MINDONE_COMPAT_GRO_H

#include <linux/netdevice.h>
#include <linux/version.h>
#include <net/gro.h>

static inline void mindone_napi_gro_flush_list(struct napi_struct *napi)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	struct gro_node *gro = &napi->gro;
#else
	struct napi_struct *gro = napi;
#endif

	napi_gro_flush(napi, false);
	if (gro->rx_count) {
		netif_receive_skb_list(&gro->rx_list);
		INIT_LIST_HEAD(&gro->rx_list);
		gro->rx_count = 0;
	}
}

#endif
