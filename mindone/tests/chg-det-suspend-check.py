# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = 'chg-det-suspend'
CTRL = 'mindone/modules/mtk_chg_det/controller.c'
ADAPT = 'mindone/modules/mtk_chg_det/adapter.c'
ETA = 'mindone/modules/eta6965_charger/eta6965_charger.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ('irq-during-suspend', 'enodata-retry-and-cap', 'silent-attach-in-sleep', 'eta-suspend-resume')
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def extract_unit(ctrl, eta):
    c_start = ctrl.index('#define MINDONE_CHR_RETRY_MAX')
    c_end = ctrl.index('static int __init mindone_chr_init')
    e_get_start = eta.index('static int eta6965_psy_get_property(')
    e_get_end = eta.index('static int eta6965_psy_is_writeable(')
    e_pm_start = eta.index('static int eta6965_suspend(')
    e_pm_end = eta.index('static const struct dev_pm_ops eta6965_pm_ops')
    return '\n'.join([
        '#include "stubs.h"',
        ctrl[c_start:c_end],
        eta[e_get_start:e_get_end],
        eta[e_pm_start:e_pm_end],
        '#include "cases.c"',
    ]) + '\n'


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
    parser = argparse.ArgumentParser(description='Verify mtk_chg_det and eta6965 suspend/resume race handling')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    ctrl = (tree / CTRL).read_text()
    adapt = (tree / ADAPT).read_text()
    eta = (tree / ETA).read_text()
    if '.pm = pm_sleep_ptr(&mindone_chr_pm_ops),' not in adapt:
        fail('mindone_chr_driver missing pm_sleep_ptr(&mindone_chr_pm_ops)')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', extract_unit(ctrl, eta))
        for case in CASES:
            res = run(candidate, case)
            if res.returncode:
                fail(f'FAIL {case}:\n{res.stdout}{res.stderr}')
            print(res.stdout.rstrip())

        mutants = (
            ('ctrl-drop-irq-suspend-defer', 'ctrl',
             'if (controller->suspended) {\n\t\tcontroller->pending_resume = true;\n\t\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\t\treturn;\n\t}\n\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\tmindone_chr_event(controller, false);',
             'if (0) {\n\t\tcontroller->pending_resume = true;\n\t\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\t\treturn;\n\t}\n\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\tmindone_chr_event(controller, false);'),
            ('ctrl-drop-enodata-retry', 'ctrl',
             'if (ret == -ENODATA || ret == -EAGAIN) {',
             'if (0 && (ret == -ENODATA || ret == -EAGAIN)) {'),
            ('ctrl-drop-silent-attach', 'ctrl',
             'if (read_ok && (!controller->last_attached_valid ||\n\t\t\tattached != controller->last_attached))\n\t\tneed = true;',
             'if (0)\n\t\tneed = true;'),
            ('ctrl-drop-event-suspend-recheck', 'ctrl',
             'if (controller->suspended) {\n\t\t\tcontroller->pending_resume = true;\n\t\t\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\t\t\tmutex_unlock(&controller->detector);\n\t\t\treturn;\n\t\t}',
             'if (0) {\n\t\t\tcontroller->pending_resume = true;\n\t\t\tspin_unlock_irqrestore(&controller->state_lock, flags);\n\t\t\tmutex_unlock(&controller->detector);\n\t\t\treturn;\n\t\t}'),
            ('ctrl-drop-second-cancel', 'ctrl',
             'mutex_unlock(&controller->detector);\n\n\tcanceled = cancel_delayed_work_sync(&controller->work);',
             'mutex_unlock(&controller->detector);\n\n\tcanceled = false;'),
            ('eta-drop-set-enodata', 'eta',
             'ret = property == POWER_SUPPLY_PROP_ONLINE ?\n\t\t\t      -ENODATA : 0;',
             'power_supply_changed(psy); ret = 0;'),
            ('eta-drop-get-notify', 'eta',
             'if (READ_ONCE(binding->eta->suspended))\n\t\t\t\t\tbinding->eta->notify_on_resume = true;',
             'if (0)\n\t\t\t\t\tbinding->eta->notify_on_resume = true;'),
            ('eta-drop-reg08-delta', 'eta',
             'if (!eta->suspend_reg08_valid || rc != 0 ||\n\t\t    ((reg08 ^ eta->suspend_reg08) &\n\t\t     (REG08_VBUS_STAT_MASK | REG08_PG_STAT_MASK |\n\t\t      REG08_CHRG_STAT_MASK)))\n\t\t\tnotify = true;',
             'if (0)\n\t\t\tnotify = true;'),
        )
        for name, target, before, after in mutants:
            m_ctrl, m_eta = ctrl, eta
            if target == 'ctrl':
                assert m_ctrl.count(before) == 1
                m_ctrl = m_ctrl.replace(before, after, 1)
            else:
                assert m_eta.count(before) == 1
                m_eta = m_eta.replace(before, after, 1)
            binary = build(directory, 'mutant-' + name, extract_unit(m_ctrl, m_eta))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(mutants)} mutants caught')


if __name__ == '__main__':
    sys.exit(main())
