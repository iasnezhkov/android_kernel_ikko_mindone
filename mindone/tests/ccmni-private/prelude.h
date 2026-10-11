#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __user
#define HZ 100
#define IFNAMSIZ 16
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define likely(a) (a)
#define unlikely(a) (a)
#define CCMNI_INF_MSG(...) ((void)0)
#define CCMNI_DBG_MSG(...) ((void)0)
#define CCMNI_PR_DBG(...) ((void)0)
#define ifr_data ifr_ifru.ifru_data
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t compat_ulong_t;
typedef int32_t compat_int_t;
typedef uint32_t compat_uptr_t;
typedef uint32_t compat_caddr_t;
typedef int atomic_t;
typedef int spinlock_t;
typedef int netdev_tx_t;
typedef void raw_hdlc_proto;
typedef void cisco_proto;
typedef void fr_proto;
typedef void fr_proto_pvc;
typedef void fr_proto_pvc_info;
typedef void x25_hdlc_proto;
typedef void sync_serial_settings;
typedef void te1_settings;
struct sockaddr { unsigned short family; char data[14]; };
struct timespec64 { int64_t tv_sec, tv_nsec; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; bool pending, running; };
struct net_device;
struct ifreq;
struct sk_buff;
