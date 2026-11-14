# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


TREE = '{tree}'
REFERENCE = '{reference}'
CHECKS = {
    'adda-enable': ('adda-enable-check.py', [TREE]),
    'ccci-mutt': ('ccci-mutt-check.py', [TREE]),
    'ccmni-private': ('ccmni-private-check.py', [TREE]),
    'charger-acceptance': ('charger-acceptance-check.py', [TREE]),
    'charger-lifecycle': ('charger-lifecycle-check.py', [TREE]),
    'clk-disable-unused': ('clk-disable-unused-check.py', [TREE]),
    'clk-upstream': ('clk-upstream-check.py', [TREE]),
    'codec-volume': ('codec-volume-check.py', [TREE]),
    'daemon-capacity': ('daemon-capacity-check.py', [TREE]),
    'dlpt-imix': ('dlpt-imix-check.py', [TREE]),
    'docpath': ('docpath-check.py', []),
    'eta-event': ('eta-event-check.py', ['--tree', TREE]),
    'eta-operations': ('eta-operations-check.py', ['--tree', TREE]),
    'gauge-activation-audit': ('gauge-activation-audit.py', [TREE]),
    'gauge-activation-controls': ('gauge-activation-controls.py', [TREE]),
    'gauge-algo': ('gauge-algo-check.py', [TREE]),
    'gauge-charger': ('gauge-charger-check.py', [TREE]),
    'gauge-debug-lifetime': ('gauge-debug-lifetime-check.py', [TREE]),
    'gauge-enable': ('gauge-enable-check.py', [TREE]),
    'gauge-iavg-contract': ('gauge-iavg-contract-check.py', [TREE]),
    'gauge-init-io': ('gauge-init-io-check.py', [TREE]),
    'gauge-irq': ('gauge-irq-check.py', [TREE]),
    'gauge-irq-setup': ('gauge-irq-setup-check.py', [TREE]),
    'gauge-optional-dt': ('gauge-optional-dt-check.py', [TREE]),
    'gauge-presence': ('gauge-presence-check.py', [TREE]),
    'gauge-reset': ('gauge-reset-check.py', [TREE]),
    'gauge-sample-check-test': ('gauge-sample-check-test.py', []),
    'gauge-setup-io': ('gauge-setup-io-check.py', [TREE]),
    'gauge-uisoc': ('gauge-uisoc-check.py', [TREE]),
    'gauge-zcv-io': ('gauge-zcv-io-check.py', [TREE]),
    'gauge-zcv-irq': ('gauge-zcv-irq-check.py', [TREE]),
    'gpu-bm': ('gpu-bm-check.py', [TREE]),
    'gpu-cooling': ('gpu-cooling-check.py', [TREE]),
    'gpufreq-mtcmos': ('gpufreq-mtcmos-check.py', [TREE]),
    'i2c-slow-target': ('i2c-slow-target-check.py', [TREE]),
    'iavg-dwa': ('iavg-dwa-check.py', [TREE]),
    'imx766-pd-vc': ('imx766-pd-vc-check.py', [TREE]),
    'isink-rac': ('isink-rac-check.py', [TREE]),
    'mali-pm': ('mali-pm-check.py', [TREE]),
    'md-mutt': ('md-mutt-check.py', [TREE]),
    'modules-tsv': ('modules-tsv-check.py', [TREE]),
    'mtk-jpeg': ('mtk-jpeg-check.py', [TREE]),
    'panel-control': ('panel-control-check.py', ['--tree', TREE]),
    'pd-adapter-tcpm': ('pd-adapter-tcpm-check.py', [TREE]),
    'pinctrl-mt6789': ('pinctrl-mt6789-check.py', [TREE]),
    'power-misc-setup': ('power-misc-setup-check.py', [TREE]),
    'ptim-sample': ('ptim-sample-check.py', [TREE]),
    'pwrap-mt6789': ('pwrap-mt6789-check.py', [TREE]),
    'scpsys-mt6789': ('scpsys-mt6789-check.py', [TREE]),
    'shutdown-voltage': ('shutdown-voltage-check.py', [TREE]),
    'tcpci-rt1711h': ('tcpci-rt1711h-check.py', [TREE]),
    'typec-dt': ('typec-dt-check.py', [TREE]),
    'ufs-screen': ('ufs-screen-check.py', ['--tree', TREE]),
    'usb-role-proxy': ('usb-role-proxy-check.py', [TREE]),
    'wifi-action-bounds': ('wifi-action-bounds-check.py', ['--tree', TREE]),
    'wifi-keepalive': ('wifi-keepalive-check.py', []),
    'wifi-roaming': ('wifi-roaming-check.py', []),
    'wifi-roaming-global': ('wifi-roaming-global-check.py', []),
    'wifi-roaming-slot': ('wifi-roaming-slot-check.py', []),
    'wifi-roaming-transaction': ('wifi-roaming-transaction-check.py', []),
    'wifi-scan-allocation': ('wifi-scan-allocation-check.py', ['--tree', TREE]),
    'wifi-tail-bounds': ('wifi-tail-bounds-check.py', []),
    'wifi-tail-count': ('wifi-tail-count-check.py', []),
    'wifi-wext-bounds': ('wifi-wext-bounds-check.py', []),
    'wlan-apf': ('wlan-apf-check.py', []),
    'wmt-sleep': ('wmt-sleep-check.py', [TREE]),
    'wsys-header': ('wsys-header-check.py', ['--reference-repo', REFERENCE]),
}
INPUTS = {
    'gauge-sample-check.py': 'reads gauge samples recorded on the device',
    'thermal-dt-check.py': 'reads a built base DTB and overlay',
}
REFERENCE_CHECKS = {'wsys-header'}
CLANG_CHECKS = {'charger-lifecycle', 'iavg-dwa', 'ptim-sample', 'wlan-apf'}


