// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include "precomp.h"
#include "rxfilter_driver.h"

static DEFINE_SPINLOCK(rxfilter_nonce_lock);
static struct rxfilter_nonce rxfilter_nonces;

static void rxfilterDriverFinish(struct ADAPTER *adapter, int error, int complete)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;

	if (!driver->waiting)
		return;
	if (error) {
		driver->control.valid &= ~(1U << driver->control.bss);
		driver->control.desired[driver->control.bss] =
			driver->committed[driver->control.bss];
	} else {
		driver->committed[driver->control.bss] =
			driver->control.desired[driver->control.bss];
	}
	driver->error = error;
	driver->waiting = 0;
	rxfilter_cancel(&driver->control);
	if (complete)
		complete_all(&driver->completion);
}

static void rxfilterDriverTimeout(struct ADAPTER *adapter,
		struct CMD_INFO *command)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;

	spin_lock_irqsave(&driver->lock, flags);
	if (command->u4PrivateData == driver->owner)
		rxfilterDriverFinish(adapter, -EIO, 1);
	spin_unlock_irqrestore(&driver->lock, flags);
}

int rxfilterDriverCommandLive(struct ADAPTER *adapter, struct CMD_INFO *command)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;
	int live;

	if (command->pfCmdTimeoutHandler != rxfilterDriverTimeout)
		return 1;
	spin_lock_irqsave(&driver->lock, flags);
	live = driver->live && driver->waiting && driver->owner &&
		command->u4PrivateData == driver->owner;
	spin_unlock_irqrestore(&driver->lock, flags);
	return live;
}

static struct CMD_INFO *rxfilterDriverAllocate(struct ADAPTER *adapter,
		unsigned int length, int query)
{
	struct CMD_INFO *command;
	u8 *payload;
	u16 size;

	if (length > 65535U - adapter->chip_info->u2CmdTxHdrSize)
		return NULL;
	size = adapter->chip_info->u2CmdTxHdrSize + length;

	command = cmdBufAllocateCmdInfo(adapter, size);
	if (!command)
		return NULL;
	command->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
	command->u2InfoBufLen = size;
	command->ucCID = CMD_ID_MAC_MCAST_ADDR;
	command->fgSetQuery = !query;
	command->fgNeedResp = FALSE;
	command->fgIsOid = FALSE;
	command->fgRxfilterQuery = query;
	command->u4SetInfoLen = length;
	command->pfCmdTimeoutHandler = rxfilterDriverTimeout;
	NIC_FILL_CMD_TX_HDR(adapter, command->pucInfoBuffer, size,
		command->ucCID, CMD_PACKET_TYPE_ID, &command->ucCmdSeqNum,
		command->fgSetQuery, &payload, FALSE, 0, S2D_INDEX_CMD_H2N);
	return command;
}

static int rxfilterDriverQuery(struct ADAPTER *adapter, struct CMD_INFO *command,
		unsigned int bss)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	u8 request[12];
	unsigned long flags;
	int result;

	spin_lock_irqsave(&rxfilter_nonce_lock, flags);
	result = rxfilter_query_begin(&driver->control, &rxfilter_nonces,
		bss, command->ucCmdSeqNum, request);
	spin_unlock_irqrestore(&rxfilter_nonce_lock, flags);
	if (result)
		return result;
	memcpy(command->pucInfoBuffer + adapter->chip_info->u2CmdTxHdrSize,
		request, sizeof(request));
	if (!driver->owner)
		driver->owner = driver->control.pending_nonce;
	command->u4PrivateData = driver->owner;
	kalEnqueueCommand(adapter->prGlueInfo, &command->rQueEntry);
	return 0;
}

static int rxfilterDriverControls(struct rxfilter_driver *driver)
{
	unsigned int bss = driver->control.bss;
	const u8 *request = driver->cache.bss[bss].payload;
	const u8 *observed = driver->control.observed_controls[bss];
	u8 expected[3];
	unsigned int failure;

	for (failure = request[0] == 255; failure < 2; failure++) {
		if (failure && !request[0])
			break;
		rxfilter_mcast_normalize(request + 5, failure, expected);
		if (driver->control.desired[bss] & 4) {
			expected[0] = ((driver->control.desired[bss] + 4) << 4) | 3;
			expected[1] = expected[0];
		}
		if (!memcmp(expected, observed, 3))
			return 1;
	}
	return 0;
}

