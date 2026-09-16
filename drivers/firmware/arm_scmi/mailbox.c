// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Mailbox Transport
 * driver.
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "common.h"

/**
 * struct scmi_mailbox - Structure representing a SCMI mailbox transport
 *
 * @cl: Mailbox Client
 * @chan: Transmit/Receive mailbox channel
 * @cinfo: SCMI channel info
 * @shmem: Transmit/Receive shared memory area
 * @chan_lock: Lock that prevents multiple xfers from being queued
 * @io_ops: Transport specific I/O operations
 */
struct scmi_mailbox {
	struct mbox_client cl;
	struct mbox_chan *chan;
	struct scmi_chan_info *cinfo;
	struct scmi_shared_mem __iomem *shmem;
	struct mutex chan_lock;
	struct scmi_shmem_io_ops *io_ops;
};

#define client_to_scmi_mailbox(c) container_of(c, struct scmi_mailbox, cl)

static void tx_prepare(struct mbox_client *cl, void *m)
{
	struct scmi_mailbox *smbox = client_to_scmi_mailbox(cl);

	shmem_tx_prepare(smbox->shmem, m, smbox->cinfo,
			 smbox->io_ops->toio);
}

static void rx_callback(struct mbox_client *cl, void *m)
{
	struct scmi_mailbox *smbox = client_to_scmi_mailbox(cl);

	/*
	 * An A2P IRQ is NOT valid when received while the platform still has
	 * the ownership of the channel, because the platform at first releases
	 * the SMT channel and then sends the completion interrupt.
	 *
	 * This addresses a possible race condition in which a spurious IRQ from
	 * a previous timed-out reply which arrived late could be wrongly
	 * associated with the next pending transaction.
	 */
	if (cl->knows_txdone && !shmem_channel_free(smbox->shmem)) {
		dev_warn(smbox->cinfo->dev, "Ignoring spurious A2P IRQ !\n");
		return;
	}

	scmi_rx_callback(smbox->cinfo, shmem_read_header(smbox->shmem), NULL);
}

static bool mailbox_chan_available(struct device *dev, int idx)
{
	return !of_parse_phandle_with_args(dev->of_node, "mboxes",
					   "#mbox-cells", idx, NULL);
}

static int mailbox_chan_validate(struct device *cdev)
{
	int num_mb, num_sh, ret = 0;
	struct device_node *np = cdev->of_node;

	num_mb = of_count_phandle_with_args(np, "mboxes", "#mbox-cells");
	num_sh = of_count_phandle_with_args(np, "shmem", NULL);
	/* Bail out if mboxes and shmem descriptors are inconsistent */
	if (num_mb <= 0 || num_sh > 2 || num_mb != num_sh) {
		dev_warn(cdev, "Invalid channel descriptor for '%s'\n",
			 of_node_full_name(np));
		return -EINVAL;
	}

	if (num_sh > 1) {
		struct device_node *np_tx, *np_rx;

		np_tx = of_parse_phandle(np, "shmem", 0);
		np_rx = of_parse_phandle(np, "shmem", 1);
		/* SCMI Tx and Rx shared mem areas have to be distinct */
		if (!np_tx || !np_rx || np_tx == np_rx) {
			dev_warn(cdev, "Invalid shmem descriptor for '%s'\n",
				 of_node_full_name(np));
			ret = -EINVAL;
		}

		of_node_put(np_tx);
		of_node_put(np_rx);
	}

	return ret;
}

/* MINDONE (F537): accept vendor shmem compatible strings too --
 * MediaTek's DT uses arm,scmi-{tx,rx}-shmem, the generic driver only
 * knew arm,scmi-shmem and returned -ENXIO, taking SCMI down and with it
 * mmdvfs, graphics, and (via the compositor crash) the whole Android
 * boot. */
/* MINDONE: defer the SCMI bind until the service processor is ready.
 * Accepting the vendor strings alone isn't enough -- without deferring,
 * the driver reads the SP's shmem region, and an early read from this
 * carve-out silently hangs the boot. Kernel 5 lands in the working
 * window by luck (139 deferred retries, succeeds at 1.968s); this
 * reproduces that deliberately via the deferred-probe mechanism. */
static int mindone_scmi_defer;		/* ms from boot start; 0 = don't defer */
static int mindone_scmi_defer_cnt;

static int __init mindone_set_scmi_defer(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_scmi_defer);
	return 1;
}
__setup("mindone_scmi_defer=", mindone_set_scmi_defer);

