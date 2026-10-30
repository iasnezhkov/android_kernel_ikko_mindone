/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IN
#define OUT
#define TRUE 1
#define FALSE 0
#define ASSERT(x) assert(x)
#define DEBUGFUNC(...)
#define DBGLOG(...)
#define log_dbg(...)
#define scanlog_dbg(...)
#define RAM_TYPE_MSG 1
#define VIR_MEM_TYPE 0
#define RST_SCAN_RECOVERY 1
#define RST_FLAG_CHIP_RESET 1
#define GL_RESET_TRIGGER(a, flags) reset_triggers++
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_FAILURE 1
#define WLAN_STATUS_ADAPTER_NOT_READY 2
#define WLAN_STATUS_INVALID_LENGTH 3
#define WLAN_STATUS_INVALID_DATA 4
#define ACPI_STATE_D3 3
#define AIS_SCN_REPORT_SEQ_NOT_SET 0xffff
#define AIS_SCN_DONE_TIMEOUT_SEC 15
#define SEC_TO_MSEC(x) ((x) * 1000)
#define OFFSET_OF(t, f) offsetof(t, f)
#define DEG_HIF_DEFAULT_DUMP 1
#define MAX_IE_LENGTH 256
#define OP_MODE_INFRASTRUCTURE 0
#define MEDIA_STATE_CONNECTED 1
#define MEDIA_STATE_DISCONNECTED 0
#define AIS_REQUEST_SCAN 1
#define RM_ON_GOING 1
#define RM_WAITING 2
#define RM_NO_REQUEST 0
#define MSG_SEND_METHOD_BUF 0
#define MSG_SEND_METHOD_UNBUF 1
#define MBOX_ID_0 0
#define CMD_ID_SCAN_CANCEL 1
#define SCN_RM_POLICY_EXCLUDE_CONNECTED 1
#define SCN_RM_POLICY_TIMEOUT 2
#define GLUE_ACQUIRE_SPIN_LOCK(...)
#define GLUE_RELEASE_SPIN_LOCK(...)
#define kalMemZero(p, s) memset(p, 0, s)
#define kalMemCopy(d, s, n) memcpy(d, s, n)
#define kalGetTimeTick() 0
#define CFG_SUPPORT_SCAN_NO_AP_RECOVERY 1
#define IS_FEATURE_ENABLED(x) (x)
typedef uint8_t u_int8_t;
enum ENUM_AIS_STATE { AIS_STATE_IDLE, AIS_STATE_SCAN, AIS_STATE_ONLINE_SCAN, AIS_STATE_NORMAL_TR, AIS_STATE_LOOKING_FOR, AIS_STATE_SEARCH };
enum ENUM_SCAN_STATE { SCAN_STATE_IDLE, SCAN_STATE_SCANNING };
enum ENUM_SCAN_STATUS { SCAN_STATUS_DONE, SCAN_STATUS_CANCELLED };
enum ENUM_MSG_ID { MID_AIS_SCN_SCAN_REQ, MID_AIS_SCN_SCAN_REQ_V2, MID_P2P_SCN_SCAN_REQ, MID_P2P_SCN_SCAN_REQ_V2, MID_BOW_SCN_SCAN_REQ, MID_BOW_SCN_SCAN_REQ_V2, MID_RLM_SCN_SCAN_REQ, MID_RLM_SCN_SCAN_REQ_V2, MID_SCN_AIS_SCAN_DONE, MID_SCN_P2P_SCAN_DONE, MID_SCN_BOW_SCAN_DONE, MID_SCN_RLM_SCAN_DONE, MID_AIS_SCN_SCAN_CANCEL };
struct MSG_HDR { enum ENUM_MSG_ID eMsgId; };
struct MSG_SCN_SCAN_REQ_V2 { struct MSG_HDR rMsgHdr; uint8_t ucSeqNum, ucBssIndex; uint16_t reserved; uint8_t aucIE[]; };
struct MSG_SCN_SCAN_CANCEL { struct MSG_HDR rMsgHdr; uint8_t ucSeqNum, ucBssIndex, fgIsChannelExt, fgIsOidRequest; };
struct CMD_SCAN_CANCEL { uint8_t ucSeqNum, ucIsExtChannel; };
struct MSG_SCN_SCAN_DONE { struct MSG_HDR rMsgHdr; uint8_t ucSeqNum, ucBssIndex; enum ENUM_SCAN_STATUS eScanStatus; };
struct SCAN_PARAM { enum ENUM_MSG_ID eMsgId; uint8_t ucSeqNum, ucBssIndex; };
struct SCAN_INFO { enum ENUM_SCAN_STATE eCurrentState; struct SCAN_PARAM rScanParam; uint8_t fgIsScanTimeout, fgIsScanForFull2Partial; uint32_t u4LastFullScanTime; uint8_t ucScnTimeoutTimes, ucScnTimeoutSubsysResetCnt; };
struct TIMER { int pending; };
struct PARAM_SCAN_REQUEST_ADV { uint8_t ucBssIndex; uint32_t u4IELength; uint8_t *pucIE; };
struct NORMAL_SCAN_PARAMS { uint8_t fgExist; struct PARAM_SCAN_REQUEST_ADV rScanRequest; uint8_t aucScanIEBuf[MAX_IE_LENGTH]; };
struct BCN_RM_PARAMS { int eState; struct NORMAL_SCAN_PARAMS rNormalScan; };
struct RADIO_MEASUREMENT_REQ_PARAMS { struct BCN_RM_PARAMS rBcnRmParam; };
struct CONNECTION_SETTINGS { uint8_t fgIsScanReqIssued; };
struct AIS_FSM_INFO { enum ENUM_AIS_STATE eCurrentState; struct PARAM_SCAN_REQUEST_ADV rScanRequest; uint8_t aucScanIEBuf[MAX_IE_LENGTH]; struct TIMER rJoinTimeoutTimer, rScanDoneTimer; uint16_t u2SeqNumOfScanReport; uint8_t ucSeqNumOfScanReq, fgIsScanning, fgIsScanOidAborted, fgIsChannelGranted, fgIsChannelRequested; };
struct BSS_INFO { uint8_t ucBssIndex; int eCurrentOPMode, eConnectionState; };
struct ROAMING_INFO { int unused; };
struct GLUE_INFO { void *prScanRequest; };
struct WIFI_VAR { struct SCAN_INFO rScanInfo; int ucScanNoApRecover, ucScanNoApRecoverTh; };
struct ADAPTER { int rAcpiState, fgTestMode, fgIsRadioOff, fgEnOnlineScan; uint32_t u4HifDbgFlag; struct GLUE_INFO *prGlueInfo; struct WIFI_VAR rWifiVar; };
static struct AIS_FSM_INFO fsm;
static struct CONNECTION_SETTINGS settings;
static struct BSS_INFO bss;
static struct RADIO_MEASUREMENT_REQ_PARAMS rm;
static struct GLUE_INFO glue;
static struct ADAPTER adapter;
static struct MSG_HDR *completion;
static int allocation_failures, allocations, callbacks, aborted, reset_triggers, firmware_cancels, timer_starts, pending_requests;
static void aisFsmSteps(struct ADAPTER *, enum ENUM_AIS_STATE, uint8_t);
static void kalScanDone(struct GLUE_INFO *, uint8_t, uint32_t);
void scnDoScanTimeoutRecoveryCheck(struct ADAPTER *, uint8_t);
void aisFsmScanRequestAdv(struct ADAPTER *, struct PARAM_SCAN_REQUEST_ADV *);
void aisFsmStateAbort_SCAN(struct ADAPTER *, uint8_t);
void scnFsmMsgAbort(struct ADAPTER *, struct MSG_HDR *);
void scnFsmGenerateScanDoneMsg(struct ADAPTER *, enum ENUM_MSG_ID, uint8_t, uint8_t, enum ENUM_SCAN_STATUS);
static struct AIS_FSM_INFO *aisGetAisFsmInfo(struct ADAPTER *a, uint8_t i) { return &fsm; }
static struct CONNECTION_SETTINGS *aisGetConnSettings(struct ADAPTER *a, uint8_t i) { return &settings; }
static struct BSS_INFO *aisGetAisBssInfo(struct ADAPTER *a, uint8_t i) { return &bss; }
static struct RADIO_MEASUREMENT_REQ_PARAMS *aisGetRmReqParam(struct ADAPTER *a, uint8_t i) { return &rm; }
static struct ROAMING_INFO *aisGetRoamingInfo(struct ADAPTER *a, uint8_t i) { return NULL; }
static struct TIMER *aisGetScanDoneTimer(struct ADAPTER *a, uint8_t i) { return &fsm.rScanDoneTimer; }
static int timerPendingTimer(struct TIMER *t) { return t->pending; }
static void cnmTimerStartTimer(struct ADAPTER *a, struct TIMER *t, unsigned duration) { assert(duration == 15000); t->pending = 1; timer_starts++; }
static void cnmTimerStopTimer(struct ADAPTER *a, struct TIMER *t) { t->pending = 0; }
static void *cnmMemAlloc(struct ADAPTER *a, int kind, size_t size) { allocations++; if (allocation_failures) { allocation_failures--; return NULL; } return calloc(1, size); }
static void cnmMemFree(struct ADAPTER *a, void *p) { free(p); }
static int kalGetMediaStateIndicated(struct GLUE_INFO *g, uint8_t i) { return fsm.eCurrentState == AIS_STATE_NORMAL_TR; }
static void aisFsmInsertRequest(struct ADAPTER *a, int type, uint8_t i) { pending_requests++; }
static void wlanClearScanningResult(struct ADAPTER *a, uint8_t i) {}
static void kalSetHifDbgEvent(struct GLUE_INFO *g) {}
static void glSetRstReason(int reason) {}
static void scnFsmRemovePendingMsg(struct ADAPTER *a, uint8_t seq, uint8_t i) { assert(0); }
static void scnFsmSteps(struct ADAPTER *a, enum ENUM_SCAN_STATE state) { a->rWifiVar.rScanInfo.eCurrentState = state; }
static void wlanSendSetQueryCmd(struct ADAPTER *a, int id, int set, int response, int oid, void *done, void *timeout, size_t size, uint8_t *cmd, void *out, unsigned length) { firmware_cancels++; }
static void mboxSendMsg(struct ADAPTER *a, int id, struct MSG_HDR *msg, int method) { if (method == MSG_SEND_METHOD_UNBUF) { assert(msg->eMsgId == MID_AIS_SCN_SCAN_CANCEL); scnFsmMsgAbort(a, msg); } else { assert(!completion); completion = msg; } }
static void start_scan(struct ADAPTER *a, struct MSG_SCN_SCAN_REQ_V2 *msg) { fsm.fgIsScanning = TRUE; a->rWifiVar.rScanInfo.eCurrentState = SCAN_STATE_SCANNING; a->rWifiVar.rScanInfo.rScanParam = (struct SCAN_PARAM){ msg->rMsgHdr.eMsgId, msg->ucSeqNum, msg->ucBssIndex }; free(msg); }
static void kalCfg80211ScanDone(void *request, uint8_t failed) { assert(request == &adapter); callbacks++; aborted = failed; }
static void scnFsmNotifyEvent(struct ADAPTER *a, enum ENUM_SCAN_STATUS s, uint8_t i) {}
static void scanGetCurrentEssChnlList(struct ADAPTER *a, uint8_t i) {}
static void aisFsmQueryCandidates(struct ADAPTER *a, uint8_t i) {}
static void rrmDoBeaconMeasurement(struct ADAPTER *a, uint8_t i) {}
static void rrmStartNextMeasurement(struct ADAPTER *a, int b, uint8_t i) {}

static uint32_t kalIoctl(struct GLUE_INFO *g, uint32_t (*fn)(struct ADAPTER *, void *, uint32_t, uint32_t *), void *buffer, uint32_t size, int read, int wait, int command, uint32_t *out) { return fn(&adapter, buffer, size, out); }
static void kalMemFree(void *p, size_t size, int type) { free(p); }
