# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
HARNESS = HERE / 'gpufreq-mtcmos'
PROVIDER = 'mindone/modules/mtk_gpufreq_mt6789/gpufreq_mt6789.c'
NAMES = ['__gpufreq_switch_clksrc', '__gpufreq_clock_control', '__gpufreq_mfg_pwr_sta',
         '__gpufreq_mfg_wait_pwr', '__gpufreq_mfg_wait_bits', '__gpufreq_mfg_pwr_control',
         '__gpufreq_mfg0_control', '__gpufreq_mfg1_control', '__gpufreq_mfg2_control',
         '__gpufreq_mfg3_control', '__gpufreq_mfg_restore', '__gpufreq_ref_mux_restore',
         '__gpufreq_mtcmos_control', '__gpufreq_buck_control', '__gpufreq_power_control']


def closing(source, index):
    depth = 0
    while True:
        char = source[index]
        if char in '"\'':
            index += 1
            while source[index] != char:
                index += 2 if source[index] == '\\' else 1
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if not depth:
                return index + 1
        index += 1


def function(source, name):
    pattern = r'^(?:static )?(?:int|void|bool) ' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise SystemExit('gpufreq-mtcmos: %s has %d definitions in %s' % (name, len(found), PROVIDER))
    start = found[0].start()
    body = source[start:closing(source, source.index('{', start))]
    return (body if body.startswith('static') else 'static ' + body) + '\n'


def main():
    parser = argparse.ArgumentParser(description='Compile the MFG MTCMOS, clock, buck and power-count code of '
                                     'gpufreq_mt6789.c against a register and regulator model with ASan/UBSan and '
                                     'inject a failure at every power-on and power-off step: the GPU must end fully '
                                     'on or fully off with consistent counters, and only double failures may panic')
    parser.add_argument('tree', type=Path)
    args = parser.parse_args()
    source = (args.tree / PROVIDER).read_text()
    defines = re.findall(r'^#define GPUFREQ_MTCMOS_\w+\s+.*$', source, re.M)
    if len(defines) != 2:
        raise SystemExit('gpufreq-mtcmos: expected the two GPUFREQ_MTCMOS_ timing constants in ' + PROVIDER)
    unit = '\n'.join(['#include "stubs.h"'] + defines + [function(source, name) for name in NAMES] +
                     ['#include "cases.c"']) + '\n'
    with tempfile.TemporaryDirectory(prefix='gpufreq-mtcmos-') as work:
        path = Path(work) / 'unit.c'
        path.write_text(unit)
        binary = Path(work) / 'unit'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-Wno-unused-parameter', '-Wno-gnu-statement-expression-from-macro-expansion',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g', '-I', str(HARNESS),
                        str(path), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode:
        raise SystemExit('gpufreq-mtcmos: FAIL (exit %d)' % result.returncode)
    print('gpufreq-mtcmos: PASS')


if __name__ == '__main__':
    main()
