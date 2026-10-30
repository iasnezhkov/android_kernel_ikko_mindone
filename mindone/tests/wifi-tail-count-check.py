# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def block(source, start):
    begin = source.index(start)
    opening = source.index('{', begin)
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[begin:end + 1]
    raise ValueError('Incomplete source block')


def fragment(source):
    function = source.split('struct TX_PWR_CTRL_ELEMENT *txPwrCtrlStringToStruct(', 1)[1]
    counter = function.split('skipLabel:', 1)[1]
    return block(counter, 'while (pcContCur <= pcContEnd)') + '\n' + block(counter, 'if (ucSettingCount == 0)')


def run(source, compiler):
    code = '#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <stddef.h>\n#define DBGLOG(...) ((void)0)\n'
    code += 'static void *count_settings(char *input, size_t length, unsigned *output) { char *pcContCur=input, *pcContEnd=input+length; uint8_t ucSettingCount=0;\n'
    code += source + '\n*output=ucSettingCount; return output; }\n'
    code += '''int main(void) {
    char input[1025];
    for (unsigned n=0; n<=1024; n++) {
        memset(input, '[', n);
        input[n]=0;
        unsigned count=0xdeadbeef;
        void *accepted=count_settings(input,n,&count);
        int valid=n>0 && n<=255;
        if ((valid && (!accepted || count!=n)) || (!valid && (accepted || count!=0xdeadbeef))) {
            fprintf(stderr,"count mismatch: n=%u accepted=%d count=%u\\n",n,accepted!=NULL,count);
            return 2;
        }
    }
    puts("PASS 1025 source-derived count boundaries");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='mindone-wifi-count-') as directory:
        root = Path(directory)
        path = root / 'check.c'
        binary = root / 'check'
        path.write_text(code)
        subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(path), '-o', str(binary)], check=True, timeout=40)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
    return {'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
            'fragment_sha256': hashlib.sha256(source.encode()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description='Check the exact WLAN power-setting counter and pre-allocation rejection with host stubs')
    parser.add_argument('--compiler', default='cc')
    parser.add_argument('--baseline', default='67bc07d4f3aa')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    relative = 'mindone/modules/wlan_drv_gen4m_6789/mgmt/rlm_domain.c'
    source = (root / relative).read_text()
    current = fragment(source)
    baseline = subprocess.run(['git', '-C', str(root), 'show', args.baseline + ':' + relative],
                              capture_output=True, text=True, check=True, timeout=20,
                              env={**os.environ, 'GIT_NO_LAZY_FETCH': '1'}).stdout
    variants = {'candidate': current, 'baseline': fragment(baseline),
                'missing_overflow_guard': current.replace('if (ucSettingCount == 0xff)', 'if (0)'),
                'reject_valid_255': current.replace('if (ucSettingCount == 0xff)', 'if (ucSettingCount == 0xfe)'),
                'accept_empty': current.replace('if (ucSettingCount == 0)', 'if (0)')}
    if len(set(variants.values())) != len(variants) or variants['baseline'] == current:
        raise ValueError('Missing or ambiguous source mutation')
    rows = {}
    for name, value in variants.items():
        checked = run(value, args.compiler)
        expected = 0 if name == 'candidate' else 2
        if checked['returncode'] != expected or (expected and 'count mismatch:' not in checked['stderr']):
            raise ValueError('Unexpected count control: ' + name + ': ' + str(checked))
        rows[name] = checked
    report = {'status': 'pass', 'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
              'cases': 1025, 'variants': rows,
              'limit': 'Exact counter and empty-guard slices with log stubs; no full profile parser, native module, firmware or device scenario.'}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