static void rxfilterDriverDone(struct ADAPTER *adapter,
		u8 sequence, const u8 *payload)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	struct CMD_INFO *write = NULL, *query = NULL;
	u8 encoded[200], *write_payload;
	unsigned int length = 0;
	unsigned long flags;
	u64 revision;
	int result, probe;
	u32 nonce = (u32)payload[4] | (u32)payload[5] << 8 |
		(u32)payload[6] << 16 | (u32)payload[7] << 24;

	write = rxfilterDriverAllocate(adapter, sizeof(encoded), 0);
	query = rxfilterDriverAllocate(adapter, 12, 1);
	spin_lock_irqsave(&driver->lock, flags);
	if (!driver->live || !driver->waiting ||
	    sequence != driver->control.sequence ||
	    nonce != driver->control.pending_nonce)
		goto unlock;
	if (!rxfilter_mcast_still_current(&driver->cache, driver->control.bss,
	    driver->revision)) {
		rxfilterDriverFinish(adapter, -EAGAIN, 1);
		goto unlock;
	}
	probe = driver->control.phase == RXFILTER_PROBE;
	result = rxfilter_event(&driver->control, 0xfd,
		sequence, payload, 12);
	if (result == RXFILTER_CONFIRMED && !rxfilterDriverControls(driver)) {
		result = probe ? RXFILTER_NEEDS_WRITE : -EIO;
		if (probe)
			driver->control.phase = RXFILTER_WRITE;
	}
	if (result != RXFILTER_NEEDS_WRITE) {
		rxfilterDriverFinish(adapter,
			result == RXFILTER_CONFIRMED ? 0 :
			result == RXFILTER_IGNORED ? -EPROTO : result, 1);
		goto unlock;
	}
	if (!write || !query) {
		rxfilterDriverFinish(adapter, -ENOMEM, 1);
		goto unlock;
	}
	result = rxfilter_mcast_encode(&driver->cache, driver->control.bss,
		driver->control.desired[driver->control.bss], encoded,
		sizeof(encoded), &length, &revision);
	if (result) {
		rxfilterDriverFinish(adapter, result, 1);
		goto unlock;
	}
	write->u2InfoBufLen = adapter->chip_info->u2CmdTxHdrSize + length;
	write->u4SetInfoLen = length;
	NIC_FILL_CMD_TX_HDR(adapter, write->pucInfoBuffer, write->u2InfoBufLen,
		write->ucCID, CMD_PACKET_TYPE_ID, &write->ucCmdSeqNum,
		write->fgSetQuery, &write_payload, FALSE, 0, S2D_INDEX_CMD_H2N);
	memcpy(write_payload, encoded, length);
	result = rxfilter_write_queued(&driver->control);
	if (result) {
		rxfilterDriverFinish(adapter, result, 1);
		goto unlock;
	}
	write->u4PrivateData = driver->owner;
	kalEnqueueCommand(adapter->prGlueInfo, &write->rQueEntry);
	write = NULL;
	result = rxfilterDriverQuery(adapter, query, driver->control.bss);
	if (result)
		rxfilterDriverFinish(adapter, result, 1);
	else
		query = NULL;
	GLUE_SET_EVENT(adapter->prGlueInfo);
unlock:
	spin_unlock_irqrestore(&driver->lock, flags);
	if (write)
		cmdBufFreeCmdInfo(adapter, write);
	if (query)
		cmdBufFreeCmdInfo(adapter, query);
}

