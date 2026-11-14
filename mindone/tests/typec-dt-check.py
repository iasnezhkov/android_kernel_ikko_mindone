# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'typec-dt'
HARNESS = Path(__file__).resolve().parent / 'typec-pdo'
BOARD = 'mindone/dts/mindone.dts'
USB = 'mindone/dts/mt6789-usb.dtsi'
OVERLAY = 'mindone/dts/mindone-overlay.dtso'
FORK_SELECT = 'mindone/modules/tcpc_class/pd_dpm_pdo_select.c'
FORK_CORE = 'mindone/modules/tcpc_class/pd_dpm_core.c'
FORK_PD = 'mindone/modules/tcpc_class/inc/pd_core.h'
FORK_SELECT_H = 'mindone/modules/tcpc_class/inc/pd_dpm_pdo_select.h'
FORK_TCPM_H = 'mindone/modules/include/drivers/misc/mediatek/typec/tcpc/inc/tcpm.h'
ACK_TCPM = 'drivers/usb/typec/tcpm/tcpm.c'
ACK_PD = 'include/linux/usb/pd.h'
ACK_DT_PD = 'include/dt-bindings/usb/pd.h'
FORK_SINK = (0x0001912c, 0x0002d0c8)
FORK_SOURCE = (0x00019032,)
FORK_POLICY = 0x31
CASES = ('caps0', 'caps1', 'caps0-vs-fork')
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
         '-Wno-sign-compare', '-Wno-gnu-variable-sized-type-not-at-end', '-fsanitize=address,undefined', '-fno-sanitize=shift',
         '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def ack_source(tree, path):
    for candidate in (tree / path, tree / 'common' / path):
        if candidate.exists():
            return candidate.read_text()
    base = (tree / 'mindone/ack-base').read_text().split()
    for repo in (tree, tree / 'ack-6.18'):
        if (repo / '.git').exists() or (repo / 'HEAD').exists():
            result = subprocess.run(['git', '-C', str(repo), 'show', f'{base[1]}:{path}'], capture_output=True,
                                    text=True, env=dict(os.environ, GIT_NO_LAZY_FETCH='1'))
            if result.returncode == 0:
                return result.stdout
    fail(f'no ACK source for {path}: not in {tree}, {tree}/common or a repository holding {base[1]}')


def node(text, opener, label):
    start = text.find(opener)
    if start < 0 or text.find(opener, start + 1) >= 0:
        fail(f'expected one {opener!r} in {label}')
    depth, index = 0, text.index('{', start)
    while True:
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if not depth:
                return text[start:index + 1]
        index += 1


def need(block, line, label):
    if line not in block:
        fail(f'{label} lacks {line!r}')


