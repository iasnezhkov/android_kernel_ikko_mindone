/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2017 MediaTek Inc. */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */

#ifndef _MINDONE_WSYS_CMD_H
#define _MINDONE_WSYS_CMD_H

#include "mac.h"

#define PARAM_MAX_LEN_RATES_EX 16
#define DISCONNECT_REASON_CODE_RESERVED 0
#define DISCONNECT_REASON_CODE_RADIO_LOST 1
#define DISCONNECT_REASON_CODE_DEAUTHENTICATED 2
#define DISCONNECT_REASON_CODE_DISASSOCIATED 3
#define DISCONNECT_REASON_CODE_NEW_CONNECTION 4
#define CONFIG_BW_20_40M 0
#define CONFIG_BW_20M 1
#define CONFIG_BW_20_40_80M 2
#define CMD_CH_ACTION_REQ 0
#define CMD_CH_ACTION_ABORT 1
#define EVENT_CH_STATUS_GRANT 0
#define PM_WOWLAN_REQ_START 0x1
#define PM_WOWLAN_REQ_STOP 0x2
#define PACKETF_CAP_TYPE_ARP BIT(1)
#define PACKETF_CAP_TYPE_MAGIC BIT(2)
#define PACKETF_CAP_TYPE_BITMAP BIT(3)
#define PACKETF_CAP_TYPE_EAPOL BIT(4)
#define PACKETF_CAP_TYPE_TDLS BIT(5)
#define PACKETF_CAP_TYPE_CF BIT(6)
#define PACKETF_CAP_TYPE_HEARTBEAT BIT(7)
#define PACKETF_CAP_TYPE_TCP_SYN BIT(8)
#define PACKETF_CAP_TYPE_UDP_SYN BIT(9)
#define PACKETF_CAP_TYPE_BCAST_SYN BIT(10)
#define PACKETF_CAP_TYPE_MCAST_SYN BIT(11)
#define PACKETF_CAP_TYPE_V6 BIT(12)
#define PACKETF_CAP_TYPE_TDIM BIT(13)
#define SCAN_DONE_EVENT_MAX_CHANNEL_NUM 64
#define MAX_TCP_UDP_PORT 20
#define HE_OP_BYTE_NUM 3
#define HE_MAC_CAP_BYTE_NUM 6
#define HE_PHY_CAP_BYTE_NUM 11
#define EHT_OP_BYTE_NUM (1)
#define EHT_MAC_CAP_BYTE_NUM (2)
#define EHT_PHY_CAP_BYTE_NUM (8)

