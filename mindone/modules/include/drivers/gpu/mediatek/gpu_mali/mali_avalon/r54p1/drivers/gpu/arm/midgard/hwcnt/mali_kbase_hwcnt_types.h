/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWCNT_TYPES_H_
#define _KBASE_HWCNT_TYPES_H_

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#define KBASE_HWCNT_BITFIELD_BYTES (sizeof(u64))

#define KBASE_HWCNT_BITFIELD_BITS (KBASE_HWCNT_BITFIELD_BYTES * BITS_PER_BYTE)

#define KBASE_HWCNT_VALUE_BYTES (sizeof(u64))

#define KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT 2

#define KBASE_HWCNT_AVAIL_MASK_BITS \
	(sizeof(u64) * KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT * BITS_PER_BYTE)

#define KBASE_HWCNT_BLOCK_BYTE_ALIGNMENT (KBASE_HWCNT_BITFIELD_BITS * KBASE_HWCNT_VALUE_BYTES)

#define KBASE_HWCNT_ALIGN_UPWARDS(value, alignment) \
	(value + ((alignment - (value % alignment)) % alignment))

typedef u8 blk_stt_t;

#define KBASE_HWCNT_BLOCK_STATE_BYTES (sizeof(blk_stt_t))

#define KBASE_HWCNT_BLOCK_STATE_STRIDE (1)

#define KBASE_HWCNT_STATE_UNKNOWN ((blk_stt_t)(0))

#define KBASE_HWCNT_STATE_ON ((blk_stt_t)(1u << 0))

#define KBASE_HWCNT_STATE_OFF ((blk_stt_t)(1u << 1))

#define KBASE_HWCNT_STATE_AVAILABLE ((blk_stt_t)(1u << 2))

#define KBASE_HWCNT_STATE_UNAVAILABLE ((blk_stt_t)(1u << 3))

#define KBASE_HWCNT_STATE_NORMAL ((blk_stt_t)(1u << 4))

#define KBASE_HWCNT_STATE_PROTECTED ((blk_stt_t)(1u << 5))

#define KBASE_HWCNT_STATE_BITS (6)

#define KBASE_HWCNT_STATE_MASK ((blk_stt_t)((1u << KBASE_HWCNT_STATE_BITS) - 1))

struct kbase_hwcnt_block_description {
	u64 type;
	size_t inst_cnt;
	size_t hdr_cnt;
	size_t ctr_cnt;
};

struct kbase_hwcnt_avail_mask {
	u64 mask[KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT];
};

struct kbase_hwcnt_description {
	size_t blk_cnt;
	const struct kbase_hwcnt_block_description *blks;
	struct kbase_hwcnt_avail_mask avail_mask;
	u8 clk_cnt;
};

struct kbase_hwcnt_block_metadata {
	u64 type;
	size_t inst_cnt;
	size_t hdr_cnt;
	size_t ctr_cnt;
	size_t enable_map_index;
	size_t enable_map_stride;
	size_t dump_buf_index;
	size_t dump_buf_stride;
	size_t avail_mask_index;
	size_t blk_stt_index;
	size_t blk_stt_stride;
};

static inline void kbase_hwcnt_set_avail_mask(struct kbase_hwcnt_avail_mask *avail_mask, u64 u0,
					      u64 u1)
{

	BUILD_BUG_ON(KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT != 2);

	avail_mask->mask[0] = u0;
	avail_mask->mask[1] = u1;
}

static inline bool kbase_hwcnt_avail_masks_equal(const struct kbase_hwcnt_avail_mask *avail_mask0,
						 const struct kbase_hwcnt_avail_mask *avail_mask1)
{
	return (!memcmp(avail_mask0, avail_mask1, sizeof(*avail_mask0)));
}

static inline bool
kbase_hwcnt_avail_masks_equal_values(const struct kbase_hwcnt_avail_mask *avail_mask, u64 u0,
				     u64 u1)
{
	BUILD_BUG_ON(KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT != 2);
	return ((avail_mask->mask[0] == u0) && (avail_mask->mask[1] == u1));
}

