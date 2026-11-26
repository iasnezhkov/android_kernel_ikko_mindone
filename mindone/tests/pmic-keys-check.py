# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'pmic-keys'
SRC = 'mindone/modules/mtk_pmic_keys/mtk_pmic_keys.c'
MFD = 'mindone/modules/mt6397/mt6397-core.c'
HARNESS = Path(__file__).resolve().parent / CHECK
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def build_and_run(work, name, src):
    unit = work / name
    unit.mkdir()
    fake = unit / 'fake'
    for inc in re.findall(r'^#include <([^>]+)>', src, re.M):
        p = fake / inc
        p.parent.mkdir(parents=True, exist_ok=True)
        p.touch()
    cpath = unit / 'mtk_pmic_keys.c'
    cpath.write_text(src + '\n#include "cases.c"\n')
    binary = unit / 'bin'
    cmd = FLAGS + ['-include', str(HARNESS / 'stubs.h'), '-I', str(HARNESS), '-I', str(fake),
                   str(cpath), '-o', str(binary)]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if res.returncode:
        return res
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Verify ACK-based mtk_pmic_keys MT6366 reset bits, named IRQs and wakeup')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    src = (tree / SRC).read_text()
    mfd = (tree / MFD).read_text()

    m = re.search(r'mt6366_keys_resources\[\]\s*=\s*\{(.*?)\};', mfd, re.S)
    if not m:
        fail('mt6366_keys_resources missing in mt6397-core.c')
    for req in ('"powerkey"', '"homekey"', '"powerkey_r"', '"homekey_r"'):
        if req not in m.group(1):
            fail(f'mt6366_keys_resources missing named IRQ {req}')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        work = Path(tmp)
        res = build_and_run(work, 'candidate', src)
        if res.returncode:
            fail(f'candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('revert-mt6358-pwrkey-rst-bit',
             'MTK_PMIC_KEYS_REGS(MT6358_TOPSTATUS,\n\t\t\t\t   0x2, MT6358_PSC_TOP_INT_CON0, 0x5,\n\t\t\t\t   MTK_PMIC_MT6331_PWRKEY_RST),',
             'MTK_PMIC_KEYS_REGS(MT6358_TOPSTATUS,\n\t\t\t\t   0x2, MT6358_PSC_TOP_INT_CON0, 0x5,\n\t\t\t\t   MTK_PMIC_PWRKEY_RST),'),
            ('revert-mt6358-rst-du-mask',
             '.pmic_rst_reg = MT6358_TOP_RST_MISC,\n\t.rst_lprst_mask = MTK_PMIC_MT6331_RST_DU_MASK,',
             '.pmic_rst_reg = MT6358_TOP_RST_MISC,\n\t.rst_lprst_mask = MTK_PMIC_RST_DU_MASK,'),
            ('drop-mt6366-compatible',
             '.compatible = "mediatek,mt6366-keys",',
             '.compatible = "mediatek,mt6366-disabled",'),
            ('drop-release-handler',
             'mtk_pmic_keys_release_irq_handler_thread,\n\t\t\t\t\t\tIRQF_ONESHOT',
             'mtk_pmic_keys_irq_handler_thread,\n\t\t\t\t\t\tIRQF_ONESHOT'),
            ('drop-pm-stay-awake',
             'pm_stay_awake(info->keys->dev);',
             '(void)info;'),
            ('drop-wake-release-irq',
             'if (keys->keys[index].irq_r > 0)\n\t\t\t\tenable_irq_wake(keys->keys[index].irq_r);',
             'if (0 && keys->keys[index].irq_r > 0)\n\t\t\t\tenable_irq_wake(keys->keys[index].irq_r);'),
            ('drop-grandparent-regmap',
             'if (!keys->regmap && pdev->dev.parent && pdev->dev.parent->parent)\n\t\tkeys->regmap = dev_get_regmap(pdev->dev.parent->parent, NULL);',
             'if (0 && !keys->regmap && pdev->dev.parent && pdev->dev.parent->parent)\n\t\tkeys->regmap = dev_get_regmap(pdev->dev.parent->parent, NULL);'),
        )
        for name, before, after in mutants:
            if src.count(before) != 1:
                fail(f'mutant {name} count={src.count(before)}')
            mres = build_and_run(work, 'mutant-' + name, src.replace(before, after, 1))
            if mres.returncode == 0:
                fail(f'mutant {name} survived')
            print(f'PASS mutant {name}')
    print(f'{CHECK}: PASS ({len(mutants)} mutants caught)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