enum ENUM_CMD_ID {
	CMD_ID_DUMMY_RSV = 0x00,
	CMD_ID_TEST_CTRL = 0x01,
	CMD_ID_BASIC_CONFIG,
	CMD_ID_SCAN_REQ_V2,
	CMD_ID_NIC_POWER_CTRL,
	CMD_ID_POWER_SAVE_MODE,
	CMD_ID_LINK_ATTRIB,
	CMD_ID_ADD_REMOVE_KEY,
	CMD_ID_DEFAULT_KEY_ID,
	CMD_ID_INFRASTRUCTURE,
	CMD_ID_SET_RX_FILTER,
	CMD_ID_DOWNLOAD_BUF,
	CMD_ID_WIFI_START,
	CMD_ID_CMD_BT_OVER_WIFI,
	CMD_ID_SET_MEDIA_CHANGE_DELAY_TIME,
	CMD_ID_SET_DOMAIN_INFO,
	CMD_ID_SET_IP_ADDRESS,
	CMD_ID_BSS_ACTIVATE_CTRL,
	CMD_ID_SET_BSS_INFO,
	CMD_ID_UPDATE_STA_RECORD,
	CMD_ID_REMOVE_STA_RECORD,
	CMD_ID_INDICATE_PM_BSS_CREATED,
	CMD_ID_INDICATE_PM_BSS_CONNECTED,
	CMD_ID_INDICATE_PM_BSS_ABORT,
	CMD_ID_UPDATE_BEACON_CONTENT,
	CMD_ID_SET_BSS_RLM_PARAM,
	CMD_ID_SCAN_REQ,
	CMD_ID_SCAN_CANCEL,
	CMD_ID_CH_PRIVILEGE,
	CMD_ID_UPDATE_WMM_PARMS,
	CMD_ID_SET_WMM_PS_TEST_PARMS,
	CMD_ID_TX_AMPDU,
	CMD_ID_ADDBA_REJECT,
	CMD_ID_SET_PS_PROFILE_ADV,
	CMD_ID_SET_RAW_PATTERN,
	CMD_ID_CONFIG_PATTERN_FUNC,
	CMD_ID_SET_TX_PWR,
	CMD_ID_SET_PWR_PARAM,
	CMD_ID_P2P_ABORT,
	CMD_ID_SET_TX_EXTEND_PWR,
	CMD_ID_SET_DBDC_PARMS,
	CMD_ID_SET_FILTER_COEFFICIENT,
	CMD_ID_SET_ACL_POLICY = 0x2A,
	CMD_ID_RANDOM_RX_RESET_EN = 0x2C,
	CMD_ID_RANDOM_RX_RESET_DE = 0x2D,
	CMD_ID_SAPP_EN = 0x2E,
	CMD_ID_SAPP_DE = 0x2F,
	CMD_ID_ROAMING_TRANSIT = 0x30,
	CMD_ID_SET_PHY_PARAM,
	CMD_ID_SET_NOA_PARAM,
	CMD_ID_SET_OPPPS_PARAM,
	CMD_ID_SET_UAPSD_PARAM,
	CMD_ID_SET_SIGMA_STA_SLEEP,
	CMD_ID_SET_EDGE_TXPWR_LIMIT,
	CMD_ID_SET_DEVICE_MODE,
	CMD_ID_SET_TXPWR_CTRL,
	CMD_ID_SET_AUTOPWR_CTRL,
	CMD_ID_SET_WFD_CTRL,
	CMD_ID_SET_NLO_REQ,
	CMD_ID_SET_NLO_CANCEL,
	CMD_ID_SET_GTK_REKEY_DATA,
	CMD_ID_ROAMING_CONTROL,
	CMD_ID_RESET_BA_SCOREBOARD = 0x3f,
	CMD_ID_SET_EDGE_TXPWR_LIMIT_5G = 0x40,
	CMD_ID_SET_CHANNEL_PWR_OFFSET,
	CMD_ID_SET_80211AC_TX_PWR,
	CMD_ID_SET_PATH_COMPASATION,
	CMD_ID_SET_RTT_REQ = 0x44,
	CMD_ID_SET_RTT_CALIBR,
	CMD_ID_GET_RTT_RANGE_UPDATE,
	CMD_ID_SET_BATCH_REQ,
	CMD_ID_SET_NVRAM_SETTINGS,
	CMD_ID_SET_COUNTRY_POWER_LIMIT,
	CMD_ID_SET_WOWLAN = 0x4A,
	CMD_ID_SET_IPV6_ADDRESS,
	CMD_ID_SET_SLTINFO = 0x50,
	CMD_ID_UART_ACK,
	CMD_ID_UART_NAK,
	CMD_ID_GET_CHIPID,
	CMD_ID_SET_MDDP_FILTER_RULE = 0x54,
	CMD_ID_SET_AM_FILTER = 0x55,
	CMD_ID_SET_AM_HEARTBEAT,
	CMD_ID_SET_AM_TCP,
	CMD_ID_SET_SUSPEND_MODE = 0x58,
	CMD_ID_SET_PF_CAPABILITY = 0x59,
	CMD_ID_SET_RRM_CAPABILITY = 0x5A,
	CMD_ID_SET_AP_CONSTRAINT_PWR_LIMIT = 0x5B,
	CMD_ID_SET_COUNTRY_POWER_LIMIT_PER_RATE = 0x5D,
	CMD_ID_SET_TSM_STATISTICS_REQUEST = 0x5E,
	CMD_ID_GET_TSM_STATISTICS = 0x5F,
	CMD_ID_GET_PSCAN_CAPABILITY = 0x60,
	CMD_ID_SET_SCAN_SCHED_ENABLE,
	CMD_ID_SET_SCAN_SCHED_REQ,
	CMD_ID_SET_GSCAN_ADD_HOTLIST_BSSID,
	CMD_ID_SET_GSCAN_ADD_SWC_BSSID,
	CMD_ID_SET_GSCAN_MAC_ADDR,
	CMD_ID_GET_GSCAN_RESULT,
	CMD_ID_SET_PSCAN_MAC_ADDR,
	CMD_ID_FAST_SCAN_DUMMY2,
	CMD_ID_FAST_SCAN_DUMMY3,
	CMD_ID_UPDATE_AC_PARMS = 0x6A,
	CMD_ID_SET_ROAMING_SKIP = 0x6D,
	CMD_ID_SET_DROP_PACKET_CFG = 0x6E,
	CMD_ID_GET_SET_CUSTOMER_CFG = 0x70,
	CMD_ID_SET_ALWAYS_SCAN_PARAM = 0x73,
	CMD_ID_TDLS_PS = 0x75,
	CMD_ID_GET_CNM = 0x79,
	CMD_ID_FRM_IND_FROM_HOST = 0x7D,
	CMD_ID_PERF_IND = 0x7E,
#if CFG_SUPPORT_SMART_GEAR
	CMD_ID_SG_PARAM = 0x7F,
#endif
	CMD_ID_GET_NIC_CAPABILITY = 0x80,
	CMD_ID_GET_LINK_QUALITY,
	CMD_ID_GET_STATISTICS,
	CMD_ID_GET_CONNECTION_STATUS,
	CMD_ID_GET_STATS_LLS,
	CMD_ID_GET_STA_STATISTICS = 0x85,
	CMD_ID_GET_LTE_CHN = 0x87,
	CMD_ID_GET_BUG_REPORT = 0x89,
	CMD_ID_GET_NIC_CAPABILITY_V2 = 0x8A,
#if CFG_SUPPORT_LOWLATENCY_MODE
	CMD_ID_SET_LOW_LATENCY_MODE = 0x8B,
#endif
	CMD_ID_LOG_UI_INFO = 0x8D,
	CMD_ID_SET_OSHARE_MODE = 0x8E,
	CMD_ID_RDD_ON_OFF_CTRL = 0x8F,
	CMD_ID_SET_FORCE_RTS = 0x90,
#if (CFG_SUPPORT_ICS == 1)
	CMD_ID_SET_ICS_SNIFFER = 0x93,
#endif
	CMD_ID_WFC_KEEP_ALIVE = 0xA0,
	CMD_ID_RSSI_MONITOR = 0xA1,
#if (CFG_SUPPORT_PKT_OFLD == 1)
	CMD_ID_PKT_OFLD = 0xA2,
#endif
	CMD_ID_CAL_BACKUP_IN_HOST_V2 = 0xAE,
	CMD_ID_MQM_UPDATE_MU_EDCA_PARMS = 0xB0,
	CMD_ID_RLM_UPDATE_SR_PARAMS = 0xB1,
	CMD_ID_ACCESS_REG = 0xc0,
	CMD_ID_MAC_MCAST_ADDR,
	CMD_ID_802_11_PMKID,
	CMD_ID_ACCESS_EEPROM,
	CMD_ID_SW_DBG_CTRL,
	CMD_ID_FW_LOG_2_HOST,
	CMD_ID_DUMP_MEM,
	CMD_ID_RESOURCE_CONFIG,
	CMD_ID_ACCESS_RX_STAT,
	CMD_ID_LIST_MODE,
	CMD_ID_CHIP_CONFIG = 0xCA,
	CMD_ID_WTBL_INFO = 0xCD,
	CMD_ID_MIB_INFO = 0xCE,
	CMD_ID_SET_TXBF_BACKOFF = 0xD1,
	CMD_ID_FAST_PATH = 0xD5,
	CMD_ID_SET_RDD_CH = 0xE1,
#if CFG_SUPPORT_NAN
	CMD_ID_NAN_EXT_CMD = 0XEB,
#endif
	CMD_ID_LAYER_0_EXT_MAGIC_NUM = 0xED,
	CMD_ID_INIT_CMD_WIFI_RESTART = 0xEF,
	CMD_ID_SET_BWCS = 0xF1,
	CMD_ID_SET_OSC = 0xF2,
	CMD_ID_HIF_CTRL = 0xF6,
	CMD_ID_GET_BUILD_DATE_CODE = 0xF8,
	CMD_ID_GET_BSS_INFO = 0xF9,
	CMD_ID_SET_HOTSPOT_OPTIMIZATION = 0xFA,
	CMD_ID_SET_TDLS_CH_SW = 0xFB,
	CMD_ID_SET_MONITOR = 0xFC,
	CMD_ID_SET_CCK_1M_PWR = 0xFD,
	CMD_ID_END
};