static inline void kbase_hwcnt_cp_avail_mask(struct kbase_hwcnt_avail_mask *dst_avail_mask,
					     const struct kbase_hwcnt_avail_mask *src_avail_mask)
{
	memcpy(dst_avail_mask, src_avail_mask, sizeof(*dst_avail_mask));
}

static inline void kbase_hwcnt_set_avail_mask_bits(struct kbase_hwcnt_avail_mask *avail_mask,
						   size_t offset_in_bits, size_t length_in_bits,
						   u64 value)
{
	size_t arr_offset = offset_in_bits / 64;
	size_t bits_set = 0;

	if (!length_in_bits)
		return;

	WARN_ON(length_in_bits > 64);
	if (WARN_ON((offset_in_bits + length_in_bits) > (KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT << 6)))
		return;

	do {
		size_t remaining_to_set = length_in_bits - bits_set;
		size_t start_dest_bit_in_word = (offset_in_bits + bits_set) - (arr_offset * 64);
		size_t bits_that_fit_into_this_word =
			min(64 - start_dest_bit_in_word, remaining_to_set);

		uint64_t dest_mask, mask, source_mask;
		uint64_t source_fragment;

		if (bits_that_fit_into_this_word == 64) {
			mask = U64_MAX;
			source_mask = U64_MAX;
			dest_mask = U64_MAX;
		} else {
			mask = (1ULL << bits_that_fit_into_this_word) - 1;
			source_mask = ((1ULL << (bits_that_fit_into_this_word)) - 1) << bits_set;
			dest_mask = mask << start_dest_bit_in_word;
		}

		source_fragment = (value & source_mask) >> bits_set;

		if (WARN_ON(arr_offset >= KBASE_HWCNT_AVAIL_MASK_ELEM_COUNT))
			break;

		avail_mask->mask[arr_offset] &= ~dest_mask;
		avail_mask->mask[arr_offset] |=
			((source_fragment & mask) << start_dest_bit_in_word);

		arr_offset++;
		bits_set += bits_that_fit_into_this_word;
	} while (bits_set < length_in_bits);
}

struct kbase_hwcnt_metadata {
	size_t blk_cnt;
	const struct kbase_hwcnt_block_metadata *blk_metadata;
	size_t enable_map_bytes;
	size_t dump_buf_bytes;
	size_t blk_stt_bytes;
	struct kbase_hwcnt_avail_mask avail_mask;
	u8 clk_cnt;
};

struct kbase_hwcnt_enable_map {
	const struct kbase_hwcnt_metadata *metadata;
	u64 *hwcnt_enable_map;
	u64 clk_enable_map;
};

struct kbase_hwcnt_dump_buffer {
	const struct kbase_hwcnt_metadata *metadata;
	u64 *dump_buf;
	u64 *clk_cnt_buf;
	blk_stt_t *blk_stt_buf;
};

int kbase_hwcnt_metadata_create(const struct kbase_hwcnt_description *desc,
				const struct kbase_hwcnt_metadata **metadata);

void kbase_hwcnt_metadata_destroy(const struct kbase_hwcnt_metadata *metadata);

static inline void kbase_hwcnt_block_state_set(blk_stt_t *blk_stt, blk_stt_t stt)
{
	if (WARN_ON(stt & ~KBASE_HWCNT_STATE_MASK))
		return;

	*blk_stt = stt;
}

static inline void kbase_hwcnt_block_state_append(blk_stt_t *blk_stt, blk_stt_t stt)
{
	if (WARN_ON(stt & ~KBASE_HWCNT_STATE_MASK))
		return;

	*blk_stt |= stt;
}

static inline void kbase_hwcnt_block_state_copy(blk_stt_t *dst_blk_stt,
						const blk_stt_t *src_blk_stt)
{
	kbase_hwcnt_block_state_set(dst_blk_stt, *src_blk_stt);
}

static inline void kbase_hwcnt_block_state_accumulate(blk_stt_t *dst_blk_stt,
						      const blk_stt_t *src_blk_stt)
{
	kbase_hwcnt_block_state_append(dst_blk_stt, *src_blk_stt);
}

