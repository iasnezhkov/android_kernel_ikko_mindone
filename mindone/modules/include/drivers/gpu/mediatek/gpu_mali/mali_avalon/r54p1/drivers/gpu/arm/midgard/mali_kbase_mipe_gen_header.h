/* SPDX-License-Identifier: GPL-2.0 */
#include "mali_kbase_mipe_proto.h"

#if !defined(MIPE_HEADER_BLOB_VAR_NAME)
#error "MIPE_HEADER_BLOB_VAR_NAME must be defined!"
#endif

#if !defined(MIPE_HEADER_BLOB_VAR_ATTRIBUTE)
#define MIPE_HEADER_BLOB_VAR_ATTRIBUTE
#endif

#if !defined(MIPE_HEADER_PACKED_ATTRIBUTE)
#define MIPE_HEADER_PACKED_ATTRIBUTE __attribute__((__packed__))
#endif

#if !defined(MIPE_HEADER_STREAM_ID)
#error "MIPE_HEADER_STREAM_ID must be defined!"
#endif

#if !defined(MIPE_HEADER_PKT_CLASS)
#error "MIPE_HEADER_PKT_CLASS must be defined!"
#endif

#if !defined(MIPE_HEADER_TRACEPOINT_LIST)
#error "MIPE_HEADER_TRACEPOINT_LIST must be defined!"
#endif

#if !defined(MIPE_HEADER_TRACEPOINT_LIST_SIZE)
#error "MIPE_HEADER_TRACEPOINT_LIST_SIZE must be defined!"
#endif

#if defined(MIPE_HEADER_ENUM_LIST)

#if !defined(MIPE_HEADER_ENUM_MSG_ID)
#error "MIPE_HEADER_ENUM_MSG_ID must be defined!"
#endif

#else
#define MIPE_HEADER_ENUM_LIST
#endif

const struct
{
	u32 _mipe_w0;
	u32 _mipe_w1;
	u8  _protocol_version;
	u8  _pointer_size;
	u32 _tp_count;
#define TRACEPOINT_DESC(name, desc, arg_types, arg_names)	\
	struct {					\
		u32  _name;				\
		u32  _size_string_name;		\
		char _string_name[sizeof(#name)];	\
		u32  _size_desc;			\
		char _desc[sizeof(desc)];		\
		u32  _size_arg_types;		\
		char _arg_types[sizeof(arg_types)];	\
		u32  _size_arg_names;		\
		char _arg_names[sizeof(arg_names)];	\
	} MIPE_HEADER_PACKED_ATTRIBUTE __ ## name;

#define ENUM_DESC(arg_name, value)					\
	struct {							\
		u32 _msg_id;					\
		u32 _arg_name_len;					\
		char _arg_name[sizeof(#arg_name)];			\
		u32 _value;						\
		u32 _value_str_len;					\
		char _value_str[sizeof(#value)];			\
	} MIPE_HEADER_PACKED_ATTRIBUTE __ ## arg_name ## _ ## value;

	MIPE_HEADER_TRACEPOINT_LIST
	MIPE_HEADER_ENUM_LIST
#undef TRACEPOINT_DESC
#undef ENUM_DESC
} MIPE_HEADER_PACKED_ATTRIBUTE MIPE_HEADER_BLOB_VAR_NAME MIPE_HEADER_BLOB_VAR_ATTRIBUTE = {
	._mipe_w0 = MIPE_PACKET_HEADER_W0(
		TL_PACKET_FAMILY_TL,
		MIPE_HEADER_PKT_CLASS,
		TL_PACKET_TYPE_HEADER,
		MIPE_HEADER_STREAM_ID),
	._mipe_w1 = MIPE_PACKET_HEADER_W1(
		sizeof(MIPE_HEADER_BLOB_VAR_NAME) - PACKET_HEADER_SIZE,
		0),
	._protocol_version = SWTRACE_VERSION,
	._pointer_size = sizeof(void *),
	._tp_count = MIPE_HEADER_TRACEPOINT_LIST_SIZE,
#define TRACEPOINT_DESC(name, desc, arg_types, arg_names)	\
	.__ ## name = {					\
		._name = name,				\
		._size_string_name = sizeof(#name),	\
		._string_name = #name,			\
		._size_desc = sizeof(desc),		\
		._desc = desc,				\
		._size_arg_types = sizeof(arg_types),	\
		._arg_types = arg_types,		\
		._size_arg_names = sizeof(arg_names),	\
		._arg_names = arg_names			\
	},
#define ENUM_DESC(arg_name, value)				\
	.__ ## arg_name ## _ ## value = {			\
		._msg_id = MIPE_HEADER_ENUM_MSG_ID,		\
		._arg_name_len = sizeof(#arg_name),		\
		._arg_name = #arg_name,				\
		._value = value,				\
		._value_str_len = sizeof(#value),		\
		._value_str = #value				\
	},

	MIPE_HEADER_TRACEPOINT_LIST
	MIPE_HEADER_ENUM_LIST
#undef TRACEPOINT_DESC
#undef ENUM_DESC
};

#undef MIPE_HEADER_BLOB_VAR_NAME
#undef MIPE_HEADER_BLOB_VAR_ATTRIBUTE
#undef MIPE_HEADER_STREAM_ID
#undef MIPE_HEADER_PKT_CLASS
#undef MIPE_HEADER_TRACEPOINT_LIST
#undef MIPE_HEADER_TRACEPOINT_LIST_SIZE
#undef MIPE_HEADER_ENUM_LIST
#undef MIPE_HEADER_ENUM_MSG_ID

