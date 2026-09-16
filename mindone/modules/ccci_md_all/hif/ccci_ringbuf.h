/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

#ifndef __CCCI_RINGBUF_H__
#define __CCCI_RINGBUF_H__
enum ccci_ringbuf_error {
	CCCI_RINGBUF_OK = 0,
	CCCI_RINGBUF_PARAM_ERR,
	CCCI_RINGBUF_NOT_ENOUGH,
	CCCI_RINGBUF_BAD_HEADER,
	CCCI_RINGBUF_BAD_FOOTER,
	CCCI_RINGBUF_NOT_COMPLETE,
	CCCI_RINGBUF_EMPTY,
};

struct ccci_ringbuf {
	struct {
		unsigned int read;
		unsigned int write;
		unsigned int length;
	} rx_control, tx_control;
	/* MINDONE: C99 flexible array, not `buffer[0]` (07.09, F3884). A zero-length array
	 * is an old GCC extension, and the bounds sanitizer treats it as having EXACTLY ZERO
	 * elements: any `buffer + N` becomes an out-of-bounds access. On 6.12 this is a trap
	 * that kills the CPU core with no explanation (`Internal error: UBSAN`); on 6.1 it
	 * stays silent only because local bounds checking is not enabled there. A flexible
	 * array describes the same memory layout but makes no bounds promise, and the check
	 * does not touch it -- this is exactly how upstream fixes it.
	 * sizeof(struct) does not change: both variants give zero.
	 */
	unsigned char buffer[];
};
#define CCCI_RINGBUF_CTL_LEN (8+sizeof(struct ccci_ringbuf)+8)

int ccci_ringbuf_readable(int md_id, struct ccci_ringbuf *ringbuf);
int ccci_ringbuf_writeable(int md_id, struct ccci_ringbuf *ringbuf,
	unsigned int write_size);
struct ccci_ringbuf *ccci_create_ringbuf(int md_id, unsigned char *buf,
	int buf_size, int rx_size, int tx_size);
int ccci_ringbuf_read(int md_id, struct ccci_ringbuf *ringbuf,
	unsigned char *buf, int read_size);
int ccci_ringbuf_write(int md_id, struct ccci_ringbuf *ringbuf,
	unsigned char *data, int data_len);
void ccci_ringbuf_move_rpointer(int md_id, struct ccci_ringbuf *ringbuf,
	int read_size);
void ccci_ringbuf_reset(int md_id, struct ccci_ringbuf *ringbuf, int dir);
#endif				/* __CCCI_RINGBUF_H__ */