enum ENUM_EVENT_ID {
	EVENT_ID_NIC_CAPABILITY = 0x01,
	EVENT_ID_LINK_QUALITY,
	EVENT_ID_STATISTICS,
	EVENT_ID_MIC_ERR_INFO,
	EVENT_ID_ACCESS_REG,
	EVENT_ID_ACCESS_EEPROM,
	EVENT_ID_SLEEPY_INFO,
	EVENT_ID_BT_OVER_WIFI,
	EVENT_ID_TEST_STATUS,
	EVENT_ID_RX_ADDBA,
	EVENT_ID_RX_DELBA,
	EVENT_ID_ACTIVATE_STA_REC,
	EVENT_ID_SCAN_DONE,
	EVENT_ID_RX_FLUSH,
	EVENT_ID_TX_DONE,
	EVENT_ID_CH_PRIVILEGE,
	EVENT_ID_BSS_ABSENCE_PRESENCE,
	EVENT_ID_STA_CHANGE_PS_MODE,
	EVENT_ID_BSS_BEACON_TIMEOUT,
	EVENT_ID_UPDATE_NOA_PARAMS,
	EVENT_ID_AP_OBSS_STATUS,
	EVENT_ID_STA_UPDATE_FREE_QUOTA,
	EVENT_ID_SW_DBG_CTRL,
	EVENT_ID_ROAMING_STATUS,
	EVENT_ID_STA_AGING_TIMEOUT,
	EVENT_ID_SEC_CHECK_RSP,
	EVENT_ID_SEND_DEAUTH,
	EVENT_ID_UPDATE_RDD_STATUS,
	EVENT_ID_UPDATE_BWCS_STATUS,
	EVENT_ID_UPDATE_BCM_DEBUG,
	EVENT_ID_RX_ERR,
	EVENT_ID_DUMP_MEM = 0x20,
	EVENT_ID_STA_STATISTICS,
	EVENT_ID_STA_STATISTICS_UPDATE,
	EVENT_ID_SCHED_SCAN_DONE,
	EVENT_ID_ADD_PKEY_DONE,
	EVENT_ID_ICAP_DONE,
	EVENT_ID_RESOURCE_CONFIG = 0x26,
	EVENT_ID_DEBUG_MSG = 0x27,
	EVENT_ID_RTT_DISCOVER_PEER = 0x28,
	EVENT_ID_RTT_UPDATE_RANGE = 0x29,
	EVENT_ID_CHECK_REORDER_BUBBLE = 0x2a,
	EVENT_ID_BATCH_RESULT = 0x2b,
	EVENT_ID_STA_ABSENCE_TX = 0x2c,
	EVENT_ID_RTT_UPDATE_LOCATION = 0x2d,
	EVENT_ID_TX_ADDBA = 0x2e,
	EVENT_ID_LTE_SAFE_CHN = 0x2f,
	EVENT_ID_GSCAN_CAPABILITY = 0x30,
	EVENT_ID_GSCAN_SCAN_COMPLETE = 0x31,
	EVENT_ID_GSCAN_FULL_RESULT = 0x32,
	EVENT_ID_GSCAN_SIGNIFICANT_CHANGE = 0x33,
	EVENT_ID_GSCAN_GEOFENCE_FOUND = 0x34,
	EVENT_ID_GSCAN_SCAN_AVAILABLE = 0x35,
	EVENT_ID_GSCAN_RESULT = 0x36,
	EVENT_ID_GSCAN_HOTLIST_RESULTS_FOUND = 0x37,
	EVENT_ID_GSCAN_HOTLIST_RESULTS_LOST = 0x38,
	EVENT_ID_FAST_SCAN_DUMMY1 = 0x39,
	EVENT_ID_FAST_SCAN_DUMMY2 = 0x3a,
	EVENT_ID_FAST_SCAN_DUMMY3 = 0x3b,
	EVENT_ID_UART_ACK = 0x40,
	EVENT_ID_UART_NAK,
	EVENT_ID_GET_CHIPID,
	EVENT_ID_SLT_STATUS,
	EVENT_ID_CHIP_CONFIG,
	EVENT_ID_ACCESS_RX_STAT = 0x45,
	EVENT_ID_LIST_MODE = 0x48,
	EVENT_ID_RDD_SEND_PULSE = 0x50,
	EVENT_ID_PFMU_TAG_READ = 0x51,
	EVENT_ID_PFMU_DATA_READ = 0x52,
	EVENT_ID_MU_GET_QD = 0x53,
	EVENT_ID_MU_GET_LQ = 0x54,
	EVENT_ID_AM_FILTER = 0x55,
	EVENT_ID_AM_HEARTBEAT = 0x56,
	EVENT_ID_AM_TCP = 0x57,
	EVENT_ID_HEARTBEAT_INFO = 0x58,
	EVENT_ID_MDDP_FILTER_RULE = 0x59,
	EVENT_ID_BA_FW_DROP_SN = 0x5A,
	EVENT_ID_LOW_LATENCY_INFO = 0x5B,
	EVENT_ID_RDD_REPORT = 0x60,
	EVENT_ID_CSA_DONE = 0x61,
	EVENT_ID_OPMODE_CHANGE = 0x63,
#if CFG_SUPPORT_IDC_CH_SWITCH
	EVENT_ID_LTE_IDC_REPORT = 0x64,
#endif
#if CFG_SUPPORT_SMART_GEAR
	EVENT_ID_SG_STATUS = 0x65,
#endif
#if CFG_SUPPORT_HE_ER
	EVENT_ID_BSS_ER_TX_MODE = 0x66,
#endif
	EVENT_ID_REPORT_U_EVENT = 0x68,
	EVENT_ID_STATS_LLS = 0x69,
	EVENT_ID_GET_CMD_INFO = 0x70,
	EVENT_ID_DBDC_SWITCH_DONE = 0x78,
	EVENT_ID_GET_CNM = 0x79,
	EVENT_ID_FRM_IND_FROM_HOST = 0x7D,
	EVENT_ID_TDLS = 0x80,
	EVENT_ID_LOG_UI_INFO = 0x8D,
	EVENT_ID_UPDATE_COEX_PHYRATE = 0x90,
	EVENT_ID_UPDATE_COEX_STATUS = 0x91,
	EVENT_ID_RSSI_MONITOR = 0xA1,
#if (CFG_SUPPORT_PKT_OFLD == 1)
	EVENT_ID_PKT_OFLD = 0xA2,
#endif
	EVENT_ID_CAL_BACKUP_IN_HOST_V2 = 0xAE,
	EVENT_ID_CAL_ALL_DONE = 0xAF,
#if CFG_SUPPORT_BAR_DELAY_INDICATION
	EVENT_ID_RXM_DELAY_BAR = 0xB5,
#endif
	EVENT_ID_TPUT_INFO = 0xCB,
	EVENT_ID_WTBL_INFO = 0xCD,
	EVENT_ID_MIB_INFO = 0xCE,
#if CFG_SUPPORT_NAN
	EVENT_ID_NAN_EXT_EVENT = 0xEB,
#endif
	EVENT_ID_FAST_PATH = 0xD5,
	EVENT_ID_NIC_CAPABILITY_V2 = 0xEC,
	EVENT_ID_LAYER_0_EXT_MAGIC_NUM = 0xED,
	EVENT_ID_ASSERT_DUMP = 0xF0,
	EVENT_ID_HIF_CTRL = 0xF6,
	EVENT_ID_BUILD_DATE_CODE = 0xF8,
	EVENT_ID_GET_AIS_BSS_INFO = 0xF9,
	EVENT_ID_DEBUG_CODE = 0xFB,
	EVENT_ID_RFTEST_READY = 0xFC,
	EVENT_ID_INIT_EVENT_CMD_RESULT = 0xFD,
	EVENT_ID_END
};

struct INIT_WIFI_CMD {
	uint8_t ucCID;
	uint8_t ucPktTypeID;
	uint8_t ucReserved;
	uint8_t ucSeqNum;
	uint32_t u4Reserved;
	uint32_t au4D3toD7Rev[5];
	uint8_t aucBuffer[0];
};

struct INIT_WIFI_EVENT {
	uint16_t u2RxByteCount;
	uint16_t u2PacketType;
	uint8_t ucEID;
	uint8_t ucSeqNum;
	uint8_t aucReserved[2];
	uint8_t aucBuffer[0];
};

struct INIT_HIF_TX_HEADER {
	uint16_t u2TxByteCount;
	uint16_t u2PQ_ID;
	struct INIT_WIFI_CMD rInitWifiCmd;
};

struct INIT_HIF_TX_HEADER_PENDING_FOR_HW_32BYTES {
	uint16_t u2TxByteCount;
	uint16_t u2PQ_ID;
	uint8_t ucWlanIdx;
	uint8_t ucHeaderFormat;
	uint8_t ucHeaderPadding;
	uint8_t ucPktFt: 2;
	uint8_t ucOwnMAC: 6;
	uint32_t au4D2toD7Rev[6];
};

struct INIT_EVENT_CMD_RESULT {
	uint8_t ucStatus;
	uint8_t ucCID;
	uint8_t aucReserved[2];
};

enum ENUM_PARAM_AUTH_MODE {
	AUTH_MODE_OPEN = 0,
	AUTH_MODE_SHARED,
	AUTH_MODE_AUTO_SWITCH,
	AUTH_MODE_WPA,
	AUTH_MODE_WPA_PSK,
	AUTH_MODE_WPA_NONE,
	AUTH_MODE_WPA2,
	AUTH_MODE_WPA2_PSK,
	AUTH_MODE_WPA2_FT,
	AUTH_MODE_WPA2_FT_PSK,
	AUTH_MODE_WPA_OSEN,
	AUTH_MODE_WPA3_SAE,
	AUTH_MODE_WPA3_OWE,
	AUTH_MODE_NUM
};

