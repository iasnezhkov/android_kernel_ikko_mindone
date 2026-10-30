/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#ifndef __VOW_SCP_H__
#define __VOW_SCP_H__

#include <linux/types.h>

#define VOW_IPI_HEADER_LENGTH         2
#define VOW_IPI_SEND_BUFFER_LENGTH    7
#define VOW_IPI_RECEIVE_LENGTH        24
#define VOW_IPI_ACK_LENGTH            2

#define VOW_IPI_WAIT_ACK_TIMEOUT      10
#define VOW_IPI_RESEND_TIMES          2

enum {
	VOW_IPI_BYPASS_ACK = 0,
	VOW_IPI_NEED_ACK,
	VOW_IPI_ACK_BACK
};

struct vow_ipi_send_info {
	unsigned char msg_id;
	unsigned char msg_need_ack;
	unsigned char param1;
	unsigned char param2;
	unsigned int msg_length;
	unsigned int payload[VOW_IPI_SEND_BUFFER_LENGTH];
};

struct vow_ipi_receive_info {
	unsigned char msg_id;
	unsigned char msg_need_ack;
	unsigned char param1;
	unsigned char param2;
	unsigned int msg_length;
	unsigned int msg_data[VOW_IPI_RECEIVE_LENGTH];
};

struct vow_ipi_ack_info {
	unsigned char msg_id;
	unsigned char msg_need_ack;
	unsigned char param1;
	unsigned char param2;
	unsigned int msg_data;
};

unsigned int vow_check_scp_status(void);

int vow_ipi_register(void (*receive)(unsigned int, void *));
void vow_ipi_unregister(void);

bool vow_ipi_send(unsigned int msg_id,
		  unsigned int payload_len,
		  unsigned int *payload,
		  unsigned int need_ack);

#endif
