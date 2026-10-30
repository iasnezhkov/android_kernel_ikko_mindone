/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
struct P2P_DEV_FSM_INFO {
    int eCurrentState;
    struct { uint8_t ucReqChnlNum; } rChnlReqInfo;
};
struct SW_RFB {
    void *pvHeader;
    uint16_t u2PacketLen;
    uint8_t ucChnlNum;
};
struct ADAPTER {
    struct {
        struct P2P_DEV_FSM_INFO *prP2pDevFsmInfo;
        void *prP2PConnSettings[2];
    } rWifiVar;
    unsigned u4OsPacketFilter;
    void *prGlueInfo;
};
static unsigned responses, aborts, notifications, buffered, indicated;
static uint8_t last_subtype;
static int buffer_requested, ap_mode;
static void p2pProcessActionResponse(struct ADAPTER *a, uint8_t subtype)
{
    responses++;
    last_subtype = subtype;
}
static void p2pFunAbortOngoingScan(struct ADAPTER *a) { aborts++; }
static void p2pDevFsmNotifyP2pRx(struct ADAPTER *a, uint8_t subtype, u_int8_t *buffer)
{
    notifications++;
    assert(subtype == last_subtype);
    *buffer = buffer_requested;
}
static int p2pFuncIsAPMode(void *settings) { return ap_mode; }
static void p2pFunBufferP2pActionFrame(struct ADAPTER *a, struct SW_RFB *r, uint8_t role)
{
    buffered++;
}
static void kalP2PIndicateRxMgmtFrame(struct ADAPTER *a, void *glue, struct SW_RFB *r,
                                   u_int8_t device, uint8_t role)
{
    indicated++;
}
