# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'usb-role-proxy'
HARNESS = Path(__file__).resolve().parent / CHECK
SOURCE = 'mindone/modules/extcon_mtk_usb/extcon_mtk_usb.c'
HEADER = 'mindone/modules/include/mindone/extcon-mtk-usb.h'
ACCDET = 'mindone/modules/mt6358_accdet/mt6358_accdet.c'
CASES = ('no-tcpm', 'pc', 'otg', 'dr-swap', 'audio', 'audio-late', 'psy-error', 'no-bypass', 'probe', 'fork-parity')
FORBIDDEN = re.compile(r'\b(tcpc_\w+|tcpm_\w+|tcpci_\w+|register_tcp_dev_notifier|TCP_NOTIFY_\w+|regulator_\w+)\b')
MUTANTS = (
    ('tcpm-gates-device', '\tdata = mtk_usb_extcon_data_port(extcon);\n',
     '\tdata = port_role == USB_ROLE_DEVICE ? mtk_usb_extcon_data_port(extcon) : 0;\n'),
    ('audio-not-none', '\tif (*audio) {\n\t\t*role = USB_ROLE_NONE;\n\t\treturn 0;\n\t}\n', ''),
    ('host-not-first', '\tif (port_role == USB_ROLE_HOST) {\n', '\tif (0) {\n'),
    ('dcp-is-data', '\treturn val.intval == POWER_SUPPLY_USB_TYPE_SDP ||\n',
     '\treturn val.intval == POWER_SUPPLY_USB_TYPE_DCP || val.intval == POWER_SUPPLY_USB_TYPE_SDP ||\n'),
    ('no-replay', '\tif (!ret && mtk_usb_extcon_audio)\n\t\tnb->notifier_call(nb, MTK_USB_EXTCON_AUDIO_ATTACH, NULL);\n',
     ''),
    ('psy-ignored', '\tif (event == PSY_EVENT_PROP_CHANGED && data == extcon->chg_psy)\n'
     '\t\tqueue_work(extcon->wq, &extcon->role_work);\n', ''),
    ('altmode-as-audio', '\tif (state->alt)\n\t\treturn 0;\n', ''),
    ('error-clears-role', '\t\tdev_warn(extcon->dev, "charger type unreadable (%d), role kept\\n",\n\t\t\t ret);\n\t\treturn;\n',
     '\t\tmtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);\n\t\treturn;\n'),
    ('no-bypass-follows-charger', '\tif (!extcon->bypss_typec_sink) {\n\t\t*role = port_role;\n\t\treturn 0;\n\t}\n', ''),
    ('no-audio-off-on-unbind', '\tret = devm_add_action_or_reset(dev, mtk_usb_extcon_audio_off, NULL);\n', '\tret = 0;\n'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-missing-field-initializers', '-Wno-unused-const-variable',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def body(source):
    text = re.sub(r'^#include .*\n', '', source, flags=re.M)
    cut = text.find('static const struct of_device_id mtk_usb_extcon_of_match[]')
    if cut < 0:
        fail('no of_device_id table in ' + SOURCE)
    return text[:cut]


def static_checks(tree, source):
    found = sorted(set(FORBIDDEN.findall(source)))
    if found:
        fail('the glue still uses the TCPC stack or a VBUS regulator: ' + ', '.join(found))
    probe = source[source.index('static int mtk_usb_extcon_probe('):]
    probe = probe[:probe.index('\n}\n')]
    defers = probe.count('-EPROBE_DEFER') + probe.count('dev_err_probe(')
    if 'usb_role_switch_get(dev)' not in probe or 'mindone_devm_power_supply_get_by_reference(dev, "charger")' not in probe:
        fail('probe no longer takes the MUSB role switch and the charger supply')
    if re.search(r'fwnode_usb_role_switch_get|typec_\w*get\(|tcpm', probe):
        fail('probe waits for a Type-C provider')
    if probe.index('typec_mux_register') > probe.index('usb_role_switch_register(dev, &port_desc)'):
        fail('the mode switch must exist before TCPM can find the role switch')
    header = (tree / HEADER).read_text()
    for name in ('mtk_usb_extcon_register_audio_notifier', 'mtk_usb_extcon_unregister_audio_notifier'):
        if name not in header or f'EXPORT_SYMBOL_GPL({name});' not in source:
            fail(f'{name} is not declared in {HEADER} and exported')
    return defers


def accdet_checks(tree):
    source = (tree / ACCDET).read_text()
    found = sorted(set(FORBIDDEN.findall(source)) - {'regulator_get', 'regulator_enable', 'regulator_disable'})
    found = [name for name in found if not name.startswith('regulator')]
    if found:
        fail(f'{ACCDET} still uses the TCPC stack: ' + ', '.join(found))
    call = re.search(r'^static int audio_typec_notifier_call\(.*?^}\n', source, re.M | re.S)
    if not call:
        fail(f'{ACCDET} has no audio_typec_notifier_call')
    cases = re.findall(r'case (MTK_USB_EXTCON_AUDIO_\w+):\n.*?accdet_eint_func_extern\((EINT_PLUG_\w+)\);', call.group(0),
                       re.S)
    if sorted(cases) != [('MTK_USB_EXTCON_AUDIO_ATTACH', 'EINT_PLUG_IN'), ('MTK_USB_EXTCON_AUDIO_DETACH', 'EINT_PLUG_OUT')]:
        fail(f'{ACCDET} does not map ATTACH/DETACH to EINT_PLUG_IN/OUT: {cases}')
    probe = source[source.index('static int mt6358_accdet_probe('):]
    probe = probe[:probe.index('\n}\n')]
    if 'accdet->audio_nb.notifier_call = audio_typec_notifier_call;' not in probe or \
            'mtk_usb_extcon_register_audio_notifier(&accdet->audio_nb)' not in probe:
        fail(f'{ACCDET} probe does not register the audio notifier')
    remove = source[source.index('static void mt6358_accdet_remove('):]
    remove = remove[:remove.index('\n}\n')]
    if 'mtk_usb_extcon_unregister_audio_notifier(&accdet->audio_nb);' not in remove:
        fail(f'{ACCDET} remove does not unregister the audio notifier')


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text('#include "stubs.h"\n' + text + '\n#include "cases.c"\n')
    binary = directory / name
    result = subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode:
        fail(f'{name} does not compile\n{result.stdout}{result.stderr}')
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Run extcon_mtk_usb, the role and mode switch TCPM drives, under '
                                     'ASan/UBSan against models of MUSB, the charger supply and TCPM: MUSB must '
                                     'become a USB device from the charger type alone when no Type-C port ever '
                                     'reports, follow TCPM for host and data role swap, stay off for an audio '
                                     'accessory and report it to accdet, and differ from the fork glue only in '
                                     'the documented cases; every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    static_checks(tree, source)
    accdet_checks(tree)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', body(source))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                fail(f'FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr[-3000:]}')
            print(result.stdout.rstrip())
        for name, before, after in MUTANTS:
            if source.count(before) != 1:
                fail(f'mutant {name}: expected one {before!r}')
            binary = build(directory, 'mutant-' + name, body(source.replace(before, after, 1)))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
