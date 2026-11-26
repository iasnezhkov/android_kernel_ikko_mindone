# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = 'charger-eoc-guard'
BASIC = 'mindone/modules/mtk_charger_framework/mtk_basic_charger.c'
CHG = 'mindone/modules/mtk_charger_framework/mtk_charger.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ('term-window', 'false-eoc-reject', 'real-eoc-accept')
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def extract_unit(basic):
    start = basic.index('static void mtk_basic_update_termination(')
    end = basic.index('static int do_algorithm(')
    return '\n'.join(['#include "stubs.h"', basic[start:end], '#include "cases.c"']) + '\n'


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    res = subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)],
                         capture_output=True, text=True, timeout=300)
    if res.returncode:
        fail(f'{name} failed to compile:\n{res.stdout}{res.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Verify mtk_basic_charger CV termination gating and premature EOC recovery')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    basic = (tree / BASIC).read_text()
    chg = (tree / CHG).read_text()
    if 'charger_dev_enable_termination(info->chg1_dev, false);' not in chg:
        fail('mtk_charger_plug_in does not disable HW termination on plug-in')
    if 'done = info->is_chg_done;' not in chg:
        fail('psy_charger_get_property does not gate chg1 FULL status on verified info->is_chg_done')
    if 'chg_done = mtk_basic_validate_chg_done(info, chg_done);' not in basic:
        fail('do_algorithm does not call mtk_basic_validate_chg_done')
    if 'mtk_basic_update_termination(info, chg_done);' not in basic:
        fail('do_algorithm does not call mtk_basic_update_termination')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', extract_unit(basic))
        for case in CASES:
            res = run(candidate, case)
            if res.returncode:
                fail(f'FAIL {case}:\n{res.stdout}{res.stderr}')
            print(res.stdout.rstrip())

        mutants = (
            ('enable-term-too-early',
             'vbat_mV >= hw_cv_mV - 15 && ibat_mA >= 0 && ibat_mA <= iterm_mA + 100',
             'vbat_mV >= hw_cv_mV - 100 && ibat_mA >= 0'),
            ('single-poll-term',
             'if (++info->term_ready_cnt >= 2) {',
             'if (++info->term_ready_cnt >= 1) {'),
            ('no-term-hysteresis-drop',
             'if (vbat_mV < hw_cv_mV - 30 || ibat_mA > iterm_mA + 180) {',
             'if (0) {'),
            ('accept-false-eoc',
             'vbat_mV < hw_cv_mV - 50 && info->false_eoc_retry < 3',
             'vbat_mV < hw_cv_mV - 150 && info->false_eoc_retry < 3'),
            ('no-retry-cap',
             'vbat_mV < hw_cv_mV - 50 && info->false_eoc_retry < 3',
             'vbat_mV < hw_cv_mV - 50'),
            ('skip-toggle-recovery',
             'charger_dev_enable(info->chg1_dev, false);\n\t\tmsleep(50);',
             '(void)0;\n\t\tmsleep(50);'),
        )
        for name, before, after in mutants:
            assert basic.count(before) == 1
            mutated = basic.replace(before, after, 1)
            binary = build(directory, 'mutant-' + name, extract_unit(mutated))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(mutants)} mutants caught')


if __name__ == '__main__':
    sys.exit(main())
