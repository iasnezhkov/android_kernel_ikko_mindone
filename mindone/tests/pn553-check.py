# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'pn553'
SRC = 'mindone/modules/pn553/pn553.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = (
    'probe-no-autoen',
    'flush-before-enable',
    'flush-while-irq-high',
    'power-off-wakes-reader',
    'signal-disables-irq',
    'release-lowers-ven',
    'suspend-wakeup-after-freeze',
    'write-nack-retry',
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-sign-compare',
         '-Wno-gnu-statement-expression-from-macro-expansion',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def build(work, name, source_text):
    unit = work / name
    unit.mkdir()
    body = re.sub(r'^#include\s+<[^>]+>\n', '', source_text, flags=re.M)
    cpath = unit / 'unit.c'
    cpath.write_text('#include "stubs.h"\n' + body + '\n#include "cases.c"\n')
    binary = unit / 'bin'
    res = subprocess.run(FLAGS + ['-I', str(HARNESS), str(cpath), '-o', str(binary)],
                         capture_output=True, text=True, timeout=120)
    if res.returncode:
        fail(f'{name} failed to compile:\n{res.stdout}{res.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(
        description='Verify pn553 NFC concurrency, IRQ/VEN lifetime, and suspend wakeup handling')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SRC).read_text()

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        work = Path(tmp)
        candidate = build(work, 'candidate', source)
        for case in CASES:
            res = run(candidate, case)
            if res.returncode:
                fail(f'FAIL {case}:\n{res.stdout}{res.stderr}')
            print(res.stdout.rstrip())

        mutants = (
            ('drop-release-read-wait',
             'READ_ONCE(nfc->release_read) ||\n\t\t\t\t!READ_ONCE(nfc->ven_high)',
             '!READ_ONCE(nfc->ven_high)'),
            ('drop-power-off-wake',
             'nfc->ven_on_by_nfc = false;\n\t\twake_up_all(&nfc->read_wq);\n\t\tbreak;',
             'nfc->ven_on_by_nfc = false;\n\t\tbreak;'),
            ('drop-signal-disable-irq',
             'if (ret) {\n\t\t\t\tnfc_disable_irq(nfc);\n\t\t\t\treturn ret;\n\t\t\t}',
             'if (ret)\n\t\t\t\treturn ret;'),
            ('drop-release-ven-low',
             'nfc_set_ven(nfc, false);\n\t\tnfc->ven_on_by_nfc = false;\n\t\twake_up_all(&nfc->read_wq);\n\t}',
             'nfc->ven_on_by_nfc = false;\n\t\twake_up_all(&nfc->read_wq);\n\t}'),
            ('suspend-gate-on-irq-enabled',
             'if (device_may_wakeup(dev) && READ_ONCE(nfc->ven_high) &&',
             'if (device_may_wakeup(dev) && nfc->irq_enabled &&'),
            ('drop-irqf-no-autoen',
             'IRQF_TRIGGER_HIGH | IRQF_NO_AUTOEN,',
             'IRQF_TRIGGER_HIGH,'),
        )
        for name, before, after in mutants:
            if source.count(before) != 1:
                fail(f'mutant {name} anchor count={source.count(before)}')
            binary = build(work, 'mutant-' + name, source.replace(before, after, 1))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} survived')
            print(f'PASS mutant-killed {name}: {", ".join(caught)}')

    print(f'{CHECK}: PASS ({len(CASES)} cases, {len(mutants)} mutants)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
