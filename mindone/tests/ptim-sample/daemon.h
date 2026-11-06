/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stddef.h>

struct iio_channel { int unused; };
struct power_supply { int unused; };
union power_supply_propval { int intval; };
struct mtk_gauge;
struct mtk_battery {
    struct { int car_tune_value; } fg_cust_data;
    struct mtk_gauge *gauge;
    struct { struct power_supply *psy; } bs_data;
    int disableGM30, init_flag, ptim_lk_v, ptim_lk_i;
    unsigned int fgd_pid;
};
struct mtk_gauge {
    struct mtk_battery *gm;
    struct iio_channel *chan_ptim_sample;
    struct power_supply *psy;
    struct mutex ops_lock;
    struct { int r_fg_value; } hw_status;
    u64 ptim_sequence;
};
static struct mtk_battery battery;
static struct mtk_gauge gauge;
static struct iio_channel channel;
static struct power_supply supply;
static int scale_calls, raw_calls, time_calls, injected_raw_status;
static u64 first_time, last_time;

static int iio_read_channel_attribute(struct iio_channel *chan, int *first, int *second, int attr)
{
    CHECK(chan == &channel && gauge.ops_lock.held);
    if (attr == IIO_CHAN_INFO_SCALE)
        scale_calls++;
    else {
        CHECK(attr == IIO_CHAN_INFO_RAW);
        raw_calls++;
        if (injected_raw_status) {
            *first = 25000;
            *second = 65400;
            return injected_raw_status;
        }
    }
    return mt635x_auxadc_read_raw(&iio, &pair_channel, first, second, attr);
}

static u64 ktime_get_boottime_ns(void)
{
    CHECK(gauge.ops_lock.held);
    return time_calls++ % 2 ? last_time : first_time;
}

typedef unsigned int u32;
typedef unsigned int kuid_t;
struct nlmsghdr { u32 nlmsg_len; uint16_t nlmsg_type, nlmsg_flags; u32 nlmsg_seq, nlmsg_pid; };
struct credentials { u32 pid; kuid_t uid; };
struct sk_buff { unsigned int len; void *data; struct credentials creds; u32 portid; };
#define NLMSG_HDRLEN 16U
#define NLMSG_ALIGN(n) (((n) + 3U) & ~3U)
#define NLMSG_DATA(h) ((char *)(h) + NLMSG_HDRLEN)
#define NETLINK_CREDS(skb) (&(skb)->creds)
#define NETLINK_CB(skb) (*(skb))
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define POWER_SUPPLY_PROP_CURRENT_NOW 0
#define GAUGE_PROP_PTIM_BATTERY_VOLTAGE 0
#define GAUGE_PROP_PTIM_RESIST 1
static int allocations, frees, sent, last_seq, ready_calls, legacy_reads;
static long last_port;
static bool forbid_allocation;
static struct fgd_nl_msg_t captured;
static size_t allocated_size;
static struct mtk_battery *get_mtk_battery(void) { return &battery; }
static int in_interrupt(void) { return 0; }
static void *kmalloc(size_t size, int flags)
{
    CHECK(!forbid_allocation);
    allocations++;
    allocated_size = size;
    CHECK(size <= sizeof(struct fgd_nl_msg_t));
    void *result = malloc(size);
    CHECK(result != NULL);
    return result;
}
static void *vmalloc(size_t size) { return kmalloc(size, 0); }
static void kvfree(void *pointer) { frees++; free(pointer); }
static void mtk_battery_send_to_user(struct mtk_battery *gm, int seq, struct fgd_nl_msg_t *reply)
{
    CHECK(gm == &battery);
    CHECK(FGD_NL_HDR_LEN + reply->fgd_data_len <= allocated_size);
    memset(&captured, 0, sizeof(captured));
    memcpy(&captured, reply, FGD_NL_HDR_LEN + reply->fgd_data_len);
    sent++;
    last_seq = seq;
    last_port = -1;
}
static void mtk_battery_send_to_port(struct mtk_battery *gm, u32 portid, int seq, struct fgd_nl_msg_t *reply)
{
    mtk_battery_send_to_user(gm, seq, reply);
    last_port = portid;
}
static void mtk_battery_daemon_ready(struct mtk_battery *gm) { ready_calls++; }
static int power_supply_get_property(struct power_supply *psy, int property, union power_supply_propval *value)
{
    CHECK(psy == &supply);
    legacy_reads++;
    value->intval = -1234;
    return 0;
}
static int gauge_get_int_property(int property)
{
    legacy_reads++;
    return property == GAUGE_PROP_PTIM_RESIST ? 87 : 4123;
}
