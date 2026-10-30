# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MODULE = Path('mindone/modules/wlan_drv_gen4m_6789')
PATHS = [MODULE / 'os/linux/gl_vendor.c', MODULE / 'nic/nic_cmd_event.c']
CASES = [('set', n, 0, 'valid') for n in (0, 1, 568, 1487, 1488, 1489, 2048)]
CASES += [('set', n, -22, 'valid') for n in (2049, 3500)]
CASES += [('set', 568, -22, mode) for mode in ('shortheader', 'truncatedattr', 'wrongtype')]
CASES += [('set', 2048, -14, mode) for mode in ('fail1', 'fail2')]
CASES += [('read', n, 0, 'valid') for n in (1, 568, 1488, 1489, 2048)]
CASES += [('read', 568, 0, 'count0'), ('read', 2048, 0, 'consumerbound')]
CASES += [('read', 0, -14, 'valid')]
CASES += [('read', 1489, -14, 'consumerbound+firstsize'), ('read', 568, -14, 'consumerbound+firstseq'), ('read', 2048, -14, 'consumerbound+secondseq')]
CASES += [('read', 568, -14, mode) for mode in ('firstseq', 'truncated', 'zerobuf', 'manyfrags', 'shortinfo', 'fail1', 'putfail', 'alloc1', 'alloc2')]
CASES += [('read', 1489, -14, 'firstsize'), ('read', 3000, -14, 'bigtotal')]
CASES += [('read', 2048, -14, mode) for mode in ('count0', 'count1', 'secondseq', 'secondtotal', 'secondcount', 'fail2')]
CASES += [('producer', 568, 0, 'valid'), ('producer', 568, 0, 'nonoid')]
CASES += [('producer', 568, 1, mode) for mode in ('shortout', 'nullout')]
CASES += [('producer', 568, 2, mode) for mode in ('zerototal', 'zerobuf', 'firstsize', 'buftotal', 'firstseq')]
CASES = [(kind, size, -14 if kind == 'read' and size < 2048 and expected == 0 else expected, mode)
         for kind, size, expected, mode in CASES]
CASES += [('read', 2048, 0, mode) for mode in ('allzero', 'allzero+consumerbound')]
CASES += [('read', size, -14, 'valid') for size in (376, 1487, 2047)]
CASES += [('read', 2048, -14, mode) for mode in ('firstseq', 'firstsize', 'truncated')]


def stripped(source):
    return re.sub(r'/\*.*?\*/', '', source, flags=re.S)


def function(source, name):
    source = stripped(source)
    found = re.search(r'(?:int|void)\s+' + re.escape(name) + r'\s*\(', source)
    assert found, name
    first = source.index('{', found.start())
    end, depth = first + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[found.start():end]


def abi():
    include = ROOT / MODULE / 'include'
    header = (include / 'wlan_lib.h').read_text()
    size = int(re.search(r'#define PKT_OFLD_BUF_SIZE\s+(\d+)', header)[1])
    vendor = (ROOT / MODULE / 'os/linux/include/gl_vendor.h').read_text()
    maximum = int(re.search(r'#define APF_MAX_PROGRAM_LEN\s+(\d+)', vendor)[1])
    declarations = []
    for path, name in [('wlan_oid.h', 'PARAM_OFLD_INFO'), ('nic_cmd_event.h', 'CMD_OFLD_INFO')]:
        source = stripped((include / path).read_text())
        declarations.append(re.search(r'struct ' + name + r'\s*\{[^}]+\};', source)[0])
    assert size == 1488 and maximum == 2048
    return f'#define PKT_OFLD_BUF_SIZE {size}\n#define APF_MAX_PROGRAM_LEN {maximum}\n' + '\n'.join(declarations)


