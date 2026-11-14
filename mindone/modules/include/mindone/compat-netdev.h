/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_NETDEV_H
#define MINDONE_COMPAT_NETDEV_H

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/version.h>

static inline int mindone_dev_set_ether_mac(struct net_device *dev, struct sockaddr_storage *ss,
					    struct netlink_ext_ack *extack)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return dev_set_mac_address(dev, ss, extack);
#else
	struct sockaddr sa = { .sa_family = ss->ss_family };

	memcpy(sa.sa_data, ss->__data, ETH_ALEN);
	return dev_set_mac_address(dev, &sa, extack);
#endif
}

#endif