def usage():
    return ('usage: run-checks.py [--tree TREE] [--reference-repo REPO] [--compiler-dir DIR] [--jobs N] [--timeout S]\n'
            '                     [--allow NAME=REASON ...] [--json OUT] [NAME ...]\n'
            'Runs the host checks of mindone/tests on TREE (default: the tree holding this script). REPO is an\n'
            'android16-6.12 clone for checks whose reference must stay out of this tree; without it they are\n'
            'skipped. DIR holds the cc and clang to put first on PATH. Exit status 1 when a check fails that\n'
            '--allow does not name, or when a check file exists that the runner does not know.')


def run(name, script, argv, tree, environment, timeout):
    started = time.monotonic()
    try:
        result = subprocess.run([sys.executable, '-B', str(tree / 'mindone/tests' / script), *argv], cwd=tree,
                                capture_output=True, text=True, timeout=timeout, env=environment)
        code, output = result.returncode, result.stdout + result.stderr
    except subprocess.TimeoutExpired as error:
        code = 'timeout'
        output = (error.stdout or b'').decode(errors='replace') + (error.stderr or b'').decode(errors='replace')
    lines = [line for line in output.strip().split('\n') if line.strip()]
    return {'name': name, 'status': 'pass' if code == 0 else 'fail', 'exit': code,
            'seconds': round(time.monotonic() - started, 1), 'last': lines[-1][:300] if lines else '', 'output': output}


def main():
    parser = argparse.ArgumentParser(usage=usage())
    parser.add_argument('names', nargs='*')
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--reference-repo', type=Path)
    parser.add_argument('--compiler-dir', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--allow', action='append', default=[])
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    tree = args.tree.resolve()
    allowed = dict(item.split('=', 1) for item in args.allow)
    unknown_names = sorted(set(args.names) - set(CHECKS))
    if unknown_names or set(allowed) - set(CHECKS):
        raise SystemExit('run-checks.py: unknown check: ' + ', '.join(unknown_names + sorted(set(allowed) - set(CHECKS))))
    present = {path.name for path in (tree / 'mindone/tests').iterdir()
               if path.suffix == '.py' and path.name not in ('run-checks.py', 'baseline_source.py')}
    unlisted = sorted(present - {script for script, _ in CHECKS.values()} - set(INPUTS))
    environment = {**os.environ, 'GIT_NO_LAZY_FETCH': '1', 'PYTHONDONTWRITEBYTECODE': '1'}
    with tempfile.TemporaryDirectory(prefix='mindone-checks-') as temporary:
        if args.compiler_dir:
            bin_dir = Path(temporary) / 'bin'
            bin_dir.mkdir()
            for tool in ('cc', 'clang'):
                target = args.compiler_dir / tool
                if not target.exists():
                    target = args.compiler_dir / 'clang'
                (bin_dir / tool).symlink_to(target)
            environment['PATH'] = str(bin_dir) + os.pathsep + environment['PATH']
        rows, jobs = [], []
        for name in args.names or CHECKS:
            script, template = CHECKS[name]
            if name in REFERENCE_CHECKS and not (args.reference_repo and (args.reference_repo / '.git').exists()):
                rows.append({'name': name, 'status': 'skip', 'reason': 'needs --reference-repo with an android16-6.12 clone'})
                continue
            if name in CLANG_CHECKS and not shutil.which('clang', path=environment['PATH']):
                rows.append({'name': name, 'status': 'skip', 'reason': 'needs clang on PATH'})
                continue
            argv = [str(tree) if item == TREE else str(args.reference_repo) if item == REFERENCE else item for item in template]
            jobs.append((name, script, argv))
        with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
            futures = [pool.submit(run, name, script, argv, tree, environment, args.timeout) for name, script, argv in jobs]
            for future in futures:
                rows.append(future.result())
    rows.sort(key=lambda row: row['name'])
    for script, reason in sorted(INPUTS.items()):
        rows.append({'name': script[:-len('-check.py')], 'status': 'skip', 'reason': reason})
    failed = [row['name'] for row in rows if row['status'] == 'fail' and row['name'] not in allowed]
    for row in rows:
        if row['status'] == 'fail' and row['name'] in allowed:
            row['status'], row['allowed'] = 'allowed-fail', allowed[row['name']]
    stale = sorted(name for name in allowed if not any(row['name'] == name and row['status'] == 'allowed-fail' for row in rows))
    result = 'pass' if not failed and not unlisted and not stale else 'fail'
    for row in rows:
        detail = row.get('reason') or row.get('allowed') or row.get('last', '')
        print(f"{row['status']:<12} {row['name']:<28} {str(row.get('seconds', '')):>6}  {detail}")
    if unlisted:
        print('unlisted check files: ' + ', '.join(unlisted))
    if stale:
        print('allowed failures that now pass or did not run: ' + ', '.join(stale))
    print(f"{result}: {sum(row['status'] == 'pass' for row in rows)} pass, {len(failed)} fail, "
          f"{sum(row['status'] == 'allowed-fail' for row in rows)} allowed, {sum(row['status'] == 'skip' for row in rows)} skip")
    if args.json:
        args.json.write_text(json.dumps({'schema': 'mindone-test-checks-v1', 'result': result, 'tree': str(tree),
                                         'failed': failed, 'unlisted': unlisted, 'stale_allowances': stale,
                                         'checks': rows}, indent=1) + '\n')
    return 0 if result == 'pass' else 1


if __name__ == '__main__':
    sys.exit(main())