def build(directory, sources, name):
    tests = ROOT / 'mindone/tests/wlan-apf'
    (directory / 'abi.h').write_text(abi())
    (directory / 'stubs.h').write_text((tests / 'stubs.h').read_text())
    extracted = function(sources[0], 'mtk_cfg80211_vendor_set_packet_filter') + '\n' + function(sources[0], 'mtk_cfg80211_vendor_read_packet_filter') + '\n' + function(sources[1], 'nicCmdEventQueryOfldInfo')
    source = directory / (name + '.c')
    source.write_text('#include "stubs.h"\n' + extracted + '\n' + (tests / 'cases.c').read_text())
    executable = directory / name
    compiler = shutil.which('clang')
    assert compiler, 'clang is required'
    command = [compiler, '-std=c11', '-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', str(source), '-o', str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, case):
    return subprocess.run([str(executable), *(str(value) for value in case)], capture_output=True, text=True, timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline')
    parser.add_argument('--read-contract-baseline')
    args = parser.parse_args()
    sources = [(ROOT / path).read_text() for path in PATHS]
    with tempfile.TemporaryDirectory(prefix='wlan-apf-check-') as temporary:
        directory = Path(temporary)
        executable = build(directory, sources, 'fixed')
        for case in CASES:
            result = run(executable, case)
            assert result.returncode == 0, (case, result.stdout, result.stderr)
        print(f'PASS {len(CASES)} exact-source ASan/UBSan bounds, wire, producer and cleanup cases')
        mutants = [
            ('program-cap', 0, 'if (u4ProgLen > APF_MAX_PROGRAM_LEN)', 'if (0)', ('set', 2049, -22, 'valid')),
            ('netlink-header', 0, 'if (!nla_ok(attr, data_len))', 'if (0)', ('set', 568, -22, 'shortheader')),
            ('read-capacity', 0, 'u4ProgLen != APF_MAX_PROGRAM_LEN ||', '0 ||', ('read', 568, -14, 'valid')),
            ('read-capacity-weakened', 0, 'u4ProgLen != APF_MAX_PROGRAM_LEN ||', 'u4ProgLen < 568 ||', ('read', 568, -14, 'valid')),
            ('read-capacity-inverted', 0, 'u4ProgLen != APF_MAX_PROGRAM_LEN ||', 'u4ProgLen == APF_MAX_PROGRAM_LEN ||', ('read', 2048, 0, 'valid')),
            ('query-sequence', 0, 'rInfo.ucFragSeq != ucCurrSeq ||', '0 ||', ('read', 2048, -14, 'firstseq')),
            ('query-fragment-size', 0, 'rInfo.u4BufLen > PKT_OFLD_BUF_SIZE ||', '0 ||', ('read', 2048, -14, 'firstsize')),
            ('query-total-stability', 0, 'rInfo.u4TotalLen != u4ProgLen', '0', ('read', 2048, -14, 'secondtotal')),
            ('query-count-stability', 0, 'rInfo.ucFragNum != ucFragNum ||', '0 ||', ('read', 2048, -14, 'secondcount')),
            ('query-completion', 0, 'if (u4RecvLen != u4ProgLen)', 'if (0)', ('read', 2048, -14, 'truncated')),
            ('producer-length', 1, 'prCmdInfo->u4InformationBufferLength <', '0 && prCmdInfo->u4InformationBufferLength <', ('producer', 568, 1, 'shortout')),
            ('producer-status', 1, 'WLAN_STATUS_INVALID_DATA', 'WLAN_STATUS_SUCCESS', ('producer', 568, 2, 'zerobuf')),
            ('producer-sequence', 1, 'prCmdOfldInfo->ucFragSeq != prParamOfldInfo->ucFragSeq', '0', ('producer', 568, 2, 'firstseq')),
        ]
        for name, index, old, new, case in mutants:
            mutated = sources.copy()
            target = 'nicCmdEventQueryOfldInfo' if index==1 else ('mtk_cfg80211_vendor_set_packet_filter' if name in ('program-cap','netlink-header') else 'mtk_cfg80211_vendor_read_packet_filter')
            extracted = function(mutated[index],target)
            assert extracted.count(old) == 1, name
            mutated[index] = stripped(mutated[index]).replace(extracted,extracted.replace(old,new,1),1)
            executable = build(directory, mutated, name)
            result = run(executable, case)
            assert result.returncode != 0, (name, case)
        print(f'PASS {len(mutants)} executed mutation counterexamples')
        if args.baseline:
            env = dict(os.environ, GIT_NO_LAZY_FETCH='1')
            baseline = [subprocess.run(['git', '-C', str(ROOT), 'show', args.baseline + ':' + str(path)], check=True, capture_output=True, text=True, timeout=10, env=env).stdout for path in PATHS]
            executable = build(directory, baseline, 'baseline')
            controls = [('set', 2049, -22, 'valid'), ('read', 1489, -14, 'firstsize'), ('read', 568, -14, 'truncated'), ('producer', 568, 2, 'zerobuf'), ('producer', 568, 1, 'shortout')]
            for case in controls:
                result = run(executable, case)
                assert result.returncode != 0, ('baseline unexpectedly passed', case)
            print(f'PASS {len(controls)} original-source failure counterexamples from {args.baseline}')
        if args.read_contract_baseline:
            env = dict(os.environ, GIT_NO_LAZY_FETCH='1')
            baseline = [subprocess.run(['git', '-C', str(ROOT), 'show', args.read_contract_baseline + ':' + str(path)], check=True, capture_output=True, text=True, timeout=10, env=env).stdout for path in PATHS]
            executable = build(directory, baseline, 'read-contract-baseline')
            for size in (1, 376, 568, 1488, 1489, 2047):
                case = ('read', size, -14, 'valid')
                result = run(executable, case)
                assert result.returncode != 0, ('short reply unexpectedly refused by baseline', case)
            for mode in ('valid', 'allzero', 'consumerbound', 'allzero+consumerbound'):
                result = run(executable, ('read', 2048, 0, mode))
                assert result.returncode == 0, ('healthy full reply changed', mode, result.stderr)
            print('PASS 6 original short-reply failures and 4 unchanged full-reply controls')
        for path, source in zip(PATHS, sources):
            print(hashlib.sha256(source.encode()).hexdigest(), path)
        print('LIMIT: Host fault controls do not establish malformed live firmware or energy benefit; native build and real APF readback remain required.')


if __name__ == '__main__':
    main()