static void rxfilterDriverReplay(struct work_struct *work)
{
	struct rxfilter_driver *driver = container_of(work,
		struct rxfilter_driver, work);
	unsigned int attempt, bss;
	unsigned long flags;

	for (attempt = 0; attempt < 4; attempt++) {
		spin_lock_irqsave(&driver->lock, flags);
		if (!driver->live || !driver->dirty) {
			spin_unlock_irqrestore(&driver->lock, flags);
			return;
		}
		for (bss = 0; bss < 4; bss++)
			if (driver->dirty & (1U << bss))
				break;
		driver->dirty &= ~(1U << bss);
		spin_unlock_irqrestore(&driver->lock, flags);
		rxfilterDriverApply(driver->adapter, bss, -1);
	}
	spin_lock_irqsave(&driver->lock, flags);
	if (driver->live && driver->dirty)
		schedule_work(&driver->work);
	spin_unlock_irqrestore(&driver->lock, flags);
}

void rxfilterDriverInit(struct ADAPTER *adapter)
{
	spin_lock_init(&adapter->rxfilter.lock);
	adapter->rxfilter.adapter = adapter;
	INIT_WORK(&adapter->rxfilter.work, rxfilterDriverReplay);
	init_completion(&adapter->rxfilter.completion);
	rxfilter_init(&adapter->rxfilter.control);
	memset(adapter->rxfilter.committed, 3, 4);
}

void rxfilterDriverStart(struct ADAPTER *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->rxfilter.lock, flags);
	rxfilterDriverFinish(adapter, -ESHUTDOWN, 1);
	rxfilter_reset(&adapter->rxfilter.control);
	adapter->rxfilter.live = 1;
	spin_unlock_irqrestore(&adapter->rxfilter.lock, flags);
}

void rxfilterDriverReady(struct ADAPTER *adapter)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;
	unsigned int bss;

	spin_lock_irqsave(&driver->lock, flags);
	if (driver->live) {
		for (bss = 0; bss < 4; bss++)
			if ((driver->committed[bss] & 4) && driver->cache.bss[bss].valid)
				driver->dirty |= 1U << bss;
		if (driver->dirty)
			schedule_work(&driver->work);
	}
	spin_unlock_irqrestore(&driver->lock, flags);
}

void rxfilterDriverStop(struct ADAPTER *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->rxfilter.lock, flags);
	adapter->rxfilter.live = 0;
	adapter->rxfilter.dirty = 0;
	rxfilterDriverFinish(adapter, -ESHUTDOWN, 1);
	rxfilter_reset(&adapter->rxfilter.control);
	spin_unlock_irqrestore(&adapter->rxfilter.lock, flags);
	cancel_work_sync(&adapter->rxfilter.work);
}

void rxfilterDriverInvalidate(struct ADAPTER *adapter, unsigned int bss)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;

	if (bss >= 4)
		return;
	spin_lock_irqsave(&driver->lock, flags);
	driver->cache.bss[bss].valid = 0;
	driver->control.valid &= ~(1U << bss);
	driver->dirty &= ~(1U << bss);
	if (driver->waiting && driver->control.bss == bss)
		rxfilterDriverFinish(adapter, -EIO, 1);
	spin_unlock_irqrestore(&driver->lock, flags);
}

int rxfilterDriverEnqueue(struct ADAPTER *adapter, struct CMD_INFO *command,
		const u8 *payload, unsigned int length)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;
	unsigned int bss;
	int result;

	if (command->ucCID != CMD_ID_MAC_MCAST_ADDR || !command->fgSetQuery)
		return 0;
	spin_lock_irqsave(&driver->lock, flags);
	if (payload && length >= 8 && payload[4] < 4 &&
	    driver->waiting && driver->control.bss == payload[4])
		rxfilterDriverFinish(adapter, -EAGAIN, 1);
	result = rxfilter_mcast_record(&driver->cache, &driver->control,
		payload, length);
	if (!result) {
		bss = payload[4];
		if (driver->live && (driver->control.capable & (1U << bss)) &&
		    (driver->committed[bss] & 4)) {
			u8 *encoded = command->pucInfoBuffer +
				adapter->chip_info->u2CmdTxHdrSize;

			encoded[5] = ((driver->committed[bss] + 4) << 4) | 3;
			encoded[6] = encoded[5];
		}
	} else if (payload && length >= 8 && payload[4] < 4) {
		driver->cache.bss[payload[4]].valid = 0;
		rxfilter_external_write(&driver->control, payload[4]);
	}
	kalEnqueueCommand(adapter->prGlueInfo, &command->rQueEntry);
	if (!result && driver->live && (driver->committed[payload[4]] & 4)) {
		driver->dirty |= 1U << payload[4];
		schedule_work(&driver->work);
	}
	spin_unlock_irqrestore(&driver->lock, flags);
	return 1;
}

