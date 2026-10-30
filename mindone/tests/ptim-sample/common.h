/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t s64;
typedef uint64_t u64;
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define U64_MAX UINT64_MAX
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define bm_debug(...) ((void)0)
#define bm_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define do_div(a, b) ((a) /= (b))
#ifndef EPROBE_DEFER
#define EPROBE_DEFER 517
#endif

static unsigned int checks;
static unsigned int cases;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
struct mutex { bool held; };
static void mutex_lock(struct mutex *lock) { CHECK(!lock->held); lock->held = true; }
static void mutex_unlock(struct mutex *lock) { CHECK(lock->held); lock->held = false; }
#define lockdep_assert_held(lock) CHECK((lock)->held)
static s64 div_s64(s64 value, int denominator) { CHECK(denominator != 0); return value / denominator; }
static void finish(void) { printf("PASS cases=%u assertions=%u\n", cases, checks); }
