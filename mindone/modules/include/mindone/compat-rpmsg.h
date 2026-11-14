/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_RPMSG_H
#define MINDONE_COMPAT_RPMSG_H

#include <linux/rpmsg.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_RPMSG_SEND_OFFCHANNEL_OP
#define MINDONE_RPMSG_TRYSEND_OFFCHANNEL_OP
#else
#define MINDONE_RPMSG_SEND_OFFCHANNEL_OP \
	int (*send_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst, void *data, int len);
#define MINDONE_RPMSG_TRYSEND_OFFCHANNEL_OP \
	int (*trysend_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst, void *data, int len);
#endif

#endif
