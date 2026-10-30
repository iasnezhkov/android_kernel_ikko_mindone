/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u_int8_t;
typedef uint16_t u16;
#define TRUE 1
#define FALSE 0
#define WLAN_STATUS_SUCCESS 0
#define MAC_ADDR_LEN 6
#define NLA_HDRLEN 4
#define NLA_TYPE_MASK 0x3fff
#define NLA_ALIGN(n) (((n) + 3) & ~3)
#define NLA_U32 1
#define NLA_BINARY 2
#define NLA_STRING 3
#define NLA_POLICY_EXACT_LEN_WARN(n) {.type=NLA_BINARY, .min=(n), .max=(n)}
#define NLA_POLICY_MIN_LEN(n) {.type=NLA_BINARY, .min=(n)}
#define DBGLOG(...) ((void)0)
#define ASSERT(x) ((void)0)
#define WIPHY_PRIV(wiphy, output) ((output) = (wiphy)->glue)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK_FAIL: %s at %d\n", #x, __LINE__); exit(2); } } while (0)
struct nlattr { uint16_t nla_len, nla_type; };
struct nla_policy { unsigned type, min, max; };
struct ADAPTER { int unused; };
struct GLUE_INFO { struct ADAPTER *prAdapter; unsigned u4ReadyFlag, u4FWRoamingEnable; };
struct wiphy { struct GLUE_INFO *glue; };
struct wireless_dev { int unused; };
struct BSS_DESC { uint8_t aucSSID[32], aucBSSID[6]; unsigned id; };
struct AIS_BLACKLIST_ITEM { u_int8_t fgIsInFWKBlacklist; };
static const uint8_t *input_start;
static size_t input_length;
static unsigned allow_mutation, refreshes, scans, adds, collected, fail_add, missing_scan;
static unsigned bssid_type, bssid_limit;
static int enabled_error;
static unsigned order[32], cases;
static struct BSS_DESC descriptors[32];
static struct AIS_BLACKLIST_ITEM items[32];
static struct ADAPTER adapter;
static struct GLUE_INFO glue = { &adapter, 1, 1 };
static void reject_boundary(const char *reason)
{
    fprintf(stderr, "BOUNDARY_REJECT: %s\n", reason);
    exit(42);
}
static void readable(const void *data, size_t length)
{
    uintptr_t start = (uintptr_t)input_start, pointer = (uintptr_t)data;
    if (!data || pointer < start || pointer - start > input_length ||
        length > input_length - (pointer - start))
        reject_boundary("input access exceeds admitted buffer");
}
static void *nla_data(const struct nlattr *attr)
{
    readable(attr, NLA_HDRLEN);
    if (attr->nla_len < NLA_HDRLEN)
        reject_boundary("attribute smaller than header");
    readable(attr, attr->nla_len);
    if ((attr->nla_type & NLA_TYPE_MASK) == bssid_type) {
        if (attr->nla_len != NLA_HDRLEN + MAC_ADDR_LEN || ++collected > bssid_limit)
            reject_boundary("BSSID admission exceeds six bytes or sixteen records");
    }
    return (char *)attr + NLA_HDRLEN;
}
static uint32_t nla_get_u32(const struct nlattr *attr)
{
    readable(attr, NLA_HDRLEN);
    if (attr->nla_len != NLA_HDRLEN + 4)
        reject_boundary("scalar attribute is not four bytes");
    readable(attr, attr->nla_len);
    uint32_t value;
    memcpy(&value, (const char *)attr + NLA_HDRLEN, sizeof(value));
    return value;
}
static int nla_validate(const struct nlattr *head, int length, int max,
                        const struct nla_policy *policy, void *extack)
{
    (void)extack;
    if (length < 0)
        return -EINVAL;
    const uint8_t *cursor = (const void *)head;
    while (length >= NLA_HDRLEN) {
        const struct nlattr *attr = (const void *)cursor;
        readable(attr, NLA_HDRLEN);
        if (attr->nla_len < NLA_HDRLEN || attr->nla_len > length)
            return -EINVAL;
        readable(attr, attr->nla_len);
        unsigned type = attr->nla_type & NLA_TYPE_MASK;
        if (!type || type > (unsigned)max || (attr->nla_type & 0x8000))
            return -EINVAL;
        const struct nla_policy *p = policy + type;
        unsigned len = attr->nla_len - NLA_HDRLEN;
        if (!p->type || (p->type == NLA_U32 && len != 4) ||
            (p->type == NLA_STRING && !len) ||
            (p->type == NLA_BINARY && (len < p->min || (p->max && len > p->max))))
            return -EINVAL;
        unsigned consumed = NLA_ALIGN(attr->nla_len);
        cursor += consumed;
        length -= consumed;
    }
    return length > 0 ? -EINVAL : 0;
}
static void aisRefreshFWKBlacklist(struct ADAPTER *a)
{
    CHECK(a == &adapter);
    if (!allow_mutation)
        reject_boundary("malformed input reached blacklist refresh");
    refreshes++;
    for (unsigned i = 0; i < 32; i++) items[i].fgIsInFWKBlacklist = 0;
}
static struct BSS_DESC *scanSearchBssDescByBssid(struct ADAPTER *a, uint8_t *mac)
{
    CHECK(a == &adapter);
    readable(mac, 6);
    if (!allow_mutation)
        reject_boundary("malformed input reached scan lookup");
    CHECK(scans < 32);
    unsigned id = mac[5];
    CHECK(id < 32);
    order[scans++] = id;
    return missing_scan == id + 1 ? NULL : &descriptors[id];
}
static struct AIS_BLACKLIST_ITEM *aisAddBlacklist(struct ADAPTER *a, struct BSS_DESC *bss)
{
    CHECK(a == &adapter && bss);
    if (!allow_mutation)
        reject_boundary("malformed input reached blacklist insertion");
    adds++;
    if (adds == fail_add)
        return NULL;
    return &items[bss->id];
}
static int kalFwkBlacklistSubmit(struct GLUE_INFO *info, uint32_t count,
    const uint8_t bssid[][MAC_ADDR_LEN])
{
    CHECK(info == &glue && count <= bssid_limit);
    if (!allow_mutation)
        reject_boundary("malformed input reached owned submission");
    if (fail_add)
        return -ENOMEM;
    unsigned selected[32], selected_count = 0;
    for (unsigned i = 0; i < count; i++) {
        CHECK(bssid[i][0] == 2 && bssid[i][1] == 4 && bssid[i][2] == 6 && bssid[i][3] == 8 && bssid[i][4] == 10);
        unsigned id = bssid[i][5];
        CHECK(id < 32 && scans < 32);
        order[scans++] = id;
        if (missing_scan == id + 1)
            continue;
        adds++;
        selected[selected_count++] = id;
    }
    aisRefreshFWKBlacklist(info->prAdapter);
    for (unsigned i = 0; i < selected_count; i++)
        items[selected[i]].fgIsInFWKBlacklist = TRUE;
    return 0;
}
#define kalMemCopy memcpy

static int kalFwkBlacklistGetEnabled(struct GLUE_INFO *info, bool *enabled)
{
    CHECK(info == &glue && enabled);
    if (enabled_error) return enabled_error;
    *enabled = info->u4FWRoamingEnable != 0;
    return 0;
}
