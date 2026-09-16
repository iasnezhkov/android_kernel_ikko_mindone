/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-net.h — networking pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_NET_H__
#define __MINDONE_COMPAT_NET_H__

#include <linux/version.h>
#include <linux/netdevice.h>

/* struct rps_map moved to <net/rps.h> in 6.10 and RPS_MAP_SIZE() went away. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#include <net/rps.h>
#endif
#define MINDONE_RPS_MAP_SIZE(n)	(sizeof(struct rps_map) + (n) * sizeof(u16))

/* struct netdev_rx_queue moved out of netdevice.h into <net/netdev_rx_queue.h> in 6.6. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <net/netdev_rx_queue.h>
#endif

#endif /* __MINDONE_COMPAT_NET_H__ */
