/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define __KAL_INLINE__ inline
#define MAC_ADDR_LEN 6
#define ELEM_MAX_LEN_SSID 32
#define U32_MAX UINT32_MAX
#define VIR_MEM_TYPE 0
#define ASSERT(x) assert(x)
#define ENTRY_OF(ptr, type, member) ((type *)((uint8_t *)(ptr) - offsetof(type, member)))
#define kalMemZero(ptr, size) memset(ptr, 0, size)
#define kalMemCopy memcpy
#define kalMemCmp memcmp
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK_FAIL: %s at %d case=%u\n", #x, __LINE__, checked_cases); exit(2); } } while (0)
typedef uint8_t u_int8_t;
typedef uint32_t OS_SYSTIME;
static unsigned checked_cases, active_allocations, allocation_calls, failed_at;
static bool forbid_allocations;
static OS_SYSTIME fake_time;
struct allocation { void *pointer; size_t size; };
static struct allocation allocated[128];
static void *kalMemAlloc(size_t size, unsigned kind)
{
    (void)kind;
    CHECK(!forbid_allocations);
    allocation_calls++;
    if (failed_at && allocation_calls == failed_at)
        return NULL;
    void *pointer = malloc(size);
    CHECK(pointer != NULL);
    memset(pointer, 0xa5, size);
    for (unsigned i = 0; i < 128; i++) {
        if (!allocated[i].pointer) {
            allocated[i] = (struct allocation){pointer, size};
            active_allocations++;
            return pointer;
        }
    }
    CHECK(false);
    return NULL;
}
static void kalMemFree(void *pointer, unsigned kind, size_t size)
{
    (void)kind;
    for (unsigned i = 0; i < 128; i++) {
        if (allocated[i].pointer == pointer) {
            CHECK(allocated[i].size == size && active_allocations);
            allocated[i].pointer = NULL;
            active_allocations--;
            free(pointer);
            return;
        }
    }
    CHECK(false);
}
#define GET_CURRENT_SYSTIME(ptr) (*(ptr) = ++fake_time)
#include "driver-link.h"
#include "driver-mac.inc"
#include "driver-item.inc"
struct BSS_DESC {
    uint8_t aucBSSID[MAC_ADDR_LEN];
    uint8_t ucSSIDLen;
    uint8_t aucSSID[ELEM_MAX_LEN_SSID];
    struct AIS_BLACKLIST_ITEM *prBlack;
};
struct ADAPTER {
    struct {
        struct LINK_MGMT rBlackList;
        u_int8_t fgAisBlacklistInitialized;
    } rWifiVar;
};
static struct BSS_DESC scan_bss[16];
static struct BSS_DESC *scanSearchBssDescByBssid(struct ADAPTER *adapter, uint8_t *address)
{
    CHECK(adapter && adapter->rWifiVar.fgAisBlacklistInitialized);
    for (unsigned i = 0; i < 16; i++)
        if (EQUAL_MAC_ADDR(address, scan_bss[i].aucBSSID))
            return &scan_bss[i];
    return NULL;
}
