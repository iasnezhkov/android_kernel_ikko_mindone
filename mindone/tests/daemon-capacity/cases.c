#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define smp_load_acquire(x) (*(x))
#define UNIT_TRANS_10 10
#define MAX_TABLE 6
#define DATA_SIZE 2048
#define FGD_NL_MSG_MAX_LEN 9200
#define FGD_NL_HDR_LEN 32
#define bm_err(...) ((void)0)
#define bm_debug(...) ((void)0)
#define fg_cmd_check(...) ((void)0)
#define clamp(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))

enum { FG_DAEMON_CMD_SET_BATTERY_CAPACITY = 1, FG_DAEMON_CMD_SET_DAEMON_PID,
       POWER_SUPPLY_PROP_CHARGE_FULL, POWER_SUPPLY_STATUS_FULL,
       POWER_SUPPLY_STATUS_CHARGING, POWER_SUPPLY_STATUS_NOT_CHARGING,
       GAUGE_PROP_BATTERY_CURRENT };
struct fuel_gauge_table { int temperature; int q_max; };
struct fuel_gauge_table_custom_data {
    int active_table_number;
    struct fuel_gauge_table fg_profile[MAX_TABLE];
};
struct battery_data { int bat_batt_temp; int bat_capacity; int bat_status; };
struct mtk_battery {
    struct fuel_gauge_table_custom_data fg_table_cust_data;
    struct battery_data bs_data;
    struct { int aging_factor; int quse_tb1; } algo;
    struct { int debug_kill_daemontest; } Bat_EC_ctrl;
    struct { int dod_init_sel; } fg_cust_data;
    int aging_factor;
    int daemon_quse;
    unsigned int fgd_pid;
    int init_flag;
    bool active;
    bool cc_soc_ready;
    int ui_soc;
    void *gauge;
};
union power_supply_propval { int intval; };
static int gauge_current = 1000;
static int counter_value, counter_ceiling;
static bool is_algo_active(struct mtk_battery *gm) { return gm->active; }
static int gauge_get_property(int p, int *value) { *value = gauge_current; return 0; }
static int gauge_get_charge_counter(void *g, long long seed, int ceiling) {
    counter_ceiling = ceiling;
    return counter_value < ceiling ? counter_value : ceiling;
}
@TYPES@
@FUNCTIONS@
static void dispatch(struct mtk_battery *gm, struct fgd_nl_msg_t *msg) {
    switch (msg->fgd_cmd) {
@CASES@
    default: break;
    }
}
static int full(struct mtk_battery *gm) {
    union power_supply_propval value;
    union power_supply_propval *val = &value;
    switch (POWER_SUPPLY_PROP_CHARGE_FULL) {
@REPORT@
    default: abort();
    }
    return value.intval;
}
typedef uint32_t u32;
typedef int kuid_t;
struct nlmsghdr { uint32_t nlmsg_len; uint16_t nlmsg_type, nlmsg_flags; uint32_t nlmsg_seq, nlmsg_pid; };
struct sk_buff { void *data; unsigned int len; int pid, uid; };
#define NETLINK_CREDS(skb) (skb)
#define NLMSG_HDRLEN 16
#define NLMSG_ALIGN(len) (((len) + 3U) & ~3U)
#define NLMSG_DATA(nlh) ((void *)((char *)(nlh) + sizeof(*(nlh))))
bool nlmsg_ok(const struct nlmsghdr *nlh, unsigned int length) {
    return length >= sizeof(*nlh) && nlh->nlmsg_len >= sizeof(*nlh) && nlh->nlmsg_len <= length;
}
unsigned int nlmsg_len(const struct nlmsghdr *nlh) { return nlh->nlmsg_len - sizeof(*nlh); }
static struct mtk_battery wire_battery;
static struct mtk_battery *get_mtk_battery(void) { return &wire_battery; }
static int accepted;
@WIRE@
static int checks, failures;
static void expect(bool condition, const char *name) {
    checks++;
    if (!condition) { failures++; printf("FAIL %s\n", name); }
}
static struct mtk_battery setup(void) {
    struct mtk_battery gm = {0};
    gm.aging_factor = gm.algo.aging_factor = 10000;
    gm.fgd_pid = 962;
    gm.init_flag = 1;
    gm.ui_soc = gm.bs_data.bat_capacity = 50;
    gm.bs_data.bat_status = POWER_SUPPLY_STATUS_CHARGING;
    gm.fg_table_cust_data.active_table_number = 2;
    gm.fg_table_cust_data.fg_profile[0] = (struct fuel_gauge_table){25, 2000};
    gm.fg_table_cust_data.fg_profile[1] = (struct fuel_gauge_table){0, 1800};
    gm.bs_data.bat_batt_temp = 25;
    return gm;
}
static struct fgd_nl_msg_t packet(int quse) {
    struct fgd_nl_msg_t msg = {0};
    struct fgd_cmd_param_t_8 data = {0};
    struct fgd_cmd_param_t_4 *part = (void *)msg.fgd_data;
    data.data[4] = quse;
    data.data[6] = 10000;
    msg.fgd_cmd = FG_DAEMON_CMD_SET_BATTERY_CAPACITY;
    msg.fgd_data_len = sizeof(*part);
    part->total_size = 2052;
    part->size = DATA_SIZE;
    memcpy(msg.fgd_data + offsetof(struct fgd_cmd_param_t_4, input), &data, sizeof(data));
    return msg;
}
int main(void) {
    struct mtk_battery gm = setup();
    struct fgd_nl_msg_t msg = packet(20002);
    expect(full(&gm) == 2000000, "initial table fallback");
    dispatch(&gm, &msg);
    expect(full(&gm) == 2000200, "daemon usable capacity exact uAh");
    expect(mtk_battery_get_learned_q_max(&gm) == 2000, "algorithm model unchanged");
    gm.aging_factor = 7500;
    gm.bs_data.bat_batt_temp = 0;
    expect(full(&gm) == 2000200, "usable capacity not aged or interpolated twice");
    expect(mtk_battery_get_learned_q_max(&gm) == 1350, "algorithm table policy unchanged");
    gm.aging_factor = 10000;
    gm.bs_data.bat_batt_temp = 25;
    expect(mtk_battery_get_charge_now_uah(&gm) == 1000100, "counter initial seed uses same model");
    gm.cc_soc_ready = true;
    counter_value = 1800123;
    expect(mtk_battery_get_charge_now_uah(&gm) == 1800123, "continuous counter value unchanged");
    expect(counter_ceiling == 2000200, "counter ceiling matches full");
    gm.active = true;
    gm.algo.quse_tb1 = 17000;
    expect(full(&gm) == 1700000, "kernel algorithm precedence");
    gm.active = false;
    int seconds = -1;
    expect(mtk_battery_time_to_full(&gm, &seconds) == 0 && seconds == 36000, "TTF uses selected usable model");
    gm.bs_data.bat_status = POWER_SUPPLY_STATUS_FULL;
    expect(mtk_battery_time_to_full(&gm, &seconds) == 0 && seconds == 0, "hardware full retains zero TTF");
    for (int value_index = 0; value_index < 6; value_index++) {
        int values[] = {-1, 0, 9, INT_MAX / 100 + 1, INT_MAX, INT_MIN};
        msg = packet(values[value_index]);
        dispatch(&gm, &msg);
        expect(full(&gm) == 2000200, "invalid capacity preserves last accepted value");
    }
    for (unsigned int length = 0; length < 56; length++) {
        msg = packet(18000);
        msg.fgd_data_len = length;
        dispatch(&gm, &msg);
        expect(full(&gm) == 2000200, "short payload cannot update model");
    }
    msg = packet(18000);
    msg.fgd_data_len = 56;
    ((struct fgd_cmd_param_t_4 *)msg.fgd_data)->size = 44;
    dispatch(&gm, &msg);
    expect(full(&gm) == 1800000, "minimum complete logged data accepted");
    for (int fault = 0; fault < 6; fault++) {
        msg = packet(21000);
        struct fgd_cmd_param_t_4 *part = (void *)msg.fgd_data;
        switch (fault) {
        case 0: part->idx = 2048; part->size = 4; break;
        case 1: part->idx = 1; break;
        case 2: part->size = 43; break;
        case 3: part->size = DATA_SIZE + 1; break;
        case 4: part->total_size = part->size - 1; break;
        case 5: msg.fgd_data_len--; break;
        }
        dispatch(&gm, &msg);
        expect(full(&gm) == 1800000, "invalid or trailing fragment cannot alter capacity");
    }
    msg = packet(20002);
    dispatch(&gm, &msg);
    msg.fgd_cmd = FG_DAEMON_CMD_SET_DAEMON_PID;
    unsigned int pid = 1000;
    memcpy(msg.fgd_data, &pid, sizeof(pid));
    dispatch(&gm, &msg);
    expect(gm.daemon_quse == 0 && full(&gm) == 2000000, "daemon restart invalidates old model");
    msg = packet(17555);
    dispatch(&gm, &msg);
    expect(full(&gm) == 1755500, "restarted daemon publishes new model");
    for (int i = 0; i < 2; i++) {
        int quse = i ? INT_MAX / 100 : 10;
        msg = packet(quse);
        dispatch(&gm, &msg);
        expect(full(&gm) == quse * 100, "accepted boundary converts without overflow");
    }
    gm.fgd_pid = 0;
    expect(full(&gm) == 2000000, "no daemon uses fallback");
    struct { struct nlmsghdr h; struct fgd_nl_msg_t msg; } wire = {0};
    wire.msg = packet(20002);
    wire.h.nlmsg_len = sizeof(wire.h) + FGD_NL_HDR_LEN + wire.msg.fgd_data_len;
    struct sk_buff skb = {&wire, wire.h.nlmsg_len, 1000, 0};
    mtk_battery_netlink_handler(&skb);
    expect(accepted == 1, "complete wire payload accepted");
    wire.h.nlmsg_len--;
    skb.len--;
    mtk_battery_netlink_handler(&skb);
    expect(accepted == 1, "truncated actual payload rejected");
    wire.h.nlmsg_len = sizeof(wire.h) + FGD_NL_HDR_LEN - 1;
    skb.len = wire.h.nlmsg_len;
    mtk_battery_netlink_handler(&skb);
    expect(accepted == 1, "short wire header rejected");
    for (unsigned int payload = 0; payload < 128; payload++) {
        wire.msg.fgd_cmd = 99;
        wire.msg.fgd_data_len = payload;
        skb.len = NLMSG_HDRLEN + FGD_NL_HDR_LEN + payload;
        wire.h.nlmsg_len = NLMSG_ALIGN(skb.len);
        int count = accepted;
        mtk_battery_netlink_handler(&skb);
        expect(accepted == count + 1, "vendor aligned declaration accepts complete unpadded payload");
        count = accepted;
        skb.len = wire.h.nlmsg_len;
        mtk_battery_netlink_handler(&skb);
        expect(accepted == count + 1, "standard padded datagram accepted");
        count = accepted;
        skb.len = NLMSG_HDRLEN + FGD_NL_HDR_LEN + payload;
        wire.h.nlmsg_len = skb.len;
        mtk_battery_netlink_handler(&skb);
        expect(accepted == count + 1, "standard exact declaration accepted");
        count = accepted;
        wire.h.nlmsg_len = NLMSG_ALIGN(skb.len) + 1;
        mtk_battery_netlink_handler(&skb);
        expect(accepted == count, "overstated declaration beyond padding rejected");
        if (payload) {
            wire.h.nlmsg_len = NLMSG_ALIGN(skb.len);
            skb.len--;
            mtk_battery_netlink_handler(&skb);
            expect(accepted == count, "real missing payload byte rejected even inside alignment");
        }
    }
    wire.msg.fgd_data_len = 4;
    skb.len = NLMSG_HDRLEN + FGD_NL_HDR_LEN + 4;
    wire.h.nlmsg_len = skb.len - 1;
    int count = accepted;
    mtk_battery_netlink_handler(&skb);
    expect(accepted == count, "declared payload shorter than data rejected");
    for (unsigned int length = 0; length < NLMSG_HDRLEN + FGD_NL_HDR_LEN; length++) {
        unsigned char *short_data = malloc(length ? length : 1);
        memset(short_data, 0, length ? length : 1);
        if (length >= sizeof(struct nlmsghdr))
            ((struct nlmsghdr *)short_data)->nlmsg_len = NLMSG_ALIGN(length);
        struct sk_buff short_skb = { short_data, length, 1000, 0 };
        mtk_battery_netlink_handler(&short_skb);
        expect(accepted == count, "short actual header rejected without out of bounds read");
        free(short_data);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
