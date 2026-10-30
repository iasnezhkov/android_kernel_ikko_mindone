// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "mtk-afe-external.h"
#include "scp.h"

static int vow_allocate(struct notifier_block *nb, unsigned long event, void *data)
{
	struct snd_pcm_substream *stream = data;
	struct snd_dma_buffer *buffer;
	phys_addr_t phys, virt, size;

	if (event != NOTIFIER_VOW_ALLOCATE_MEM)
		return NOTIFY_DONE;
	if (!stream || !stream->runtime || !stream->pcm || !stream->pcm->card)
		return NOTIFY_BAD;
	size = scp_get_reserve_mem_size(VOW_BARGEIN_MEM_ID);
	phys = scp_get_reserve_mem_phys(VOW_BARGEIN_MEM_ID);
	virt = scp_get_reserve_mem_virt(VOW_BARGEIN_MEM_ID);
	if (!phys || !virt || !stream->runtime->dma_bytes ||
	    stream->runtime->dma_bytes > size)
		return NOTIFY_BAD;
	buffer = &stream->dma_buffer;
	buffer->dev.type = SNDRV_DMA_TYPE_DEV;
	buffer->dev.dev = stream->pcm->card->dev;
	buffer->private_data = NULL;
	buffer->bytes = stream->runtime->dma_bytes;
	buffer->addr = phys;
	buffer->area = (unsigned char *)virt;
	stream->runtime->dma_addr = buffer->addr;
	stream->runtime->dma_area = buffer->area;
	return NOTIFY_STOP;
}

static struct notifier_block vow_allocate_notifier = {
	.notifier_call = vow_allocate,
};

static int __init mtk_scp_vow_init(void)
{
	return register_afe_allocate_mem_notifier(&vow_allocate_notifier);
}

module_init(mtk_scp_vow_init);
MODULE_AUTHOR("MediaTek Inc.");
MODULE_DESCRIPTION("MediaTek SCP VoW AFE bridge");
MODULE_LICENSE("GPL v2");
