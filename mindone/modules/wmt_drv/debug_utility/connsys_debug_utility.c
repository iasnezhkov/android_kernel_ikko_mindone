// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/ratelimit.h>
#include "connsys_debug_utility.h"
#include "ring_emi.h"
#include "ring.h"
#include "wmt_exp.h"
#include <linux/alarmtimer.h>
#include <linux/suspend.h>
#include <linux/rtc.h>
#include <linux/mutex.h>

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
enum FW_LOG_MODE {
	PRINT_TO_KERNEL_LOG = 0,
	LOG_TO_FILE = 1,
};

static atomic_t log_mode = ATOMIC_INIT(LOG_TO_FILE);
static DEFINE_MUTEX(connlog_alarm_mutex);
static DEFINE_MUTEX(connlog_callback_mutex);
static DEFINE_MUTEX(connlog_readers);

#define CONNLOG_ALARM_STATE_DISABLE	0x0
#define CONNLOG_ALARM_STATE_ENABLE	0x01
#define CONNLOG_ALARM_STATE_RUNNING	0x03

#define CONNLOG_LOG_BUFFER_SIZE		(64*1024)

struct connlog_alarm {
	struct alarm alarm_timer;
	unsigned int alarm_state;
	unsigned int blank_state;
	unsigned int alarm_sec;
	spinlock_t alarm_lock;
};

struct connlog_dev {
	phys_addr_t phyAddrEmiBase;
	void __iomem *virAddrEmiLogBase;
	struct connlog_emi_config emi_config;
	int conn2ApIrqId;
	bool eirqOn;
	bool active;
	bool wake_enabled;
	spinlock_t irq_lock;
	unsigned int irq_counter;
	CONNLOG_IRQ_CB irq_callback;
	struct timer_list workTimer;
	struct work_struct logDataWorker;
	/* alarm timer for suspend */
	struct connlog_alarm log_alarm;
	void *log_data;
};
static struct connlog_dev gDev = { 0 };

static CONNLOG_EVENT_CB event_callback_table[CONNLOG_TYPE_END] = { 0x0 };

struct connlog_buffer {
	struct ring_emi ring_emi;
	struct ring ring_cache;
	void *cache_base;
};
static struct connlog_buffer connlog_buffer_table[CONNLOG_TYPE_END];

struct connlog_offset {
	unsigned int emi_base_offset;
	unsigned int emi_size;
	unsigned int emi_read;
	unsigned int emi_write;
	unsigned int emi_buf;
};

static struct connlog_offset emi_offset_table[CONNLOG_TYPE_END];

static char *type_to_title[CONNLOG_TYPE_END] = {
	"wifi_fw", "bt_fw", "gps_fw", "mcu_fw"
};

static size_t cache_size_table[CONNLOG_TYPE_END];

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static int connlog_eirq_init(const struct connlog_irq_config *irq_config);
static void connlog_eirq_deinit(void);
static int connlog_emi_init(phys_addr_t emi_base, const struct connlog_emi_config *emi_config);
static void connlog_emi_deinit(void);
static int connlog_ring_buffer_init(void);
static void connlog_ring_buffer_deinit(void);
static int connlog_set_ring_buffer_base_addr(void);
static irqreturn_t connlog_eirq_isr(int irq, void *arg);
static void connlog_set_ring_ready(void);
static int connlog_buffer_init(int conn_type);
static void connlog_ring_emi_to_cache(int conn_type);
static void connlog_dump_buf(const char *title, const char *buf, ssize_t sz);
static void connlog_ring_print(int conn_type);
static void connlog_event_set(int conn_type);
static void connlog_log_data_handler(struct work_struct *work);
static void work_timer_handler(struct timer_list *t);
static void connlog_do_schedule_work(bool count);
static void connlog_emi_status_dump(void);

/* connlog when suspend */
static int connlog_alarm_init(void);
static enum alarmtimer_restart alarm_timer_handler(struct alarm *alarm, ktime_t);
static inline bool connlog_is_alarm_enable(void);
static int connlog_set_alarm_timer(void);
static int connlog_cancel_alarm_timer(void);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
static void connlog_set_ring_ready(void);


/*****************************************************************************
* FUNCTION
*  connlog_emi_status_dump
* DESCRIPTION
*  Dump emi control block .
*****************************************************************************/
void connlog_emi_status_dump(void)
{
	/* Dump header (0x40) and MCU read/write pointer */
	connsys_dedicated_log_dump_emi(0x0, 0x60);
	/* 32 byte wifi read/write pointer */
	connsys_dedicated_log_dump_emi(emi_offset_table[CONNLOG_TYPE_WIFI].emi_base_offset, 0x20);
	/* 32 byte bt read/write pointer */
	connsys_dedicated_log_dump_emi(emi_offset_table[CONNLOG_TYPE_BT].emi_base_offset, 0x20);
	/* 32 byte gps read/write pointer */
	connsys_dedicated_log_dump_emi(emi_offset_table[CONNLOG_TYPE_GPS].emi_base_offset, 0x20);
}


/*****************************************************************************
* FUNCTION
*  connlog_cache_allocate
* DESCRIPTION
*  Allocate memroy for cache .
* PARAMETERS
*  size      [IN]        data buffer length
* RETURNS
*  void*  buffer pointer
*****************************************************************************/
void *connlog_cache_allocate(size_t size)
{
	void *pBuffer = NULL;

	pBuffer = vmalloc(size);
	if (!pBuffer)
		return NULL;
	return pBuffer;
}

