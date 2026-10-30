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


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + len('\n}')
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description='Check actual owned roaming slot code with controlled pthread actors and virtual deadlines')
    parser.add_argument('--compiler', default='cc')
    parser.add_argument('--fault-controls', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if Path(tempfile.gettempdir()).resolve().is_relative_to(root) or (args.output and args.output.resolve().is_relative_to(root)):
        raise ValueError('Output and temporary files must be outside kernel source tree')
    driver = root / 'mindone/modules/wlan_drv_gen4m_6789'
    fixture = Path(__file__).with_name('wifi-roaming-slot')
    paths = [driver / name for name in ('os/linux/gl_kal.c', 'os/linux/include/gl_os.h', 'include/config.h', 'os/linux/gl_init.c', 'os/linux/gl_cfg80211.c')]
    paths += [fixture / 'stubs.h', fixture / 'cases.c', Path(__file__).resolve()]
    before = [identity(path) for path in paths]
    kal, header, config, init, cfg = (path.read_text() for path in paths[:5])
    start = kal.index('static int kalFwkBlacklistAdmissionStatus(')
    end = kal.index('int main_thread(void *data)', start)
    body = kal[start:end]
    start = header.index('enum GL_FWK_BLACKLIST_STATE {')
    end = header.index('struct GLUE_INFO {', start)
    types = header[start:end]
    shared = re.search(r'^#define CFG_FW_ROAMING_BLACKLIST_SIZE[^\n]*', config, re.M).group(0)
    source_main = function(kal, 'int main_thread(void *data)')
    halted = source_main.index('complete(&prGlueInfo->rHaltComp);')
    if source_main.index('kalFwkBlacklistOwnerDrain(prGlueInfo)') > halted or 'kalFwkBlacklist' in source_main[halted:]:
        raise ValueError('Owned slot outlives halt completion')
    constructor = function(init, 'void wlanCreateWirelessDevice(')
    create = function(init, 'struct wireless_dev *wlanNetCreate(')
    stop = function(init, 'void wlanOffStopWlanThreads(')
    change = function(cfg, 'int mtk_cfg_change_iface(')
    if constructor.count('kalFwkBlacklistInit(prGlueInfo)') != 1 or 'kalFwkBlacklistInit' in create or 'kalMemZero(prGlueInfo, sizeof(struct GLUE_INFO))' in create:
        raise ValueError('Persistent slot is reinitialized across sessions')
    if stop.index('kalFwkBlacklistCallerDrain(prGlueInfo)') < stop.index('complete(&prGlueInfo->rPendComp)'):
        raise ValueError('Caller barrier blocks queued OID completion')
    if change.index('prAdapter = prGlueInfo->prAdapter') < change.index('kalFwkBlacklistRoleBegin(prGlueInfo)'):
        raise ValueError('Stale adapter captured before role fence')
    variants = {'candidate': body}
    if args.fault_controls:
        mutations = {
            'commit-after-cancel': ('if (!result && !cancelled)', 'if (!result)'),
            'publish-idle-before-abort': ('\tif (plan)\n\t\taisAbortFwkBlacklist(plan);', '\tcomplete_all(&slot->owner_idle);\n\tif (plan)\n\t\taisAbortFwkBlacklist(plan);'),
            'report-timeout-after-commit': ('if (slot->state == GL_FWK_BLACKLIST_DONE)', 'if (slot->state == GL_FWK_BLACKLIST_DONE && !time_after_eq(jiffies, slot->deadline))'),
            'stale-owner-idle': ('\treinit_completion(&slot->owner_idle);\n', ''),
            'erase-live-mutexes': ('memset(glue, 0, offsetof(struct GLUE_INFO, rFwkBlacklist));', 'memset(glue, 0, sizeof(*glue));'),
        }
        for name, (old, new) in mutations.items():
            if body.count(old) != 1:
                raise ValueError('Unknown mutation: ' + name)
            variants[name] = body.replace(old, new)
    reports = []
    with tempfile.TemporaryDirectory(prefix='mindone-roaming-slot-') as temporary:
        work = Path(temporary)
        (work / 'driver-slot-types.inc').write_text(types)
        for name, implementation in variants.items():
            source = work / (name + '.c')
            binary = source.with_suffix('')
            source.write_text(shared + '\n' + (fixture / 'stubs.h').read_text() + '\n' + implementation + '\n' + (fixture / 'cases.c').read_text())
            command = [args.compiler, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-pthread', '-I', str(work), str(source), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=45)
            if built.returncode:
                raise RuntimeError('Compilation is not a semantic rejection: ' + name + ': ' + built.stderr)
            process = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if name == 'candidate':
                if process.returncode or 'owned roaming slot scenarios' not in process.stdout:
                    raise RuntimeError('Candidate failed: ' + process.stderr)
            else:
                diagnostic = (process.stdout + process.stderr).lower()
                if not process.returncode or not any(token in diagnostic for token in ('check_fail:', 'addresssanitizer', 'runtime error:')):
                    raise RuntimeError('Mutant missed semantic rejection: ' + name + ': ' + process.stderr)
            reports.append({'case': name, 'compile_rc': built.returncode, 'run_rc': process.returncode,
                            'stdout': process.stdout, 'stderr': process.stderr, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
    after = [identity(path) for path in paths]
    if before != after:
        raise RuntimeError('Owned slot inputs changed')
    report = {'status': 'PASS_EXACT_OWNED_ROAMING_SLOT', 'cases': reports, 'fault_controls': args.fault_controls,
              'inputs_before': before, 'inputs_after': after, 'input_continuity': True,
              'lifecycle_hook_order_checked': True,
              'limits': ['Actual slot/controller bodies and types with pthread mutex/completion/actor and virtual-clock shims.',
                         'AIS backend is an owned-plan model; real AIS allocation/list semantics have a separate exact-source gate.',
                         'No native Linux scheduling proof, generic WLAN concurrency, physical roaming or device acceptance.']}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'executions': len(reports)}))


if __name__ == '__main__':
    main()
