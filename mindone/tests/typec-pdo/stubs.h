/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t __le16;
typedef uint32_t __le32;

#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))
#define FIELD_GET(mask, reg) ((typeof(mask))(((reg) & (mask)) >> __builtin_ctzll(mask)))
#define FIELD_PREP(mask, val) ((typeof(mask))(((typeof(mask))(val) << __builtin_ctzll(mask)) & (mask)))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define le16_to_cpu(x) ((u16)(x))
#define __packed __attribute__((packed))
#define __maybe_unused __attribute__((unused))
#define IS_ENABLED(option) 0

struct request {
	int pos;
	int mv;
	int ma;
	int max_ma;
	int mismatch;
	int usb_comm;
};
