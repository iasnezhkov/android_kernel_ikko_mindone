/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef ETA_TEST_KERNEL_H
#define ETA_TEST_KERNEL_H
#include <linux/types.h>
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define U8_MAX 255
#define min(a, b) ((a) < (b) ? (a) : (b))
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#endif