enum ENUM_WEP_STATUS {
	ENUM_WEP_ENABLED,
	ENUM_ENCRYPTION1_ENABLED = ENUM_WEP_ENABLED,
	ENUM_WEP_DISABLED,
	ENUM_ENCRYPTION_DISABLED = ENUM_WEP_DISABLED,
	ENUM_WEP_KEY_ABSENT,
	ENUM_ENCRYPTION1_KEY_ABSENT = ENUM_WEP_KEY_ABSENT,
	ENUM_WEP_NOT_SUPPORTED,
	ENUM_ENCRYPTION_NOT_SUPPORTED = ENUM_WEP_NOT_SUPPORTED,
	ENUM_ENCRYPTION2_ENABLED,
	ENUM_ENCRYPTION2_KEY_ABSENT,
	ENUM_ENCRYPTION3_ENABLED,
	ENUM_ENCRYPTION3_KEY_ABSENT,
	ENUM_ENCRYPTION4_ENABLED,
	ENUM_ENCRYPTION4_KEY_ABSENT,
	ENUM_ENCRYPTION_NUM
};

struct CMD_802_11_KEY {
	uint8_t ucAddRemove;
	uint8_t ucTxKey;
	uint8_t ucKeyType;
	uint8_t ucIsAuthenticator;
	uint8_t aucPeerAddr[6];
	uint8_t ucBssIdx;
	uint8_t ucAlgorithmId;
	uint8_t ucKeyId;
	uint8_t ucKeyLen;
	uint8_t ucWlanIndex;
	uint8_t ucMgmtProtection;
	uint8_t aucKeyMaterial[32];
	uint8_t aucKeyRsc[16];
};

typedef uint8_t PARAM_PMKID_VALUE[16];

struct PARAM_BSSID_INFO {
	uint8_t aucBssid[MAC_ADDR_LEN];
	PARAM_PMKID_VALUE arPMKID;
};

struct CMD_SET_BSS_RLM_PARAM {
	uint8_t ucBssIndex;
	uint8_t ucRfBand;
	uint8_t ucPrimaryChannel;
	uint8_t ucRfSco;
	uint8_t ucErpProtectMode;
	uint8_t ucHtProtectMode;
	uint8_t ucGfOperationMode;
	uint8_t ucTxRifsMode;
	uint16_t u2HtOpInfo3;
	uint16_t u2HtOpInfo2;
	uint8_t ucHtOpInfo1;
	uint8_t ucUseShortPreamble;
	uint8_t ucUseShortSlotTime;
	uint8_t ucVhtChannelWidth;
	uint8_t ucVhtChannelFrequencyS1;
	uint8_t ucVhtChannelFrequencyS2;
	uint16_t u2VhtBasicMcsSet;
	uint8_t ucTxNss;
	uint8_t ucRxNss;
};

struct CMD_SET_BSS_INFO {
	uint8_t ucBssIndex;
	uint8_t ucConnectionState;
	uint8_t ucCurrentOPMode;
	uint8_t ucSSIDLen;
	uint8_t aucSSID[32];
	uint8_t aucBSSID[6];
	uint8_t ucIsQBSS;
	uint8_t ucVersion;
	uint16_t u2OperationalRateSet;
	uint16_t u2BSSBasicRateSet;
	uint8_t ucStaRecIdxOfAP;
	uint8_t aucPadding0[1];
	uint16_t u2HwDefaultFixedRateCode;
	uint8_t ucNonHTBasicPhyType;
	uint8_t ucAuthMode;
	uint8_t ucEncStatus;
	uint8_t ucPhyTypeSet;
	uint8_t ucWapiMode;
	uint8_t ucIsApMode;
	uint8_t ucBMCWlanIndex;
	uint8_t ucHiddenSsidMode;
	uint8_t ucDisconnectDetectThreshold;
	uint8_t ucIotApAct;
	uint8_t aucPadding1[2];
	uint32_t u4PrivateData;
	struct CMD_SET_BSS_RLM_PARAM rBssRlmParam;
	uint8_t ucDBDCBand;
	uint8_t ucWmmSet;
	uint8_t ucDBDCAction;
	uint8_t ucNss;
	uint8_t aucPadding2[2];
	uint8_t ucHeOpParams[HE_OP_BYTE_NUM];
	uint8_t ucBssColorInfo;
	uint16_t u2HeBasicMcsSet;
	uint8_t ucMaxBSSIDIndicator;
	uint8_t ucMBSSIDIndex;
	uint8_t aucPadding[12];
};

struct CMD_HTVHT_BA_SIZE {
	uint8_t ucTxBaSize;
	uint8_t ucRxBaSize;
	uint8_t aucReserved[2];
};

struct CMD_HE_BA_SIZE {
	uint16_t u2TxBaSize;
	uint16_t u2RxBaSize;
};

struct CMD_EHT_BA_SIZE {
	uint16_t u2TxBaSize;
	uint16_t u2RxBaSize;
};

struct CMD_UPDATE_STA_RECORD {
	uint8_t ucStaIndex;
	uint8_t ucStaType;
	uint8_t aucMacAddr[MAC_ADDR_LEN];
	uint16_t u2AssocId;
	uint16_t u2ListenInterval;
	uint8_t ucBssIndex;
	uint8_t ucDesiredPhyTypeSet;
	uint16_t u2DesiredNonHTRateSet;
	uint16_t u2BSSBasicRateSet;
	uint8_t ucIsQoS;
	uint8_t ucIsUapsdSupported;
	uint8_t ucStaState;
	uint8_t ucMcsSet;
	uint8_t ucSupMcs32;
	uint8_t ucVersion;
	uint8_t aucRxMcsBitmask[10];
	uint16_t u2RxHighestSupportedRate;
	uint32_t u4TxRateInfo;
	uint16_t u2HtCapInfo;
	uint16_t u2HtExtendedCap;
	uint32_t u4TxBeamformingCap;
	uint8_t ucAmpduParam;
	uint8_t ucAselCap;
	uint8_t ucRCPI;
	uint8_t ucNeedResp;
	uint8_t ucUapsdAc;
	uint8_t ucUapsdSp;
	uint8_t ucWlanIndex;
	uint8_t ucBMCWlanIndex;
	uint32_t u4VhtCapInfo;
	uint16_t u2VhtRxMcsMap;
	uint16_t u2VhtRxHighestSupportedDataRate;
	uint16_t u2VhtTxMcsMap;
	uint16_t u2VhtTxHighestSupportedDataRate;
	uint8_t ucRtsPolicy;
	uint8_t ucVhtOpMode;
	uint8_t ucTrafficDataType;
	uint8_t ucTxGfMode;
	uint8_t ucTxSgiMode;
	uint8_t ucTxStbcMode;
	uint16_t u2HwDefaultFixedRateCode;
	uint8_t ucTxAmpdu;
	uint8_t ucRxAmpdu;
	uint32_t u4FixedPhyRate;
	uint16_t u2MaxLinkSpeed;
	uint16_t u2MinLinkSpeed;
	uint32_t u4Flags;
	union BA_SIZE {
		struct CMD_HTVHT_BA_SIZE rHtVhtBaSize;
		struct CMD_HE_BA_SIZE rHeBaSize;
		struct CMD_EHT_BA_SIZE rEhtBaSize;
	} rBaSize;
	struct_group(rTxBfPfmuStaInfo,
	uint16_t u2PfmuId;
	uint8_t fgSU_MU;
	uint8_t fgETxBfCap;
	uint8_t ucSoundingPhy;
	uint8_t ucNdpaRate;
	uint8_t ucNdpRate;
	uint8_t ucReptPollRate;
	uint8_t ucTxMode;
	uint8_t ucNc;
	uint8_t ucNr;
	uint8_t ucCBW;
	uint8_t ucTotMemRequire;
	uint8_t ucMemRequire20M;
	uint8_t ucMemRow0;
	uint8_t ucMemCol0;
	uint8_t ucMemRow1;
	uint8_t ucMemCol1;
	uint8_t ucMemRow2;
	uint8_t ucMemCol2;
	uint8_t ucMemRow3;
	uint8_t ucMemCol3;
	uint16_t u2SmartAnt;
	uint8_t ucSEIdx;
	uint8_t uciBfTimeOut;
	uint8_t uciBfDBW;
	uint8_t uciBfNcol;
	uint8_t uciBfNrow;
	uint8_t aucPadding1[3];
	);
	uint8_t ucTxAmsduInAmpdu;
	uint8_t ucRxAmsduInAmpdu;
	uint8_t aucPadding2[2];
	uint32_t u4TxMaxAmsduInAmpduLen;
#if CFG_SUPPORT_802_11AX
	uint8_t ucHeMacCapInfo[HE_MAC_CAP_BYTE_NUM];
	uint8_t ucHePhyCapInfo[HE_PHY_CAP_BYTE_NUM];
	uint8_t aucPadding3[2];
	uint16_t u2HeRxMcsMapBW80;
	uint16_t u2HeTxMcsMapBW80;
	uint16_t u2HeRxMcsMapBW160;
	uint16_t u2HeTxMcsMapBW160;
	uint16_t u2HeRxMcsMapBW80P80;
	uint16_t u2HeTxMcsMapBW80P80;
	uint16_t u2He6gBandCapInfo;
	uint8_t aucPadding4[2];
	uint8_t aucPadding5[28];
#if (CFG_SUPPORT_802_11BE == 1)
	uint8_t ucEhtMacCapInfo[EHT_MAC_CAP_BYTE_NUM];
	uint8_t ucEhtPhyCapInfo[EHT_PHY_CAP_BYTE_NUM];
#endif
#else
	uint8_t aucPadding4[32];
#endif
};

