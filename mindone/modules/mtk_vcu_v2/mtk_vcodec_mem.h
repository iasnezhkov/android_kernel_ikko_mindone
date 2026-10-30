/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 MediaTek Inc. */

#ifndef MTK_VCODEC_MEM_H
#define MTK_VCODEC_MEM_H

#include <media/videobuf2-dma-contig.h>
#include <linux/mtk_vcu_controls.h>
#include <linux/slab.h>
#include <linux/refcount.h>
#include <asm/cacheflush.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#ifdef CMDQ_SEC_SUPPORT
#include "mailbox/cmdq-sec.h"
#endif
#ifndef CONFIG_ARM64
#include "mm/dma.h"
#endif

#ifndef dmac_map_area
#define dmac_map_area __dma_map_area
#endif
#ifndef dmac_unmap_area
#define dmac_unmap_area __dma_unmap_area
#endif
#ifndef dmac_flush_range
#define dmac_flush_range __dma_flush_range
#endif

#define CODEC_MAX_BUFFER 768U
#define CODEC_ALLOCATE_MAX_BUFFER_SIZE 0x20000000UL
#define CODEC_MSK(addr) ((addr >> PAGE_SHIFT) & 0xFFFF)

struct mtk_vcu_mem {
	void *mem_priv;
	struct vb2_buffer vb;
	size_t size;
	struct dma_buf *dbuf;
	dma_addr_t iova;
	atomic_t ref_cnt;
	uint64_t va_id;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
};

struct vcu_pa_pages {
	unsigned long pa;
	unsigned long kva;
	atomic_t ref_cnt;
	struct list_head list;
};

struct vcu_page_info {
	struct vcu_pa_pages *page;
	struct list_head list;
};

struct mtk_vcu_sec_handle {
	void *mem_priv;
	size_t size;
	struct dma_buf *dbuf;
	uint32_t sec_handle;
	atomic_t ref_cnt;
};

struct mtk_vcu_queue {
	refcount_t refs;
	atomic_t jobs;
	void *vcu;
	struct mutex mmap_lock;
	struct device *dev;
	struct mutex dev_lock;
	struct cmdq_client *cmdq_clt;
	unsigned int num_buffers;
	unsigned int num_sec_handles;
	unsigned int num_sec_buffers;
	const struct vb2_mem_ops *mem_ops;
	struct vb2_queue vb_queue;
	struct mtk_vcu_mem bufs[CODEC_MAX_BUFFER];
	struct mtk_vcu_sec_handle sec_handles[CODEC_MAX_BUFFER];
	struct mtk_vcu_mem sec_bufs[CODEC_MAX_BUFFER];
	uint64_t map_buf_pa;
	struct vcu_pa_pages pa_pages;
	int enable_vcu_dbg_log;
	uint32_t map_buf_type;
};

struct mtk_vcu_queue *mtk_vcu_mem_init(struct device *dev,
	struct cmdq_client *cmdq_clt);

void mtk_vcu_mem_release(struct mtk_vcu_queue *vcu_queue);
void mtk_vcu_mem_job_get(struct mtk_vcu_queue *vcu_queue);
void mtk_vcu_mem_job_put(struct mtk_vcu_queue *vcu_queue);

void *mtk_vcu_set_buffer(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data, struct vb2_buffer *src_vb,
	struct vb2_buffer *dst_vb, struct dma_buf **map_dbuf, size_t *offset);

void *mtk_vcu_get_buffer(struct mtk_vcu_queue *vcu_queue,
						 struct mem_obj *mem_buff_data);
void *mtk_vcu_get_page(struct mtk_vcu_queue *vcu_queue,
						 struct mem_obj *mem_buff_data);
void *mtk_vcu_get_sec_handle(struct mtk_vcu_queue *vcu_queue,
						 struct mem_obj *mem_buff_data);
void *mtk_vcu_get_sec_buffer(struct device *dev,
						struct mtk_vcu_queue *vcu_queue,
						 struct mem_obj *mem_buff_data);

int mtk_vcu_free_buffer(struct mtk_vcu_queue *vcu_queue,
						struct mem_obj *mem_buff_data);
int mtk_vcu_free_page(struct mtk_vcu_queue *vcu_queue,
						struct mem_obj *mem_buff_data);
int mtk_vcu_free_sec_handle(struct mtk_vcu_queue *vcu_queue,
						struct mem_obj *mem_buff_data);
int mtk_vcu_free_sec_buffer(struct mtk_vcu_queue *vcu_queue,
						struct mem_obj *mem_buff_data);

void mtk_vcu_buffer_ref_dec(struct mtk_vcu_queue *vcu_queue,
	void *mem_priv);

int vcu_buffer_flush_all(struct device *dev, struct mtk_vcu_queue *vcu_queue);

int vcu_buffer_cache_sync(struct device *dev, struct mtk_vcu_queue *vcu_queue,
	dma_addr_t dma_addr, size_t size, int op);

int mtk_vcu_get_dma_addr(struct device *dev, struct mtk_vcu_queue *vcu_queue,
				   int share_fd, dma_addr_t *dma_addr, int *is_sec);
#endif
