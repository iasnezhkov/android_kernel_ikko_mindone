# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'i2c-slow-target'
SOURCE = 'mindone/modules/i2c_mt65xx/i2c_mt65xx.c'
BUS_DTS = 'mindone/dts/mt6789-bus.dtsi'
OVERLAY_DTS = 'mindone/dts/mindone-overlay.dtso'
LOAD = 'mindone/vendor_dlkm.modules.load'
HARNESS = Path(__file__).resolve().parent / CHECK

PINS = {
    'mtk_i2c_readw': '3444940cb112290b953ab3852c9f992119e1a084f834552aeab71b78be0a38f0',
    'mtk_i2c_writew': 'f20cdf74046ac9a4b9c67f5cb15e64f32e2f69e275ae076791ee74e30d6b77ae',
    'mtk_i2c_init_hw': 'ecc5e2c26971d076e787b7f9fb4ee6389470f486abac5029a50c3fb96ebe3cd2',
    'mtk_i2c_get_spec': '1e4c4de64899c6cbb9c3d66416b7ed5012f9540668cdfc843bf07f118ad2c7ab',
    'mtk_i2c_max_step_cnt': '76d7e14580f7bd805e85fa5419106525e71d4dc6bf879b7c572570b4c82ff1fa',
    'mtk_i2c_check_ac_timing': 'b1fa3bdabc585a5ac4eeac7be978745d5f95a71dff5789280e58140d2616a634',
    'mtk_i2c_calculate_speed': 'e59ada4eb31a05a4ad00a3a4e4ee227348553dcb262b7af4d992940665a9a24a',
    'mtk_i2c_probe': 'bcc28cb4d20b46eebb92379fe305326516eaf9b445d44cc76c9007a71fe14299',
    'mtk_i2c_resume_noirq': 'f0523ae2f8226b27d5ced29068c787c00e6c315bb5cb945ae85b88b4ea2656fc',
    'mtk_i2c_do_transfer': '1044c19ea9597fa2078e51b51072c32b8e5e756e98e1860da730f436d62abf86',
    'mtk_i2c_irq': 'de70e0ab7177b0ed811460b206fbb12e8c97196a1bdeba5a4dc8eed9feb58e24',
}
ORIGINALS = {
    'mtk_i2c_calc_timing': ('mtk_i2c_set_speed', 'aa3041ac6fbafdc3efe643a8dcba42072576daddba886e1704fb583daeb209e6', (
        ('static int mtk_i2c_calc_timing(', 'static int mtk_i2c_set_speed('),
        ('\treturn clk_div > max_clk_div ? -EINVAL : 0;\n', '\treturn 0;\n'),
    )),
    'mtk_i2c_transfer': ('mtk_i2c_transfer', '6ccf00e80baf076bb568625b3827da6e343f36e7d6298e683c437291bc629606', (
        ('\tconst struct mtk_i2c_speed *want_speed;\n\tunsigned int slow_hz;\n\n\tslow_hz = mtk_i2c_slow_hz(i2c, msgs, num);\n\tif (slow_hz) {\n'
         '\t\tret = mtk_i2c_prepare_slow(i2c, slow_hz);\n\t\tif (ret)\n\t\t\treturn ret;\n\t}\n\n', '\n'),
        ('\twant_speed = slow_hz ? &i2c->slow_speed : &i2c->dt_speed;\n\tif (i2c->hw_speed_hz != want_speed->speed_hz)\n'
         '\t\tmtk_i2c_use_speed(i2c, want_speed);\n\n', ''),
    )),
}
FUNCTIONS = ['mtk_i2c_readw', 'mtk_i2c_writew', 'mtk_i2c_init_hw', 'mtk_i2c_get_spec', 'mtk_i2c_max_step_cnt',
             'mtk_i2c_check_ac_timing', 'mtk_i2c_calculate_speed', 'mtk_i2c_calc_timing', 'mtk_i2c_save_speed',
             'mtk_i2c_load_speed', 'mtk_i2c_set_speed', 'mtk_i2c_use_speed', 'mtk_i2c_prepare_slow',
             'mtk_i2c_slow_hz', 'mtk_i2c_transfer', 'mtk_i2c_slow_parse', 'mtk_i2c_slow_set', 'mtk_i2c_slow_get',
             'mtk_i2c_set_slow_target']
GLOBALS = (r'^static DEFINE_SPINLOCK\(mtk_i2c_slow_lock\);$',
           r'^static struct mtk_i2c_target mtk_i2c_slow\[MTK_I2C_TARGETS\];$')
CASES = ['equivalence', 'transfer-off', 'slow-100k', 'rate-change', 'slow-from-hs', 'unsupported', 'parse', 'setter',
         'fast-1m', 'targets']
