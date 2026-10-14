#ifndef MINDONE_RXFILTER_MULTICAST_H
#define MINDONE_RXFILTER_MULTICAST_H

#include "rxfilter_control.h"

struct rxfilter_mcast_request {
	u64 revision;
	u16 length;
	u8 valid;
	u8 payload[200];
};

struct rxfilter_mcast_cache {
	struct rxfilter_mcast_request bss[4];
};

static inline void rxfilter_mcast_normalize(const u8 requested[3],
		int seek_failed, u8 controls[3])
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		u8 value = requested[i];
		u8 fallback = i ? 0x42 : 0x53;

		if (!(value & 15))
			value |= fallback & 15;
		if (!(value & 0xf0))
			value |= fallback & 0xf0;
		controls[i] = seek_failed ? 0x53 : value;
	}
}

static inline int rxfilter_mcast_record(struct rxfilter_mcast_cache *cache,
		struct rxfilter_control *control, const u8 *payload,
		unsigned int length)
{
	struct rxfilter_mcast_request *request;
	u32 count;
	unsigned int i, needed, bss;

	if (!payload || length < 8)
		return -EINVAL;
	bss = payload[4];
	if (bss >= 4)
		return -EINVAL;
	count = 0;
	for (i = 0; i < 4; i++)
		count |= (u32)payload[i] << (8 * i);
	if (count != 255 && count > 32)
		return -E2BIG;
	needed = count == 255 ? 8 : 8 + count * 6;
	if (length < needed || length > 200)
		return -EINVAL;
	if ((payload[5] >> 4) >= 8 && (payload[5] >> 4) <= 11 &&
	    (payload[5] >> 4) == (payload[6] >> 4))
		return -EINVAL;
	request = &cache->bss[bss];
	if (request->revision == (u64)~0ULL)
		return -EOVERFLOW;
	request->revision++;
	memcpy(request->payload, payload, needed);
	memset(request->payload + needed, 0, sizeof(request->payload) - needed);
	request->length = needed;
	request->valid = 1;
	rxfilter_external_write(control, bss);
	return 0;
}

static inline int rxfilter_mcast_encode(const struct rxfilter_mcast_cache *cache,
		unsigned int bss, u8 desired, u8 *payload, unsigned int capacity,
		unsigned int *length, u64 *revision)
{
	const struct rxfilter_mcast_request *request;

	if (bss >= 4 || desired > 7 || !payload || !length || !revision)
		return -EINVAL;
	request = &cache->bss[bss];
	if (!request->valid)
		return -ENODATA;
	if (capacity < request->length)
		return -ENOSPC;
	memcpy(payload, request->payload, request->length);
	if (desired & 4) {
		payload[5] = ((desired + 4) << 4) | 3;
		payload[6] = payload[5];
	}
	*length = request->length;
	*revision = request->revision;
	return 0;
}

static inline int rxfilter_mcast_still_current(
		const struct rxfilter_mcast_cache *cache, unsigned int bss,
		u64 revision)
{
	return bss < 4 && cache->bss[bss].valid &&
		cache->bss[bss].revision == revision;
}

#endif
