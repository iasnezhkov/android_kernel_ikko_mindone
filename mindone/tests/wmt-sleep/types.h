/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef int INT32;
typedef unsigned int UINT32;
typedef unsigned short UINT16;
typedef unsigned char UINT8;
typedef unsigned long long UINT64;
typedef unsigned long ULONG;
typedef uintptr_t SIZE_T;
typedef UINT64 *PUINT64;
typedef UINT8 *PUINT8;
typedef int MTK_WCN_BOOL;
typedef int OSAL_SLEEPABLE_LOCK;
typedef int atomic_t;
typedef void VOID;
typedef long long loff_t;
#define __user
#define DBG_LOG_STR_SIZE 256
#define current get_current()
#define WMT_LIB_DMP_SLOT 2
#define WMT_LIB_DMP_CONSYS_MAX_TIMES 10
#define WMTDRV_TYPE_WMT 0
#define DRV_STS_FUNC_ON 1
#define MTK_WCN_BOOL_TRUE 1
#define MTK_WCN_BOOL_FALSE 0
#define WMT_STAT_PWR 0
#define WMT_OPID_GET_CONSYS_STATE 39
#define WMT_OPID_HW_RST 1
#define WMT_OPID_SW_RST 2
#define WMT_OPID_GPIO_STATE 3
#define WMT_OPID_FUNC_ON 4
#define WMTDRV_TYPE_WIFI 1
#define OSAL_OP_DATA_SIZE 8

typedef struct { UINT32 opId, u4InfoBit; SIZE_T au4OpData[OSAL_OP_DATA_SIZE]; } WMT_OP, *P_WMT_OP;
typedef struct { int timeoutValue, raised; } OSAL_SIGNAL, *P_OSAL_SIGNAL;
typedef struct { WMT_OP op; OSAL_SIGNAL signal; INT32 result; atomic_t ref_count; } OSAL_OP, *P_OSAL_OP;
struct work_struct { int unused; };
struct file { int unused; };