/* True once it's time to let the driver proceed. */
static bool mindone_scmi_ready(void)
{
	s64 ms;

	if (mindone_scmi_defer <= 0)
		return true;
	ms = ktime_to_ms(ktime_get_boottime());
	if (ms < mindone_scmi_defer) {
		mindone_scmi_defer_cnt++;
		/* log rarely: the bind gets deferred hundreds of times */
		if ((mindone_scmi_defer_cnt & 0x1f) == 1)
			pr_info("MINDONE-SCMI: deferring, %lld ms of %d (count: %d)\n",
				ms, mindone_scmi_defer, mindone_scmi_defer_cnt);
		return false;
	}
	if (mindone_scmi_defer_cnt) {
		pr_info("MINDONE-SCMI: proceeding at %lld ms, deferred count: %d\n",
			ms, mindone_scmi_defer_cnt);
		mindone_scmi_defer_cnt = 0;
	}
	return true;
}

static bool mindone_shmem_ok(struct device_node *np)
{
	return of_device_is_compatible(np, "arm,scmi-shmem") ||
	       of_device_is_compatible(np, "arm,scmi-tx-shmem") ||
	       of_device_is_compatible(np, "arm,scmi-rx-shmem");
}

/* MINDONE: wake-up -- once the defer window elapses, re-scan the
 * deferred-probe queue. Without it the window expires with nobody left
 * to retry the bind (measured: the last of 105 attempts landed at
 * 0.648s). */
extern void driver_deferred_probe_trigger(void);	/* drivers/base/base.h, not static */

#define MINDONE_SCMI_KICKS	8	/* retry count */
#define MINDONE_SCMI_GAP	250	/* ms between retries */

static struct delayed_work mindone_scmi_work;
static int mindone_scmi_kicks_left = MINDONE_SCMI_KICKS;

static void mindone_scmi_kick(struct work_struct *w)
{
	pr_info("MINDONE-SCMI: wake-up at %lld ms, retries left %d\n",
		ktime_to_ms(ktime_get_boottime()), mindone_scmi_kicks_left);
	driver_deferred_probe_trigger();
	if (--mindone_scmi_kicks_left > 0)
		schedule_delayed_work(&mindone_scmi_work,
				      msecs_to_jiffies(MINDONE_SCMI_GAP));
}

static int __init mindone_scmi_kick_init(void)
{
	s64 left;

	if (mindone_scmi_defer <= 0)
		return 0;
	left = mindone_scmi_defer - ktime_to_ms(ktime_get_boottime()) + 50;
	if (left < 50)
		left = 50;
	INIT_DELAYED_WORK(&mindone_scmi_work, mindone_scmi_kick);
	schedule_delayed_work(&mindone_scmi_work, msecs_to_jiffies((int)left));
	pr_info("MINDONE-SCMI: wake-up armed in %lld ms\n", left);
	return 0;
}
late_initcall(mindone_scmi_kick_init);

/* MINDONE: step-by-step bisection limiter.
 * The boot outcome is the readout: survives => steps up to N are safe,
 * dies => step N is the culprit. Needed because a log snapshot is only
 * captured at 4s and death happens earlier. */
static int mindone_scmi_stop;

static int __init mindone_set_scmi_stop(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_scmi_stop);
	return 1;
}
__setup("mindone_scmi_stop=", mindone_set_scmi_stop);

/* True once it's time to stop at this step. */
static bool mindone_stop_here(int step)
{
	if (mindone_scmi_stop <= 0 || step < mindone_scmi_stop)
		return false;
	pr_info("MINDONE-SCMI: stopping at step %d (limit %d)\n",
		step, mindone_scmi_stop);
	return true;
}

