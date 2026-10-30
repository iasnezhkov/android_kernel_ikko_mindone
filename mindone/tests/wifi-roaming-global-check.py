# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import runpy
import json
from pathlib import Path
import re
import subprocess
import tempfile


def identity(path):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'size': path.stat().st_size, 'sha256': digest}


def function(source, name):
    matches = list(re.finditer(r'^void\s+' + re.escape(name) + r'\(', source, re.M))
    if len(matches) != 1:
        raise ValueError('Unknown function: ' + name)
    start = matches[0].start()
    return source[start:source.index('\n}', start) + 2]


def main():
    parser = argparse.ArgumentParser(description='Check actual management blacklist lifetime across dual AIS and retained reset')
    parser.add_argument('--compiler', default='cc')
    parser.add_argument('--fault-controls', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if Path(tempfile.gettempdir()).resolve().is_relative_to(root) or (args.output and args.output.resolve().is_relative_to(root)):
        raise ValueError('Output and temporary files must be outside kernel source tree')
    driver = root / 'mindone/modules/wlan_drv_gen4m_6789'
    fixture = Path(__file__).with_name('wifi-roaming-transaction')
    helper_path = Path(__file__).with_name('wifi-roaming-transaction-check.py')
    helper = runpy.run_path(str(helper_path))
    paths = [driver / name for name in ('nic/nic.c', 'mgmt/ais_fsm.c', 'include/mgmt/ais_fsm.h', 'include/config.h', 'include/link.h', 'include/nic/mac.h')]
    paths += [fixture / 'stubs.h', fixture / 'global.c', helper_path, Path(__file__).resolve()]
    before = [identity(path) for path in paths]
    nic, ais, header, config, links, macs = (path.read_text() for path in paths[:6])
    if 'rBlackList' in function(ais, 'aisFsmInit') or 'rBlackList' in function(ais, 'aisFsmUninit'):
        raise ValueError('A per-AIS lifecycle still manages the global blacklist')
    candidate = function(nic, 'nicInitMGMT') + '\n' + function(nic, 'nicUninitMGMT')
    variants = {'candidate': candidate}
    if args.fault_controls:
        substitutions = {
            'reset-drops-retained-heads': ('if (!prAdapter->rWifiVar.fgAisBlacklistInitialized)', 'if (true)'),
            'partial-start-reads-uninitialized-heads': ('if (prAdapter->rWifiVar.fgAisBlacklistInitialized)', 'if (true)'),
            'flag-survives-global-free': ('prAdapter->rWifiVar.fgAisBlacklistInitialized = FALSE;', '(void)prAdapter;'),
        }
        for name, (old, new) in substitutions.items():
            if candidate.count(old) != 1: raise ValueError(name)
            variants[name] = candidate.replace(old, new)
        begin = candidate.index('\tif (prAdapter->rWifiVar.fgAisBlacklistInitialized)')
        end = candidate.index('\n\t}', begin) + len('\n\t}')
        block = candidate[begin:end]
        variants['free-before-scan-teardown'] = candidate[:begin] + candidate[begin:end].replace(block, '') + candidate[end:]
        old = '\tscnUninit(prAdapter);'
        variants['free-before-scan-teardown'] = variants['free-before-scan-teardown'].replace(old, block + '\n' + old)
    reports = []
    with tempfile.TemporaryDirectory(prefix='mindone-roaming-global-') as temporary:
        work = Path(temporary)
        (work / 'gl_typedef.h').write_text('')
        (work / 'driver-link.h').write_text(links)
        item = header[header.index('struct AIS_BLACKLIST_ITEM {'):]
        item = item[:item.index('\n};') + len('\n};')]
        (work / 'driver-item.inc').write_text(item)
        (work / 'driver-mac.inc').write_text('\n'.join(helper['macro'](macs, name) for name in ('COPY_MAC_ADDR', 'EQUAL_MAC_ADDR', 'COPY_SSID', 'EQUAL_SSID')))
        shared = helper['macro'](config, 'CFG_FW_ROAMING_BLACKLIST_SIZE')
        for mbo in (0, 1):
            for name, body in variants.items():
                (work / 'driver-mgmt.inc').write_text(body)
                source = work / (name + '-' + str(mbo) + '.c'); binary = source.with_suffix('')
                source.write_text(shared + '\n' + (fixture / 'stubs.h').read_text() + '\n' + (fixture / 'global.c').read_text())
                command = [args.compiler, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DCFG_SUPPORT_MBO=' + str(mbo), '-I', str(work), str(source), '-o', str(binary)]
                built = subprocess.run(command, capture_output=True, text=True, timeout=45)
                if built.returncode: raise RuntimeError('Compilation is not semantic rejection: ' + name + ': ' + built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                diagnostic = (result.stdout + result.stderr).lower()
                if name == 'candidate':
                    if result.returncode or 'retained-reset' not in result.stdout: raise RuntimeError(result.stderr)
                elif not result.returncode or not any(token in diagnostic for token in ('check_fail:', 'addresssanitizer', 'runtime error:')):
                    raise RuntimeError('Mutant missed: ' + name + ': ' + result.stderr)
                reports.append({'case': name, 'mbo': mbo, 'compile_rc': built.returncode, 'run_rc': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
    after = [identity(path) for path in paths]
    if before != after: raise RuntimeError('Global lifecycle inputs changed')
    report = {'status': 'PASS_EXACT_GLOBAL_BLACKLIST_LIFETIME', 'cases': reports, 'inputs_before': before, 'inputs_after': after,
              'input_continuity': True, 'fault_controls': args.fault_controls,
              'limits': ['Actual nicInitMGMT/nicUninitMGMT and driver list macros; per-AIS and scan subsystems use lifecycle shims.',
                         'Per-role source is checked to exclude global manager operations; no general kernel concurrency or hardware acceptance.']}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'executions': len(reports)}))


if __name__ == '__main__':
    main()