def pdo_values(expression, dt_pd):
    names = dict(re.findall(r'^#define (PDO_FIXED_\w+)\s+\((1 << \d+)\)', dt_pd, re.M))
    for name in ('PDO_FIXED_DUAL_ROLE', 'PDO_FIXED_DATA_SWAP'):
        if name not in names:
            fail(f'{ACK_DT_PD} has no {name}')
    values = []
    for mv, ma, flags in re.findall(r'PDO_FIXED\((\d+), (\d+), ([^)]*?)\)(?=\s|$)', expression):
        bits = 0
        for flag in flags.split('|'):
            flag = flag.strip()
            if flag != '0':
                bits |= eval(names[flag])
        values.append(bits | ((int(mv) // 50) & 0x3ff) << 10 | ((int(ma) // 10) & 0x3ff))
    return values


def caps(connector, name, dt_pd):
    block = node(connector, f'{name} {{', BOARD)
    found = {}
    for key in ('source-pdos', 'sink-pdos'):
        match = re.search(key + r' = <(.*?)>;', block, re.S)
        if not match:
            fail(f'{name} has no {key}')
        if re.sub(r'PDO_FIXED\(\d+, \d+, [^)]*\)', '', match.group(1)).strip():
            fail(f'{name} {key} holds something other than fixed PDOs')
        found[key] = pdo_values(match.group(1), dt_pd)
    match = re.search(r'op-sink-microwatt = <(\d+)>;', block)
    if not match:
        fail(f'{name} has no op-sink-microwatt')
    found['op'] = int(match.group(1))
    return found


def dt_checks(tree, dt_pd):
    board, usb, overlay = ((tree / path).read_text() for path in (BOARD, USB, OVERLAY))
    need(board, '#include <dt-bindings/usb/pd.h>', BOARD)
    i2c6 = node(board, '&i2c6 {', BOARD)
    for line in ('#address-cells = <1>;', '#size-cells = <0>;'):
        need(i2c6, line, '&i2c6 in ' + BOARD)
    tcpc = node(i2c6, 'rt1715: tcpc@4e {', BOARD)
    for line in ('compatible = "richtek,rt1715";', 'reg = <0x4e>;', 'vbus-supply = <&eta6965_otg_vbus>;',
                 'interrupts-extended = <&pio 13 IRQ_TYPE_LEVEL_LOW>;'):
        need(tcpc, line, 'tcpc@4e')
    connector = node(tcpc, 'connector {', BOARD)
    for line in ('compatible = "usb-c-connector";', 'data-role = "dual";', 'power-role = "dual";',
                 'try-power-role = "sink";', 'accessory-mode-audio;'):
        need(connector, line, 'the connector')
    for line in ('pd-disable', 'self-powered', 'accessory-mode-debug', 'slow-charger-loop'):
        if line in connector:
            fail(f'the connector sets {line}')
    endpoint = node(connector, 'typec_hs: endpoint {', BOARD)
    need(endpoint, 'remote-endpoint = <&usb_typec_glue>;', 'the connector endpoint')
    flags = (1 << 29) | (1 << 25)
    zero, one = caps(connector, 'caps-0', dt_pd), caps(connector, 'caps-1', dt_pd)
    source = [FORK_SOURCE[0] | flags]
    if zero['source-pdos'] != source or one['source-pdos'] != source:
        fail(f'source PDOs {zero["source-pdos"]} {one["source-pdos"]} are not the fork 5 V/500 mA with dual role and '
             f'data swap {source}')
    if zero['sink-pdos'] != [FORK_SINK[0] | flags]:
        fail(f'caps-0 sink PDOs {zero["sink-pdos"]} are not the fork 5 V/3 A PDO alone')
    if one['sink-pdos'] != [FORK_SINK[0] | flags, FORK_SINK[1]]:
        fail(f'caps-1 sink PDOs {one["sink-pdos"]} are not the fork 5 V/3 A + 9 V/2 A PDOs')
    if zero['op'] != 15000000 or one['op'] != 18000000:
        fail('op-sink-microwatt is not 15 W for caps-0 and 18 W for caps-1')
    if connector.index('caps-0 {') > connector.index('caps-1 {'):
        fail('caps-0 (5 V only) must be the first and default capability set')
    glue = node(usb, 'extcon_usb: extcon_usb {', USB)
    for line in ('compatible = "mediatek,extcon-usb";', 'charger = <&eta6965_chg>;',
                 'mediatek,bypss-typec-sink = <1>;', 'usb-role-switch;', 'mode-switch;'):
        need(glue, line, 'extcon_usb')
    for word in ('vbus-supply', 'tcpc =', 'orientation-switch'):
        if word in glue:
            fail(f'extcon_usb still has {word}')
    need(node(glue, 'usb_role: endpoint@0 {', USB), 'remote-endpoint = <&musb_drd_switch>;', 'extcon_usb endpoint@0')
    need(node(glue, 'usb_typec_glue: endpoint@1 {', USB), 'remote-endpoint = <&typec_hs>;', 'extcon_usb endpoint@1')
    musb = node(usb, 'usb: usb0@11200000 {', USB)
    for line in ('dr_mode = "otg";', 'usb-role-switch;'):
        need(musb, line, 'the MUSB node')
    need(node(musb, 'musb_drd_switch: endpoint@0 {', USB), 'remote-endpoint = <&usb_role>;', 'the MUSB endpoint')
    adapter = node(board, 'pd_adapter: pd_adapter {', BOARD)
    for line in ('compatible = "mediatek,pd_adapter";', 'adapter_name = "pd_adapter";', 'typec = <&rt1715>;'):
        need(adapter, line, 'pd_adapter')
    for text, label in ((board, BOARD), (usb, USB), (overlay, OVERLAY)):
        for word in ('rt-pd-manager', 'rt1711_type_c_port0', 'rt-tcpc,', 'rt1711pd,', 'type_c_port0'):
            if word in text:
                fail(f'{label} still has the fork {word}')
    return zero, one, tcpc


def strip_includes(text):
    return re.sub(r'^#include .*\n', '', text, flags=re.M)


def function(source, name, label):
    found = list(re.finditer(r'^(?:static )?(?:inline )?\w[^;{}=()]*\b' + re.escape(name) + r'\(\s*[^;{}]*\)\s*\{',
                             source, re.M))
    if len(found) != 1:
        fail(f'{name} has {len(found)} definitions in {label}')
    start, depth, index = found[0].start(), 0, source.index('{', found[0].start())
    while True:
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if not depth:
                return source[start:index + 1] + '\n'
        index += 1


def enum(source, name, label):
    match = re.search(r'^enum ' + re.escape(name) + r' \{.*?^\};\n', source, re.M | re.S)
    if not match:
        fail(f'no enum {name} in {label}')
    return match.group(0)


def ack_unit(tree):
    tcpm, pd = ack_source(tree, ACK_TCPM), ack_source(tree, ACK_PD)
    helpers = ''.join(re.findall(r'^#define min_(?:power|current)\(x, y\).*\n', tcpm, re.M))
    if helpers.count('#define') != 2:
        fail('tcpm.c no longer defines min_power and min_current')
    port = (HARNESS / 'ack_port.h').read_text()
    wrapper = (HARNESS / 'ack_wrapper.c').read_text()
    return ('#include "stubs.h"\n' + strip_includes(pd) + enum(tcpm, 'spr_avs_status', ACK_TCPM) + port + helpers
            + function(tcpm, 'tcpm_pd_select_pdo', ACK_TCPM) + function(tcpm, 'tcpm_pd_build_request', ACK_TCPM)
            + wrapper)


def fork_unit(tree):
    pd_core, tcpm_h = (tree / FORK_PD).read_text(), (tree / FORK_TCPM_H).read_text()
    select_h, select, core = ((tree / path).read_text() for path in (FORK_SELECT_H, FORK_SELECT, FORK_CORE))
    defines = ''.join(re.findall(r'^#define A?PDO_\w+.*?(?<!\\)\n', pd_core, re.M | re.S))
    mask = re.search(r'^#define DPM_CHARGING_POLICY_MASK\s.*\n', tcpm_h, re.M)
    if not mask:
        fail(f'no DPM_CHARGING_POLICY_MASK in {FORK_TCPM_H}')
    defines += mask.group(0)
    structs = re.search(r'^struct dpm_pdo_info_t \{.*?^#define DPM_APDO_TYPE_PPS_CF.*?\n', select_h, re.M | re.S)
    if not structs:
        fail(f'no dpm_pdo_info_t/dpm_rdo_info_t in {FORK_SELECT_H}')
    body = select[select.index('#include "inc/pd_dpm_pdo_select.h"\n') + len('#include "inc/pd_dpm_pdo_select.h"\n'):]
    body = body[:body.rindex('#endif')]
    pdo_loop = function(core, 'dpm_build_request_info_pdo', FORK_CORE)
    for word in ('dpm_find_match_req_info(req_info,', 'max_uw = req_info->vmax * req_info->oper_ma;',
                 'max_uw = req_info->oper_uw;'):
        if word not in pdo_loop:
            fail(f'dpm_build_request_info_pdo no longer contains {word!r}')
    prelude = (HARNESS / 'fork_prelude.h').read_text()
    wrapper = (HARNESS / 'fork_wrapper.c').read_text()
    return ('#include "stubs.h"\n' + f'#define FORK_POLICY {FORK_POLICY}\n' + defines
            + enum(tcpm_h, 'tcpm_power_cap_val_type', FORK_TCPM_H) + enum(tcpm_h, 'tcpm_power_cap_apdo_type', FORK_TCPM_H)
            + enum(tcpm_h, 'dpm_charging_policy', FORK_TCPM_H).replace('#define', '') + prelude + structs.group(0)
            + body + pdo_loop + wrapper)


def caps_unit(zero, one):
    def array(name, values):
        return f'u32 {name}[] = {{ {", ".join(hex(v) for v in values)} }};\nint {name}_n = {len(values)};\n'
    return ('#include "stubs.h"\n' + array('dt_caps0', zero['sink-pdos']) + array('dt_caps1', one['sink-pdos'])
            + f'u32 dt_caps0_op_mw = {zero["op"] // 1000};\nu32 dt_caps1_op_mw = {one["op"] // 1000};\n')


def main():
    parser = argparse.ArgumentParser(description='Check the TCPM DT of the RT1715 (connector, PDOs, graph to the role '
                                     'and mode switch and to MUSB, pd_adapter reference, no fork nodes left) and '
                                     'compare the request ACK tcpm_pd_build_request() makes from the DT sink PDOs '
                                     'with the request of the fork policy engine (dpm_build_request_info_pdo, '
                                     'charging_policy 0x31) for 200000 random source capability sets each')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    dt_pd = ack_source(tree, ACK_DT_PD)
    zero, one, _ = dt_checks(tree, dt_pd)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        sources = {'ack.c': ack_unit(tree), 'fork.c': fork_unit(tree), 'caps.c': caps_unit(zero, one),
                   'cases.c': (HARNESS / 'cases.c').read_text()}
        for name, text in sources.items():
            (directory / name).write_text(text)
        binary = directory / 'pdo'
        result = subprocess.run(FLAGS + ['-I', str(HARNESS)] + [str(directory / name) for name in sources]
                                + ['-o', str(binary)], capture_output=True, text=True, timeout=300)
        if result.returncode:
            fail(f'the PDO harness does not compile\n{result.stdout}{result.stderr[-6000:]}')
        for case in CASES:
            result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)
            if result.returncode:
                fail(f'FAIL {case}\n{result.stdout[-4000:]}{result.stderr[-4000:]}')
            print(result.stdout.rstrip())
    print(f'{CHECK}: PASS DT structure and {len(CASES)} PDO comparisons; device check pending')


if __name__ == '__main__':
    main()