/*  */
/*****************************************************************************
* FUNCTION
*  connlog_set_ring_ready
* DESCRIPTION
*  set reserved bit be EMIFWLOG to indicate that init is ready.
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static void connlog_set_ring_ready(void)
{
	const char ready_str[] = "EMIFWLOG";

	memcpy_toio(gDev.virAddrEmiLogBase + CONNLOG_READY_PATTERN_BASE,
		    ready_str, CONNLOG_READY_PATTERN_BASE_SIZE);
}

/*****************************************************************************
* FUNCTION
*  connlog_buffer_init
* DESCRIPTION
*  Initialize ring and cache buffer
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  void
*****************************************************************************/
static int connlog_buffer_init(int conn_type)
{
	struct connlog_buffer *buffer = &connlog_buffer_table[conn_type];

	buffer->cache_base = connlog_cache_allocate(cache_size_table[conn_type]);
	if (!buffer->cache_base)
		return -ENOMEM;
	memset(buffer->cache_base, 0, cache_size_table[conn_type]);
	ring_emi_init(
		gDev.virAddrEmiLogBase + emi_offset_table[conn_type].emi_buf,
		emi_offset_table[conn_type].emi_size,
		gDev.virAddrEmiLogBase + emi_offset_table[conn_type].emi_read,
		gDev.virAddrEmiLogBase + emi_offset_table[conn_type].emi_write,
		&buffer->ring_emi);
	ring_init(buffer->cache_base, cache_size_table[conn_type], 0, 0,
		&buffer->ring_cache);
	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_ring_emi_to_cache
* DESCRIPTION
*  copy data from emi ring buffer to cache
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  int    0=failed, others=buffer length
*****************************************************************************/
static void connlog_ring_emi_to_cache(int conn_type)
{
	struct ring_emi_segment emi_seg;
	struct ring_emi *emi = &connlog_buffer_table[conn_type].ring_emi;
	struct ring *cache = &connlog_buffer_table[conn_type].ring_cache;
	unsigned int dropped = 0;

	if (EMI_READ32(emi->read) >= emi->max_size ||
		EMI_READ32(emi->write) >= emi->max_size) {
		pr_err_ratelimited("%s EMI ring pointers out of bounds\n", type_to_title[conn_type]);
		return;
	}
	if (!ring_emi_read_all_prepare(&emi_seg, emi))
		return;

	RING_EMI_READ_ALL_FOR_EACH(emi_seg, emi) {
		struct ring_segment cache_seg;
		unsigned int copied = 0;

		RING_WRITE_FOR_EACH(emi_seg.sz, cache_seg, cache) {
			memcpy_fromio(cache_seg.ring_pt,
				emi_seg.ring_emi_pt + cache_seg.data_pos, cache_seg.sz);
			copied += cache_seg.sz;
		}
		dropped += emi_seg.sz - copied;
	}
	if (dropped)
		pr_debug_ratelimited("%s log cache dropped %u bytes\n",
			type_to_title[conn_type], dropped);
}

/* output format
 * xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx ................
 * 3 digits hex * 16 + 16 single char + 1 NULL terminate = 64+1 bytes
 */
#define BYETES_PER_LINE 16
#define LOG_LINE_SIZE (3*BYETES_PER_LINE + BYETES_PER_LINE + 1)
#define IS_VISIBLE_CHAR(c) ((c) >= 32 && (c) <= 126)
static void connlog_dump_buf(const char *title, const char *buf, ssize_t sz)
{
	int i;
	char line[LOG_LINE_SIZE];

	i = 0;
	line[LOG_LINE_SIZE-1] = 0;
	while (sz--) {
		if (snprintf(line + i*3, 3, "%02x", *buf) < 0)
			return;
		line[i*3 + 2] = ' ';

		if (IS_VISIBLE_CHAR(*buf))
			line[3*BYETES_PER_LINE + i] = *buf;
		else
			line[3*BYETES_PER_LINE + i] = '`';

		i++;
		buf++;

		if (i >= BYETES_PER_LINE || !sz) {
			if (i < BYETES_PER_LINE) {
				memset(line+i*3, ' ', (BYETES_PER_LINE-i)*3);
				memset(line+3*BYETES_PER_LINE+i, '.', BYETES_PER_LINE-i);
			}
			pr_info("%s: %s\n", title, line);
			i = 0;
		}
	}
}

#define LOG_MAX_LEN 1024
#define LOG_HEAD_LENG 16
#define TIMESYNC_LENG 40
const char log_head[] = {0x55, 0x00, 0x00, 0x62};
const char timesync_head[] = {0x55, 0x00, 0x25, 0x62};
static char log_line[LOG_MAX_LEN];
static void connlog_fw_log_parser(int conn_type, const char *buf, ssize_t sz)
{
	unsigned int systime = 0;
	unsigned int utc_s = 0;
	unsigned int utc_us = 0;
	unsigned int buf_len = 0;
	unsigned int print_len = 0;

	if (conn_type < 0 || conn_type >= CONNLOG_TYPE_END)
		return;

	while (sz > LOG_HEAD_LENG) {
		if (*buf == log_head[0]) {
			if (!memcmp(buf, log_head, sizeof(log_head))) {
				buf_len = (unsigned char)buf[14] | ((unsigned char)buf[15] << 8);
				if (buf_len > sz - LOG_HEAD_LENG)
					break;
				print_len = buf_len >= LOG_MAX_LEN ? LOG_MAX_LEN - 1 : buf_len;
				memcpy(log_line, buf + LOG_HEAD_LENG, print_len);
				log_line[print_len] = 0;
				pr_info("%s: %s\n", type_to_title[conn_type], log_line);
				sz -= (LOG_HEAD_LENG + buf_len);
				buf += (LOG_HEAD_LENG + buf_len);
				continue;
			} else if (sz >= TIMESYNC_LENG &&
				!memcmp(buf, timesync_head, sizeof(timesync_head))) {
				memcpy(&systime, buf + 28, sizeof(systime));
				memcpy(&utc_s, buf + 32, sizeof(utc_s));
				memcpy(&utc_us, buf + 36, sizeof(utc_us));
				pr_info("%s: timesync :  (%u) %u.%06u\n",
					type_to_title[conn_type], systime, utc_s, utc_us);
				sz -= TIMESYNC_LENG;
				buf += TIMESYNC_LENG;
				continue;
			}
		}
		sz--;
		buf++;
	}
}
/*****************************************************************************
* FUNCTION
*  connlog_ring_print
* DESCRIPTION
*  print log data on kernel log
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  void
*****************************************************************************/
static void connlog_ring_print(int conn_type)
{
	unsigned int written = 0;
	unsigned int buf_size;
	struct ring_emi_segment ring_emi_seg;
	struct ring_emi *ring_emi;

	if (conn_type < 0 || conn_type >= CONNLOG_TYPE_END)
		return;

	ring_emi = &connlog_buffer_table[conn_type].ring_emi;
	if (RING_EMI_EMPTY(ring_emi) || !ring_emi_read_all_prepare(&ring_emi_seg, ring_emi)) {
		pr_err("type(%s) no data, possibly taken by concurrent reader.\n", type_to_title[conn_type]);
		return;
	}
	buf_size = ring_emi_seg.remain;
	memset(gDev.log_data, 0, CONNLOG_LOG_BUFFER_SIZE);

	/* Check ring_emi buffer memory. Dump EMI data if it's corruption. */
	if (EMI_READ32(ring_emi->read) > emi_offset_table[conn_type].emi_size ||
	    EMI_READ32(ring_emi->write) > emi_offset_table[conn_type].emi_size) {
		pr_err("%s read/write pointer out-of-bounds.\n", type_to_title[conn_type]);
		connlog_emi_status_dump();
		/* Trigger Connsys Assert */
		mtk_wcn_wmt_assert(WMTDRV_TYPE_WMT, 46);
		return;
	}

	RING_EMI_READ_ALL_FOR_EACH(ring_emi_seg, ring_emi) {
		memcpy_fromio(gDev.log_data + written, ring_emi_seg.ring_emi_pt, ring_emi_seg.sz);
		/* connlog_dump_buf("fw_log", gDev.log_data + written, ring_emi_seg.sz); */
		buf_size -= ring_emi_seg.sz;
		written += ring_emi_seg.sz;
	}
	if (conn_type != CONNLOG_TYPE_BT)
		connlog_fw_log_parser(conn_type, gDev.log_data, written);
}

/*****************************************************************************
* FUNCTION
*  connlog_event_set
* DESCRIPTION
*  Trigger  event call back to wakeup waitqueue
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  void
*****************************************************************************/
static void connlog_event_set(int conn_type)
{
	CONNLOG_EVENT_CB callback;

	if (conn_type < 0 || conn_type >= CONNLOG_TYPE_END)
		return;
	mutex_lock(&connlog_callback_mutex);
	callback = event_callback_table[conn_type];
	if (callback)
		callback();
	mutex_unlock(&connlog_callback_mutex);
}

/*****************************************************************************
* FUNCTION
*  connlog_do_schedule_work
* DESCRIPTION
*  schedule work to read emi log data
* PARAMETERS
*  count      [IN]        write irq counter to EMI
* RETURNS
*  void
*****************************************************************************/
static void connlog_do_schedule_work(bool count)
{
	unsigned long flags;

	if (!READ_ONCE(gDev.active))
		return;
	spin_lock_irqsave(&gDev.irq_lock, flags);
	if (gDev.active) {
		if (count) {
			gDev.irq_counter++;
			EMI_WRITE32(gDev.virAddrEmiLogBase + CONNLOG_IRQ_COUNTER_BASE,
				gDev.irq_counter);
		}
		gDev.eirqOn = !schedule_work(&gDev.logDataWorker);
	}
	spin_unlock_irqrestore(&gDev.irq_lock, flags);
}

/*****************************************************************************
* FUNCTION
*  work_timer_handler
* DESCRIPTION
*  IRQ is still on, do schedule_work again
* PARAMETERS
*  data      [IN]        input data
* RETURNS
*  void
*****************************************************************************/
static void work_timer_handler(struct timer_list *t)
{
	connlog_do_schedule_work(false);
}

/*****************************************************************************
* FUNCTION
*  connlog_alarm_init
* DESCRIPTION
*  init alarm timer
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static int connlog_alarm_init(void)
{
	alarm_init(&gDev.log_alarm.alarm_timer, ALARM_REALTIME, alarm_timer_handler);
	gDev.log_alarm.alarm_state = CONNLOG_ALARM_STATE_DISABLE;
	spin_lock_init(&gDev.log_alarm.alarm_lock);
	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_is_alarm_enable
* DESCRIPTION
*  is alarm timer enable
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static inline bool connlog_is_alarm_enable(void)
{
	if ((gDev.log_alarm.alarm_state & CONNLOG_ALARM_STATE_ENABLE) > 0)
		return true;
	return false;
}

/*****************************************************************************
* FUNCTION
*  connlog_set_alarm_timer
* DESCRIPTION
*  setup alarm timer
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static int connlog_set_alarm_timer(void)
{
	ktime_t kt;

	kt = ktime_set(gDev.log_alarm.alarm_sec, 0);
	alarm_start_relative(&gDev.log_alarm.alarm_timer, kt);

	pr_info("[connsys_log_alarm] alarm timer enabled timeout=[%d]", gDev.log_alarm.alarm_sec);
	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_cancel_alarm_timer
* DESCRIPTION
*  cancel alarm timer
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static int connlog_cancel_alarm_timer(void)
{
	pr_info("[connsys_log_alarm] alarm timer cancel");
	return alarm_cancel(&gDev.log_alarm.alarm_timer);
}

/*****************************************************************************
* FUNCTION
*  connsys_log_alarm_enable
* DESCRIPTION
*  enable screen off alarm timer mechanism
* PARAMETERS
*  sec		[IN] alarm timeout in seconds
* RETURNS
*  void
*****************************************************************************/
int connsys_log_alarm_enable(unsigned int sec)
{
	unsigned long flags;
	int ret = 0;

	mutex_lock(&connlog_alarm_mutex);
	if (!READ_ONCE(gDev.active) || !sec) {
		ret = -EINVAL;
		goto out;
	}
	spin_lock_irqsave(&gDev.log_alarm.alarm_lock, flags);
	gDev.log_alarm.alarm_sec = sec;
	gDev.log_alarm.alarm_state = CONNLOG_ALARM_STATE_ENABLE;
	if (gDev.log_alarm.blank_state == 0)
		connlog_set_alarm_timer();
	spin_unlock_irqrestore(&gDev.log_alarm.alarm_lock, flags);
out:
	mutex_unlock(&connlog_alarm_mutex);
	return ret;
}
EXPORT_SYMBOL(connsys_log_alarm_enable);

/*****************************************************************************
* FUNCTION
*  connsys_log_alarm_disable
* DESCRIPTION
*  disable screen off alarm timer mechanism
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
int connsys_log_alarm_disable(void)
{
	unsigned long flags;
	int ret = 0;

	mutex_lock(&connlog_alarm_mutex);
	if (!READ_ONCE(gDev.active)) {
		ret = -ENODEV;
		goto out;
	}
	spin_lock_irqsave(&gDev.log_alarm.alarm_lock, flags);
	gDev.log_alarm.alarm_state = CONNLOG_ALARM_STATE_DISABLE;
	spin_unlock_irqrestore(&gDev.log_alarm.alarm_lock, flags);
	ret = connlog_cancel_alarm_timer();
out:
	mutex_unlock(&connlog_alarm_mutex);
	return ret;
}
EXPORT_SYMBOL(connsys_log_alarm_disable);

/*****************************************************************************
* FUNCTION
*  connsys_log_blank_state_changed
* DESCRIPTION
*  listen blank on/off to suspend/reusme alarm timer
* PARAMETERS
*  int		[IN] blank state
* RETURNS
*  void
*****************************************************************************/
int connsys_log_blank_state_changed(int blank_state)
{
	unsigned long flags;
	bool cancel = false;
	int ret = 0;

	mutex_lock(&connlog_alarm_mutex);
	if (!READ_ONCE(gDev.active)) {
		ret = -ENODEV;
		goto out;
	}
	spin_lock_irqsave(&gDev.log_alarm.alarm_lock, flags);
	gDev.log_alarm.blank_state = blank_state;
	if (connlog_is_alarm_enable()) {
		if (blank_state == 0)
			connlog_set_alarm_timer();
		else
			cancel = true;
	}
	spin_unlock_irqrestore(&gDev.log_alarm.alarm_lock, flags);
	if (cancel)
		ret = connlog_cancel_alarm_timer();
out:
	mutex_unlock(&connlog_alarm_mutex);
	return ret;
}
EXPORT_SYMBOL(connsys_log_blank_state_changed);

/*****************************************************************************
* FUNCTION
*  alarm_timer_handler
* DESCRIPTION
*  handler for alarm timer
* PARAMETERS
*  int		[IN] blank state
* RETURNS
*  void
*****************************************************************************/
static enum alarmtimer_restart alarm_timer_handler(struct alarm *alarm,
	ktime_t now)
{
	unsigned long flags;

	connlog_do_schedule_work(false);
	spin_lock_irqsave(&gDev.log_alarm.alarm_lock, flags);
	if (READ_ONCE(gDev.active) && connlog_is_alarm_enable() &&
		gDev.log_alarm.blank_state == 0)
		alarm_start_relative(&gDev.log_alarm.alarm_timer,
			ktime_set(gDev.log_alarm.alarm_sec, 0));
	spin_unlock_irqrestore(&gDev.log_alarm.alarm_lock, flags);
	return ALARMTIMER_NORESTART;
}

/*****************************************************************************
* FUNCTION
*  connlog_print_log
* DESCRIPTION
*  Print FW log to kernel log
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  void
*****************************************************************************/
static void connlog_log_data_handler(struct work_struct *work)
{
	unsigned long flags;
	int i;

	for (i = 0; i < CONNLOG_TYPE_END; i++) {
		if (RING_EMI_EMPTY(&connlog_buffer_table[i].ring_emi))
			continue;
		if (atomic_read(&log_mode) == LOG_TO_FILE) {
			mutex_lock(&connlog_readers);
			connlog_ring_emi_to_cache(i);
			mutex_unlock(&connlog_readers);
		} else {
			connlog_ring_print(i);
		}
		connlog_event_set(i);
	}
	spin_lock_irqsave(&gDev.irq_lock, flags);
	if (gDev.active && gDev.eirqOn)
		mod_timer(&gDev.workTimer, jiffies + 1);
	spin_unlock_irqrestore(&gDev.irq_lock, flags);
}

/*****************************************************************************
* FUNCTION
*  connlog_eirq_isr
* DESCRIPTION
*  IRQ handler to notify subsys that EMI has logs.
* PARAMETERS
*  irq      [IN]        irq number
*  art      [IN]        other argument
* RETURNS
*  irqreturn_t           irq status
*     @IRQ_HANDLED       interrupt was handled by this device
*****************************************************************************/
static irqreturn_t connlog_eirq_isr(int irq, void *arg)
{
	connlog_do_schedule_work(true);

	if (gDev.irq_callback)
		(*gDev.irq_callback)();

	return IRQ_HANDLED;
}

/*****************************************************************************
* FUNCTION
*  connlog_eirq_init
* DESCRIPTION
*  To register IRQ
* PARAMETERS
*  irq_id      [IN]        irq number
*  irq_flag    [IN]        irq type
* RETURNS
*  int    0=success, others=error
*****************************************************************************/
static int connlog_eirq_init(const struct connlog_irq_config *irq_config)
{
	int ret;

	if (!irq_config || !irq_config->irq_num || !irq_config->irq_callback)
		return -EINVAL;
	gDev.irq_callback = irq_config->irq_callback;
	ret = request_irq(irq_config->irq_num, connlog_eirq_isr,
		irq_config->irq_flag, "CONN_LOG_IRQ", NULL);
	if (ret)
		return ret;
	gDev.conn2ApIrqId = irq_config->irq_num;
	ret = enable_irq_wake(gDev.conn2ApIrqId);
	if (ret)
		return ret;
	gDev.wake_enabled = true;
	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_eirq_deinit
* DESCRIPTION
*  unrigester irq
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static void connlog_eirq_deinit(void)
{
	if (gDev.wake_enabled) {
		disable_irq_wake(gDev.conn2ApIrqId);
		gDev.wake_enabled = false;
	}
	if (gDev.conn2ApIrqId) {
		free_irq(gDev.conn2ApIrqId, NULL);
		gDev.conn2ApIrqId = 0;
	}
}

/*****************************************************************************
* FUNCTION
*  connlog_set_ring_buffer_base_addr
* DESCRIPTION
*  Set subsys log base address on EMI for FW
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static int connlog_set_ring_buffer_base_addr(void)
{
	if (!gDev.virAddrEmiLogBase)
		return -1;

	/* set up subsys base address */
	EMI_WRITE32(gDev.virAddrEmiLogBase + 0,  emi_offset_table[CONNLOG_TYPE_MCU].emi_base_offset);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 4,  emi_offset_table[CONNLOG_TYPE_MCU].emi_size);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 8,  emi_offset_table[CONNLOG_TYPE_WIFI].emi_base_offset);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 12, emi_offset_table[CONNLOG_TYPE_WIFI].emi_size);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 16, emi_offset_table[CONNLOG_TYPE_BT].emi_base_offset);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 20, emi_offset_table[CONNLOG_TYPE_BT].emi_size);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 24, emi_offset_table[CONNLOG_TYPE_GPS].emi_base_offset);
	EMI_WRITE32(gDev.virAddrEmiLogBase + 28, emi_offset_table[CONNLOG_TYPE_GPS].emi_size);
	/* set state to resume initially */
	EMI_WRITE32(gDev.virAddrEmiLogBase + 32, 1);
	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_emi_init
