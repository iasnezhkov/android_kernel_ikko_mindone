# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'lvts-mt6789'
COPY = 'mindone/modules/mtk_lvts_thermal_ack/lvts_thermal.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ('cold-boot-and-rck', 'fake-efuse-and-trips', 'irq-and-suspend-resume')
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-Wno-unused-const-variable', '-Wno-gnu-statement-expression-from-macro-expansion',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def build(work, name, source_text):
    unit = work / name
    unit.mkdir()
    src = unit / 'lvts_thermal.c'
    src.write_text(source_text)
    fake = unit / 'fake'
    for inc in re.findall(r'^#include <([^>]+)>', source_text, re.M):
        p = fake / inc
        p.parent.mkdir(parents=True, exist_ok=True)
        p.touch()
    flags = FLAGS + ['-include', str(HARNESS / 'stubs.h'), '-I', str(HARNESS), '-I', str(fake)]
    objs = []
    for idx, path in enumerate((HARNESS / 'model.c', src)):
        obj = unit / f'{idx}.o'
        res = subprocess.run(flags + ['-c', str(path), '-o', str(obj)], capture_output=True, text=True, timeout=300)
        if res.returncode:
            fail(f'{name} failed to compile {path.name}:\n{res.stdout}{res.stderr}')
        objs.append(str(obj))
    binary = unit / 'bin'
    res = subprocess.run(['cc', '-fsanitize=address,undefined'] + objs + ['-o', str(binary)],
                         capture_output=True, text=True, timeout=300)
    if res.returncode:
        fail(f'{name} failed to link:\n{res.stdout}{res.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Verify MT6789 ACK LVTS driver MMIO, RCK v5, trips, IRQ, and suspend/resume')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / COPY).read_text()

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        work = Path(temporary)
        candidate = build(work, 'candidate', source)
        for case in CASES:
            res = run(candidate, case)
            if res.returncode:
                fail(f'FAIL {case}:\n{res.stdout}{res.stderr}')
            print(res.stdout.rstrip())

        mutants = (
            ('drop-calscale-write',
             'value = LVTS_CALSCALE_CONF;\n\twritel(value, LVTS_CALSCALE(lvts_ctrl->base));',
             'value = LVTS_CALSCALE_CONF;'),
            ('revert-temp-to-raw-u32',
             's64 raw_temp = ((s64)(golden_temp_offset - temperature)) << 14;',
             'u32 raw_temp = ((s64)(golden_temp_offset - temperature)) << 14;'),
            ('restore-gt-62-cap',
             'golden_temp = all_zero ?\n\t\t\t(lvts_data->default_golden_temp ? lvts_data->default_golden_temp : LVTS_GOLDEN_TEMP_DEFAULT) :\n\t\t\t(int)gt;',
             'golden_temp = (!all_zero && gt < LVTS_GOLDEN_TEMP_MAX) ? (int)gt : LVTS_GOLDEN_TEMP_DEFAULT;'),
            ('wrong-monctl1',
             '.monctl1\t= LVTS_MT6789_MONCTL1,',
             '.monctl1\t= 0,'),
            ('wrong-dominator-ctrl0',
             '.dominator_sensor = 1,\n\t\t.rck_word_index = 8,',
             '.dominator_sensor = 0,\n\t\t.rck_word_index = 8,'),
            ('drop-low-power-single',
             'lvts_write_device_cmd(lvts_ctrl, LVTS_DEV_LOW_POWER_SINGLE);',
             '(void)lvts_ctrl;'),
            ('drop-rck-drift-fallback',
             'rc_now = delta > 1061 ? lvts_ctrl->count_rc : rc_data;',
             'rc_now = delta > 99999 ? lvts_ctrl->count_rc : rc_data;'),
            ('drop-irq-null-tz-guard',
             'if (lvts_ctrl->sensors[i].tz)\n\t\t\tthermal_zone_device_update(lvts_ctrl->sensors[i].tz,',
             'thermal_zone_device_update(lvts_ctrl->sensors[i].tz,'),
            ('drop-resume-reinit',
             'ret = lvts_domain_hw_init(dev, lvts_td, lvts_td->lvts_data);',
             'ret = 0;'),
            ('drop-soc-max-zone',
             '.has_soc_max\t= true,',
             '.has_soc_max\t= false,'),
            ('drop-prottc-clamp',
             'msr_raw = min_t(u32, msr_raw, 0x7FFF);',
             '(void)msr_raw;'),
            ('drop-rck-timeout-check',
             'rc_data = poll_rc ? 0 : (lvts_read_device_reg0(lvts_ctrl) & GENMASK(23, 0));',
             'rc_data = lvts_read_device_reg0(lvts_ctrl) & GENMASK(23, 0);'),
            ('drop-resume-clk-cleanup',
             'clk_disable_unprepare(lvts_td->clk);\n\t\t\treturn ret;',
             'return ret;'),
            ('drop-devm-hw-stop',
             'ret = devm_add_action_or_reset(dev, lvts_domain_hw_stop, lvts_td);',
             'ret = 0;'),
        )
        for name, before, after in mutants:
            if source.count(before) != 1:
                fail(f'mutant {name} target count={source.count(before)}')
            mutant = build(work, name, source.replace(before, after, 1))
            if all(run(mutant, case).returncode == 0 for case in CASES):
                fail(f'mutant {name} survived')
            print(f'PASS mutant-killed {name}')
    print(f'{CHECK}: PASS ({len(CASES)} cases, {len(mutants)} mutants)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
