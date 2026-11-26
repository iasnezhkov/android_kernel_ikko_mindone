/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef long ssize_t;
typedef unsigned long snd_pcm_uframes_t;

#define BIT(n) (1U << (n))
#define __iomem
#define GFP_KERNEL 0
#define SNDRV_PCM_STREAM_PLAYBACK 0
#define SNDRV_PCM_STREAM_CAPTURE 1
#define IRQF_TRIGGER_NONE 0
#define IRQF_TRIGGER_LOW 8

typedef struct { int locked; } spinlock_t;
#define spin_lock_irqsave(l, f) do { (void)(f); (l)->locked++; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); (l)->locked--; } while (0)

struct device { int dummy; };
#define dev_info(d, fmt, ...) do { (void)(d); } while (0)
#define dev_warn(d, fmt, ...) do { (void)(d); } while (0)
#define dev_dbg(d, fmt, ...) do { (void)(d); } while (0)
#define dev_err(d, fmt, ...) do { (void)(d); } while (0)

static int transfer_calls;
static void *last_transfer_dst;

static void mtk_btcvsd_snd_data_transfer(int dir, u8 *src, u8 *dst,
					 unsigned int blk_size, unsigned int blk_num)
{
	(void)dir; (void)src; (void)blk_size; (void)blk_num;
	transfer_calls++;
	last_transfer_dst = dst;
}
