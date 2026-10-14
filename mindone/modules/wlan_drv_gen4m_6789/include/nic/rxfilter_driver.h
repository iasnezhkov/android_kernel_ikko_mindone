#ifndef MINDONE_RXFILTER_DRIVER_H
#define MINDONE_RXFILTER_DRIVER_H

#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include "rxfilter_multicast.h"
#include "rxfilter_wire.h"

struct ADAPTER;
struct CMD_INFO;

struct rxfilter_driver {
	spinlock_t lock;
	struct work_struct work;
	struct ADAPTER *adapter;
	struct rxfilter_control control;
	struct rxfilter_mcast_cache cache;
	struct completion completion;
	u32 owner;
	int waiting;
	int error;
	u64 revision;
	u8 live;
	u8 committed[4];
	u8 dirty;
};

void rxfilterDriverInit(struct ADAPTER *adapter);
void rxfilterDriverStart(struct ADAPTER *adapter);
void rxfilterDriverReady(struct ADAPTER *adapter);
void rxfilterDriverStop(struct ADAPTER *adapter);
int rxfilterDriverApply(struct ADAPTER *adapter, unsigned int bss, int operation);
int rxfilterDriverCommandLive(struct ADAPTER *adapter, struct CMD_INFO *command);
void rxfilterDriverInvalidate(struct ADAPTER *adapter, unsigned int bss);
int rxfilterDriverEnqueue(struct ADAPTER *adapter, struct CMD_INFO *command,
		const u8 *payload, unsigned int length);
int rxfilterDriverEvent(struct ADAPTER *adapter, const u8 *packet,
		unsigned int length);


#endif
