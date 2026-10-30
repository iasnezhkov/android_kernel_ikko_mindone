// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/soc/mediatek/mtk-mbox.h>
#include "scp.h"
#include "vow.h"

static DEFINE_MUTEX(vow_tx_lock);
static DECLARE_WAIT_QUEUE_HEAD(vow_ack_wait);
static unsigned int ack_id = UINT_MAX;
static unsigned int ack_data;
static void (*vow_receive)(unsigned int, void *);
static struct mtk_mbox_pin_recv *vow_pins[2];
static int vow_irq;

unsigned int vow_check_scp_status(void)
{
	return is_scp_ready(SCP_A_ID);
}

static int vow_receive_ipi(unsigned int id, void *private, void *data,
			   unsigned int len)
{
	struct vow_ipi_receive_info *msg = data;

	if (len < sizeof(*msg) || msg->msg_length > VOW_IPI_RECEIVE_LENGTH)
		return -EINVAL;
	vow_receive(msg->msg_id, msg->msg_data);
	return 0;
}

static int vow_receive_ack(unsigned int id, void *private, void *data,
			   unsigned int len)
{
	struct vow_ipi_ack_info *msg = data;

	if (len < sizeof(*msg) || msg->msg_need_ack != VOW_IPI_ACK_BACK)
		return -EINVAL;
	WRITE_ONCE(ack_data, msg->msg_data);
	WRITE_ONCE(ack_id, msg->msg_id);
	wake_up(&vow_ack_wait);
	return 0;
}

int vow_ipi_register(void (*receive)(unsigned int, void *))
{
	struct device_node *node;
	struct platform_device *pdev;
	struct mtk_mbox_pin_recv *pin;
	const unsigned int ids[] = { IPI_IN_AUDIO_VOW_1, IPI_IN_AUDIO_VOW_ACK_1 };
	mbox_pin_cb_t callbacks[] = { vow_receive_ipi, vow_receive_ack };
	int i, ret = 0;

	if (!scp_ipidev.ipi_inited)
		return -EPROBE_DEFER;
	node = of_find_compatible_node(NULL, NULL, "mediatek,scp");
	if (!node)
		return -ENODEV;
	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev)
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		pin = scp_ipidev.table[ids[i]].pin_recv;
		if (!pin || pin->cb_ctx_opt != MBOX_CB_IN_ISR || pin->mbox != 0 ||
		    pin->msg_size != (i ? PIN_IN_SIZE_AUDIO_VOW_ACK_1 :
					PIN_IN_SIZE_AUDIO_VOW_1)) {
			dev_err(&pdev->dev, "VoW mailbox layout does not match DT\n");
			ret = -EINVAL;
			goto put_device;
		}
		vow_pins[i] = pin;
	}
	if (!scp_ipidev.table[IPI_OUT_AUDIO_VOW_1].pin_send ||
	    scp_ipidev.table[IPI_OUT_AUDIO_VOW_1].pin_send->msg_size !=
	    PIN_OUT_SIZE_AUDIO_VOW_1) {
		ret = -EINVAL;
		goto put_device;
	}
	vow_irq = scp_ipidev.mbdev->info_table[0].irq_num;
	if (vow_irq <= 0) {
		ret = -EINVAL;
		goto put_device;
	}
	vow_receive = receive;
	disable_irq(vow_irq);
	mutex_lock(&scp_ipidev.mutex_ipi_reg);
	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		pin = vow_pins[i];
		if (pin->mbox_pin_cb || pin->prdata) {
			dev_err(&pdev->dev, "VoW mailbox already has an owner\n");
			ret = -EBUSY;
			goto unlock;
		}
		if (!pin->pin_buf) {
			pin->pin_buf = devm_kzalloc(&pdev->dev,
						pin->msg_size * sizeof(u32), GFP_KERNEL);
			if (!pin->pin_buf) {
				ret = -ENOMEM;
				goto unlock;
			}
		}
	}
	for (i = 0; i < ARRAY_SIZE(ids); i++)
		vow_pins[i]->mbox_pin_cb = callbacks[i];
unlock:
	mutex_unlock(&scp_ipidev.mutex_ipi_reg);
	enable_irq(vow_irq);
put_device:
	put_device(&pdev->dev);
	return ret;
}

void vow_ipi_unregister(void)
{
	int i;

	disable_irq(vow_irq);
	mutex_lock(&scp_ipidev.mutex_ipi_reg);
	for (i = 0; i < ARRAY_SIZE(vow_pins); i++) {
		vow_pins[i]->mbox_pin_cb = NULL;
		vow_pins[i]->prdata = NULL;
	}
	mutex_unlock(&scp_ipidev.mutex_ipi_reg);
	enable_irq(vow_irq);
	vow_receive = NULL;
}

bool vow_ipi_send(unsigned int id, unsigned int count, unsigned int *payload,
		  unsigned int need_ack)
{
	struct vow_ipi_send_info msg = {};
	unsigned int retry;
	int ret = -EIO;

	if (id > U8_MAX || count > ARRAY_SIZE(msg.payload) ||
	    (count && !payload))
		return false;
	msg.msg_id = id;
	msg.msg_need_ack = need_ack;
	msg.msg_length = count;
	if (count)
		memcpy(msg.payload, payload, count * sizeof(u32));
	mutex_lock(&vow_tx_lock);
	WRITE_ONCE(ack_id, UINT_MAX);
	for (retry = 0; retry < VOW_IPI_SEND_CNT_TIMEOUT; retry++) {
		if (!vow_check_scp_status()) {
			ret = -ENODEV;
			break;
		}
		ret = mtk_ipi_send(&scp_ipidev, IPI_OUT_AUDIO_VOW_1, 0, &msg,
				   PIN_OUT_SIZE_AUDIO_VOW_1, 0);
		if (ret == IPI_ACTION_DONE)
			break;
		msleep(VOW_WAITCHECK_INTERVAL_MS);
	}
	if (!ret && need_ack == VOW_IPI_NEED_ACK) {
		if (!wait_event_timeout(vow_ack_wait, READ_ONCE(ack_id) == id,
					msecs_to_jiffies(100)))
			ret = -ETIMEDOUT;
		else if (READ_ONCE(ack_data))
			ret = -EREMOTEIO;
	}
	mutex_unlock(&vow_tx_lock);
	return !ret;
}
EXPORT_SYMBOL_GPL(vow_ipi_send);
