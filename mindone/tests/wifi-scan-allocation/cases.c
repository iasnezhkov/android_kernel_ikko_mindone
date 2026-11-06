// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void setup(enum ENUM_AIS_STATE state)
{
    memset(&adapter, 0, sizeof(adapter));
    memset(&fsm, 0, sizeof(fsm));
    memset(&settings, 0, sizeof(settings));
    memset(&bss, 0, sizeof(bss));
    memset(&rm, 0, sizeof(rm));
    memset(&glue, 0, sizeof(glue));
    fsm.eCurrentState = state;
    fsm.u2SeqNumOfScanReport = AIS_SCN_REPORT_SEQ_NOT_SET;
    adapter.prGlueInfo = &glue;
    adapter.fgEnOnlineScan = TRUE;
    adapter.rWifiVar.ucScanNoApRecover = TRUE;
    adapter.rWifiVar.ucScanNoApRecoverTh = 3;
    bss.eConnectionState = state == AIS_STATE_NORMAL_TR;
    allocation_failures = allocations = callbacks = aborted = reset_triggers = firmware_cancels = timer_starts = pending_requests = 0;
    completion = NULL;
}

static void request(void)
{
    assert(request_scan() == 0);
}

static void timeout_scan(void)
{
    assert(fsm.rScanDoneTimer.pending);
    fsm.rScanDoneTimer.pending = 0;
    aisFsmRunEventScanDoneTimeOut(&adapter, 0);
}

static void finish_scan(void)
{
    assert(completion);
    struct MSG_HDR *message = completion;
    completion = NULL;
    aisFsmRunEventScanDone(&adapter, message);
}

int main(int argc, char **argv)
{
    unsigned checks = 0;
    for (unsigned variant = 0; variant < 8; variant++) {
        unsigned online = variant & 1;
        enum ENUM_AIS_STATE initial = online ? AIS_STATE_NORMAL_TR : AIS_STATE_IDLE;
        setup(initial);
        fsm.ucSeqNumOfScanReq = variant & 4 ? 255 : 0;
        allocation_failures = 1;
        request();
        assert(settings.fgIsScanReqIssued);
        assert(!fsm.fgIsScanning);
        assert(fsm.u2SeqNumOfScanReport == AIS_SCN_REPORT_SEQ_NOT_SET);
        assert(adapter.rWifiVar.rScanInfo.eCurrentState == SCAN_STATE_IDLE);
        assert(fsm.eCurrentState == (online ? AIS_STATE_ONLINE_SCAN : AIS_STATE_SCAN));
        assert(timer_starts == 1);
        allocation_failures = variant & 2 ? 1 : 0;
        timeout_scan();
        assert(adapter.rWifiVar.rScanInfo.ucScnTimeoutTimes == 1);
        assert(reset_triggers == 0);
        assert(firmware_cancels == 0);
        assert(completion == NULL);
        assert(!fsm.rScanDoneTimer.pending);
        if (argc > 1) {
            assert(glue.prScanRequest);
            assert(settings.fgIsScanReqIssued);
            assert(callbacks == 0);
            assert(request_scan() == -EBUSY);
            checks++;
            continue;
        }
        assert(glue.prScanRequest == NULL);
        assert(!settings.fgIsScanReqIssued);
        assert(callbacks == 1 && aborted);
        assert(fsm.eCurrentState == initial);
        request();
        assert(fsm.fgIsScanning);
        assert(fsm.ucSeqNumOfScanReq == (variant & 4 ? 0 : 1));
        timeout_scan();
        assert(callbacks == 1);
        assert(completion);
        finish_scan();
        assert(callbacks == 2);
        assert(glue.prScanRequest == NULL);
        assert(!settings.fgIsScanReqIssued);
        assert(fsm.eCurrentState == initial);
        checks++;
    }
    if (argc == 1) {
        for (unsigned online = 0; online < 2; online++) {
            setup(online ? AIS_STATE_NORMAL_TR : AIS_STATE_IDLE);
            request();
            assert(fsm.fgIsScanning);
            scnFsmGenerateScanDoneMsg(&adapter, MID_AIS_SCN_SCAN_REQ_V2, fsm.ucSeqNumOfScanReq, 0, SCAN_STATUS_DONE);
            scnFsmSteps(&adapter, SCAN_STATE_IDLE);
            finish_scan();
            assert(callbacks == 1 && !aborted);
            assert(glue.prScanRequest == NULL);
            assert(!fsm.rScanDoneTimer.pending);
            assert(firmware_cancels == 0);
            checks++;
        }
    }
    if (argc == 1) {
        for (unsigned guard = 0; guard < 4; guard++) {
            setup(AIS_STATE_IDLE);
            allocation_failures = 1;
            request();
            if (guard == 0)
                fsm.eCurrentState = AIS_STATE_LOOKING_FOR;
            else if (guard == 1)
                settings.fgIsScanReqIssued = FALSE;
            else if (guard == 2)
                fsm.u2SeqNumOfScanReport = 0;
            else
                fsm.fgIsScanning = TRUE;
            timeout_scan();
            assert(callbacks == 0);
            assert(glue.prScanRequest);
            assert(completion == NULL);
            checks++;
        }
    }
    printf("%u scenarios passed (%s)\n", checks, argc > 1 ? "baseline retained request after timeout" : "allocation failure recovered and normal scans completed");
    return 0;
}