static inline size_t kbase_hwcnt_metadata_block_count(const struct kbase_hwcnt_metadata *metadata)
{
	if (WARN_ON(!metadata))
		return 0;

	return metadata->blk_cnt;
}

static inline u64 kbase_hwcnt_metadata_block_type(const struct kbase_hwcnt_metadata *metadata,
						  size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].type;
}

static inline size_t
kbase_hwcnt_metadata_block_instance_count(const struct kbase_hwcnt_metadata *metadata, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].inst_cnt;
}

static inline size_t
kbase_hwcnt_metadata_block_headers_count(const struct kbase_hwcnt_metadata *metadata, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].hdr_cnt;
}

static inline size_t
kbase_hwcnt_metadata_block_counters_count(const struct kbase_hwcnt_metadata *metadata, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].ctr_cnt;
}

static inline size_t
kbase_hwcnt_metadata_block_enable_map_stride(const struct kbase_hwcnt_metadata *metadata,
					     size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].enable_map_stride;
}

static inline size_t
kbase_hwcnt_metadata_block_values_count(const struct kbase_hwcnt_metadata *metadata, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return kbase_hwcnt_metadata_block_counters_count(metadata, blk) +
	       kbase_hwcnt_metadata_block_headers_count(metadata, blk);
}

#define kbase_hwcnt_metadata_for_each_block(md, blk, blk_inst)                            \
	for ((blk) = 0; (blk) < kbase_hwcnt_metadata_block_count((md)); (blk)++)          \
		for ((blk_inst) = 0;                                                      \
		     (blk_inst) < kbase_hwcnt_metadata_block_instance_count((md), (blk)); \
		     (blk_inst)++)

static inline size_t
kbase_hwcnt_metadata_block_avail_bit(const struct kbase_hwcnt_metadata *metadata, size_t blk)
{
	if (WARN_ON(!metadata) || WARN_ON(blk >= metadata->blk_cnt))
		return 0;

	return metadata->blk_metadata[blk].avail_mask_index;
}

static inline bool
kbase_hwcnt_metadata_block_instance_avail(const struct kbase_hwcnt_metadata *metadata, size_t blk,
					  size_t blk_inst)
{
	size_t bit;
	size_t mask_index;
	u64 mask;

	if (WARN_ON(!metadata))
		return false;

	bit = kbase_hwcnt_metadata_block_avail_bit(metadata, blk) + blk_inst;
	mask_index = bit >> 6;
	mask = 1ull << (bit & 0x3f);

	return (metadata->avail_mask.mask[mask_index] & mask) != 0;
}

int kbase_hwcnt_enable_map_alloc(const struct kbase_hwcnt_metadata *metadata,
				 struct kbase_hwcnt_enable_map *enable_map);

void kbase_hwcnt_enable_map_free(struct kbase_hwcnt_enable_map *enable_map);

static inline u64 *kbase_hwcnt_enable_map_block_instance(const struct kbase_hwcnt_enable_map *map,
							 size_t blk, size_t blk_inst)
{
	if (WARN_ON(!map) || WARN_ON(!map->hwcnt_enable_map))
		return NULL;

	if (WARN_ON(!map->metadata) || WARN_ON(blk >= map->metadata->blk_cnt) ||
	    WARN_ON(blk_inst >= map->metadata->blk_metadata[blk].inst_cnt))
		return map->hwcnt_enable_map;

	return map->hwcnt_enable_map + map->metadata->blk_metadata[blk].enable_map_index +
	       (map->metadata->blk_metadata[blk].enable_map_stride * blk_inst);
}

static inline size_t kbase_hwcnt_bitfield_count(size_t val_cnt)
{
	return (val_cnt + KBASE_HWCNT_BITFIELD_BITS - 1) / KBASE_HWCNT_BITFIELD_BITS;
}

