// SPDX-License-Identifier: GPL-2.0
/*
 * For transport using shared mem structure.
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/ktime.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/processor.h>
#include <linux/types.h>

#include <asm-generic/bug.h>

#include "common.h"

/*
 * SCMI specification requires all parameters, message headers, return
 * arguments or any protocol data to be expressed in little endian
 * format only.
 */
struct scmi_shared_mem {
	__le32 reserved;
	__le32 channel_status;
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR	BIT(1)
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE	BIT(0)
	__le32 reserved1[2];
	__le32 flags;
#define SCMI_SHMEM_FLAG_INTR_ENABLED	BIT(0)
	__le32 length;
	__le32 msg_header;
	u8 msg_payload[];
};

static inline void shmem_memcpy_fromio32(void *to,
					 const void __iomem *from,
					 size_t count)
{
	WARN_ON(!IS_ALIGNED((unsigned long)from, 4) ||
		!IS_ALIGNED((unsigned long)to, 4) ||
		count % 4);

	__ioread32_copy(to, from, count / 4);
}

static inline void shmem_memcpy_toio32(void __iomem *to,
				       const void *from,
				       size_t count)
{
	WARN_ON(!IS_ALIGNED((unsigned long)from, 4) ||
		!IS_ALIGNED((unsigned long)to, 4) ||
		count % 4);

	__iowrite32_copy(to, from, count / 4);
}

static struct scmi_shmem_io_ops shmem_io_ops32 = {
	.fromio	= shmem_memcpy_fromio32,
	.toio	= shmem_memcpy_toio32,
};

/* Wrappers are needed for proper memcpy_{from,to}_io expansion by the
 * pre-processor.
 */
static inline void shmem_memcpy_fromio(void *to,
				       const void __iomem *from,
				       size_t count)
{
	memcpy_fromio(to, from, count);
}

static inline void shmem_memcpy_toio(void __iomem *to,
				     const void *from,
				     size_t count)
{
	memcpy_toio(to, from, count);
}

static struct scmi_shmem_io_ops shmem_io_ops_default = {
	.fromio = shmem_memcpy_fromio,
	.toio	= shmem_memcpy_toio,
};

/* MINDONE (F563): block reads of the shmem region. Separates two causes of kernel 6
 * dying during the SCMI pass: reading the region vs. ringing the mailbox
 * doorbell. The boot outcome itself is the readout, no log needed. */
static int mindone_shmem_noread;
/* MINDONE (F571): cap response length to the MAPPED region's size. This
 * device's region is 0x80 = 128B TOTAL including the 28-byte header,
 * leaving 100B (96B from msg_payload+4) for payload. Generic code reads
 * the length FROM the region itself, capped only by the rx buffer (up to
 * 128B), so a long response (e.g. the protocol list) reads PAST the
 * mapped region. mindone_shmem_limit=<bytes>; 0 = unlimited. */
static int mindone_shmem_limit;

/* MINDONE (F571): bisect the base-protocol conversation itself -- level
 * 8 showed it is the culprit, but several commands run in sequence.
 * mindone_shmem_maxmsg=N passes the first N responses through for real,
 * then reports "not supported" so the conversation ends cleanly and the
 * boot outcome is the readout. Every response is logged so a surviving
 * boot shows which commands got through. */
static int mindone_shmem_maxmsg;
static int mindone_msg_cnt;

/* MINDONE: same bisection, but ONLY for vendor protocol 0x80 -- a
 * through-counter doesn't work because with the protocol list allowed,
 * the base protocol sends more messages and the threshold drifts onto
 * its own fatal command. mindone_p80_max=N passes the first N 0x80
 * responses, marks the rest unsupported; -1 blocks all of them. */
static int mindone_p80_max = -2;	/* -2 = disabled */
static int mindone_p80_cnt;

static int __init mindone_set_p80_max(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_p80_max);
	return 1;
}
__setup("mindone_p80_max=", mindone_set_p80_max);

static int __init mindone_set_shmem_maxmsg(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_shmem_maxmsg);
	return 1;
}
__setup("mindone_shmem_maxmsg=", mindone_set_shmem_maxmsg);

