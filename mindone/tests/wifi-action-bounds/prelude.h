/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u_int8_t;
typedef uint32_t u_int32_t;
#define IN
#define MAC_ADDR_LEN 6
#define __KAL_ATTRIB_PACKED__ __attribute__((packed))
#define OFFSET_OF(type, field) offsetof(type, field)
#define DEBUGFUNC(...)
#define DBGLOG(...)
#define FALSE 0
#define TRUE 1
#define CATEGORY_PUBLIC_ACTION 4
#define OUI_WFA 0x506f9a
#define P2P_OUI_TYPE 9
#define DPP_OUI_TYPE 0x1a
#define P2P_GO_NEG_REQ 0
#define P2P_DEV_STATE_REQING_CHANNEL 2
#define P2P_DEV_STATE_CHNL_ON_HAND 3
#define PARAM_PACKET_FILTER_ACTION_FRAME 0x40000000
#define fallthrough __attribute__((fallthrough))