static inline void kbase_hwcnt_enable_map_block_disable_all(struct kbase_hwcnt_enable_map *dst,
							    size_t blk, size_t blk_inst)
{
	size_t val_cnt;
	size_t bitfld_cnt;
	u64 *const block_enable_map = kbase_hwcnt_enable_map_block_instance(dst, blk, blk_inst);

	if (WARN_ON(!dst))
		return;

	val_cnt = kbase_hwcnt_metadata_block_values_count(dst->metadata, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	memset(block_enable_map, 0, bitfld_cnt * KBASE_HWCNT_BITFIELD_BYTES);
}

static inline void kbase_hwcnt_enable_map_disable_all(struct kbase_hwcnt_enable_map *dst)
{
	if (WARN_ON(!dst) || WARN_ON(!dst->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL)
		memset(dst->hwcnt_enable_map, 0, dst->metadata->enable_map_bytes);

	dst->clk_enable_map = 0;
}

static inline void kbase_hwcnt_enable_map_block_enable_all(struct kbase_hwcnt_enable_map *dst,
							   size_t blk, size_t blk_inst)
{
	size_t val_cnt;
	size_t bitfld_cnt;
	u64 *const block_enable_map = kbase_hwcnt_enable_map_block_instance(dst, blk, blk_inst);
	size_t bitfld_idx;

	if (WARN_ON(!dst))
		return;

	val_cnt = kbase_hwcnt_metadata_block_values_count(dst->metadata, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	for (bitfld_idx = 0; bitfld_idx < bitfld_cnt; bitfld_idx++) {
		const u64 remaining_values = val_cnt - (bitfld_idx * KBASE_HWCNT_BITFIELD_BITS);
		u64 block_enable_map_mask = U64_MAX;

		if (remaining_values < KBASE_HWCNT_BITFIELD_BITS)
			block_enable_map_mask = (1ull << remaining_values) - 1;

		block_enable_map[bitfld_idx] = block_enable_map_mask;
	}
}

static inline void kbase_hwcnt_enable_map_enable_all(struct kbase_hwcnt_enable_map *dst)
{
	size_t blk, blk_inst;

	if (WARN_ON(!dst) || WARN_ON(!dst->metadata))
		return;

	kbase_hwcnt_metadata_for_each_block(dst->metadata, blk, blk_inst)
		kbase_hwcnt_enable_map_block_enable_all(dst, blk, blk_inst);

	dst->clk_enable_map = (1ull << dst->metadata->clk_cnt) - 1;
}

static inline void kbase_hwcnt_enable_map_copy(struct kbase_hwcnt_enable_map *dst,
					       const struct kbase_hwcnt_enable_map *src)
{
	if (WARN_ON(!dst) || WARN_ON(!src) || WARN_ON(!dst->metadata) ||
	    WARN_ON(dst->metadata != src->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL) {
		if (WARN_ON(!src->hwcnt_enable_map))
			return;

		memcpy(dst->hwcnt_enable_map, src->hwcnt_enable_map,
		       dst->metadata->enable_map_bytes);
	}

	dst->clk_enable_map = src->clk_enable_map;
}

static inline void kbase_hwcnt_enable_map_union(struct kbase_hwcnt_enable_map *dst,
						const struct kbase_hwcnt_enable_map *src)
{
	if (WARN_ON(!dst) || WARN_ON(!src) || WARN_ON(!dst->metadata) ||
	    WARN_ON(dst->metadata != src->metadata))
		return;

	if (dst->hwcnt_enable_map != NULL) {
		size_t i;
		size_t const bitfld_count =
			dst->metadata->enable_map_bytes / KBASE_HWCNT_BITFIELD_BYTES;

		if (WARN_ON(!src->hwcnt_enable_map))
			return;

		for (i = 0; i < bitfld_count; i++)
			dst->hwcnt_enable_map[i] |= src->hwcnt_enable_map[i];
	}

	dst->clk_enable_map |= src->clk_enable_map;
}

static inline bool
kbase_hwcnt_enable_map_block_enabled(const struct kbase_hwcnt_enable_map *enable_map, size_t blk,
				     size_t blk_inst)
{
	bool any_enabled = false;
	size_t val_cnt;
	size_t bitfld_cnt;
	const u64 *const block_enable_map =
		kbase_hwcnt_enable_map_block_instance(enable_map, blk, blk_inst);
	size_t bitfld_idx;

	if (WARN_ON(!enable_map))
		return false;

	val_cnt = kbase_hwcnt_metadata_block_values_count(enable_map->metadata, blk);
	bitfld_cnt = kbase_hwcnt_bitfield_count(val_cnt);

	for (bitfld_idx = 0; bitfld_idx < bitfld_cnt; bitfld_idx++) {
		const u64 remaining_values = val_cnt - (bitfld_idx * KBASE_HWCNT_BITFIELD_BITS);
		u64 block_enable_map_mask = U64_MAX;

		if (remaining_values < KBASE_HWCNT_BITFIELD_BITS)
			block_enable_map_mask = (1ull << remaining_values) - 1;

		any_enabled = any_enabled || (block_enable_map[bitfld_idx] & block_enable_map_mask);
	}

	return any_enabled;
}

static inline bool
kbase_hwcnt_enable_map_any_enabled(const struct kbase_hwcnt_enable_map *enable_map)
{
	size_t blk, blk_inst;
	u64 clk_enable_map_mask;

	if (WARN_ON(!enable_map) || WARN_ON(!enable_map->metadata))
		return false;

	clk_enable_map_mask = (1ull << enable_map->metadata->clk_cnt) - 1;

	if (enable_map->metadata->clk_cnt > 0 && (enable_map->clk_enable_map & clk_enable_map_mask))
		return true;

	kbase_hwcnt_metadata_for_each_block(enable_map->metadata, blk, blk_inst) {
		if (kbase_hwcnt_enable_map_block_enabled(enable_map, blk, blk_inst))
			return true;
	}

	return false;
}

static inline bool kbase_hwcnt_enable_map_block_value_enabled(const u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	return (bitfld[idx] & mask) != 0;
}

static inline void kbase_hwcnt_enable_map_block_enable_value(u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	bitfld[idx] |= mask;
}

static inline void kbase_hwcnt_enable_map_block_disable_value(u64 *bitfld, size_t val_idx)
{
	const size_t idx = val_idx / KBASE_HWCNT_BITFIELD_BITS;
	const size_t bit = val_idx % KBASE_HWCNT_BITFIELD_BITS;
	const u64 mask = 1ull << bit;

	bitfld[idx] &= ~mask;
}

int kbase_hwcnt_dump_buffer_alloc(const struct kbase_hwcnt_metadata *metadata,
				  struct kbase_hwcnt_dump_buffer *dump_buf);

void kbase_hwcnt_dump_buffer_free(struct kbase_hwcnt_dump_buffer *dump_buf);

static inline u64 *kbase_hwcnt_dump_buffer_block_instance(const struct kbase_hwcnt_dump_buffer *buf,
							  size_t blk, size_t blk_inst)
{
	if (WARN_ON(!buf) || WARN_ON(!buf->dump_buf))
		return NULL;

	if (WARN_ON(!buf->metadata) || WARN_ON(blk >= buf->metadata->blk_cnt) ||
	    WARN_ON(blk_inst >= buf->metadata->blk_metadata[blk].inst_cnt))
		return buf->dump_buf;

	return buf->dump_buf + buf->metadata->blk_metadata[blk].dump_buf_index +
	       (buf->metadata->blk_metadata[blk].dump_buf_stride * blk_inst);
}

static inline blk_stt_t *
kbase_hwcnt_dump_buffer_block_state_instance(const struct kbase_hwcnt_dump_buffer *buf, size_t blk,
					     size_t blk_inst)
{
	if (WARN_ON(!buf) || WARN_ON(!buf->dump_buf))
		return NULL;

	if (WARN_ON(!buf->metadata) || WARN_ON(blk >= buf->metadata->blk_cnt) ||
	    WARN_ON(blk_inst >= buf->metadata->blk_metadata[blk].inst_cnt))
		return buf->blk_stt_buf;

	return buf->blk_stt_buf + buf->metadata->blk_metadata[blk].blk_stt_index +
	       (buf->metadata->blk_metadata[blk].blk_stt_stride * blk_inst);
}

void kbase_hwcnt_dump_buffer_zero(struct kbase_hwcnt_dump_buffer *dst,
				  const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_zero(u64 *dst_blk, size_t val_cnt)
{
	if (WARN_ON(!dst_blk))
		return;

	memset(dst_blk, 0, (val_cnt * KBASE_HWCNT_VALUE_BYTES));
}

void kbase_hwcnt_dump_buffer_zero_strict(struct kbase_hwcnt_dump_buffer *dst);

void kbase_hwcnt_dump_buffer_zero_non_enabled(struct kbase_hwcnt_dump_buffer *dst,
					      const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_zero_non_enabled(u64 *dst_blk, const u64 *blk_em,
								  size_t val_cnt)
{
	size_t val;

	if (WARN_ON(!dst_blk))
		return;

	for (val = 0; val < val_cnt; val++) {
		if (!kbase_hwcnt_enable_map_block_value_enabled(blk_em, val))
			dst_blk[val] = 0;
	}
}

void kbase_hwcnt_dump_buffer_copy(struct kbase_hwcnt_dump_buffer *dst,
				  const struct kbase_hwcnt_dump_buffer *src,
				  const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_copy(u64 *dst_blk, const u64 *src_blk,
						      size_t val_cnt)
{
	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	memcpy(dst_blk, src_blk, (val_cnt * KBASE_HWCNT_VALUE_BYTES));
}

void kbase_hwcnt_dump_buffer_copy_strict(struct kbase_hwcnt_dump_buffer *dst,
					 const struct kbase_hwcnt_dump_buffer *src,
					 const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_copy_strict(u64 *dst_blk, const u64 *src_blk,
							     const u64 *blk_em, size_t val_cnt)
{
	size_t val;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	for (val = 0; val < val_cnt; val++) {
		bool val_enabled = kbase_hwcnt_enable_map_block_value_enabled(blk_em, val);

		dst_blk[val] = val_enabled ? src_blk[val] : 0;
	}
}

void kbase_hwcnt_dump_buffer_accumulate(struct kbase_hwcnt_dump_buffer *dst,
					const struct kbase_hwcnt_dump_buffer *src,
					const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_accumulate(u64 *dst_blk, const u64 *src_blk,
							    size_t hdr_cnt, size_t ctr_cnt)
{
	size_t ctr;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	memcpy(dst_blk, src_blk, hdr_cnt * KBASE_HWCNT_VALUE_BYTES);

	for (ctr = hdr_cnt; ctr < ctr_cnt + hdr_cnt; ctr++)
		dst_blk[ctr] += src_blk[ctr];
}

void kbase_hwcnt_dump_buffer_accumulate_strict(struct kbase_hwcnt_dump_buffer *dst,
					       const struct kbase_hwcnt_dump_buffer *src,
					       const struct kbase_hwcnt_enable_map *dst_enable_map);

static inline void kbase_hwcnt_dump_buffer_block_accumulate_strict(u64 *dst_blk, const u64 *src_blk,
								   const u64 *blk_em,
								   size_t hdr_cnt, size_t ctr_cnt)
{
	size_t ctr;

	if (WARN_ON(!dst_blk) || WARN_ON(!src_blk))
		return;

	kbase_hwcnt_dump_buffer_block_copy_strict(dst_blk, src_blk, blk_em, hdr_cnt);

	for (ctr = hdr_cnt; ctr < ctr_cnt + hdr_cnt; ctr++) {
		bool ctr_enabled = kbase_hwcnt_enable_map_block_value_enabled(blk_em, ctr);

		if (ctr_enabled)
			dst_blk[ctr] += src_blk[ctr];
		else
			dst_blk[ctr] = 0;
	}
}

#define kbase_hwcnt_metadata_for_each_clock(md, clk) for ((clk) = 0; (clk) < (md)->clk_cnt; (clk)++)

static inline bool kbase_hwcnt_clk_enable_map_enabled(const u64 clk_enable_map, const size_t index)
{
	if (WARN_ON(index >= 64))
		return false;
	if (clk_enable_map & (1ull << index))
		return true;
	return false;
}

void kbase_hwcnt_dump_buffer_block_state_update(struct kbase_hwcnt_dump_buffer *dst,
						const struct kbase_hwcnt_enable_map *dst_enable_map,
						blk_stt_t blk_stt_val);

#endif
