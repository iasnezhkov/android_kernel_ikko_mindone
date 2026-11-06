# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--reference')
    parser.add_argument('--expect-stuck', action='store_true')
    args = parser.parse_args()
    base = 'mindone/modules/wlan_drv_gen4m_6789/'
    sources = {}
    for name in ('mgmt/ais_fsm.c', 'mgmt/scan_fsm.c', 'common/wlan_oid.c', 'os/linux/gl_kal.c', 'os/linux/gl_cfg80211.c'):
        if args.reference:
            sources[name] = subprocess.run(['git', 'show', args.reference + ':' + base + name],
                cwd=args.tree, env={**os.environ, 'GIT_NO_LAZY_FETCH': '1'}, capture_output=True,
                text=True, check=True, timeout=30).stdout
        else:
            sources[name] = (args.tree / base / name).read_text()
    fragments = {}
    def get(name, pattern, source):
        matches = list(re.finditer(pattern, sources[source], re.M | re.S))
        if len(matches) != 1:
            raise ValueError('Ambiguous source fragment: ' + name)
        fragments[name] = matches[0].group(0)
        return fragments[name]
    functions = []
    for name, source in (
        ('wlanoidSetBssidListScanAdv', 'common/wlan_oid.c'),
        ('aisFsmScanRequestAdv', 'mgmt/ais_fsm.c'),
        ('aisFsmStateAbort_SCAN', 'mgmt/ais_fsm.c'),
        ('aisFsmRunEventScanDoneTimeOut', 'mgmt/ais_fsm.c'),
        ('scnFsmMsgAbort', 'mgmt/scan_fsm.c'),
        ('scnFsmGenerateScanDoneMsg', 'mgmt/scan_fsm.c'),
        ('aisFsmRunEventScanDone', 'mgmt/ais_fsm.c'),
        ('scnDoScanTimeoutRecoveryCheck', 'mgmt/scan_fsm.c')):
        functions.append(get(name, r'^(?:static )?(?:void|uint32_t)\s+' + name + r'\([^;]*?\)\s*\{.*?^\}', source))
    allocation = get('scan_allocation', r'\t\t\tprScanReqMsg =\n.*?prAisBssInfo->ucBssIndex;', 'mgmt/ais_fsm.c')
    transition = get('state_assignment', r'^\t\tprAisFsmInfo->eCurrentState = eNextState;', 'mgmt/ais_fsm.c')
    completion = get('cfg80211_completion', r'\t\tGLUE_ACQUIRE_SPIN_LOCK\(prGlueInfo, SPIN_LOCK_NET_DEV\);\n\t\tif \(prGlueInfo->prScanRequest != NULL\).*?GLUE_RELEASE_SPIN_LOCK\(prGlueInfo, SPIN_LOCK_NET_DEV\);', 'os/linux/gl_kal.c')
    pending = get('cfg80211_pending_guard', r'^\tif \(prGlueInfo->prScanRequest != NULL\) \{.*?^\t\}', 'os/linux/gl_cfg80211.c')
    issue = get('cfg80211_issue_request', r'^\tprGlueInfo->prScanRequest = request;.*?^\treturn 0;', 'os/linux/gl_cfg80211.c')
    fixture = Path(__file__).with_name('wifi-scan-allocation')
    code = (fixture / 'prelude.h').read_text()
    code += '\n'.join(functions)
    code += '\nstatic void allocate_scan(struct ADAPTER *prAdapter, uint8_t ucBssIndex) {\n'
    code += 'struct AIS_FSM_INFO *prAisFsmInfo = &fsm; struct BSS_INFO *prAisBssInfo = &bss; struct MSG_SCN_SCAN_REQ_V2 *prScanReqMsg; uint16_t u2ScanIELen = 0;\n'
    code += allocation + '\nstart_scan(prAdapter, prScanReqMsg);\n}\n'
    code += '\nstatic void aisFsmSteps(struct ADAPTER *prAdapter, enum ENUM_AIS_STATE eNextState, uint8_t ucBssIndex) { struct AIS_FSM_INFO *prAisFsmInfo = &fsm;\n'
    code += transition + '\nif (eNextState == AIS_STATE_SCAN || eNextState == AIS_STATE_ONLINE_SCAN) allocate_scan(prAdapter, ucBssIndex);\n}\n'
    code += '\nstatic void kalScanDone(struct GLUE_INFO *prGlueInfo, uint8_t ucBssIndex, uint32_t status) { uint8_t fgScanAborted = status != WLAN_STATUS_SUCCESS;\n'
    code += completion + '\n}\n'
    code += '\nstatic int request_scan(void) { struct GLUE_INFO *prGlueInfo = &glue; struct PARAM_SCAN_REQUEST_ADV *prScanRequest; void *request = &adapter; uint32_t rStatus, u4BufLen;\n'
    code += pending + '\nprScanRequest = calloc(1, sizeof(*prScanRequest)); assert(prScanRequest);\n'
    code += issue + '\n}\n'
    code += (fixture / 'cases.c').read_text()
    with tempfile.TemporaryDirectory(prefix='wifi-scan-allocation-') as temp:
        path = Path(temp) / 'check'
        path.with_suffix('.c').write_text(code)
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-but-set-variable', '-Wno-unused-variable', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(path.with_suffix('.c')), '-o', str(path)], check=True, timeout=40)
        result = subprocess.run([str(path)] + (['baseline'] if args.expect_stuck else []),
                                capture_output=True, text=True, timeout=15)
        output = dict(passed=result.returncode == 0, reference=args.reference,
                      expected_stuck=args.expect_stuck, returncode=result.returncode,
                      stdout=result.stdout, stderr=result.stderr,
                      extracted_sha256={name: hashlib.sha256(value.encode()).hexdigest()
                                        for name, value in fragments.items()})
        print(json.dumps(output, indent=2))
        raise SystemExit(0 if output['passed'] else 1)


if __name__ == '__main__':
    main()