MUTANTS = (
    ('swap-want-speed', (('want_speed = slow_hz ? &i2c->slow_speed : &i2c->dt_speed;',
                          'want_speed = slow_hz ? &i2c->dt_speed : &i2c->slow_speed;'),),
     'slow-100k', 'same_regs(&x->regs, r_slow)'),
    ('first-message-only', (('msgs[i].addr == slow[j].addr', 'msgs[0].addr == slow[j].addr'),),
     'slow-100k', 'same_regs(&x->regs, r_slow)'),
    ('no-dt-return', (('want_speed = slow_hz ? &i2c->slow_speed : &i2c->dt_speed;\n'
                       '\tif (i2c->hw_speed_hz != want_speed->speed_hz)\n\t\tmtk_i2c_use_speed(i2c, want_speed);',
                       'want_speed = slow_hz ? &i2c->slow_speed : &i2c->dt_speed;\n'
                       '\tif (slow_hz && i2c->hw_speed_hz != want_speed->speed_hz)\n'
                       '\t\tmtk_i2c_use_speed(i2c, want_speed);'),),
     'slow-100k', 'x->speed_hz == DT_HZ && same_regs(&x->regs, r_dt)'),
    ('always-reprogram', (('\tif (i2c->hw_speed_hz != want_speed->speed_hz)\n\t\tmtk_i2c_use_speed(i2c, want_speed);',
                           '\tmtk_i2c_use_speed(i2c, want_speed);'),),
     'slow-100k', 'd->m.writes == writes'),
    ('adapter-ignored', (('if (slow[j].hz && slow[j].nr == i2c->adap.nr &&', 'if (slow[j].hz &&'),),
     'transfer-off', 'same_log(&devs[0].m, &devs[1].m)'),
    ('dt-rate-reprograms', (('\treturn hz == dt_hz ? 0 : hz;\n', '\treturn hz;\n'),),
     'transfer-off', 'dev_info_count == info && dev_warn_count == warn'),
    ('ten-bit-matches', (('\t\t\t    !(msgs[i].flags & I2C_M_TEN) &&\n', ''),),
     'slow-100k', 'x->speed_hz == DT_HZ && same_regs(&x->regs, r_dt)'),
    ('mixed-takes-fastest', (('if (!i || msg_hz < hz)', 'if (!i || msg_hz > hz)'),),
     'fast-1m', 'd->xfer[i].speed_hz == st->hz && same_regs(&d->xfer[i].regs, want)'),
    ('address-range', (('a > 0x77', 'a > 0x78'),), 'parse', 'mtk_i2c_slow_set(bad[i], NULL) == -EINVAL'),
    ('duplicate-target', (('\t\t\tfor (i = 0; i < count; i++)\n\t\t\t\tif (slow[i].nr == slow[count].nr &&\n'
                           '\t\t\t\t    slow[i].addr == slow[count].addr)\n\t\t\t\t\treturn -EINVAL;\n', ''),),
     'parse', 'mtk_i2c_slow_set(bad[i], NULL) == -EINVAL'),
    ('four-targets', (('#define MTK_I2C_TARGETS\t\t\t3\n', '#define MTK_I2C_TARGETS\t\t\t4\n'),),
     'parse', 'mtk_i2c_slow_set(bad[i], NULL) == -EINVAL'),
    ('no-prev-reload', (('\t}\n\tmtk_i2c_load_speed(i2c, &prev);\n', '\t}\n'),),
     'unsupported', 'same_speed(&now, &d->i2c.dt_speed)'),
    ('stale-fields', (('\tmtk_i2c_load_speed(i2c, &fresh);\n', '\ti2c->speed_hz = fresh.speed_hz;\n'),),
     'slow-from-hs', 'same_speed(&d->i2c.slow_speed, &s100)'),
    ('one-cached-set', (('\t\ti2c->slow_next = (i2c->slow_next + 1) % MTK_I2C_TARGETS;\n', ''),),
     'targets', 'dev_info_count == info + 3'),
    ('setter-overwrites', (('\t} else if (hz) {\n\t\tret = -EBUSY;\n\t}\n',
                            '\t} else if (hz) {\n\t\tmtk_i2c_slow[0].nr = nr;\n\t\tmtk_i2c_slow[0].addr = addr;\n'
                            '\t\tmtk_i2c_slow[0].hz = hz;\n\t}\n'),),
     'setter', 'mtk_i2c_set_slow_target(4, 0x50, 100000) == -EBUSY'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-unused-const-variable', '-Wno-missing-field-initializers', '-Wno-sign-compare',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def skip(source, index):
    if source.startswith('//', index):
        return source.index('\n', index)
    if source.startswith('/*', index):
        return source.index('*/', index + 2) + 2
    if source[index] in '"\'':
        quote, index = source[index], index + 1
        while source[index] != quote:
            index += 2 if source[index] == '\\' else 1
        return index + 1
    return None


def closing(source, index):
    depth = 0
    while True:
        after = skip(source, index)
        if after is not None:
            index = after
            continue
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if not depth:
                return index + 1
        index += 1


def function(source, name):
    pattern = r'^(?:static )?\w[^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {name} has {len(found)} definitions in {SOURCE}')
    start = found[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def replace_once(text, before, after, label):
    if text.count(before) != 1:
        raise SystemExit(f'{CHECK}: {label}: expected one {before!r}')
    return text.replace(before, after)


def originals(source):
    result = {}
    for name, (old_name, pin, edits) in ORIGINALS.items():
        text = function(source, name)
        for before, after in edits:
            text = replace_once(text, before, after, name)
        if digest(text) != pin:
            raise SystemExit(f'{CHECK}: {name} is not the reviewed {old_name} plus the slow_target edits: sha256 '
                             f'{digest(text)}, pinned {pin}')
        result[old_name] = text.replace(old_name + '(', 'orig_' + old_name + '(', 1)
    return result


def reviewed(source):
    for name, pin in PINS.items():
        if digest(function(source, name)) != pin:
            raise SystemExit(f'{CHECK}: {name} changed since review: sha256 {digest(function(source, name))}, '
                             f'pinned {pin}')
    return originals(source)


def bindings(tree, source):
    if not re.search(r'^module_param_cb\(slow_target, &mtk_i2c_slow_ops, NULL, 0644\);$', source, re.M):
        raise SystemExit(f'{CHECK}: slow_target is not a 0644 module parameter on mtk_i2c_slow_ops')
    ops = re.search(r'^static const struct kernel_param_ops mtk_i2c_slow_ops = \{(.*?)^\};', source, re.M | re.S)
    if not ops or '.set = mtk_i2c_slow_set,' not in ops.group(1) or '.get = mtk_i2c_slow_get,' not in ops.group(1):
        raise SystemExit(f'{CHECK}: mtk_i2c_slow_ops does not bind mtk_i2c_slow_set and mtk_i2c_slow_get')
    if '\t.master_xfer = mtk_i2c_transfer,\n' not in source:
        raise SystemExit(f'{CHECK}: mtk_i2c_transfer is no longer the adapter transfer')
    if 'i2c_mt65xx.ko' not in (tree / LOAD).read_text().split():
        raise SystemExit(f'{CHECK}: i2c_mt65xx.ko is not in {LOAD}')
    bus = re.search(r'^\ti2c4: i2c@11eb1000 \{(.*?)^\t\};', (tree / BUS_DTS).read_text(), re.M | re.S)
    if not bus or 'compatible = "mediatek,mt6983-i2c";' not in bus.group(1) or 'clock-div = <0x01>;' not in bus.group(1):
        raise SystemExit(f'{CHECK}: i2c4 is no longer a mt6983-i2c controller with clock-div 1 in {BUS_DTS}')
    speeds = re.findall(r'^&i2c4 \{.*?clock-frequency = <(0x[0-9a-f]+)>;', (tree / OVERLAY_DTS).read_text(),
                        re.M | re.S)
    if not speeds or {int(value, 16) for value in speeds} != {400000}:
        raise SystemExit(f'{CHECK}: i2c4 does not run at 400 kHz in {OVERLAY_DTS}: {speeds}')


def unit(source, old):
    start = source.index('#define I2C_RS_TRANSFER')
    head = source[start:source.index('static const struct of_device_id mtk_i2c_of_match')]
    statics = []
    for pattern in GLOBALS:
        found = re.findall(pattern, source, re.M)
        if len(found) != 1:
            raise SystemExit(f'{CHECK}: expected one {pattern} in {SOURCE}')
        statics += found
    parts = ['#include "stubs.h"', head, '\n'.join(statics), '#include "hooks.c"']
    parts += [function(source, name) for name in FUNCTIONS]
    parts += [old['mtk_i2c_set_speed'], old['mtk_i2c_transfer'], '#include "cases.c"']
    return '\n'.join(parts) + '\n'


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)], check=True, timeout=300)
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Compile the timing, transfer and slow_target code of i2c_mt65xx.c '
                                     'against a register model with ASan/UBSan, next to the reviewed original '
                                     'mtk_i2c_set_speed and mtk_i2c_transfer: with slow_target off or not matching the '
                                     'registers and transactions must equal the original; a transfer must run on the '
                                     'registers a probe at the lowest rate its messages allow programs (a target\'s '
                                     'rate, slower or up to 1 MHz faster than the DT bus, or the DT rate) and return to '
                                     'the DT set; the 1 MHz set must meet the Fm+ limits; parsing must reject malformed '
                                     'values; every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    old = reviewed(source)
    bindings(tree, source)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', unit(source, old))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                raise SystemExit(f'{CHECK}: FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr}')
            print(result.stdout.rstrip())
        for name, edits, case, assertion in MUTANTS:
            mutated = source
            for before, after in edits:
                mutated = replace_once(mutated, before, after, 'mutant ' + name)
            result = run(build(directory, 'mutant-' + name, unit(mutated, old)), case)
            if not result.returncode or 'Assertion' not in result.stderr or assertion not in result.stderr:
                raise SystemExit(f'{CHECK}: mutant {name} was not caught by {case} at {assertion!r}\n'
                                 f'{result.stdout}{result.stderr}')
            print(f'PASS mutant {name}: {case} fails at {assertion}')
    print(f'{CHECK}: PASS {len(PINS)} reviewed functions equal their pins, {len(ORIGINALS)} originals rebuilt from the '
          f'patched source match their pins, {len(CASES)} cases, {len(MUTANTS)} mutants caught; kernel build and '
          f'device check pending')


if __name__ == '__main__':
    main()
