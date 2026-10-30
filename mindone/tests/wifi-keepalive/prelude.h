/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define IN
#define OUT
#define TRUE 1
#define FALSE 0
#define VIR_MEM_TYPE 0
#define PARAM_MAC_ADDR_LEN 6
#define MAC_ADDR_LEN 6
#define ASSERT(x) ((void)0)
#define DBGLOG(...) ((void)0)
#define DEBUGFUNC(...) ((void)0)
#define kalMemZero memset_zero
#define kalMemCopy checked_copy
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_BUFFER_TOO_SHORT UINT32_C(0xc0010016)
#define WLAN_STATUS_INVALID_DATA UINT32_C(0xc0010015)
#define CMD_ID_WFC_KEEP_ALIVE 0xa0
#define nicCmdEventSetCommon NULL
#define nicOidCmdTimeoutCommon NULL
#define NLA_HDRLEN 4
#define NLA_F_NESTED 0x8000
#define NLA_ALIGN(n) (((n) + 3) & ~3)
#define NLA_U8 1
#define NLA_U16 2
#define NLA_U32 3
#define NLA_BINARY 4
#define NLA_POLICY_EXACT_LEN_WARN(n) {.type=NLA_BINARY, .min=(n), .len=(n)}
struct nlattr { uint16_t nla_len, nla_type; };
struct nla_policy { unsigned type, len, min; };
struct ADAPTER { int unused; };
struct GLUE_INFO { unsigned u4ReadyFlag; };
struct wiphy { struct GLUE_INFO *glue; };
struct wireless_dev { int unused; };
#define WIPHY_PRIV(wiphy, output) ((output)=(wiphy)->glue)
static unsigned live_allocations, allocation_calls, ioctl_calls, sends;
static unsigned fail_allocation;
static uint32_t send_status, ioctl_status;
static struct ADAPTER adapter;
static struct GLUE_INFO glue;
static struct { void *data; size_t size; } allocations[16];
static const struct nlattr *request_attrs[16];
static unsigned request_attr_count;
static size_t packet_size, packet_data_offset, packet_data_size;
static void boundary_reject(const char *reason)
{
    fprintf(stderr, "BOUNDARY_REJECT: %s\n", reason);
    exit(42);
}
static void *checked_copy(void *dst, const void *src, size_t length)
{
    for (unsigned i = 0; i < 16; i++) {
        uintptr_t begin = (uintptr_t)allocations[i].data;
        uintptr_t target = (uintptr_t)dst;
        if (begin && allocations[i].size == packet_size &&
            target == begin + packet_data_offset && length > packet_data_size)
            boundary_reject("copy exceeds packet array");
        if (begin && target >= begin && target <= begin + allocations[i].size &&
            length > begin + allocations[i].size - target)
            boundary_reject("copy exceeds destination allocation");
    }
    for (unsigned i = 0; i < request_attr_count; i++) {
        const struct nlattr *a = request_attrs[i];
        if (src == (const char *)a + NLA_HDRLEN && length > a->nla_len - NLA_HDRLEN)
            boundary_reject("copy exceeds attribute payload");
    }
    return memcpy(dst, src, length);
}
static void memset_zero(void *p, size_t n) { memset(p, 0, n); }
static void *kalMemAlloc(size_t size, int type)
{
    (void)type;
    allocation_calls++;
    if (allocation_calls == fail_allocation)
        return NULL;
    void *p = malloc(size);
    assert(p);
    live_allocations++;
    for (unsigned i = 0; i < 16; i++) {
        if (!allocations[i].data) {
            allocations[i].data = p;
            allocations[i].size = size;
            return p;
        }
    }
    abort();
}
static void kalMemFree(void *p, int type, size_t size)
{
    (void)type;
    (void)size;
    assert(p && live_allocations);
    for (unsigned i = 0; i < 16; i++) {
        if (allocations[i].data == p)
            allocations[i].data = NULL;
    }
    live_allocations--;
    free(p);
}
static int nla_len(const struct nlattr *a)
{
    if (!a) boundary_reject("missing attribute length access");
    return a->nla_len - NLA_HDRLEN;
}
static void *nla_data(const struct nlattr *a)
{
    if (!a) boundary_reject("missing attribute data access");
    return (char *)a + NLA_HDRLEN;
}
static uint8_t nla_get_u8(const struct nlattr *a) { return *(uint8_t *)nla_data(a); }
static uint16_t nla_get_u16(const struct nlattr *a) { return *(uint16_t *)nla_data(a); }
static uint32_t nla_get_u32(const struct nlattr *a) { return *(uint32_t *)nla_data(a); }
static int parse_attrs(struct nlattr **attrs, int max, const void *data, int len,
                       const struct nla_policy *policy)
{
    memset(attrs, 0, sizeof(*attrs) * (max + 1));
    const uint8_t *cursor = data;
    while (len >= NLA_HDRLEN) {
        const struct nlattr *a = (const void *)cursor;
        if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
            return -EINVAL;
        unsigned type = a->nla_type & 0x3fff;
        if (!type || type > (unsigned)max || (a->nla_type & NLA_F_NESTED))
            return -EINVAL;
        const struct nla_policy *p = policy + type;
        int n = nla_len(a);
        if ((p->type == NLA_U8 && n != 1) ||
            (p->type == NLA_U16 && n != 2) ||
            (p->type == NLA_U32 && n != 4) ||
            (p->type == NLA_BINARY && ((p->len && n > (int)p->len) || n < (int)p->min)))
            return -EINVAL;
        attrs[type] = (struct nlattr *)a;
        int consumed = NLA_ALIGN(a->nla_len);
        cursor += consumed;
        len -= consumed;
    }
    return len > 0 ? -EINVAL : 0;
}
static int parse_nested(struct nlattr **attrs, int max, const struct nlattr *a,
                        const struct nla_policy *policy)
{
    if (!(a->nla_type & NLA_F_NESTED))
        return -EINVAL;
    return parse_attrs(attrs, max, nla_data(a), nla_len(a), policy);
}
#define NLA_PARSE(attrs, max, data, len, policy) parse_attrs(attrs, max, data, len, policy)
#define NLA_PARSE_NESTED(attrs, max, a, policy) parse_nested(attrs, max, a, policy)
