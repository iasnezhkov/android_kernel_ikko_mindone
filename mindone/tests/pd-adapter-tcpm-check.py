# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'pd-adapter-tcpm'
HARNESS = Path(__file__).resolve().parent / CHECK
SOURCE = 'mindone/modules/mtk_pd_adapter/mtk_pd_adapter.c'
HEADER = 'mindone/modules/mtk_pd_adapter/adapter_class.h'
CLASS_HEADER = 'mindone/modules/adapter_class/adapter_class.h'
CONSUMERS = ('mindone/modules/mtk_charger_framework/mtk_basic_charger.c', 'mindone/modules/mtk_pd_charging/mtk_pd_hal.c')
CASES = ('non-pd', 'pd', 'hard-reset', 'pps', 'avs', 'late', 'error', 'coalesced')
FORBIDDEN = re.compile(r'\b(tcpc_\w+|tcpm_\w+|register_tcp_dev_notifier|TCP_NOTIFY_\w+|power_supply_set_property)\b')
MUTANTS = (
    ('typec-only-at-once', '\t\t} else if (info->pd_type == MTK_PD_CONNECT_NONE &&\n\t\t\t   !delayed_work_pending(&info->settle_work)) {\n'
     '\t\t\tschedule_delayed_work(&info->settle_work,\n\t\t\t\tmsecs_to_jiffies(MTK_PD_ADAPTER_TYPEC_SETTLE_MS));\n',
     '\t\t} else if (info->pd_type == MTK_PD_CONNECT_NONE) {\n'
     '\t\t\tmtk_pd_adapter_notify(info, MTK_PD_CONNECT_TYPEC_ONLY_SNK);\n'),
    ('hard-reset-as-detach', '\t\tif (pd && mtk_pd_adapter_pd_ready(info->pd_type))\n', '\t\tif (0)\n'),
    ('default-rp-zero', '\treturn 500;\n}', '\treturn current_ma;\n}'),
    ('any-voltage', '\tif (mV == READ_ONCE(info->contract_mv) && mA > 0 &&\n', '\tif (mA > 0 &&\n'),
    ('cap-without-pd', '\tif (!info || type != MTK_PD ||\n\t    !mtk_pd_adapter_pd_ready(READ_ONCE(info->pd_type)))\n',
     '\tif (!info || type != MTK_PD)\n'),
    ('pps-as-fixed', '\tevent = pps ? MTK_PD_CONNECT_PE_READY_SNK_APDO :\n\t\t      MTK_PD_CONNECT_PE_READY_SNK;\n',
     '\tevent = MTK_PD_CONNECT_PE_READY_SNK;\n'),
    ('avs-as-type-c', '\tpd = pps || usb_type == POWER_SUPPLY_USB_TYPE_PD ||\n\t     usb_type == POWER_SUPPLY_USB_TYPE_PD_SPR_AVS;\n',
     '\tpd = pps || usb_type == POWER_SUPPLY_USB_TYPE_PD;\n'),
    ('contract-change-silent', '\tif (event != info->pd_type || info->contract_mv != old_mv ||\n\t    info->contract_ma != old_ma)\n',
     '\tif (event != info->pd_type)\n'),
    ('rp-kept-after-detach', '\t\tWRITE_ONCE(info->rp_ma, 0);\n', ''),
    ('late-tcpm-missed', '(psy->dev.parent && dev_fwnode(psy->dev.parent) == info->typec)', '0'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-missing-field-initializers', '-Wno-unused-const-variable',
         '-Wno-unused-but-set-variable',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def strip_includes(text):
    return re.sub(r'^#include .*\n', '', text, flags=re.M)


def unit(header, source):
    return '#include "stubs.h"\n' + strip_includes(header) + strip_includes(source) + '\n#include "cases.c"\n'


def static_checks(tree, source):
    found = sorted(set(FORBIDDEN.findall(source)))
    if found:
        fail('the adapter still uses the TCPC stack or writes charger properties: ' + ', '.join(found))
    if (tree / HEADER).read_text() != (tree / CLASS_HEADER).read_text():
        fail(f'{HEADER} is not the adapter_class header')
    used = set()
    for consumer in CONSUMERS:
        used |= set(re.findall(r'\badapter_dev_(\w+)\(', (tree / consumer).read_text()))
    provided = {'get_property', 'get_cap', 'set_cap'}
    if used - provided:
        fail('consumers call adapter operations the adapter does not provide: ' + ', '.join(sorted(used - provided)))


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    result = subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode:
        fail(f'{name} does not compile\n{result.stdout}{result.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Run mtk_pd_adapter on a model of the TCPM power supply and the '
                                     'charger framework under ASan/UBSan: Type-C only, PD, PPS, hard reset, '
                                     'detach, late TCPM and read errors must give the MTK adapter events, Rp '
                                     'level and contract capability the fork derived from the Richtek notifier; '
                                     'every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    header = (tree / HEADER).read_text()
    static_checks(tree, source)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', unit(header, source))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                fail(f'FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr[-3000:]}')
            print(result.stdout.rstrip())
        for name, before, after in MUTANTS:
            if source.count(before) != 1:
                fail(f'mutant {name}: expected one {before!r}')
            binary = build(directory, 'mutant-' + name, unit(header, source.replace(before, after, 1)))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