static int mailbox_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			      bool tx)
{
	const char *desc = tx ? "Tx" : "Rx";
	struct device *cdev = cinfo->dev;
	struct scmi_mailbox *smbox;
	struct device_node *shmem;
	int ret, idx = tx ? 0 : 1;
	struct mbox_client *cl;
	resource_size_t size;
	struct resource res;

	if (!mindone_scmi_ready())	/* MINDONE: service processor not up yet */
		return -EPROBE_DEFER;

	ret = mailbox_chan_validate(cdev);
	if (ret)
		return ret;
	if (mindone_stop_here(1))	/* MINDONE */
		return -ENXIO;

	smbox = devm_kzalloc(dev, sizeof(*smbox), GFP_KERNEL);
	if (!smbox)
		return -ENOMEM;

	shmem = of_parse_phandle(cdev->of_node, "shmem", idx);
	if (!mindone_shmem_ok(shmem)) {	/* MINDONE: plus vendor strings */
		of_node_put(shmem);
		return -ENXIO;
	}
	if (mindone_stop_here(2))	/* MINDONE */
		return -ENXIO;

	ret = of_address_to_resource(shmem, 0, &res);
	of_node_put(shmem);
	if (ret) {
		dev_err(cdev, "failed to get SCMI %s shared memory\n", desc);
		return ret;
	}
	if (mindone_stop_here(3))	/* MINDONE */
		return -ENXIO;

	size = resource_size(&res);
	pr_info("MINDONE-SCMI: region %s: 0x%llx size 0x%llx\n",	/* MINDONE */
		desc, (u64)res.start, (u64)resource_size(&res));
	smbox->shmem = devm_ioremap(dev, res.start, size);
	if (!smbox->shmem) {
		dev_err(dev, "failed to ioremap SCMI %s shared memory\n", desc);
		return -EADDRNOTAVAIL;
	}
	if (mindone_stop_here(4))	/* MINDONE */
		return -ENXIO;

	cl = &smbox->cl;
	cl->dev = cdev;
	cl->tx_prepare = tx ? tx_prepare : NULL;
	cl->rx_callback = rx_callback;
	cl->tx_block = false;
	cl->knows_txdone = tx;
	smbox->io_ops = shmem_get_io_ops(shmem);
	if (mindone_stop_here(5))	/* MINDONE */
		return -ENXIO;

	smbox->chan = mbox_request_channel(cl, tx ? 0 : 1);
	if (IS_ERR(smbox->chan)) {
		ret = PTR_ERR(smbox->chan);
		if (ret != -EPROBE_DEFER)
			dev_err(cdev, "failed to request SCMI %s mailbox\n",
				tx ? "Tx" : "Rx");
		return ret;
	}

	if (mindone_stop_here(6))	/* MINDONE */
		return -ENXIO;

	cinfo->transport_info = smbox;
	smbox->cinfo = cinfo;
	mutex_init(&smbox->chan_lock);

	return 0;
}

static int mailbox_chan_free(int id, void *p, void *data)
{
	struct scmi_chan_info *cinfo = p;
	struct scmi_mailbox *smbox = cinfo->transport_info;

	if (smbox && !IS_ERR(smbox->chan)) {
		mbox_free_channel(smbox->chan);
		cinfo->transport_info = NULL;
		smbox->chan = NULL;
		smbox->cinfo = NULL;
	}

	scmi_free_channel(cinfo, data, id);

	return 0;
}

static int mailbox_send_message(struct scmi_chan_info *cinfo,
				struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;
	int ret;

	/* MINDONE: level 7 -- the bind completed FULLY, but sending is
	 * blocked. Separates "the exchange itself hangs" from "something
	 * between a successful bind and the send hangs". */
	if (mindone_scmi_stop == 7) {
		pr_info_once("MINDONE-SCMI: send blocked (level 7)\n");
		return -EIO;
	}

	/*
	 * The mailbox layer has its own queue. However the mailbox queue confuses
	 * the per message SCMI timeouts since the clock starts when the message is
	 * submitted into the mailbox queue. So when multiple messages are queued up
	 * the clock starts on all messages instead of only the one inflight.
	 */
	mutex_lock(&smbox->chan_lock);

	ret = mbox_send_message(smbox->chan, xfer);

	/* mbox_send_message returns non-negative value on success, so reset */
	if (ret < 0) {
		mutex_unlock(&smbox->chan_lock);
		return ret;
	}

	return 0;
}

static void mailbox_mark_txdone(struct scmi_chan_info *cinfo, int ret,
				struct scmi_xfer *__unused)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	mbox_client_txdone(smbox->chan, ret);

	/* Release channel */
	mutex_unlock(&smbox->chan_lock);
}

static void mailbox_fetch_response(struct scmi_chan_info *cinfo,
				   struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	shmem_fetch_response(smbox->shmem, xfer, smbox->io_ops->fromio);
}

static void mailbox_fetch_notification(struct scmi_chan_info *cinfo,
				       size_t max_len, struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	shmem_fetch_notification(smbox->shmem, max_len, xfer,
				 smbox->io_ops->fromio);
}

static void mailbox_clear_channel(struct scmi_chan_info *cinfo)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	shmem_clear_channel(smbox->shmem);
}

static bool
mailbox_poll_done(struct scmi_chan_info *cinfo, struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	return shmem_poll_done(smbox->shmem, xfer);
}

static const struct scmi_transport_ops scmi_mailbox_ops = {
	.chan_available = mailbox_chan_available,
	.chan_setup = mailbox_chan_setup,
	.chan_free = mailbox_chan_free,
	.send_message = mailbox_send_message,
	.mark_txdone = mailbox_mark_txdone,
	.fetch_response = mailbox_fetch_response,
	.fetch_notification = mailbox_fetch_notification,
	.clear_channel = mailbox_clear_channel,
	.poll_done = mailbox_poll_done,
};

const struct scmi_desc scmi_mailbox_desc = {
	.ops = &scmi_mailbox_ops,
	.max_rx_timeout_ms = 30, /* We may increase this if required */
	.max_msg = 20, /* Limited by MBOX_TX_QUEUE_LEN */
	.max_msg_size = 128,
};
