# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def identity(path):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'size': path.stat().st_size, 'sha256': digest}


def macro(text, name):
    lines = text.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if re.match(r'^#define\s+' + re.escape(name) + r'(?:\(|\s)', line)]
    if len(starts) != 1:
        raise ValueError('Unknown macro: ' + name)
    index = starts[0]
    end = index + 1
    while lines[end - 1].rstrip().endswith('\\'):
        end += 1
    return ''.join(lines[index:end])


def main():
    parser = argparse.ArgumentParser(description='Exercise actual AIS blacklist prepare/commit/abort with allocation failures and exact list macros')
    parser.add_argument('--compiler', default='cc')
    parser.add_argument('--fault-controls', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if Path(tempfile.gettempdir()).resolve().is_relative_to(root) or (args.output and args.output.resolve().is_relative_to(root)):
        raise ValueError('Output and temporary files must be outside kernel source tree')
    driver = root / 'mindone/modules/wlan_drv_gen4m_6789'
    fixture = Path(__file__).with_name('wifi-roaming-transaction')
    paths = [driver / name for name in ['mgmt/ais_fsm.c', 'include/mgmt/ais_fsm.h', 'include/config.h', 'include/link.h', 'include/nic/mac.h']]
    paths += [fixture / 'stubs.h', fixture / 'cases.c', Path(__file__).resolve()]
    before = [identity(path) for path in paths]
    text, header, config, links, macs = (path.read_text() for path in paths[:5])
    start = text.index('enum AIS_FWK_PLAN_ORIGIN {')
    stop = text.index('void aisRefreshFWKBlacklist(', start)
    implementation = text[start:stop]
    start = header.index('struct AIS_BLACKLIST_ITEM {')
    stop = header.index('\n};', start) + len('\n};')
    item = header[start:stop]
    originals = {'candidate': implementation}
    if args.fault_controls:
        mutations = {
            'early-state-change': ('plan->adapter = adapter;', 'plan->adapter = adapter; adapter->rWifiVar.rBlackList.rUsingLink.u4NumElem = 0;'),
            'zero-free-during-prepare': ('item->origin = AIS_FWK_PLAN_FREE;', 'item->origin = AIS_FWK_PLAN_FREE; kalMemZero(item->entry, sizeof(*item->entry));'),
            'leak-aborted-allocation': ('if (plan->items[index].origin == AIS_FWK_PLAN_NEW)', 'if (false)'),
            'keep-stale-fwk-flag': ('entry->fgIsInFWKBlacklist = FALSE;', '(void)entry;'),
            'free-published-nodes': ('\tkalMemFree(plan, VIR_MEM_TYPE, sizeof(*plan));\n}', '\taisAbortFwkBlacklist(plan);\n}'),
            'unsaturated-count': ('entry->ucCount = count > 10 ? 10 : count;', 'entry->ucCount = count;'),
            'trust-stale-cache': ('item->entry = aisFwkFindUsing(adapter, bss);', 'item->entry = bss->prBlack;'),
            'missing-descriptor-cache': ('plan->items[plan->touches[index].item].entry;', 'NULL;'),
            'free-node-still-linked': ('LINK_REMOVE_KNOWN_ENTRY(&blacklist->rFreeLink,\n\t\t\t\t\t&entry->rLinkEntry);', '(void)entry;'),
        }
        for name, (old, new) in mutations.items():
            expected_count = 2 if name == 'free-published-nodes' else 1
            if implementation.count(old) != expected_count:
                raise ValueError('Unknown mutation contract: ' + name)
            originals[name] = implementation.replace(old, new) if expected_count == 1 else implementation[:implementation.rfind(old)] + implementation[implementation.rfind(old):].replace(old, new, 1)
    reports = []
    with tempfile.TemporaryDirectory(prefix='mindone-ais-transaction-') as temporary:
        work = Path(temporary)
        (work / 'gl_typedef.h').write_text('')
        (work / 'driver-link.h').write_text(links)
        (work / 'driver-item.inc').write_text(item)
        (work / 'driver-mac.inc').write_text('\n'.join(macro(macs, name) for name in ('COPY_MAC_ADDR', 'EQUAL_MAC_ADDR', 'COPY_SSID', 'EQUAL_SSID')))
        shared = macro(config, 'CFG_FW_ROAMING_BLACKLIST_SIZE')
        for mbo in (0, 1):
            for name, body in originals.items():
                source = work / (name + '-' + str(mbo) + '.c')
                binary = source.with_suffix('')
                source.write_text(shared + '\n' + (fixture / 'stubs.h').read_text() + '\n' + body + '\n' + (fixture / 'cases.c').read_text())
                command = [args.compiler, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DCFG_SUPPORT_MBO=' + str(mbo), '-I', str(work), str(source), '-o', str(binary)]
                compiled = subprocess.run(command, capture_output=True, text=True, timeout=45)
                if compiled.returncode:
                    raise RuntimeError('Compilation is not semantic rejection: ' + name + ': ' + compiled.stderr)
                executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=25)
                if name == 'candidate':
                    if executed.returncode or 'exact AIS transaction cases' not in executed.stdout:
                        raise RuntimeError('Candidate failed: ' + executed.stderr)
                else:
                    diagnostic = (executed.stdout + executed.stderr).lower()
                    if not executed.returncode or not any(value in diagnostic for value in ('check_fail:', 'addresssanitizer', 'runtime error:')):
                        raise RuntimeError('Mutant missed semantic rejection: ' + name + ': ' + executed.stderr)
                reports.append({'case': name, 'mbo': mbo, 'compile_rc': compiled.returncode, 'run_rc': executed.returncode,
                                'stdout': executed.stdout, 'stderr': executed.stderr, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
    after = [identity(path) for path in paths]
    if before != after:
        raise RuntimeError('Transaction inputs changed')
    result = {'status': 'PASS_EXACT_AIS_BLACKLIST_TRANSACTIONS', 'cases': reports, 'inputs_before': before, 'inputs_after': after,
              'input_continuity': True, 'fault_controls': args.fault_controls,
              'limits': ['Actual prepare/commit/abort, blacklist item definition and driver list/MAC macros with host allocator/scan/time shims.',
                         'This is not the Linux request slot, native module ABI, concurrent kernel actor or hardware roaming acceptance.']}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'executions': len(reports)}))


if __name__ == '__main__':
    main()