struct CMD_REMOVE_STA_RECORD {
	uint8_t ucActionType;
	uint8_t ucStaIndex;
	uint8_t ucBssIndex;
	uint8_t ucReserved;
};

struct CMD_INDICATE_PM_BSS_CREATED {
	uint8_t ucBssIndex;
	uint8_t ucDtimPeriod;
	uint16_t u2BeaconInterval;
	uint16_t u2AtimWindow;
	uint8_t aucReserved[2];
};

struct CMD_INDICATE_PM_BSS_CONNECTED {
	uint8_t ucBssIndex;
	uint8_t ucDtimPeriod;
	uint16_t u2AssocId;
	uint16_t u2BeaconInterval;
	uint16_t u2AtimWindow;
	uint8_t fgIsUapsdConnection;
	uint8_t ucBmpDeliveryAC;
	uint8_t ucBmpTriggerAC;
	uint8_t aucReserved[1];
};

struct CMD_INDICATE_PM_BSS_ABORT {
	uint8_t ucBssIndex;
	uint8_t aucReserved[3];
};

struct CMD_SET_WMM_PS_TEST_STRUCT {
	uint8_t ucBssIndex;
	uint8_t bmfgApsdEnAc;
	uint8_t ucIsEnterPsAtOnce;
	uint8_t ucIsDisableUcTrigger;
};

struct CMD_CUSTOM_NOA_PARAM_STRUCT {
	uint32_t u4NoaDurationMs;
	uint32_t u4NoaIntervalMs;
	uint32_t u4NoaCount;
	uint8_t ucBssIdx;
	uint8_t aucReserved[3];
};

struct CMD_CUSTOM_OPPPS_PARAM_STRUCT {
	uint32_t u4CTwindowMs;
	uint8_t ucBssIdx;
	uint8_t aucReserved[3];
};

struct CMD_CUSTOM_UAPSD_PARAM_STRUCT {
	uint8_t fgEnAPSD;
	uint8_t fgEnAPSD_AcBe;
	uint8_t fgEnAPSD_AcBk;
	uint8_t fgEnAPSD_AcVo;
	uint8_t fgEnAPSD_AcVi;
	uint8_t ucMaxSpLen;
	uint8_t aucResv[2];
};

struct CHANNEL_INFO {
	uint8_t ucBand;
	uint8_t ucChannelNum;
};

struct PARAM_SSID {
	uint32_t u4SsidLen;
	uint8_t aucSsid[ELEM_MAX_LEN_SSID];
};

struct CMD_SCAN_REQ_V2 {
	uint8_t ucSeqNum;
	uint8_t ucBssIndex;
	uint8_t ucScanType;
	uint8_t ucSSIDType;
	uint8_t ucSSIDNum;
	uint8_t ucNumProbeReq;
	uint8_t ucScnFuncMask;
	uint8_t auVersion[1];
	struct PARAM_SSID arSSID[4];
	uint16_t u2ProbeDelayTime;
	uint16_t u2ChannelDwellTime;
	uint16_t u2TimeoutValue;
	uint8_t ucChannelType;
	uint8_t ucChannelListNum;
	struct CHANNEL_INFO arChannelList[32];
	uint16_t u2IELen;
	uint8_t aucIE[600];
	uint8_t ucChannelListExtNum;
	uint8_t ucSSIDExtNum;
	uint16_t u2ChannelMinDwellTime;
	struct CHANNEL_INFO arChannelListExtend[32];
	struct PARAM_SSID arSSIDExtend[6];
	uint8_t aucBSSID[MAC_ADDR_LEN];
	uint8_t aucRandomMac[MAC_ADDR_LEN];
	uint8_t aucExtBSSID[CFG_SCAN_OOB_MAX_NUM][MAC_ADDR_LEN];
	uint8_t ucShortSSIDNum;
	uint8_t ucBssidMatchCh[CFG_SCAN_OOB_MAX_NUM];
	uint8_t ucBssidMatchSsidInd[CFG_SCAN_OOB_MAX_NUM];
	uint8_t aucPadding_3[31];
};

struct CMD_SCAN_CANCEL {
	uint8_t ucSeqNum;
	uint8_t ucIsExtChannel;
	uint8_t aucReserved[2];
};

struct CMD_RESET_BA_SCOREBOARD {
	uint8_t ucflag;
	uint8_t ucTID;
	uint8_t aucMacAddr[MAC_ADDR_LEN];
};

struct CMD_IPV4_NETWORK_ADDRESS {
	uint8_t aucIpAddr[IPV4_ADDR_LEN];
	uint8_t aucIpMask[IPV4_ADDR_LEN];
};

struct CMD_SET_NETWORK_ADDRESS_LIST {
	uint8_t ucBssIndex;
	uint8_t ucAddressCount;
	uint8_t ucVersion;
	uint8_t ucReserved[1];
	struct CMD_IPV4_NETWORK_ADDRESS arNetAddress[];
};

struct CMD_IPV6_NETWORK_ADDRESS {
	uint8_t aucIpAddr[IPV6_ADDR_LEN];
};

struct CMD_IPV6_NETWORK_ADDRESS_LIST {
	uint8_t ucBssIndex;
	uint8_t ucAddressCount;
	uint8_t ucReserved[2];
	struct CMD_IPV6_NETWORK_ADDRESS arNetAddress[];
};

