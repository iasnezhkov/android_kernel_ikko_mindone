# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'codec-volume'
SOURCE = 'mindone/modules/snd_soc_mt6366/mt6358.c'
HEADER = 'mindone/modules/include/sound/soc/codecs/mt6358.h'
FUNCTIONS = ('mt6358_snd_soc_put_volsw', 'mt6358_put_volsw')
FLAGS = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
         '-Wno-sign-compare', '-Wno-missing-field-initializers', '-Wno-unused-variable', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all']
MUTANTS = (
    ('dl-not-inverted', ('MT6358_ZCD_CON2, 0, 7, 0x12, 1,', 'MT6358_ZCD_CON2, 0, 7, 0x12, 0,'),
     'FAIL Headphone Volume: value v writes register field'),
    ('handset-not-inverted', ('MT6358_ZCD_CON3, 0, 0x12, 1,', 'MT6358_ZCD_CON3, 0, 0x12, 0,'),
     'FAIL Handset Volume: value v writes register field'),
    ('pga-left-gain-only', ('\tcase MT6358_AUDENC_ANA_CON0:\n\tcase MT6358_AUDENC_ANA_CON1:\n',
                            '\tcase MT6358_AUDENC_ANA_CON1:\n'),
     'FAIL PGA Volume writes left to CON0'),
    ('old-headset-name', ('"Headphone Volume"', '"Headset Volume"'), 'FAIL Headphone Volume exists'),
)


def closing(source, start):
    depth = 0
    for index in range(start, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return index + 1
    raise SystemExit(f'{CHECK}: unbalanced braces in {SOURCE}')


def function(source, name):
    found = list(re.finditer(r'^static int ' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {name} has {len(found)} definitions in {SOURCE}')
    start = found[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def unit(source):
    parts = []
    for name in ('playback_tlv', 'capture_tlv'):
        found = re.findall(r'^static const DECLARE_TLV_DB_SCALE\(' + name + r', .*\);$', source, re.M)
        if len(found) != 1:
            raise SystemExit(f'{CHECK}: expected one {name} in {SOURCE}')
        parts += found
    table = re.search(r'^static const struct snd_kcontrol_new mt6358_snd_controls\[\] = \{.*?^\};', source,
                      re.M | re.S)
    if not table:
        raise SystemExit(f'{CHECK}: mt6358_snd_controls is missing from {SOURCE}')
    parts += [function(source, name) for name in FUNCTIONS]
    parts.append(table.group())
    return '\n'.join(parts) + '\n'


def build(directory, name, tree, code):
    fixture = (Path(__file__).with_name('codec-volume.c')).read_text().replace('PRODUCTION_CODE', code)
    path = directory / (name + '.c')
    path.write_text(fixture)
    binary = directory / name
    subprocess.run(FLAGS + [f'-DCODEC_HEADER="{tree / HEADER}"', str(path), '-o', str(binary)], check=True,
                   timeout=120)
    return binary


def main():
    parser = argparse.ArgumentParser(description='Compile the MT6366 codec volume controls and their put handlers '
                                     'against a register model: the three playback volumes must be inverted like '
                                     'upstream mt6358.c (largest value = +8 dB = register 0), PGA Volume must be '
                                     'one stereo control over both preamplifier registers, the fork-only names '
                                     'must be gone, and every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        result = subprocess.run([str(build(directory, 'candidate', tree, unit(source)))], capture_output=True,
                                text=True, timeout=60)
        print(result.stdout.rstrip())
        if result.returncode:
            raise SystemExit(f'{CHECK}: candidate failed\n{result.stderr}')
        for name, (before, after), expected in MUTANTS:
            if source.count(before) != 1:
                raise SystemExit(f'{CHECK}: mutant {name}: expected one {before!r}')
            mutated = source.replace(before, after)
            outcome = subprocess.run([str(build(directory, 'mutant-' + name, tree, unit(mutated)))],
                                     capture_output=True, text=True, timeout=60)
            if not outcome.returncode or expected not in outcome.stdout:
                raise SystemExit(f'{CHECK}: mutant {name} was not caught\n{outcome.stdout}{outcome.stderr}')
            print(f'PASS mutant {name} caught')


if __name__ == '__main__':
    sys.exit(main())
