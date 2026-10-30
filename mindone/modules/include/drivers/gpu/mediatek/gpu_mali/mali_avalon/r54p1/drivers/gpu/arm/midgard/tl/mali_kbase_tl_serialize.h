/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_KBASE_TL_SERIALIZE_H)
#define _KBASE_TL_SERIALIZE_H

#include <mali_kbase.h>

#include <linux/timer.h>

#define NSECS_IN_SEC 1000000000ull

static inline size_t kbasep_serialize_bytes(char *buffer, size_t pos, const void *bytes, size_t len)
{
	KBASE_DEBUG_ASSERT(buffer);
	KBASE_DEBUG_ASSERT(bytes);

	memcpy(&buffer[pos], bytes, len);

	return pos + len;
}

static inline size_t kbasep_serialize_string(char *buffer, size_t pos, const char *string,
					     size_t max_write_size)
{
	u32 string_len;

	KBASE_DEBUG_ASSERT(buffer);
	KBASE_DEBUG_ASSERT(string);

	KBASE_DEBUG_ASSERT(max_write_size >= sizeof(string_len) + sizeof(char));
	max_write_size -= sizeof(string_len);

	string_len = strscpy(&buffer[pos + sizeof(string_len)], string, max_write_size);
	string_len += sizeof(char);

	KBASE_DEBUG_ASSERT(string_len <= max_write_size);

	memcpy(&buffer[pos], &string_len, sizeof(string_len));

	return pos + sizeof(string_len) + string_len;
}

static inline size_t kbasep_serialize_timestamp(void *buffer, size_t pos)
{
	u64 timestamp;

	timestamp = ktime_get_raw_ns();

	return kbasep_serialize_bytes(buffer, pos, &timestamp, sizeof(timestamp));
}
#endif