static int __init mindone_set_shmem_limit(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_shmem_limit);
	return 1;
}
__setup("mindone_shmem_limit=", mindone_set_shmem_limit);
static int mindone_shmem_nowrite;	/* MINDONE: also skip writes to the region */

static int __init mindone_set_shmem_nowrite(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_shmem_nowrite);
	return 1;
}
__setup("mindone_shmem_nowrite=", mindone_set_shmem_nowrite);

static int __init mindone_set_shmem_noread(char *str)
{
	if (str)
		(void)kstrtoint(str, 10, &mindone_shmem_noread);
	return 1;
}
__setup("mindone_shmem_noread=", mindone_set_shmem_noread);

void shmem_tx_prepare(struct scmi_shared_mem __iomem *shmem,
		      struct scmi_xfer *xfer, struct scmi_chan_info *cinfo,
		      shmem_copy_toio_t copy_toio)
{
	ktime_t stop;

	/*
	 * Ideally channel must be free by now unless OS timeout last
	 * request and platform continued to process the same, wait
	 * until it releases the shared memory, otherwise we may endup
	 * overwriting its response with new message payload or vice-versa.
	 * Giving up anyway after twice the expected channel timeout so as
	 * not to bail-out on intermittent issues where the platform is
	 * occasionally a bit slower to answer.
	 *
	 * Note that after a timeout is detected we bail-out and carry on but
	 * the transport functionality is probably permanently compromised:
	 * this is just to ease debugging and avoid complete hangs on boot
	 * due to a misbehaving SCMI firmware.
	 */
	if (mindone_shmem_noread) {	/* MINDONE: no reads of the region at all */
		pr_info_once("MINDONE-SHMEM: region reads blocked, treating channel as free\n");
	} else {
	stop = ktime_add_ms(ktime_get(), 2 * cinfo->rx_timeout_ms);
	spin_until_cond((ioread32(&shmem->channel_status) &
			 SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE) ||
			 ktime_after(ktime_get(), stop));
	if (!(ioread32(&shmem->channel_status) &
	      SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE)) {
		WARN_ON_ONCE(1);
		dev_err(cinfo->dev,
			"Timeout waiting for a free TX channel !\n");
		return;
	}
	}

	if (mindone_shmem_nowrite) {	/* MINDONE: don't touch the region at all */
		pr_info_once("MINDONE-SHMEM: region writes blocked\n");
		return;
	}

	/* Mark channel busy + clear error */
	iowrite32(0x0, &shmem->channel_status);
	iowrite32(xfer->hdr.poll_completion ? 0 : SCMI_SHMEM_FLAG_INTR_ENABLED,
		  &shmem->flags);
	iowrite32(sizeof(shmem->msg_header) + xfer->tx.len, &shmem->length);
	iowrite32(pack_scmi_header(&xfer->hdr), &shmem->msg_header);
	if (xfer->tx.buf)
		copy_toio(shmem->msg_payload, xfer->tx.buf, xfer->tx.len);
}

u32 shmem_read_header(struct scmi_shared_mem __iomem *shmem)
{
	u32 mnd_v;

	if (mindone_shmem_noread == 1) {	/* MINDONE: NO access at all */
		pr_info_once("MINDONE-SHMEM: rx region read blocked\n");
		return 0;
	}
	mnd_v = ioread32(&shmem->msg_header);
	if (mindone_shmem_noread == 2) {	/* MINDONE: mode 2 -- read and discard */
		pr_info_once("MINDONE-SHMEM: read performed, result discarded\n");
		return 0;
	}
	return mnd_v;
}

