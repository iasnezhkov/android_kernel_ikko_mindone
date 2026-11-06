// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_tlstream.h"
#include "mali_kbase_tl_serialize.h"
#include "mali_kbase_mipe_proto.h"

static void kbasep_packet_header_setup(char *buffer, enum tl_packet_family pkt_family,
				       enum tl_packet_class pkt_class, enum tl_packet_type pkt_type,
				       unsigned int stream_id, int numbered)
{
	u32 words[2] = {
		MIPE_PACKET_HEADER_W0(pkt_family, pkt_class, pkt_type, stream_id),
		MIPE_PACKET_HEADER_W1(0, !!numbered),
	};
	memcpy(buffer, words, sizeof(words));
}

static void kbasep_packet_header_update(char *buffer, size_t data_size, int numbered)
{
	u32 word1 = MIPE_PACKET_HEADER_W1((u32)data_size, !!numbered);

	KBASE_DEBUG_ASSERT(buffer);

	memcpy(&buffer[sizeof(u32)], &word1, sizeof(word1));
}

static void kbasep_packet_number_update(char *buffer, u32 counter)
{
	KBASE_DEBUG_ASSERT(buffer);

	memcpy(&buffer[PACKET_HEADER_SIZE], &counter, sizeof(counter));
}

void kbase_tlstream_reset(struct kbase_tlstream *stream)
{
	unsigned int i;

	for (i = 0; i < PACKET_COUNT; i++) {
		if (stream->numbered)
			atomic_set(&stream->buffer[i].size,
				   PACKET_HEADER_SIZE + PACKET_NUMBER_SIZE);
		else
			atomic_set(&stream->buffer[i].size, PACKET_HEADER_SIZE);
	}

	atomic_set(&stream->wbi, 0);
	atomic_set(&stream->rbi, 0);
}

static const struct {
	enum tl_packet_family pkt_family;
	enum tl_packet_class pkt_class;
	enum tl_packet_type pkt_type;
	enum tl_stream_id stream_id;
} tl_stream_cfg[TL_STREAM_TYPE_COUNT] = {
	{
		TL_PACKET_FAMILY_TL,
		TL_PACKET_CLASS_OBJ,
		TL_PACKET_TYPE_SUMMARY,
		TL_STREAM_ID_KERNEL,
	},
	{
		TL_PACKET_FAMILY_TL,
		TL_PACKET_CLASS_OBJ,
		TL_PACKET_TYPE_BODY,
		TL_STREAM_ID_KERNEL,
	},
	{
		TL_PACKET_FAMILY_TL,
		TL_PACKET_CLASS_AUX,
		TL_PACKET_TYPE_BODY,
		TL_STREAM_ID_KERNEL,
	},
#if MALI_USE_CSF
	{
		TL_PACKET_FAMILY_TL,
		TL_PACKET_CLASS_OBJ,
		TL_PACKET_TYPE_BODY,
		TL_STREAM_ID_CSFFW,
	},
#endif
};

void kbase_tlstream_init(struct kbase_tlstream *stream, enum tl_stream_type stream_type,
			 wait_queue_head_t *ready_read)
{
	unsigned int i;

	KBASE_DEBUG_ASSERT(stream);
	KBASE_DEBUG_ASSERT(stream_type < TL_STREAM_TYPE_COUNT);

	spin_lock_init(&stream->lock);

	if (tl_stream_cfg[stream_type].pkt_type == TL_PACKET_TYPE_BODY)
		stream->numbered = 1;
	else
		stream->numbered = 0;

	for (i = 0; i < PACKET_COUNT; i++)
		kbasep_packet_header_setup(stream->buffer[i].data,
					   tl_stream_cfg[stream_type].pkt_family,
					   tl_stream_cfg[stream_type].pkt_class,
					   tl_stream_cfg[stream_type].pkt_type,
					   tl_stream_cfg[stream_type].stream_id, stream->numbered);

#if MALI_UNIT_TEST
	atomic_set(&stream->bytes_generated, 0);
#endif
	stream->ready_read = ready_read;

	kbase_tlstream_reset(stream);
}

void kbase_tlstream_term(struct kbase_tlstream *stream)
{
	KBASE_DEBUG_ASSERT(stream);
}

static size_t kbasep_tlstream_msgbuf_submit(struct kbase_tlstream *stream, unsigned int wb_idx_raw,
					    unsigned int wb_size)
{
	unsigned int wb_idx = wb_idx_raw % PACKET_COUNT;

	atomic_set(&stream->autoflush_counter, -1);

	kbasep_packet_header_update(stream->buffer[wb_idx].data, wb_size - PACKET_HEADER_SIZE,
				    stream->numbered);

	if (stream->numbered)
		kbasep_packet_number_update(stream->buffer[wb_idx].data, wb_idx_raw);

	smp_wmb();
	atomic_inc(&stream->wbi);

	wake_up_interruptible(stream->ready_read);

	wb_size = PACKET_HEADER_SIZE;
	if (stream->numbered)
		wb_size += PACKET_NUMBER_SIZE;

	return wb_size;
}

char *kbase_tlstream_msgbuf_acquire(struct kbase_tlstream *stream, size_t msg_size,
				    unsigned long *flags) __acquires(&stream->lock)
{
	unsigned int wb_idx_raw;
	unsigned int wb_idx;
	size_t wb_size;

	KBASE_DEBUG_ASSERT(PACKET_SIZE - PACKET_HEADER_SIZE - PACKET_NUMBER_SIZE >= msg_size);

	spin_lock_irqsave(&stream->lock, *flags);

	wb_idx_raw = (unsigned int)atomic_read(&stream->wbi);
	wb_idx = wb_idx_raw % PACKET_COUNT;
	wb_size = (size_t)atomic_read(&stream->buffer[wb_idx].size);

	if (wb_size + msg_size > PACKET_SIZE) {
		wb_size = kbasep_tlstream_msgbuf_submit(stream, wb_idx_raw, wb_size);
		wb_idx = (wb_idx_raw + 1) % PACKET_COUNT;
	}

	atomic_set(&stream->buffer[wb_idx].size, wb_size + msg_size);

#if MALI_UNIT_TEST
	atomic_add(msg_size, &stream->bytes_generated);
#endif

	return &stream->buffer[wb_idx].data[wb_size];
}

void kbase_tlstream_msgbuf_release(struct kbase_tlstream *stream, unsigned long flags)
	__releases(&stream->lock)
{

	atomic_set(&stream->autoflush_counter, 0);

	spin_unlock_irqrestore(&stream->lock, flags);
}

size_t kbase_tlstream_flush_stream(struct kbase_tlstream *stream)
{
	unsigned long flags;
	unsigned int wb_idx_raw;
	unsigned int wb_idx;
	size_t wb_size;
	size_t min_size = PACKET_HEADER_SIZE;

	if (stream->numbered)
		min_size += PACKET_NUMBER_SIZE;

	spin_lock_irqsave(&stream->lock, flags);

	wb_idx_raw = (unsigned int)atomic_read(&stream->wbi);
	wb_idx = wb_idx_raw % PACKET_COUNT;
	wb_size = (size_t)atomic_read(&stream->buffer[wb_idx].size);

	if (wb_size > min_size) {
		wb_size = kbasep_tlstream_msgbuf_submit(stream, wb_idx_raw, wb_size);
		wb_idx = (wb_idx_raw + 1) % PACKET_COUNT;
		atomic_set(&stream->buffer[wb_idx].size, wb_size);
	} else {

		wb_size = 0;
	}

	spin_unlock_irqrestore(&stream->lock, flags);
	return wb_size;
}
