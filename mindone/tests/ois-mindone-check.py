# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'ois-mindone'
SOURCE = 'mindone/modules/ois_mindone/ois_mindone.c'
OVERLAY_DTS = 'mindone/dts/mindone-overlay.dtso'
LOAD = 'mindone/vendor_dlkm.modules.load'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ('probe-remove', 'write-allowlist', 'boot-and-park', 'chip-id-and-cancel', 'reg26-params')
MUTANTS = (
    ('allowlist-bypass',
     'if (reg != OIS_REG_ENABLE && reg != OIS_REG_MODE)\n\t\treturn -EACCES;',
     'if (reg == 0xff)\n\t\treturn -EACCES;'),
    ('skip-poweroff-park',
     'if (ois->ready && ois->active_mode != OIS_MODE_OFF)\n\t\t\tois_apply_locked(ois, OIS_MODE_OFF);',
     'if (0)\n\t\t\tois_apply_locked(ois, OIS_MODE_OFF);'),
    ('skip-remove-slow-clear',
     'mtk_i2c_set_slow_target(client->adapter->nr, client->addr, 0);\n}',
     '}'),
    ('accept-wrong-chip-id',
     'if (ret || id != OIS_CHIP_ID) {',
     'if (ret) {'),
    ('ignore-stale-seq',
     'if (!atomic_read(&ois->powered) || atomic_read(&ois->seq) != seq)\n\t\t\treturn;',
     'if (0)\n\t\t\treturn;'),
    ('write-reg26-when-neg1',
     'if (want_26 >= 0) {',
     'if (want_26 >= -1) {'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def bindings(tree):
    overlay = (tree / OVERLAY_DTS).read_text()
    if not re.search(r'camera_main_ois@69\s*\{[^}]*compatible\s*=\s*"mindone,ois-5a30";[^}]*reg\s*=\s*<0x69>;',
                     overlay, re.S):
        fail(f'camera_main_ois@69 binding missing in {OVERLAY_DTS}')
    if 'ois_mindone.ko' not in (tree / LOAD).read_text().split():
        fail(f'ois_mindone.ko missing from {LOAD}')


def unit(source):
    start = source.index('#define OIS_BUS_HZ')
    end = source.index('static struct attribute *ois_mindone_attrs')
    body = source[start:end]
    probe_start = source.index('static int ois_mindone_probe')
    probe_end = source.index('static const struct of_device_id ois_mindone_of_match')
    probe = source[probe_start:probe_end]
    return '\n'.join(['#include "stubs.h"', body, probe, '#include "cases.c"']) + '\n'


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    result = subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)],
                            capture_output=True, text=True, timeout=300)
    if result.returncode:
        fail(f'{name} failed to compile:\n{result.stdout}{result.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Compile ois_mindone.c against an I2C and imgsensor-power model with '
                                     'ASan/UBSan and verify write allowlist, boot retry/cancel, power-off park, '
                                     'slow_target lifecycle, reg26 bounds, DT bindings, and mutants')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    bindings(tree)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', unit(source))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                fail(f'FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr}')
            print(result.stdout.rstrip())
        for name, before, after in MUTANTS:
            if source.count(before) != 1:
                fail(f'mutant {name}: expected one {before!r}')
            mutated = source.replace(before, after, 1)
            binary = build(directory, 'mutant-' + name, unit(mutated))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(MUTANTS)} mutants caught, DT and load list checked')


if __name__ == '__main__':
    sys.exit(main())
