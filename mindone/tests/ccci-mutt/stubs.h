/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
#define __packed __attribute__((packed))
#define likely(x) (x)
#define unlikely(x) (x)
#define ERESTARTSYS 512
static void discard_log(const char *format, ...) { (void)format; }
#define CCCI_NORMAL_LOG(md, tag, ...) ((void)(md), discard_log(__VA_ARGS__))
#define CCCI_ERROR_LOG(...) ((void)0)
#define CCCI_DEBUG_LOG(...) ((void)0)
#define CCCI_REPEAT_LOG(...) ((void)0)
#define SKB_4K 3584
#define NET_SKB_PAD 32
#define CCCI_BUF_MAGIC 0xFECDBA89
#define CCCI_MAGIC_NUM 0xFFFFFFFF
#define GFP_KERNEL 1
#define GFP_ATOMIC 2
#define C2K_OVER_MAX_CH 255
#define FLOW_CTRL_HEAD 0x464C4F57
#define FLOW_CTRL_TAIL 0x4354524C
#define TX_FULL 1

struct sk_buff { unsigned char bytes[128]; unsigned char *data; unsigned int len; };
struct flow_ctrl { u32 head_magic, tail_magic; };
struct md_ccif_queue { int tx_lock, index, ccif_ch, req_wq, wakeup; void *ringbuf; };
struct md_ccif_ctrl { int md_id; struct { int md_gen; } plat_val; struct md_ccif_queue txq[16]; struct { int logic_ch_pkt_cnt; } traffic_info; int heart_beat_counter; struct flow_ctrl *flow_ctrl; };
struct ccci_per_md { int data_usb_bypass; };
struct port_t { int skb_from_pool, hif_id, md_id, txq_index, txq_exp_index, rxq_index, rxq_exp_index; };
struct port_proxy { int md_id; struct port_t *sys_port, *ctl_port; };
struct hif_ops { int (*send_skb)(unsigned char, int, struct sk_buff *, int, int); };
