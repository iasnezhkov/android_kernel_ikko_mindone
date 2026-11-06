/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_MIPE_PROTO_H)
#define _KBASE_MIPE_PROTO_H

#define _BITFIELD_MASK_FIELD(pos, len) \
	(((1u << len) - 1) << pos)

#define _BITFIELD_SET_FIELD(pos, len, value) \
	(_BITFIELD_MASK_FIELD(pos, len) & (((u32) value) << pos))

#define BITFIELD_SET(field_name, value) \
	_BITFIELD_SET_FIELD(field_name ## _POS, field_name ## _LEN, value)

#define SWTRACE_VERSION    3

#define PACKET_STREAMID_POS  0
#define PACKET_STREAMID_LEN  8
#define PACKET_RSVD1_POS     (PACKET_STREAMID_POS + PACKET_STREAMID_LEN)
#define PACKET_RSVD1_LEN     8
#define PACKET_TYPE_POS      (PACKET_RSVD1_POS + PACKET_RSVD1_LEN)
#define PACKET_TYPE_LEN      3
#define PACKET_CLASS_POS     (PACKET_TYPE_POS + PACKET_TYPE_LEN)
#define PACKET_CLASS_LEN     7
#define PACKET_FAMILY_POS    (PACKET_CLASS_POS + PACKET_CLASS_LEN)
#define PACKET_FAMILY_LEN    6

#define PACKET_LENGTH_POS    0
#define PACKET_LENGTH_LEN    24
#define PACKET_SEQBIT_POS    (PACKET_LENGTH_POS + PACKET_LENGTH_LEN)
#define PACKET_SEQBIT_LEN    1
#define PACKET_RSVD2_POS     (PACKET_SEQBIT_POS + PACKET_SEQBIT_LEN)
#define PACKET_RSVD2_LEN     7

#define MIPE_PACKET_HEADER_W0(pkt_family, pkt_class, pkt_type, stream_id) \
	(0                                          \
	| BITFIELD_SET(PACKET_FAMILY,   pkt_family) \
	| BITFIELD_SET(PACKET_CLASS,    pkt_class)  \
	| BITFIELD_SET(PACKET_TYPE,     pkt_type)   \
	| BITFIELD_SET(PACKET_STREAMID, stream_id))

#define MIPE_PACKET_HEADER_W1(packet_length, seqbit) \
	(0                                           \
	| BITFIELD_SET(PACKET_LENGTH, packet_length) \
	| BITFIELD_SET(PACKET_SEQBIT, seqbit))

#define PACKET_HEADER_SIZE 8

#define PACKET_NUMBER_SIZE 4

enum tl_packet_family {
	TL_PACKET_FAMILY_CTRL = 0,
	TL_PACKET_FAMILY_TL = 1,
	TL_PACKET_FAMILY_COUNT
};

enum tl_packet_class {
	TL_PACKET_CLASS_OBJ = 0,
	TL_PACKET_CLASS_AUX = 1,
};

enum tl_packet_type {
	TL_PACKET_TYPE_HEADER = 0,
	TL_PACKET_TYPE_BODY = 1,
	TL_PACKET_TYPE_SUMMARY = 2,
};

enum tl_stream_id {
	TL_STREAM_ID_USER = 0,
	TL_STREAM_ID_KERNEL = 1,
	TL_STREAM_ID_CSFFW = 2,
};

#endif

