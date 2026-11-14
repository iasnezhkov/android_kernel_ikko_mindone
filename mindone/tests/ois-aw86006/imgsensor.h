/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef OIS_AW86006_IMGSENSOR_H
#define OIS_AW86006_IMGSENSOR_H

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;

#define IMGSENSOR_HW_POWER_INFO_MAX 12
#define IMGSENSOR_HW_SENSOR_MAX_NUM 12
#define PLATFORM_POWER_SEQ_NAME "platform_power_seq"
#define LENGTH_FOR_SNPRINTF 256
#define IS_MT6873(id) ((id) == 0x6873)
#define IS_MT6853(id) ((id) == 0x6853)
#define IS_MT6833(id) ((id) == 0x6833)
#define pr_info(...) imgsensor_log(__VA_ARGS__)
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, value) (*(volatile __typeof__(x) *)&(x) = (value))
#define jiffies clock_ms

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
	struct notifier_block *next;
	int priority;
};

struct blocking_notifier_head {
	struct notifier_block *head;
};

#define BLOCKING_NOTIFIER_HEAD(name) struct blocking_notifier_head name = { NULL }

static inline int blocking_notifier_chain_register(struct blocking_notifier_head *h, struct notifier_block *nb)
{
	nb->next = h->head;
	h->head = nb;
	return 0;
}

static inline int blocking_notifier_chain_unregister(struct blocking_notifier_head *h, struct notifier_block *nb)
{
	struct notifier_block **p;

	for (p = &h->head; *p; p = &(*p)->next)
		if (*p == nb) {
			*p = nb->next;
			return 0;
		}
	return -ENOENT;
}

static inline int blocking_notifier_call_chain(struct blocking_notifier_head *h, unsigned long action, void *data)
{
	struct notifier_block *nb;

	for (nb = h->head; nb; nb = nb->next)
		nb->notifier_call(nb, action, data);
	return 0;
}

struct mutex {
	int locked;
};

static inline void mutex_lock(struct mutex *m)
{
	assert(!m->locked);
	m->locked = 1;
}

static inline void mutex_unlock(struct mutex *m)
{
	assert(m->locked);
	m->locked = 0;
}

static unsigned long clock_ms;
static int curr_sensor_id;
static char log_buf[512];

static inline void mdelay(unsigned long ms)
{
	clock_ms += ms;
}

__attribute__((format(printf, 1, 2))) static void imgsensor_log(const char *fmt, ...)
{
	size_t used = strlen(log_buf);
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(log_buf + used, sizeof(log_buf) - used, fmt, ap);
	va_end(ap);
}

typedef struct wait_queue_head {
	int waiters;
	int wakes;
} wait_queue_head_t;

static void (*wait_hook)(void);

static inline void init_waitqueue_head(wait_queue_head_t *wq)
{
	wq->waiters = 0;
	wq->wakes = 0;
}

static inline void wake_up_all(wait_queue_head_t *wq)
{
	wq->wakes++;
}

static inline unsigned long msecs_to_jiffies(unsigned int ms)
{
	return ms;
}

static inline unsigned int jiffies_to_msecs(unsigned long j)
{
	return j;
}

static inline void wait_tick(void)
{
	clock_ms++;
	if (wait_hook)
		wait_hook();
}

#define wait_event_timeout(wq, condition, timeout)                                      \
	({                                                                              \
		unsigned long __end = clock_ms + (timeout);                             \
		long __ret;                                                             \
		int __wakes;                                                            \
		(wq).waiters++;                                                         \
		for (;;) {                                                              \
			__wakes = (wq).wakes;                                           \
			if (condition) {                                                \
				__ret = clock_ms < __end ? (long)(__end - clock_ms) : 1; \
				break;                                                  \
			}                                                               \
			while ((wq).wakes == __wakes && clock_ms < __end)               \
				wait_tick();                                            \
			if ((wq).wakes == __wakes) {                                    \
				__ret = (condition) ? 1 : 0;                            \
				break;                                                  \
			}                                                               \
		}                                                                       \
		(wq).waiters--;                                                         \
		__ret;                                                                  \
	})

enum IMGSENSOR_RETURN {
	IMGSENSOR_RETURN_SUCCESS = 0,
	IMGSENSOR_RETURN_ERROR = -1,
	IMGSENSOR_RETURN_BUSY = -EBUSY,
};

enum IMGSENSOR_SENSOR_IDX {
	IMGSENSOR_SENSOR_IDX_MIN_NUM = 0,
	IMGSENSOR_SENSOR_IDX_MAIN = IMGSENSOR_SENSOR_IDX_MIN_NUM,
	IMGSENSOR_SENSOR_IDX_SUB,
	IMGSENSOR_SENSOR_IDX_MAX_NUM = 12,
	IMGSENSOR_SENSOR_IDX_NONE,
};

