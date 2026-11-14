// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/poll.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "ccci_config.h"
#include "ccci_common_config.h"
#include "ccci_bm.h"
#include "port_proxy.h"
#include "port_char.h"
#include "port_smem.h"
#include "ccci_fsm.h"
#include "ccci_ipc_task_ID.h"

#define MAX_QUEUE_LENGTH 32

unsigned int port_char_dev_poll(struct file *fp,
	struct poll_table_struct *poll)
{
	struct port_t *port = fp->private_data;
	unsigned int mask = 0;
	int md_id = port->md_id;
	int md_state = ccci_fsm_get_md_state(md_id);

	CCCI_DEBUG_LOG(md_id, CHAR, "poll on %s\n", port->name);
	poll_wait(fp, &port->rx_wq, poll);
	/* TODO: lack of poll wait for Tx */
	if (!skb_queue_empty(&port->rx_skb_list))
		mask |= POLLIN | POLLRDNORM;
	if (port_write_room_to_md(port) > 0)
		mask |= POLLOUT | POLLWRNORM;
	if (port->rx_ch == CCCI_UART1_RX &&
	    md_state != READY &&
		md_state != EXCEPTION) {
		/* notify MD logger to save its log
		 * before md_init kills it
		 */
		mask |= POLLERR;
		CCCI_NORMAL_LOG(md_id, CHAR,
			"poll error for MD logger at state %d,mask=%d\n",
			md_state, mask);
	}

	return mask;
}

static const struct file_operations char_dev_fops = {
	.owner = THIS_MODULE,
	.open = &port_dev_open, /*use default API*/
	.read = &port_dev_read, /*use default API*/
	.write = &port_dev_write, /*use default API*/
	.release = &port_dev_close,/*use default API*/
	.unlocked_ioctl = &port_dev_ioctl,/*use default API*/
#ifdef CONFIG_COMPAT
	.compat_ioctl = &port_dev_compat_ioctl,/*use default API*/
#endif
	.poll = &port_char_dev_poll,/*use port char self API*/
	.mmap = &port_dev_mmap,
};
static int port_char_init(struct port_t *port)
{
	struct cdev *dev = NULL;
	int ret = 0;
	int md_id = port->md_id;

	CCCI_DEBUG_LOG(md_id, CHAR,
		"char port %s is initializing\n", port->name);
	port->rx_length_th = MAX_QUEUE_LENGTH;
	port->skb_from_pool = 1;
	port->interception = 0;
	if (port->flags & PORT_F_WITH_CHAR_NODE) {
		dev = kmalloc(sizeof(struct cdev), GFP_KERNEL);
		if (unlikely(!dev)) {
			CCCI_ERROR_LOG(port->md_id, CHAR,
				"alloc char dev fail!!\n");
			return -1;
		}
		cdev_init(dev, &char_dev_fops);
		dev->owner = THIS_MODULE;
		ret = cdev_add(dev, MKDEV(port->major,
				port->minor_base + port->minor), 1);
		ret = ccci_register_dev_node(port->name, port->major,
				port->minor_base + port->minor);
		port->flags |= PORT_F_ADJUST_HEADER;
	}
#ifndef DPMAIF_DEBUG_LOG
	if (port->rx_ch == CCCI_UART2_RX ||
		port->rx_ch == CCCI_C2K_AT ||
		port->rx_ch == CCCI_C2K_AT2 ||
		port->rx_ch == CCCI_C2K_AT3 ||
		port->rx_ch == CCCI_C2K_AT4 ||
		port->rx_ch == CCCI_C2K_AT5 ||
		port->rx_ch == CCCI_C2K_AT6 ||
		port->rx_ch == CCCI_C2K_AT7 ||
		port->rx_ch == CCCI_C2K_AT8)
		port->flags |= PORT_F_CH_TRAFFIC;
	else if (port->rx_ch == CCCI_FS_RX)
		port->flags |= (PORT_F_CH_TRAFFIC | PORT_F_DUMP_RAW_DATA);
#endif
	return ret;
}

static int port_char_recv_skb(struct port_t *port, struct sk_buff *skb)
{
	int md_id = port->md_id;

	if (!atomic_read(&port->usage_cnt) &&
		(port->rx_ch != CCCI_UART2_RX &&
		port->rx_ch != CCCI_C2K_AT &&
		port->rx_ch != CCCI_PCM_RX &&
		port->rx_ch != CCCI_FS_RX &&
		port->rx_ch != CCCI_RPC_RX &&
		port->rx_ch != CCCI_UDC_RX &&
		!(port->rx_ch == CCCI_IPC_RX &&
		port->minor ==
		AP_IPC_LWAPROXY + CCCI_IPC_MINOR_BASE)))
		return -CCCI_ERR_DROP_PACKET;

	CCCI_DEBUG_LOG(md_id, CHAR, "recv on %s, len=%d\n",
		port->name, port->rx_skb_list.qlen);
	return port_recv_skb(port, skb);
}

void port_char_dump_info(struct port_t *port, unsigned int flag)
{
	if (port == NULL) {
		CCCI_ERROR_LOG(0, CHAR, "%s: port==NULL\n", __func__);
		return;
	}
	if (atomic_read(&port->usage_cnt) == 0)
		return;
	if (port->flags & PORT_F_CH_TRAFFIC)
		CCCI_REPEAT_LOG(port->md_id, CHAR,
			"CHR:(%d):%dR(%d,%d,%d):%dT(%d)\n",
			port->flags, port->rx_ch,
			port->rx_skb_list.qlen,
			atomic_read(&port->rx_pkg_cnt), port->rx_drop_cnt,
			port->tx_ch, port->tx_pkg_cnt);
}
struct port_ops char_port_ops = {
	.init = &port_char_init,
	.recv_skb = &port_char_recv_skb,
	.dump_info = &port_char_dump_info,
};