void shmem_fetch_response(struct scmi_shared_mem __iomem *shmem,
			  struct scmi_xfer *xfer,
			  shmem_copy_fromio_t copy_fromio)
{
	if (mindone_shmem_noread == 1) {	/* MINDONE: NO access */
		xfer->hdr.status = 0;
		xfer->rx.len = 0;
		return;
	}
	if (mindone_shmem_noread == 2) {	/* MINDONE: mode 2 -- read and discard */
		(void)ioread32(&shmem->length);
		(void)ioread32(shmem->msg_payload);
		xfer->hdr.status = 0;
		xfer->rx.len = 0;
		return;
	}
	size_t len = ioread32(&shmem->length);

	if (mindone_p80_max != -2 && xfer->hdr.protocol_id == 0x80) {
		mindone_p80_cnt++;
		pr_info("MINDONE-P80: response #%d cmd=0x%x len=%zu\n",
			mindone_p80_cnt, xfer->hdr.id, len);
		if (mindone_p80_max < 0 || mindone_p80_cnt > mindone_p80_max) {
			xfer->hdr.status = -3;	/* NOT SUPPORTED */
			xfer->rx.len = 0;
			return;
		}
	}

	if (mindone_shmem_maxmsg > 0) {
		mindone_msg_cnt++;
		pr_info("MINDONE-SCMI: response #%d proto=0x%x cmd=0x%x len=%zu\n",
			mindone_msg_cnt, xfer->hdr.protocol_id, xfer->hdr.id, len);
		if (mindone_msg_cnt > mindone_shmem_maxmsg) {
			xfer->hdr.status = -3;	/* NOT SUPPORTED -- end the conversation cleanly */
			xfer->rx.len = 0;
			return;
		}
	}

	xfer->hdr.status = ioread32(shmem->msg_payload);
	/* Skip the length of header and status in shmem area i.e 8 bytes */
	xfer->rx.len = min_t(size_t, xfer->rx.len, len > 8 ? len - 8 : 0);
	if (mindone_shmem_limit > 0 &&
	    xfer->rx.len > (size_t)mindone_shmem_limit - 4) {	/* MINDONE */
		pr_info_once("MINDONE-SHMEM: response %zu B truncated to %d B (region claimed %zu)\n",
			     xfer->rx.len, mindone_shmem_limit - 4, len);
		xfer->rx.len = (size_t)mindone_shmem_limit - 4;
	}

	/* Take a copy to the rx buffer.. */
	copy_fromio(xfer->rx.buf, shmem->msg_payload + 4, xfer->rx.len);
}

void shmem_fetch_notification(struct scmi_shared_mem __iomem *shmem,
			      size_t max_len, struct scmi_xfer *xfer,
			      shmem_copy_fromio_t copy_fromio)
{
	if (mindone_shmem_noread) {	/* MINDONE */
		xfer->rx.len = 0;
		return;
	}
	size_t len = ioread32(&shmem->length);

	/* Skip only the length of header in shmem area i.e 4 bytes */
	xfer->rx.len = min_t(size_t, max_len, len > 4 ? len - 4 : 0);
	if (mindone_shmem_limit > 0 && xfer->rx.len > (size_t)mindone_shmem_limit)	/* MINDONE */
		xfer->rx.len = (size_t)mindone_shmem_limit;

	/* Take a copy to the rx buffer.. */
	copy_fromio(xfer->rx.buf, shmem->msg_payload, xfer->rx.len);
}

void shmem_clear_channel(struct scmi_shared_mem __iomem *shmem)
{
	iowrite32(SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE, &shmem->channel_status);
}

bool shmem_poll_done(struct scmi_shared_mem __iomem *shmem,
		     struct scmi_xfer *xfer)
{
	if (mindone_shmem_noread)	/* MINDONE: response is never ready */
		return false;
	u16 xfer_id;

	xfer_id = MSG_XTRACT_TOKEN(ioread32(&shmem->msg_header));

	if (xfer->hdr.seq != xfer_id)
		return false;

	return ioread32(&shmem->channel_status) &
		(SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR |
		 SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);
}

bool shmem_channel_free(struct scmi_shared_mem __iomem *shmem)
{
	if (mindone_shmem_noread)	/* MINDONE: treat channel as free */
		return true;
	return (ioread32(&shmem->channel_status) &
			SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);
}

struct scmi_shmem_io_ops *shmem_get_io_ops(struct device_node *shmem)
{
	u32 reg_io_width;

	of_property_read_u32(shmem, "reg-io-width", &reg_io_width);
	switch (reg_io_width) {
	case 4:
		return &shmem_io_ops32;
	}

	return &shmem_io_ops_default;
}