enum IMGSENSOR_HW_PIN {
	IMGSENSOR_HW_PIN_NONE = 0,
	IMGSENSOR_HW_PIN_PDN,
	IMGSENSOR_HW_PIN_RST,
	IMGSENSOR_HW_PIN_AVDD,
	IMGSENSOR_HW_PIN_AVDD1,
	IMGSENSOR_HW_PIN_AFVDD,
	IMGSENSOR_HW_PIN_DVDD,
	IMGSENSOR_HW_PIN_DOVDD,
	IMGSENSOR_HW_PIN_MIPI_SWITCH_EN,
	IMGSENSOR_HW_PIN_MIPI_SWITCH_SEL,
	IMGSENSOR_HW_PIN_MCLK,
	IMGSENSOR_HW_PIN_MAX_NUM,
	IMGSENSOR_HW_PIN_UNDEF = -1
};

enum IMGSENSOR_HW_PIN_STATE {
	IMGSENSOR_HW_PIN_STATE_LEVEL_0,
	IMGSENSOR_HW_PIN_STATE_LEVEL_1100 = 2,
	IMGSENSOR_HW_PIN_STATE_LEVEL_1800 = 7,
	IMGSENSOR_HW_PIN_STATE_LEVEL_2800 = 10,
	IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH = 12,
};

enum IMGSENSOR_HW_ID {
	IMGSENSOR_HW_ID_MCLK,
	IMGSENSOR_HW_ID_REGULATOR,
	IMGSENSOR_HW_ID_GPIO,
	IMGSENSOR_HW_ID_MAX_NUM,
	IMGSENSOR_HW_ID_NONE = -1
};

enum IMGSENSOR_HW_POWER_STATUS {
	IMGSENSOR_HW_POWER_STATUS_OFF,
	IMGSENSOR_HW_POWER_STATUS_ON
};

struct IMGSENSOR_HW_POWER_INFO {
	enum IMGSENSOR_HW_PIN pin;
	enum IMGSENSOR_HW_PIN_STATE pin_state_on;
	u32 pin_on_delay;
	enum IMGSENSOR_HW_PIN_STATE pin_state_off;
	u32 pin_off_delay;
};

struct IMGSENSOR_HW_POWER_SEQ {
	char *name;
	struct IMGSENSOR_HW_POWER_INFO pwr_info[IMGSENSOR_HW_POWER_INFO_MAX];
	u32 _idx;
};

struct IMGSENSOR_HW_DEVICE {
	enum IMGSENSOR_HW_ID id;
	void *pinstance;
	enum IMGSENSOR_RETURN (*set)(void *pinstance, enum IMGSENSOR_SENSOR_IDX, enum IMGSENSOR_HW_PIN,
				     enum IMGSENSOR_HW_PIN_STATE);
};

struct IMGSENSOR_HW_SENSOR_POWER {
	enum IMGSENSOR_HW_ID id[IMGSENSOR_HW_PIN_MAX_NUM];
};

struct IMGSENSOR_HW {
	struct IMGSENSOR_HW_DEVICE *pdev[IMGSENSOR_HW_ID_MAX_NUM];
	struct IMGSENSOR_HW_SENSOR_POWER sensor_pwr[IMGSENSOR_SENSOR_IDX_MAX_NUM];
	const char *enable_sensor_by_index[IMGSENSOR_SENSOR_IDX_MAX_NUM];
	unsigned int g_platform_id;
	struct mutex power_mutex;
	wait_queue_head_t power_wait;
	enum IMGSENSOR_SENSOR_IDX power_owner;
};

struct IMGSENSOR_SENSOR_LIST {
	char name[32];
};

struct IMGSENSOR_SENSOR_INST {
	enum IMGSENSOR_SENSOR_IDX sensor_idx;
	struct IMGSENSOR_SENSOR_LIST *psensor_list;
};

struct IMGSENSOR_SENSOR {
	struct IMGSENSOR_SENSOR_INST inst;
};

static struct IMGSENSOR_HW_POWER_SEQ platform_power_sequence[2];
static struct IMGSENSOR_HW_POWER_SEQ platform_power_sequence_for_mipi_switch[1];
static struct IMGSENSOR_HW_POWER_SEQ platform_power_sequence_for_mt6833[1];
static struct IMGSENSOR_HW_POWER_SEQ sensor_power_sequence[2];

#endif
