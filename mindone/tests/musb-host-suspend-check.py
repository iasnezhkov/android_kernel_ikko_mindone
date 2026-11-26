# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = 'musb-host-suspend'
CORE = 'mindone/modules/musb_hdrc_recon/musb_core.c'
VIRTHUB = 'mindone/modules/musb_hdrc_recon/musb_virthub.c'
HOST = 'mindone/modules/musb_hdrc_recon/musb_host.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = (
    'a-suspend-no-idle-timer',
    'musb-suspend-drains-idle',
    'uwk-v2-disable-clears-cd',
    'port-suspend-no-spin',
    'bus-suspend-already-suspended',
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def extract_unit(core, virthub, host):
    s_start = core.index('static int musb_suspend(struct device *dev)')
    s_end = core.index('const struct dev_pm_ops musb_dev_pm_ops')
    w_start = core.index('static struct regmap *pericfg;')
    w_end = core.index('static int mt_usb_wakeup_init(struct musb *musb)')
    t_start = core.index('static void mt_usb_try_idle(struct musb *musb, unsigned long timeout)')
    t_end = core.index('#endif\n\nstatic void __iomem *infra_mbist;')
    p_start = virthub.index('int musb_port_suspend(struct musb *musb, bool do_suspend)')
    p_end = virthub.index('static void musb_port_reset(struct musb *musb, bool do_reset)')
    b_start = host.index('static int musb_bus_suspend(struct usb_hcd *hcd)')
    b_end = host.index('static int musb_bus_resume(struct usb_hcd *hcd)')
    return '\n'.join([
        '#include "stubs.h"',
        core[s_start:s_end],
        core[w_start:w_end],
        core[t_start:t_end],
        'static ' + virthub[p_start:p_end],
        host[b_start:b_end],
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
    parser = argparse.ArgumentParser(description='Verify MUSB host suspend, idle timer, and UWK V2 cleanup')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    core = (tree / CORE).read_text()
    virthub = (tree / VIRTHUB).read_text()
    host = (tree / HOST).read_text()
    if '.suspend = musb_suspend,' not in core:
        fail('musb_dev_pm_ops missing .suspend = musb_suspend')
    exit_fn = core[core.index('static int mt_usb_exit(struct musb *musb)'):core.index('static void mt_usb_enable_clk')]
    if 'cancel_delayed_work_sync(&idle_work);' not in exit_fn:
        fail('mt_usb_exit missing cancel_delayed_work_sync(&idle_work)')
    if 'devm_regulator_get_optional(&pdev->dev, "vbus")' not in core:
        fail('musb_probe must use devm_regulator_get_optional for vbus')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', extract_unit(core, virthub, host))
        for case in CASES:
            res = run(candidate, case)
            if res.returncode:
                fail(f'FAIL {case}:\n{res.stdout}{res.stderr}')
            print(res.stdout.rstrip())

        mutants = (
            ('drop-a-suspend-idle-guard', 'core',
             'musb->xceiv->otg->state == OTG_STATE_A_SUSPEND ||',
             '0 ||'),
            ('drop-musb-suspend-drain', 'core',
             '\t\ttimer_delete_sync(&musb->idle_timer);\n\tcancel_delayed_work_sync(&idle_work);\n\treturn 0;',
             '\t\ttimer_delete_sync(&musb->idle_timer);\n\t(void)idle_work;\n\treturn 0;'),
            ('drop-uwk-v2-cd-clr', 'core',
             'if (infracg != NULL) {',
             'if (0 && infracg != NULL) {'),
            ('revert-uwk-v2-debounce-mask', 'core',
             'tmp &= ~(USB_CDEN | USB_CDDEBOUNCE(0xf));',
             'tmp &= ~(USB_CDEN | USB_CDDEBOUNCE(0x8));'),
            ('revert-virthub-10k-spin', 'virthub',
             'if (otg->host->b_hnp_enable) {\n\t\t\t\tint retries = 10000;',
             'if (1 || otg->host->b_hnp_enable) {\n\t\t\t\tint retries = 10000;'),
            ('drop-bus-suspend-skip', 'host',
             'if (!(musb->port1_status & USB_PORT_STAT_SUSPEND) ||\n\t    musb->xceiv->otg->state != OTG_STATE_A_SUSPEND) {',
             'if (1) {'),
        )
        for name, target, before, after in mutants:
            mc, mv, mh = core, virthub, host
            if target == 'core':
                assert mc.count(before) == 1, name
                mc = mc.replace(before, after, 1)
            elif target == 'virthub':
                assert mv.count(before) == 1, name
                mv = mv.replace(before, after, 1)
            else:
                assert mh.count(before) == 1, name
                mh = mh.replace(before, after, 1)
            mutant = build(directory, name, extract_unit(mc, mv, mh))
            if all(run(mutant, case).returncode == 0 for case in CASES):
                fail(f'mutant {name} survived')
            print(f'PASS mutant-killed {name}')
    print(f'{CHECK}: PASS ({len(CASES)} cases, {len(mutants)} mutants)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