struct CMD_SET_RRM_CAPABILITY {
	uint8_t ucCmdVer;
	uint8_t aucPadding0[1];
	uint16_t u2CmdLen;
	uint8_t ucBssIndex;
	uint8_t ucRrmEnable;
	uint8_t ucCapabilities[5];
	uint8_t aucPadding1[1];
	uint8_t aucPadding2[32];
};

struct CMD_SET_AP_CONSTRAINT_PWR_LIMIT {
	uint8_t ucCmdVer;
	uint8_t aucPadding0[1];
	uint16_t u2CmdLen;
	uint8_t ucBssIndex;
	uint8_t ucPwrSetEnable;
	int8_t cMaxTxPwr;
	int8_t cMinTxPwr;
	uint8_t aucPadding1[32];
};

struct CMD_SET_SCHED_SCAN_ENABLE {
	uint8_t ucSchedScanAct;
	uint8_t aucReserved[3];
};

struct CMD_MONITOR_SET_INFO {
	uint8_t ucEnable;
	uint8_t ucBand;
	uint8_t ucPriChannel;
	uint8_t ucSco;
	uint8_t ucChannelWidth;
	uint8_t ucChannelS1;
	uint8_t ucChannelS2;
	uint8_t ucBandIdx;
	uint16_t u2Aid;
	uint8_t fgDropFcsErrorFrame;
	uint8_t aucResv[5];
};

struct EVENT_MIC_ERR_INFO {
	uint32_t u4Flags;
	uint8_t aucStaAddr[6];
};

struct EVENT_ADD_KEY_DONE_INFO {
	uint8_t ucBSSIndex;
	uint8_t ucReserved;
	uint8_t aucStaAddr[6];
};

#if CFG_SUPPORT_BAR_DELAY_INDICATION
#define BAR_DELAY_INDICATION_BA_MAX 80
#endif

#if CFG_SUPPORT_BAR_DELAY_INDICATION
struct EVENT_STORED_BA_REQUEST {
	uint16_t u2SSN;
	uint8_t ucTid;
	uint8_t ucStaRecIdx;
	uint8_t ucStoredBARCount;
	uint8_t aucPadding[3];
};
#endif

#if CFG_SUPPORT_BAR_DELAY_INDICATION
struct EVENT_BAR_DELAY {
	uint8_t ucEvtVer;
	uint8_t ucBaNum;
	uint8_t aucPadding[2];
	struct EVENT_STORED_BA_REQUEST
		rBAR[BAR_DELAY_INDICATION_BA_MAX];
};
#endif

struct LINK_QUALITY {
	int8_t cRssi;
	int8_t cLinkQuality;
	uint16_t u2LinkSpeed;
	uint8_t ucMediumBusyPercentage;
	uint8_t ucIsLQ0Rdy;
	uint8_t aucReserve[2];
};

struct EVENT_LINK_QUALITY {
	struct LINK_QUALITY rLq[4];
};

struct EVENT_ACTIVATE_STA_REC {
	uint8_t aucMacAddr[6];
	uint8_t ucStaRecIdx;
	uint8_t ucBssIndex;
};

struct EVENT_BUG_REPORT {
	uint32_t u4BugReportVersion;
	uint32_t u4FWState;
	uint32_t u4FWScanState;
	uint32_t u4FWCnmState;
	uint32_t u4ReceivedBeaconCount;
	uint32_t u4ReceivedProbeResponseCount;
	uint32_t u4SentProbeRequestCount;
	uint32_t u4SentProbeRequestFailCount;
	uint32_t u4RoamingDebugFlag;
	uint32_t u4RoamingThreshold;
	uint32_t u4RoamingCurrentRcpi;
	uint32_t u4RFPriChannel;
	uint32_t u4RFChannelS1;
	uint32_t u4RFChannelS2;
	uint32_t u4RFChannelWidth;
	uint32_t u4RFSco;
	uint32_t u4BTProfile;
	uint32_t u4BTOn;
	uint32_t u4LTEOn;
	uint32_t u4LPTxUcPktNum;
	uint32_t u4LPRxUcPktNum;
	uint32_t u4LPPSProfile;
	uint32_t u4OfdmPdCnt;
	uint32_t u4CckPdCnt;
	uint32_t u4CckSigErrorCnt;
	uint32_t u4CckSfdErrorCnt;
	uint32_t u4OfdmSigErrorCnt;
	uint32_t u4OfdmTaqErrorCnt;
	uint32_t u4OfdmFcsErrorCnt;
	uint32_t u4CckFcsErrorCnt;
	uint32_t u4OfdmMdrdyCnt;
	uint32_t u4CckMdrdyCnt;
	uint32_t u4PhyCcaStatus;
	uint32_t u4WifiFastSpiStatus;
	uint32_t u4RxMdrdyCount;
	uint32_t u4RxFcsErrorCount;
	uint32_t u4RxbFifoFullCount;
	uint32_t u4RxMpduCount;
	uint32_t u4RxLengthMismatchCount;
	uint32_t u4RxCcaPrimCount;
	uint32_t u4RxEdCount;
	uint32_t u4LmacFreeRunTimer;
	uint32_t u4WtblReadPointer;
	uint32_t u4RmacWritePointer;
	uint32_t u4SecWritePointer;
	uint32_t u4SecReadPointer;
	uint32_t u4DmaReadPointer;
	uint32_t u4TxChannelIdleCount;
	uint32_t u4TxCcaNavTxTime;
};

