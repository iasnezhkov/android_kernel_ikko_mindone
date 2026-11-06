// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct ADAPTER adapter;
static struct P2P_DEV_FSM_INFO fsm;
static struct SW_RFB rfb;
static unsigned checks;

static void setup(size_t size)
{
    memset(&adapter, 0, sizeof(adapter));
    memset(&fsm, 0, sizeof(fsm));
    memset(&rfb, 0, sizeof(rfb));
    responses = aborts = notifications = buffered = indicated = 0;
    last_subtype = 255;
    buffer_requested = ap_mode = 0;
    adapter.rWifiVar.prP2pDevFsmInfo = &fsm;
    adapter.u4OsPacketFilter = PARAM_PACKET_FILTER_ACTION_FRAME;
    rfb.pvHeader = calloc(size ? size : 1, 1);
    assert(rfb.pvHeader);
    rfb.u2PacketLen = size;
    rfb.ucChnlNum = 6;
    const uint8_t body[] = {4, 9, 0x50, 0x6f, 0x9a, 9, 0};
    for (size_t i = 24; i < size && i < 31; i++)
        ((uint8_t *)rfb.pvHeader)[i] = body[i - 24];
}

static void verify(unsigned response, unsigned abort, unsigned notify,
                   unsigned buffer, unsigned indicate)
{
    assert(responses == response);
    assert(aborts == abort);
    assert(notifications == notify);
    assert(buffered == buffer);
    assert(indicated == indicate);
    checks++;
    free(rfb.pvHeader);
}

int main(int argc, char **argv)
{
    _Static_assert(offsetof(struct WLAN_ACTION_FRAME, ucActionDetails) == 26, "action layout");
    _Static_assert(offsetof(struct WLAN_PUBLIC_VENDOR_ACTION_FRAME, ucOUI) == 26, "OUI layout");
    _Static_assert(offsetof(struct WLAN_PUBLIC_VENDOR_ACTION_FRAME, ucSubType) == 29, "type layout");
    _Static_assert(sizeof(struct WLAN_PUBLIC_VENDOR_ACTION_FRAME) == 31, "vendor layout");
    if (argc > 1) {
        setup(31);
        p2pFuncValidateRxActionFrame(&adapter, &rfb, 0, 0);
        free(rfb.pvHeader);
        return 0;
    }
    for (size_t size = 0; size <= 40; size++) {
        setup(size);
        p2pFuncValidateRxActionFrame(&adapter, &rfb, 0, 0);
        verify(size >= 31, size >= 31, 0, 0, size >= 26);
    }
    setup(31);
    p2pFuncValidateRxActionFrame(NULL, &rfb, 0, 0);
    p2pFuncValidateRxActionFrame(&adapter, NULL, 0, 0);
    free(rfb.pvHeader);
    rfb.pvHeader = NULL;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 0, 0);
    verify(0, 0, 0, 0, 0);
    setup(31);
    ((uint8_t *)rfb.pvHeader)[26] = 0;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 1);
    setup(31);
    ((uint8_t *)rfb.pvHeader)[24] = 3;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 1);
    setup(31);
    ((uint8_t *)rfb.pvHeader)[25] = 1;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 1);
    setup(31);
    ((uint8_t *)rfb.pvHeader)[29] = 255;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 1);
    for (int ap = 0; ap <= 1; ap++) {
        setup(31);
        ap_mode = ap;
        ((uint8_t *)rfb.pvHeader)[29] = DPP_OUI_TYPE;
        p2pFuncValidateRxActionFrame(&adapter, &rfb, 0, 1);
        verify(0, 0, 0, 0, ap);
    }
    setup(31);
    ((uint8_t *)rfb.pvHeader)[30] = 7;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    assert(last_subtype == 7);
    verify(1, 0, 1, 0, 1);
    setup(31);
    buffer_requested = 1;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(1, 1, 1, 1, 0);
    setup(31);
    adapter.u4OsPacketFilter = 0;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 0, 0);
    verify(1, 1, 0, 0, 0);
    setup(31);
    fsm.eCurrentState = P2P_DEV_STATE_REQING_CHANNEL;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 0);
    setup(31);
    fsm.eCurrentState = P2P_DEV_STATE_CHNL_ON_HAND;
    fsm.rChnlReqInfo.ucReqChnlNum = 1;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(0, 0, 0, 0, 0);
    setup(31);
    fsm.eCurrentState = P2P_DEV_STATE_CHNL_ON_HAND;
    fsm.rChnlReqInfo.ucReqChnlNum = 6;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(1, 1, 1, 0, 1);
    setup(31);
    adapter.rWifiVar.prP2pDevFsmInfo = NULL;
    p2pFuncValidateRxActionFrame(&adapter, &rfb, 1, 0);
    verify(1, 1, 1, 0, 1);
    printf("%u scenarios passed\n", checks);
    return 0;
}
