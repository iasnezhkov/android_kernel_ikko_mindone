/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_TLSTREAM_H)
#define _KBASE_TLSTREAM_H

#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/wait.h>

#define PACKET_SIZE 4096

#define PACKET_COUNT 128

#define STRLEN_MAX 64

struct kbase_tlstream {
	spinlock_t lock;

	struct {
		atomic_t size;
		char data[PACKET_SIZE];
	} buffer[PACKET_COUNT];

	atomic_t wbi;
	atomic_t rbi;

	int numbered;
	atomic_t autoflush_counter;
	wait_queue_head_t *ready_read;
#if MALI_UNIT_TEST
	atomic_t bytes_generated;
#endif
};

enum tl_stream_type {
	TL_STREAM_TYPE_FIRST,
	TL_STREAM_TYPE_OBJ_SUMMARY = TL_STREAM_TYPE_FIRST,
	TL_STREAM_TYPE_OBJ,
	TL_STREAM_TYPE_AUX,
#if MALI_USE_CSF
	TL_STREAM_TYPE_CSFFW,
#endif
	TL_STREAM_TYPE_COUNT
};

void kbase_tlstream_init(struct kbase_tlstream *stream, enum tl_stream_type stream_type,
			 wait_queue_head_t *ready_read);

void kbase_tlstream_term(struct kbase_tlstream *stream);

void kbase_tlstream_reset(struct kbase_tlstream *stream);

char *kbase_tlstream_msgbuf_acquire(struct kbase_tlstream *stream, size_t msg_size,
				    unsigned long *flags) __acquires(&stream->lock);

void kbase_tlstream_msgbuf_release(struct kbase_tlstream *stream, unsigned long flags)
	__releases(&stream->lock);

size_t kbase_tlstream_flush_stream(struct kbase_tlstream *stream);

#endif