struct EVENT_STATISTICS {
	union LARGE_INTEGER rTransmittedFragmentCount;
	union LARGE_INTEGER rMulticastTransmittedFrameCount;
	union LARGE_INTEGER rFailedCount;
	union LARGE_INTEGER rRetryCount;
	union LARGE_INTEGER rMultipleRetryCount;
	union LARGE_INTEGER rRTSSuccessCount;
	union LARGE_INTEGER rRTSFailureCount;
	union LARGE_INTEGER rACKFailureCount;
	union LARGE_INTEGER rFrameDuplicateCount;
	union LARGE_INTEGER rReceivedFragmentCount;
	union LARGE_INTEGER rMulticastReceivedFrameCount;
	union LARGE_INTEGER rFCSErrorCount;
	union LARGE_INTEGER rMdrdyCnt;
	union LARGE_INTEGER rChnlIdleCnt;
	uint32_t u4HwMacAwakeDuration;
	uint32_t au4Padding3[15];
	int32_t i4RadioIdx;
	uint32_t u4OnTime;
	uint32_t u4TxTime;
	uint32_t u4NumTxLevels;
	uint32_t u4RxTime;
	uint32_t u4OnTimeScan;
	uint32_t u4OnTimeNbd;
	uint32_t u4OnTimeGscan;
	uint32_t u4OnTimeRoamScan;
	uint32_t u4OnTimePnoScan;
	uint32_t u4OnTimeHs20;
	uint32_t u4NumChannels;
	uint32_t au4TxTimePerLevels[78];
	int32_t i4IfaceIdx;
	uint32_t u4NumWifiInterfaceLinkLayerInfo;
	uint32_t u4BeaconRx;
	uint32_t u4AverageTsfOffsetHigh;
	uint32_t u4AverageTsfOffsetLow;
	uint32_t u4LeakyApDetected;
	uint32_t u4LeakyApAvgNumFramesLeaked;
	uint32_t u4Leaky_ap_guard_time;
	uint32_t u4MgmtRx;
	uint32_t u4MgmtActionRx;
	uint32_t u4MgmtActionTx;
	int32_t i4RssiMgmt;
	int32_t i4RssiData;
	int32_t i4RssiAck;
	uint32_t u4NumAc;
	uint32_t u4NumPeers;
	int16_t i2Mode;
	uint8_t aucMacAddr[6];
	int16_t i2State;
	int16_t i2Roaming;
	uint32_t u4Capabilities;
	uint8_t aucSsid[33];
	uint8_t aucPadding4[1];
	uint8_t aucBssid[6];
	uint8_t aucPadding5[2];
	uint8_t aucApCountryStr[3];
	uint8_t aucPadding6[1];
	uint8_t aucCountryStr[3];
	uint8_t aucPadding7[1];
	uint32_t u4VoTxMpdu;
	uint32_t u4VoRxMpdu;
	uint32_t u4VoTxMcast;
	uint32_t u4VoRxMcast;
	uint32_t u4VoRxAmpdu;
	uint32_t u4VoTxAmpdu;
	uint32_t u4VoMpduLost;
	uint32_t u4VoRetries;
	uint32_t u4VoRetriesShort;
	uint32_t u4VoRetriesLong;
	uint32_t u4VoContentionTimeMin;
	uint32_t u4VoContentionTimeMax;
	uint32_t u4VoContentionTimeAvg;
	uint32_t u4VoContentionNumSamples;
	uint32_t u4ViTxMpdu;
	uint32_t u4ViRxMpdu;
	uint32_t u4ViTxMcast;
	uint32_t u4ViRxMcast;
	uint32_t u4ViRxAmpdu;
	uint32_t u4ViTxAmpdu;
	uint32_t u4ViMpduLost;
	uint32_t u4ViRetries;
	uint32_t u4ViRetriesShort;
	uint32_t u4ViRetriesLong;
	uint32_t u4ViContentionTimeMin;
	uint32_t u4ViContentionTimeMax;
	uint32_t u4ViContentionTimeAvg;
	uint32_t u4ViContentionNumSamples;
	uint32_t u4BeTxMpdu;
	uint32_t u4BeRxMpdu;
	uint32_t u4BeTxMcast;
	uint32_t u4BeRxMcast;
	uint32_t u4BeRxAmpdu;
	uint32_t u4BeTxAmpdu;
	uint32_t u4BeMpduLost;
	uint32_t u4BeRetries;
	uint32_t u4BeRetriesShort;
	uint32_t u4BeRetriesLong;
	uint32_t u4BeContentionTimeMin;
	uint32_t u4BeContentionTimeMax;
	uint32_t u4BeContentionTimeAvg;
	uint32_t u4BeContentionNumSamples;
	uint32_t u4BkTxMpdu;
	uint32_t u4BkRxMpdu;
	uint32_t u4BkTxMcast;
	uint32_t u4BkRxMcast;
	uint32_t u4BkRxAmpdu;
	uint32_t u4BkTxAmpdu;
	uint32_t u4BkMpduLost;
	uint32_t u4BkRetries;
	uint32_t u4BkRetriesShort;
	uint32_t u4BkRetriesLong;
	uint32_t u4BkContentionTimeMin;
	uint32_t u4BkContentionTimeMax;
	uint32_t u4BkContentionTimeAvg;
	uint32_t u4BkContentionNumSamples;
	uint32_t au4PaddingTail[128];
};