static int rxfilterDriverBegin(struct ADAPTER *adapter, unsigned int bss,
		int operation, u32 *owner)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	struct CMD_INFO *query;
	unsigned long flags;
	int result;

	query = rxfilterDriverAllocate(adapter, 12, 1);
	if (!query)
		return -ENOMEM;
	spin_lock_irqsave(&driver->lock, flags);
	result = !driver->live || kalIsResetting() ? -ESHUTDOWN :
		driver->owner ? -EBUSY : bss >= 4 ? -EINVAL :
		!driver->cache.bss[bss].valid ? -ENODATA : 0;
	if (!result && operation == -1) {
		if (!(driver->committed[bss] & 4))
			result = -ECANCELED;
		operation = RXFILTER_START;
	}
	if (!result)
		result = rxfilter_set_desired(&driver->control, bss, operation);
	if (!result) {
		driver->revision = driver->cache.bss[bss].revision;
		result = rxfilterDriverQuery(adapter, query, bss);
	}
	if (!result) {
		driver->waiting = 1;
		driver->error = -EINPROGRESS;
		reinit_completion(&driver->completion);
		*owner = driver->owner;
	} else if (!driver->owner && bss < 4) {
		driver->control.desired[bss] = driver->committed[bss];
	}
	spin_unlock_irqrestore(&driver->lock, flags);
	if (result)
		cmdBufFreeCmdInfo(adapter, query);
	else
		GLUE_SET_EVENT(adapter->prGlueInfo);
	return result;
}

static int rxfilterDriverRelease(struct ADAPTER *adapter, u32 owner)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	unsigned long flags;
	int result;

	spin_lock_irqsave(&driver->lock, flags);
	if (!owner || driver->owner != owner) {
		result = -ESTALE;
	} else {
		rxfilterDriverFinish(adapter, -ETIMEDOUT, 0);
		result = driver->error;
		driver->owner = 0;
	}
	spin_unlock_irqrestore(&driver->lock, flags);
	return result;
}

int rxfilterDriverApply(struct ADAPTER *adapter, unsigned int bss, int operation)
{
	u32 owner;
	int result;

	if (down_interruptible(&g_halt_sem))
		return -ERESTARTSYS;
	if (g_u4HaltFlag || !adapter->prGlueInfo->u4ReadyFlag) {
		up(&g_halt_sem);
		return -ESHUTDOWN;
	}
	result = rxfilterDriverBegin(adapter, bss, operation, &owner);
	if (!result) {
		wait_for_completion_timeout(&adapter->rxfilter.completion,
			msecs_to_jiffies(2000));
		result = rxfilterDriverRelease(adapter, owner);
	}
	up(&g_halt_sem);
	return result;
}

int rxfilterDriverEvent(struct ADAPTER *adapter, const u8 *packet,
		unsigned int length)
{
	struct rxfilter_driver *driver = &adapter->rxfilter;
	const u8 *payload;
	u8 sequence;
	u32 nonce;
	unsigned int i;
	unsigned long flags;
	int match;

	if (!packet || length < 10 || packet[4] != 0xfd || packet[9] != 0xc1)
		return 0;
	if (rxfilter_wire_reply(packet, length, &sequence, &payload) != 1)
		return 1;
	nonce = 0;
	for (i = 0; i < 4; i++)
		nonce |= (u32)payload[4 + i] << (i * 8);
	spin_lock_irqsave(&driver->lock, flags);
	match = driver->live && driver->waiting &&
		driver->control.sequence == sequence &&
		driver->control.pending_nonce == nonce;
	spin_unlock_irqrestore(&driver->lock, flags);
	if (match)
		rxfilterDriverDone(adapter, sequence, payload);
	return 1;
}
