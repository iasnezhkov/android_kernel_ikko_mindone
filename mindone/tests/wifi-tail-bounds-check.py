# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference')
    parser.add_argument('--expect-trap', action='store_true')
    args = parser.parse_args()
    tree = Path(__file__).resolve().parents[2]
    base = 'mindone/modules/wlan_drv_gen4m_6789/'
    sources = {}
    environment = {**os.environ, 'GIT_NO_LAZY_FETCH': '1'}
    def stored(name):
        return subprocess.run(['git', 'cat-file', '-e', args.reference + ':' + base + name], cwd=tree,
                              capture_output=True, timeout=20, env=environment).returncode == 0
    wsys = 'include/mindone_wsys_cmd.h'
    if args.reference and not stored(wsys):
        wsys = 'include/wsys_cmd_handler_fw.h'
    for name in ('include/mgmt/rlm_domain.h', 'include/link.h', 'include/wlan_oid.h',
                 wsys, 'os/linux/include/gl_wext.h',
                 'mgmt/rlm_domain.c', 'os/linux/gl_wext.c'):
        sources['wsys' if name == wsys else name] = subprocess.run(['git', 'show', args.reference + ':' + base + name],
            cwd=tree, capture_output=True, text=True, check=True, timeout=20,
            env=environment).stdout if args.reference else (tree / base / name).read_text()
    fragments = {}
    def extract(name, pattern, source):
        matches = list(re.finditer(pattern, sources[source], re.M | re.S))
        if len(matches) != 1:
            raise ValueError('Ambiguous fragment: ' + name)
        fragments[name] = matches[0].group(0)
        return fragments[name]
    code = '#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <errno.h>\n'
    code += '#define CFG_SUPPORT_DYNAMIC_PWR_LIMIT_ANT_TAG 1\n#define CFG_SUPPORT_WIFI_6G 0\n#define CFG_SUPPORT_DYNA_TX_PWR_CTRL_11AC_V2_SETTING 0\n#define MAX_TX_PWR_CTRL_ELEMENT_NAME_SIZE 16\n#define PARAM_MAC_ADDR_LEN 6\n#define PARAM_MAX_LEN_RATES_EX 16\n#define ELEM_MAX_LEN_SSID 32\n#define OFFSET_OF offsetof\ntypedef uint8_t u_int8_t;\n'
    groups = {
        'include/link.h': ('struct LINK_ENTRY',),
        'include/mgmt/rlm_domain.h': ('enum ENUM_TX_POWER_CTRL_TYPE', 'enum ENUM_TX_POWER_CTRL_VALUE_SIGN',
            'enum ENUM_TX_POWER_CTRL_CHANNEL_TYPE', 'enum ENUM_POWER_LIMIT', 'enum ENUM_POWER_LIMIT_HE',
            'enum ENUM_POWER_ANT_TAG', 'enum ENUM_POWER_ANT_PARA', 'struct TX_PWR_CTRL_CHANNEL_SETTING',
            'struct TX_PWR_CTRL_ANT_SETTING', 'struct TX_PWR_CTRL_ELEMENT'),
        'wsys': ('struct PARAM_SSID',),
        'include/wlan_oid.h': ('enum ENUM_PARAM_OP_MODE', 'enum ENUM_PARAM_NETWORK_TYPE',
            'struct PARAM_802_11_CONFIG_FH', 'struct PARAM_802_11_CONFIG', 'struct PARAM_BSSID_EX'),
        'os/linux/include/gl_wext.h': ('struct PARAM_FIXED_IEs',)}
    for source, names in groups.items():
        for name in names:
            code += extract(name, '^' + name + r' \{.*?^\};', source) + '\n'
    alloc = extract('allocation', r'\tu4MemSize \+= .*?sizeof\(struct TX_PWR_CTRL_CHANNEL_SETTING\);', 'mgmt/rlm_domain.c')
    access = extract('tx_access', r'\t+prChlSetting = prCurElement->rChlSettingList \+ j;', 'mgmt/rlm_domain.c')
    code += 'static __attribute__((noinline)) void tx(unsigned count) { uint8_t ucSettingCount=count; uint32_t u4MemSize=sizeof(struct TX_PWR_CTRL_ELEMENT);\n'
    code += alloc + '\nstruct TX_PWR_CTRL_ELEMENT *prCurElement=calloc(1,u4MemSize); assert(prCurElement); struct TX_PWR_CTRL_CHANNEL_SETTING *prChlSetting;\n'
    code += 'for (unsigned j=0;j<count;j++) {\n' + access + '\nprChlSetting->channelParam[0]=j; assert((uint8_t*)(prChlSetting+1)<=(uint8_t*)prCurElement+u4MemSize); }\n'
    code += 'for (unsigned j=0;j<count;j++) {\n' + access + '\nassert(prChlSetting->channelParam[0]==(uint8_t)j); } free(prCurElement); }\n'
    if args.reference:
        pointers = re.findall(r'prBss->aucIEs \+\s*sizeof\(struct PARAM_FIXED_IEs\)', sources['os/linux/gl_wext.c'])
        if not pointers or len({re.sub(r'\s+', '', value) for value in pointers}) != 1:
            raise ValueError('Missing or inconsistent baseline WEXT pointers')
        old = pointers[0]
        fragments['wext_pointer'] = old
        code += 'static __attribute__((noinline)) int wext_get_variable_ies(struct PARAM_BSSID_EX *prBss, uint32_t available, uint8_t **output) { *output=' + old + '; return 0; }\n'
    else:
        code += extract('wext_helper', r'^static int wext_get_variable_ies\(.*?^\}', 'os/linux/gl_wext.c') + '\n'
    code += (Path(__file__).with_name('wifi-tail-bounds') / 'cases.c').read_text()
    results = []
    with tempfile.TemporaryDirectory(prefix='wifi-tail-bounds-') as temporary:
        path = Path(temporary) / 'test'
        path.with_suffix('.c').write_text(code)
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-fstrict-flex-arrays=3', '-fsanitize=address,undefined', '-fsanitize-trap=bounds',
            '-fno-sanitize-recover=all', str(path.with_suffix('.c')), '-o', str(path)], check=True, timeout=40)
        for mode in ('tx', 'wext'):
            result = subprocess.run([str(path), mode], capture_output=True, text=True, timeout=15)
            passed = result.returncode in (-signal.SIGILL, -signal.SIGTRAP) if args.expect_trap else result.returncode == 0
            results.append(dict(mode=mode, passed=passed, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
    print(json.dumps(dict(reference=args.reference, results=results, fragments={k:hashlib.sha256(v.encode()).hexdigest() for k,v in fragments.items()}), indent=2))
    raise SystemExit(0 if all(r['passed'] for r in results) else 1)


if __name__ == '__main__':
    main()