struct EVENT_SCAN_DONE {
	uint8_t ucSeqNum;
	uint8_t ucSparseChannelValid;
	struct CHANNEL_INFO rSparseChannel;
	uint8_t ucCompleteChanCount;
	uint8_t ucCurrentState;
	uint8_t ucScanDoneVersion;
	uint8_t ucReserved;
	uint32_t u4ScanDurBcnCnt;
	uint8_t fgIsPNOenabled;
	uint8_t aucReserving[3];
	uint8_t ucSparseChannelArrayValidNum;
	uint8_t aucReserved[3];
	uint8_t aucChannelNum[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	uint16_t au2ChannelIdleTime[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	uint8_t aucChannelBAndPCnt[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	uint8_t aucChannelMDRDYCnt[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	uint32_t u4ScanDurBcnCnt2G4;
	uint32_t u4ScanDurBcnCnt5G;
	uint16_t au2ChannelScanTime[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
};

struct EVENT_CH_PRIVILEGE {
	uint8_t ucBssIndex;
	uint8_t ucTokenID;
	uint8_t ucStatus;
	uint8_t ucPrimaryChannel;
	uint8_t ucRfSco;
	uint8_t ucRfBand;
	uint8_t ucRfChannelWidth;
	uint8_t ucRfCenterFreqSeg1;
	uint8_t ucRfCenterFreqSeg2;
	uint8_t ucReqType;
	uint8_t ucDBDCBand;
	uint8_t aucReserved;
	uint32_t u4GrantInterval;
	uint8_t aucReserved2[8];
};

enum ENUM_BEACON_TIMEOUT_REASON {
	BEACON_TIMEOUT_REASON_HW_BEACON_LOST_NONADHOC,
	BEACON_TIMEOUT_REASON_HW_BEACON_LOST_ADHOC,
	BEACON_TIMEOUT_REASON_HW_TSF_DRIFT,
	BEACON_TIMEOUT_REASON_NULL_FRAME_THRESHOLD,
	BEACON_TIMEOUT_REASON_AGING_THRESHOLD,
	BEACON_TIMEOUT_REASON_BSSID_BEACON_PEIROD_NOT_ILLIGAL,
	BEACON_TIMEOUT_REASON_CONNECTION_FAIL,
	BEACON_TIMEOUT_REASON_ALLOCAT_NULL_PKT_FAIL_THRESHOLD,
	BEACON_TIMEOUT_REASON_NO_TX_DONE_EVENT,
	BEACON_TIMEOUT_REASON_UNSPECIF_REASON,
	BEACON_TIMEOUT_REASON_SET_CHIP,
	BEACON_TIMEOUT_REASON_KEEP_SCAN_AP_MISS_CHECK_FAIL,
	BEACON_TIMEOUT_REASON_KEEP_UNCHANGED_LOW_RSSI_CHECK_FAIL,
	BEACON_TIMEOUT_REASON_NULL_FRAME_LIFE_TIMEOUT,
	BEACON_TIMEOUT_REASON_HIGH_PER,
	BEACON_TIMEOUT_REASON_NUM
};

struct EVENT_BSS_BEACON_TIMEOUT {
	uint8_t ucBssIndex;
	uint8_t ucReasonCode;
	uint8_t aucReserved[2];
};

struct EVENT_STA_AGING_TIMEOUT {
	uint8_t ucStaRecIdx;
	uint8_t aucReserved[3];
};

struct EVENT_NOA_TIMING {
	uint8_t ucIsInUse;
	uint8_t ucCount;
	uint8_t aucReserved[2];
	uint32_t u4Duration;
	uint32_t u4Interval;
	uint32_t u4StartTime;
};

struct EVENT_UPDATE_NOA_PARAMS {
	uint8_t ucBssIndex;
	uint8_t aucReserved[2];
	uint8_t ucEnableOppPS;
	uint16_t u2CTWindow;
	uint8_t ucNoAIndex;
	uint8_t ucNoATimingCount;
	struct EVENT_NOA_TIMING arEventNoaTiming[8];
};

struct EVENT_AP_OBSS_STATUS {
	uint8_t ucBssIndex;
	uint8_t ucObssErpProtectMode;
	uint8_t ucObssHtProtectMode;
	uint8_t ucObssGfOperationMode;
	uint8_t ucObssRifsOperationMode;
	uint8_t ucObssBeaconForcedTo20M;
	uint8_t aucReserved[2];
};

struct EVENT_RDD_STATUS {
	uint8_t ucRddStatus;
	uint8_t aucReserved[3];
};

struct EVENT_NLO_DONE {
	uint8_t ucSeqNum;
	uint8_t ucStatus;
	uint8_t aucReserved[2];
};

struct EVENT_BUILD_DATE_CODE {
	uint8_t aucDateCode[16];
};

struct EVENT_BATCH_RESULT_ENTRY {
	uint8_t aucBssid[MAC_ADDR_LEN];
	uint8_t aucSSID[ELEM_MAX_LEN_SSID];
	uint8_t ucSSIDLen;
	int8_t cRssi;
	uint32_t ucFreq;
	uint32_t u4Age;
	uint32_t u4Dist;
	uint32_t u4Distsd;
};

struct EVENT_BATCH_RESULT {
	uint8_t ucScanCount;
	uint8_t aucReserved[3];
	struct EVENT_BATCH_RESULT_ENTRY arBatchResult[12];
};

enum ENUM_PARAM_MEDIA_STATE {
	MEDIA_STATE_CONNECTED = 0,
	MEDIA_STATE_DISCONNECTED,
	MEDIA_STATE_ROAMING_DISC_PREV,
	MEDIA_STATE_TO_BE_INDICATED,
	MEDIA_STATE_NUM
};

struct CMD_EVENT_LOG_UI_INFO {
	uint32_t ucVersion;
	uint32_t ucLogLevel;
	uint8_t aucReserved[4];
};

struct CMD_WAKE_HIF {
	uint8_t ucWakeupHif;
	uint8_t ucGpioPin;
	uint8_t ucTriggerLvl;
	uint8_t aucResv1[1];
	uint32_t u4GpioInterval;
	uint8_t aucResv2[4];
};

struct WOW_PORT {
	uint8_t ucIPv4UdpPortCnt;
	uint8_t ucIPv4TcpPortCnt;
	uint8_t ucIPv6UdpPortCnt;
	uint8_t ucIPv6TcpPortCnt;
	uint16_t ausIPv4UdpPort[MAX_TCP_UDP_PORT];
	uint16_t ausIPv4TcpPort[MAX_TCP_UDP_PORT];
	uint16_t ausIPv6UdpPort[MAX_TCP_UDP_PORT];
	uint16_t ausIPv6TcpPort[MAX_TCP_UDP_PORT];
};

struct CMD_WOWLAN_PARAM {
	uint8_t ucCmd;
	uint8_t ucDetectType;
	uint16_t u2FilterFlag;
	uint8_t ucScenarioID;
	uint8_t ucBlockCount;
	uint8_t aucReserved1[2];
	struct CMD_WAKE_HIF astWakeHif[2];
	struct WOW_PORT stWowPort;
	uint8_t aucReserved2[32];
};

struct CMD_DOMAIN_CHANNEL {
	uint16_t u2ChNum;
	uint8_t aucPadding[2];
	uint32_t eFlags;
};

struct CMD_DOMAIN_ACTIVE_CHANNEL_LIST {
	uint8_t u1ActiveChNum2g;
	uint8_t u1ActiveChNum5g;
	uint8_t u1ActiveChNum6g;
	uint8_t aucPadding[1];
	struct CMD_DOMAIN_CHANNEL arChannels[0];
};

struct CMD_SET_DOMAIN_INFO_V2 {
	uint32_t u4CountryCode;
	uint8_t uc2G4Bandwidth;
	uint8_t uc5GBandwidth;
	uint8_t uc6GBandwidth;
	uint8_t aucPadding[1];
	struct CMD_DOMAIN_ACTIVE_CHANNEL_LIST arActiveChannels;
};

#define SINGLE_SKU_PARAM_NUM 69

struct CMD_SKU_TABLE_TYPE {
	int8_t i1PwrLimit[SINGLE_SKU_PARAM_NUM];
};

struct CMD_TXPOWER_CHANNEL_POWER_LIMIT_PER_RATE {
	uint8_t u1CentralCh;
	struct CMD_SKU_TABLE_TYPE aucTxPwrLimit;
};

struct CMD_SET_TXPOWER_COUNTRY_TX_POWER_LIMIT_PER_RATE {
	uint8_t ucCmdVer;
	uint8_t aucPadding0[1];
	uint16_t u2CmdLen;
	uint8_t ucNum;
	uint8_t eBand;
	uint8_t bCmdFinished;
	uint8_t aucPadding1[1];
	uint32_t u4CountryCode;
	uint8_t aucPadding2[32];
	struct CMD_TXPOWER_CHANNEL_POWER_LIMIT_PER_RATE rChannelPowerLimit[];
};

#define POWER_LIMIT_TXBF_BACKOFF_PARAM_NUM 6

struct CMD_TXPWR_TXBF_CHANNEL_BACKOFF {
	uint8_t ucCentralCh;
	uint8_t aucPadding0[1];
	int8_t acTxBfBackoff[POWER_LIMIT_TXBF_BACKOFF_PARAM_NUM];
};

#define CMD_POWER_LIMIT_TABLE_SUPPORT_CHANNEL_NUM 64

struct CMD_TXPWR_TXBF_SET_BACKOFF {
	uint8_t ucCmdVer;
	uint8_t aucPadding0[1];
	uint16_t u2CmdLen;
	uint8_t ucNum;
	uint8_t ucBssIdx;
	uint8_t aucPadding1[2];
	uint8_t aucPadding2[32];
	struct CMD_TXPWR_TXBF_CHANNEL_BACKOFF
		rChannelTxBfBackoff[CMD_POWER_LIMIT_TABLE_SUPPORT_CHANNEL_NUM];
};

enum ENUM_EVENT_OPMODE_CHANGE_REASON {
	EVENT_OPMODE_CHANGE_REASON_DBDC = 0,
	EVENT_OPMODE_CHANGE_REASON_COANT = 1,
	EVENT_OPMODE_CHANGE_REASON_DBDC_SCAN = 2,
	EVENT_OPMODE_CHANGE_REASON_SMARTGEAR = 3,
	EVENT_OPMODE_CHANGE_REASON_COEX = 4,
	EVENT_OPMODE_CHANGE_REASON_SMARTGEAR_1T2R = 5,
	EVENT_OPMODE_CHANGE_REASON_ANT_CTRL = 6,
	EVENT_OPMODE_CHANGE_REASON_ANT_CTRL_1T2R = 7,
};

struct EVENT_OPMODE_CHANGE {
	uint8_t ucEvtVer;
	uint8_t aucPadding0[1];
	uint16_t u2EvtLen;
	uint8_t ucBssBitmap;
	uint8_t ucEnable;
	uint8_t ucOpTxNss;
	uint8_t ucOpRxNss;
	uint8_t ucReason;
	uint8_t aucPadding1[63];
};

struct CMD_SET_FORCE_RTS {
	uint8_t ucForceRtsEn;
	uint8_t ucRtsPktNum;
	uint8_t aucReserved[2];
};

struct CMD_FAST_PATH {
	uint8_t ucCmdVer;
	uint8_t aucPadding0[1];
	uint16_t u2CmdLen;
	uint8_t aucOwnMac[6];
	uint16_t u2RandomNum;
	uint32_t u4Keybitmap[4];
};

struct EVENT_FAST_PATH {
	uint8_t ucEvtVer;
	uint8_t aucPadding0[1];
	uint16_t u2EvtLen;
	uint16_t u2Mic;
	uint8_t ucKeynum;
	uint8_t ucKeyBitmapMatchStatus;
};

#endif