* DESCRIPTION
*  Do ioremap for log buffer on EMI
* PARAMETERS
*  emiaddr      [IN]        physical EMI base address
* RETURNS
*  void
*****************************************************************************/
static int connlog_emi_init(phys_addr_t emi_base, const struct connlog_emi_config *emi_config)
{
	unsigned int mcu_base, wifi_base, bt_base, gps_base;

	if (emi_config == 0) {
		pr_err("consys emi memory address gPhyAddrEmiBase invalid\n");
		return -1;
	}

	if (gDev.phyAddrEmiBase) {
		pr_warn("emi base address has been initialized\n");
		return -2;
	}

	gDev.phyAddrEmiBase = emi_base;
	gDev.virAddrEmiLogBase = ioremap(gDev.phyAddrEmiBase +
		emi_config->emi_offset, emi_config->emi_size_total);
	if (gDev.virAddrEmiLogBase) {
		pr_info("EMI mapping OK virtual(0x%p) physical(0x%x)\n",
				gDev.virAddrEmiLogBase,
				(unsigned int)(gDev.phyAddrEmiBase + emi_config->emi_offset));
		memset_io(gDev.virAddrEmiLogBase, 0, emi_config->emi_size_total);
	} else {
		gDev.phyAddrEmiBase = 0;
		return -ENOMEM;
	}

	memcpy(&gDev.emi_config, emi_config, sizeof(struct connlog_emi_config));

	mcu_base = CONNLOG_CONTROL_RING_BUFFER_BASE_SIZE;
	wifi_base =
		mcu_base + gDev.emi_config.emi_size_mcu + CONNLOG_CONTROL_RING_BUFFER_RESERVE_SIZE +
		CONNLOG_EMI_32_BYTE_ALIGNED;
	bt_base =
		wifi_base + gDev.emi_config.emi_size_wifi + CONNLOG_CONTROL_RING_BUFFER_RESERVE_SIZE +
		CONNLOG_EMI_32_BYTE_ALIGNED;
	gps_base =
		bt_base + gDev.emi_config.emi_size_bt + CONNLOG_CONTROL_RING_BUFFER_RESERVE_SIZE +
		CONNLOG_EMI_32_BYTE_ALIGNED;

#define INIT_EMI_OFFSET_TABLE(index, base, size, read_offset, write_offset, buf_offset) \
	emi_offset_table[index].emi_base_offset = base; \
	emi_offset_table[index].emi_size = size; \
	emi_offset_table[index].emi_read = read_offset; \
	emi_offset_table[index].emi_write = write_offset; \
	emi_offset_table[index].emi_buf = buf_offset

	INIT_EMI_OFFSET_TABLE(
		CONNLOG_TYPE_MCU,
		mcu_base, gDev.emi_config.emi_size_mcu,
		mcu_base + 0, mcu_base + 4,
		mcu_base + CONNLOG_EMI_32_BYTE_ALIGNED);
	INIT_EMI_OFFSET_TABLE(
		CONNLOG_TYPE_WIFI,
		wifi_base, gDev.emi_config.emi_size_wifi,
		wifi_base + 0, wifi_base + 4,
		wifi_base + CONNLOG_EMI_32_BYTE_ALIGNED);
	INIT_EMI_OFFSET_TABLE(
		CONNLOG_TYPE_BT,
		bt_base, gDev.emi_config.emi_size_bt,
		bt_base + 0, bt_base + 4,
		bt_base + CONNLOG_EMI_32_BYTE_ALIGNED);
	INIT_EMI_OFFSET_TABLE(
		CONNLOG_TYPE_GPS,
		gps_base, gDev.emi_config.emi_size_gps,
		gps_base + 0, gps_base + 4,
		gps_base + CONNLOG_EMI_32_BYTE_ALIGNED);

	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_emi_deinit
* DESCRIPTION
*  Do iounmap for log buffer on EMI
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static void connlog_emi_deinit(void)
{
	if (gDev.virAddrEmiLogBase)
		iounmap(gDev.virAddrEmiLogBase);
	gDev.virAddrEmiLogBase = NULL;
	gDev.phyAddrEmiBase = 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_ring_buffer_init
* DESCRIPTION
*  Initialize ring buffer setting for subsys
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static int connlog_ring_buffer_init(void)
{
	int i;

	if (!gDev.virAddrEmiLogBase) {
		pr_err("consys emi memory address phyAddrEmiBase invalid\n");
		return -1;
	}

	connlog_set_ring_buffer_base_addr();
	/* cache table size init */
	cache_size_table[CONNLOG_TYPE_WIFI] = (emi_offset_table[CONNLOG_TYPE_WIFI].emi_size * 2);
	cache_size_table[CONNLOG_TYPE_BT] = (emi_offset_table[CONNLOG_TYPE_BT].emi_size * 2);
	cache_size_table[CONNLOG_TYPE_GPS] = emi_offset_table[CONNLOG_TYPE_GPS].emi_size;
	cache_size_table[CONNLOG_TYPE_MCU] = emi_offset_table[CONNLOG_TYPE_MCU].emi_size;

	for (i = 0; i < CONNLOG_TYPE_END; i++) {
		if (connlog_buffer_init(i))
			return -ENOMEM;
	}
	gDev.log_data = connlog_cache_allocate(CONNLOG_LOG_BUFFER_SIZE);
	if (!gDev.log_data)
		return -ENOMEM;

	return 0;
}

/*****************************************************************************
* FUNCTION
*  connlog_ring_buffer_deinit
* DESCRIPTION
*  Initialize ring buffer setting for subsys
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static void connlog_ring_buffer_deinit(void)
{
	int i = 0;

	for (i = 0; i < CONNLOG_TYPE_END; i++) {
		kvfree(connlog_buffer_table[i].cache_base);
		connlog_buffer_table[i].cache_base = NULL;
	}
	kvfree(gDev.log_data);
	gDev.log_data = NULL;
}

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_path_apsoc_init
* DESCRIPTION
*  Initialize API for common driver to initialize connsys dedicated log
*  for APSOC platform
* PARAMETERS
*  emiaddr      [IN]        EMI physical base address
*  irq_num      [IN]        IRQ id from device tree
*  irq_flag     [IN]        IRQ flag from device tree
* RETURNS
*  void
*****************************************************************************/
int connsys_dedicated_log_path_apsoc_init(
	phys_addr_t emi_base,
	const struct connlog_emi_config *emi_config,
	const struct connlog_irq_config *irq_config)
{
	int ret;

	if (READ_ONCE(gDev.active))
		return -EALREADY;
	gDev.eirqOn = false;
	gDev.irq_counter = 0;
	gDev.irq_callback = NULL;
	spin_lock_init(&gDev.irq_lock);
	timer_setup(&gDev.workTimer, work_timer_handler, 0);
	INIT_WORK(&gDev.logDataWorker, connlog_log_data_handler);
	connlog_alarm_init();
	ret = connlog_emi_init(emi_base, emi_config);
	if (ret)
		return ret;
	ret = connlog_ring_buffer_init();
	if (ret)
		goto free_buffers;
	WRITE_ONCE(gDev.active, true);
	ret = connlog_eirq_init(irq_config);
	if (ret) {
		connsys_dedicated_log_path_apsoc_deinit();
		return ret;
	}
	connlog_set_ring_ready();
	return 0;

free_buffers:
	connlog_ring_buffer_deinit();
	connlog_emi_deinit();
	return ret;
}
EXPORT_SYMBOL(connsys_dedicated_log_path_apsoc_init);

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_path_apsoc_deinit
* DESCRIPTION
*  De-Initialize API for common driver to release cache, un-remap emi and free
*  irq for APSOC platform
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
void connsys_dedicated_log_path_apsoc_deinit(void)
{
	unsigned long flags;

	if (!READ_ONCE(gDev.active))
		return;
	spin_lock_irqsave(&gDev.irq_lock, flags);
	gDev.active = false;
	memset_io(gDev.virAddrEmiLogBase + CONNLOG_READY_PATTERN_BASE, 0,
		CONNLOG_READY_PATTERN_BASE_SIZE);
	spin_unlock_irqrestore(&gDev.irq_lock, flags);
	connlog_eirq_deinit();
	mutex_lock(&connlog_alarm_mutex);
	spin_lock_irqsave(&gDev.log_alarm.alarm_lock, flags);
	gDev.log_alarm.alarm_state = CONNLOG_ALARM_STATE_DISABLE;
	spin_unlock_irqrestore(&gDev.log_alarm.alarm_lock, flags);
	alarm_cancel(&gDev.log_alarm.alarm_timer);
	mutex_unlock(&connlog_alarm_mutex);
	timer_shutdown_sync(&gDev.workTimer);
	cancel_work_sync(&gDev.logDataWorker);
	mutex_lock(&connlog_readers);
	connlog_ring_buffer_deinit();
	connlog_emi_deinit();
	mutex_unlock(&connlog_readers);
}
EXPORT_SYMBOL(connsys_dedicated_log_path_apsoc_deinit);

/*****************************************************************************
* FUNCTION
*  connsys_log_init
* DESCRIPTION
*  Init API for subsys driver.
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  int    0=success, others=error
*****************************************************************************/
int connsys_log_init(int conn_type)
{
	return 0;
}
EXPORT_SYMBOL(connsys_log_init);

/*****************************************************************************
* FUNCTION
*  connsys_log_deinit
* DESCRIPTION
*  De-init API for subsys driver.
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  int    0=success, others=error
*****************************************************************************/
int connsys_log_deinit(int conn_type)
{
	if (conn_type >= CONNLOG_TYPE_END || conn_type < 0)
		return -1;
	mutex_lock(&connlog_callback_mutex);
	event_callback_table[conn_type] = NULL;
	mutex_unlock(&connlog_callback_mutex);
	return 0;
}
EXPORT_SYMBOL(connsys_log_deinit);

/*****************************************************************************
* FUNCTION
*  connsys_log_get_buf_size
* DESCRIPTION
*  Get ring buffer unread size on EMI.
* PARAMETERS
*  conn_type      [IN]        subsys type
* RETURNS
*  unsigned int    Ring buffer unread size
*****************************************************************************/
unsigned int connsys_log_get_buf_size(int conn_type)
{
	unsigned int size = 0;

	if (conn_type >= CONNLOG_TYPE_END || conn_type < 0)
		return -1;
	mutex_lock(&connlog_readers);
	if (READ_ONCE(gDev.active))
		size = RING_SIZE(&connlog_buffer_table[conn_type].ring_cache);
	mutex_unlock(&connlog_readers);
	return size;
}
EXPORT_SYMBOL(connsys_log_get_buf_size);

/*****************************************************************************
* FUNCTION
*  connsys_log_register_event_cb
* DESCRIPTION
*  Register callback function. It'll be trigger while receive conn2ap IRQ.
* PARAMETERS
*  conn_type      [IN]        subsys type
*  func           [IN]        callback function pointer
* RETURNS
*  int    0=success, others=error
*****************************************************************************/
int connsys_log_register_event_cb(int conn_type, CONNLOG_EVENT_CB func)
{
	if (conn_type >= CONNLOG_TYPE_END || conn_type < 0)
		return -1;
	mutex_lock(&connlog_callback_mutex);
	event_callback_table[conn_type] = func;
	mutex_unlock(&connlog_callback_mutex);
	return 0;
}
EXPORT_SYMBOL(connsys_log_register_event_cb);

/*****************************************************************************
* FUNCTION
*  connsys_log_read
* DESCRIPTION
*  Copy EMI ring buffer data to the buffer that provided by sub-module.
* PARAMETERS
*  conn_type      [IN]        subsys type
*  buf            [IN]        buffer from driver
*  count          [IN]        buffer length
* RETURNS
*  ssize_t    read buffer size
*****************************************************************************/
ssize_t connsys_log_read(int conn_type, char *buf, size_t count)
{
	unsigned int written = 0;
	unsigned int cache_buf_size;
	struct ring_segment ring_seg;
	struct ring *ring;
	unsigned int size = 0;

	if (conn_type < 0 || conn_type >= CONNLOG_TYPE_END)
		return 0;

	mutex_lock(&connlog_readers);
	if (!READ_ONCE(gDev.active))
		goto done;
	ring = &connlog_buffer_table[conn_type].ring_cache;

	if (atomic_read(&log_mode) != LOG_TO_FILE)
		goto done;

	size = count < RING_SIZE(ring) ? count : RING_SIZE(ring);
	if (RING_EMPTY(ring) || !ring_read_prepare(size, &ring_seg, ring)) {
		pr_err("type(%d) no data, possibly taken by concurrent reader.\n", conn_type);
		goto done;
	}
	cache_buf_size = ring_seg.remain;

	RING_READ_FOR_EACH(size, ring_seg, ring) {
		memcpy(buf + written, ring_seg.ring_pt, ring_seg.sz);
		cache_buf_size -= ring_seg.sz;
		written += ring_seg.sz;
	}
done:
	mutex_unlock(&connlog_readers);
	return written;
}
EXPORT_SYMBOL(connsys_log_read);

/*****************************************************************************
* FUNCTION
*  connsys_log_read_to_user
* DESCRIPTION
*  Copy EMI ring buffer data to the user buffer.
* PARAMETERS
*  conn_type      [IN]        subsys type
*  buf            [IN]        user buffer
*  count          [IN]        buffer length
* RETURNS
*  ssize_t    read buffer size
*****************************************************************************/
ssize_t connsys_log_read_to_user(int conn_type, char __user *buf, size_t count)
{
	int retval;
	unsigned int written = 0;
	static DEFINE_RATELIMIT_STATE(_rs, 10 * HZ, 1);
	unsigned int cache_buf_size;
	struct ring_segment ring_seg;
	struct ring *ring;
	unsigned int size = 0;

	ratelimit_set_flags(&_rs, RATELIMIT_MSG_ON_RELEASE);

	if (conn_type < 0 || conn_type >= CONNLOG_TYPE_END)
		return 0;

	mutex_lock(&connlog_readers);
	if (!READ_ONCE(gDev.active))
		goto done;
	ring = &connlog_buffer_table[conn_type].ring_cache;

	if (atomic_read(&log_mode) != LOG_TO_FILE)
		goto done;

	size = count < RING_SIZE(ring) ? count : RING_SIZE(ring);
	if (RING_EMPTY(ring) || !ring_read_prepare(size, &ring_seg, ring)) {
		pr_err("type(%d) no data, possibly taken by concurrent reader.\n", conn_type);
		goto done;
	}
	cache_buf_size = ring_seg.remain;

	RING_READ_FOR_EACH(size, ring_seg, ring) {
		retval = copy_to_user(buf + written, ring_seg.ring_pt, ring_seg.sz);
		if (retval) {
			if (__ratelimit(&_rs))
				pr_err("copy to user buffer failed, ret:%d\n", retval);
			goto done;
		}
		cache_buf_size -= ring_seg.sz;
		written += ring_seg.sz;
	}
done:
	mutex_unlock(&connlog_readers);
	return written;
}
EXPORT_SYMBOL(connsys_log_read_to_user);

/*****************************************************************************
* FUNCTION
*  connsys_log_get_emi_log_base_vir_addr
* DESCRIPTION
*  return EMI log base address whitch has done ioremap.
* PARAMETERS
*  void
* RETURNS
*  void __iomem *    ioremap EMI log base address
*****************************************************************************/
void __iomem *connsys_log_get_emi_log_base_vir_addr(void)
{
	return gDev.virAddrEmiLogBase;
}
EXPORT_SYMBOL(connsys_log_get_emi_log_base_vir_addr);

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_get_utc_time
* DESCRIPTION
*  return EMI log base address whitch has done ioremap.
* PARAMETERS
*  second         [IN]        UTC seconds
*  usecond        [IN]        UTC usecons
* RETURNS
*  void
*****************************************************************************/
void connsys_dedicated_log_get_utc_time(unsigned int *second,
	unsigned int *usecond)
{
	struct timespec64 time;

	ktime_get_real_ts64(&time);
	*second = (unsigned int)time.tv_sec; /* UTC time second unit */
	*usecond = (unsigned int)(time.tv_nsec / NSEC_PER_USEC); /* UTC time microsecond unit */
}
EXPORT_SYMBOL(connsys_dedicated_log_get_utc_time);

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_flush_emi
* DESCRIPTION
*  flush EMI buffer to log cache.
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
void connsys_dedicated_log_flush_emi(void)
{
	connlog_do_schedule_work(false);
}

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_dump_emi
* DESCRIPTION
*  dump EMI buffer for debug.
* PARAMETERS
*  offset          [IN]        buffer offset
*  size            [IN]        dump buffer size
* RETURNS
*  void
*****************************************************************************/
void connsys_dedicated_log_dump_emi(int offset, int size)
{
	mutex_lock(&connlog_readers);
	if (READ_ONCE(gDev.active) && offset >= 0 && size >= 0 &&
		(unsigned int)offset <= gDev.emi_config.emi_size_total &&
		(unsigned int)size <= gDev.emi_config.emi_size_total - offset)
		connlog_dump_buf("emi", gDev.virAddrEmiLogBase + offset, size);
	mutex_unlock(&connlog_readers);
}

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_set_log_to_file
* DESCRIPTION
*  set log mode.
* PARAMETERS
*  mode            [IN]        log mode
* RETURNS
*  void
*****************************************************************************/
void connsys_dedicated_log_set_log_mode(int mode)
{
	atomic_set(&log_mode, (mode > 0 ? LOG_TO_FILE : PRINT_TO_KERNEL_LOG));
}

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_get_log_mode
* DESCRIPTION
*  get log mode.
* PARAMETERS
*  void
* RETURNS
* int    log mode
*****************************************************************************/
int connsys_dedicated_log_get_log_mode(void)
{
	return atomic_read(&log_mode);
}

/*****************************************************************************
* FUNCTION
*  connsys_dedicated_log_set_ap_state
* DESCRIPTION
*  set ap state
* PARAMETERS
*  int state  0:suspend, 1:resume
* RETURNS
*  0: successfuly, negative if error
*****************************************************************************/
int connsys_dedicated_log_set_ap_state(int state)
{
	unsigned long flags;
	int ret = -ENODEV;

	if (state < 0 || state > 1)
		return -EINVAL;
	if (!READ_ONCE(gDev.active))
		return ret;
	spin_lock_irqsave(&gDev.irq_lock, flags);
	if (gDev.active) {
		EMI_WRITE32(gDev.virAddrEmiLogBase + 32, state);
		ret = 0;
	}
	spin_unlock_irqrestore(&gDev.irq_lock, flags);
	return ret;
}

