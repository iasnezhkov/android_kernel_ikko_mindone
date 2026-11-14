# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from baseline_source import require


SOURCE = 'mindone/modules/ccci_md_all/ccci_modem.c'
SOURCE_SHA256 = '55fce76ca15f871a72efa3bedd01305c567b1e4cb3cc1cfe2579ee4ce092064d'
DEPENDENCIES = (
    ('mindone/modules/ccci_md_all/ccci_bm.c', {'ccci_alloc_skb': '57746ac28d75934165df9722978dea0ca282b41a2a128da65ee2cc96f0fed59c'}),
    ('mindone/modules/ccci_md_all/hif/ccci_hif.c', {'ccci_hif_send_skb': '51a17ed5e2a3400c06900bb1a89cb6435a7507312d6a96b9410d361d9d29ba7d'}),
    ('mindone/modules/ccci_md_all/port/port_proxy.c', {
        'port_get_queue_no': 'd9820cf77db4ec069a4e713ce020087743fd7ab85d8aa75e4593221683db3bc6',
        'proxy_send_msg_to_md': '176c74ca6eb3f84d6bad68fb0ab2f070448cc1416390795eaa5a9347f0c934cc',
        'ccci_port_send_msg_to_md': '2a25051d7fcb4aa4ba893ae9c27783f1d3704c46d545aff633cde2bca9fb9593',
    }),
    ('mindone/modules/ccci_ccif/ccci_hif_ccif.c', {'md_ccif_op_send_skb': '222069260ff63ed483b47b27a2d4faefebaaee6585b1e9e749e4ac884e90cc34'}),
)
FIX = (
    ('#define TAG "md"\n', '#define TAG "md"\n#define MD_THROTTLING_COOLER_LV_COMMAND 3\n'),
    ('\tcase ID_THROTTLING_CFG:\n\t\tret = ccci_port_send_msg_to_md(md_id, CCCI_SYSTEM_TX,\n\t\t\t\tMD_THROTTLING,\n\t\t\t\t*((int *)buf), 1);',
     '\tcase ID_THROTTLING_CFG:\n\t\ttmp_data = *((int *)buf);\n\t\tret = ccci_port_send_msg_to_md(md_id, CCCI_SYSTEM_TX,\n\t\t\t\tMD_THROTTLING,\n\t\t\t\ttmp_data,\n\t\t\t\t(tmp_data & 0xff) != MD_THROTTLING_COOLER_LV_COMMAND);'),
)
CASES = ('healthy-command-parity', 'mutt-empty-pool', 'mutt-full-ring', 'mutt-full-usb', 'mutt-invalid-flow', 'mutt-signal-pending', 'modem-off', 'gated-before-send', 'exception-before-send', 'full-ring-state-race', 'unrelated-classes-preserved', 'invalid-id', 'ready-pool-recovery', 'transport-readback-contract', 'length-handling', 'off-null-pointer')


def function(source, name):
    match = re.search(r'^(?:static )?[A-Za-z_][\w\s*]*\b' + re.escape(name) + r'\(', source, re.M)
    if not match:
        raise ValueError('Missing actual function: ' + name)
    opening = source.index('{', match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def enum(source, name):
    match = re.search(r'^enum ' + name + r' \{.*?^\};', source, re.M | re.S)
    if not match:
        raise ValueError('Missing actual enum: ' + name)
    return match.group(0) + '\n'


def anonymous(source, symbol):
    for match in re.finditer(r'^enum \{.*?^\};', source, re.M | re.S):
        if symbol in match.group(0):
            return match.group(0) + '\n'
    raise ValueError('Missing actual protocol enum: ' + symbol)


def struct(source, name, suffix=';'):
    match = re.search(r'^struct ' + name + r' \{.*?^\} ' + re.escape(suffix), source, re.M | re.S)
    if not match:
        match = re.search(r'^struct ' + name + r' \{.*?^\};', source, re.M | re.S)
    if not match:
        raise ValueError('Missing actual struct: ' + name)
    return match.group(0) + '\n'


def macros(source, prefix):
    result = []
    lines = iter(source.splitlines())
    for line in lines:
        if line.startswith('#define ' + prefix):
            result.append(line)
            while line.endswith('\\'):
                line = next(lines)
                result.append(line)
    return '\n'.join(result) + '\n'


def uncomment(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Control must match exactly once: ' + before)
    return source.replace(before, after, 1)


def assemble(tree, dispatcher, candidate):
    fixtures = Path(__file__).resolve().parent / 'ccci-mutt'
    shared = tree / 'mindone/modules/include/drivers/misc/mediatek'
    common = (shared / 'include/mt-plat/mtk_ccci_common.h').read_text()
    core = (shared / 'eccci/inc/ccci_core.h').read_text()
    hif = (shared / 'eccci/inc/ccci_hif.h').read_text()
    bm = (shared / 'eccci/inc/ccci_bm.h').read_text()
    flow = (shared / 'eccci/hif/ccci_hif_ccif.h').read_text()
    ring = (shared / 'eccci/hif/ccci_ringbuf.h').read_text()
    result = (fixtures / 'stubs.h').read_text()
    for name in ('MD_SYS', 'MD_STATE', 'KERN_FUNC_ID'):
        result += enum(common, name)
    result += anonymous(common, 'MD_THROTTLING') + anonymous(common, 'MODEM_CAP_TXBUSY_STOP')
    result += enum(core, 'CCCI_CH') + enum(core, 'DIRECTION') + enum(hif, 'CCCI_HIF')
    result += enum(bm, 'DATA_POLICY') + enum(ring, 'ccci_ringbuf_error') + macros(common, 'CCCI_ERR')
    result += struct(common, 'ccci_header', '__packed;') + struct(bm, 'ccci_buffer_ctrl')
    result += (fixtures / 'stubs-after.h').read_text()
    proxy = (tree / 'mindone/modules/ccci_md_all/port/port_proxy.c').read_text()
    result += '#define CHECK_MD_ID(md_id) ((void)(md_id))\n'
    result += function(proxy, 'port_get_queue_no')
    result += function(flow, 'ccif_is_md_flow_ctrl_supported')
    result += function((tree / 'mindone/modules/ccci_md_all/ccci_bm.c').read_text(), 'ccci_alloc_skb')
    result += function((tree / 'mindone/modules/ccci_ccif/ccci_hif_ccif.c').read_text(), 'md_ccif_op_send_skb')
    result += function((tree / 'mindone/modules/ccci_md_all/hif/ccci_hif.c').read_text(), 'ccci_hif_send_skb')
    result += function(proxy, 'proxy_send_msg_to_md')
    result += 'static struct port_proxy *proxy_table[MAX_MD_NUM];\n#define GET_PORT_PROXY(md_id) proxy_table[md_id]\n'
    result += function(proxy, 'ccci_port_send_msg_to_md')
    header = tree / 'mindone/modules/md_cooling_all/md_cooling.h'
    if header.resolve() != (tree / 'mindone/modules/board_temp/md_cooling.h').resolve():
        raise ValueError('Unexpected compiled cooler protocol header')
    result += '#define IS_ENABLED(value) 0\nstruct list_head { void *next, *prev; };\n'
    result += header.read_text().replace('#include <linux/thermal.h>', '')
    result += 'static int enabled = 1;\nstatic int get_modem_is_enabled(int id) { return id == MD_SYS1 && enabled; }\n'
    prefix = re.search(r'\tif \(!get_modem_is_enabled\(md_id\)\) \{.*?\n\t\}', dispatcher, re.S)
    branch = re.search(r'\tcase ID_THROTTLING_CFG:\n.*?\t\tbreak;', dispatcher, re.S)
    if not prefix or not branch:
        raise ValueError('Missing actual throttling branch')
    if branch.group(0).count('buf') != 1:
        raise ValueError('Throttling payload must be read exactly once')
    constant = macros(dispatcher, 'MD_THROTTLING_COOLER_LV_COMMAND') if candidate else '#define MD_THROTTLING_COOLER_LV_COMMAND TMC_CTRL_CMD_COOLER_LV\n'
    result += constant + ('#define EXPECT_NONBLOCKING 1\n' if candidate else '#define EXPECT_NONBLOCKING 0\n')
    result += 'static int exact_throttling_case(int md_id, unsigned int id, char *buf, unsigned int len)\n{\nint ret = 0;\nint tmp_data = 0;\n(void)tmp_data;\n(void)len;\n'
    result += prefix.group(0) + '\nswitch (id) {\n' + branch.group(0) + '\ndefault: return -EINVAL;\n}\nreturn ret;\n}\n'
    return uncomment(result + (fixtures / 'cases.c').read_text())


def compile_source(directory, name, source):
    path = directory / (name + '.c')
    path.write_text(source)
    binary = directory / name
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare', '-fsanitize=address,undefined', str(path), '-o', str(binary)], check=True)
    return binary


def run(binary, case, rejection=None):
    result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
    if rejection is not None:
        if not result.returncode or rejection not in result.stderr or 'Assertion' not in result.stderr:
            raise RuntimeError(f'Mutant did not reach its assertion: {binary.name}/{case}: {result.stderr}')
        print(f'PASS rejected {binary.name}/{case}: {rejection}')
    else:
        if result.returncode:
            raise RuntimeError(f'{binary.name}/{case}: {result.returncode}: {result.stderr}')
        print(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser(description='Exercise actual class3 CCCI dispatch, skb ownership, pool/full-ring backpressure and unchanged command classes')
    parser.add_argument('kernel_tree', type=Path)
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    source = (tree / SOURCE).read_text()
    baseline = source
    for before, after in reversed(FIX):
        baseline = replace_once(baseline, after, before)
    require(SOURCE + ' without the class3 transport change', baseline, SOURCE_SHA256)
    for path, pins in DEPENDENCIES:
        current = (tree / path).read_text()
        for name, pin in pins.items():
            require('Transport dependency ' + name, function(current, name), pin)
    build = (tree / 'mindone/modules/BUILD.bazel').read_text()
    if '"ccci_md_all/ccci_modem.c",' not in build:
        raise ValueError('Dispatcher is not in the actual DDK target')
    with tempfile.TemporaryDirectory(prefix='ccci-mutt-') as temporary:
        directory = Path(temporary)
        candidate = assemble(tree, source, True)
        original = assemble(tree, baseline, False)
        for label, assembled in (('candidate', candidate), ('baseline', original)):
            binary = compile_source(directory, label, assembled)
            for case in CASES:
                run(binary, case)
        controls = (
            ('all-nonblocking', '(tmp_data & 0xff) != MD_THROTTLING_COOLER_LV_COMMAND', '0', 'unrelated-classes-preserved', 'stopped == 1'),
            ('all-blocking', '(tmp_data & 0xff) != MD_THROTTLING_COOLER_LV_COMMAND', '1', 'mutt-full-ring', 'stopped == 0'),
            ('wrong-command-class', '#define MD_THROTTLING_COOLER_LV_COMMAND 3', '#define MD_THROTTLING_COOLER_LV_COMMAND 10', 'healthy-command-parity', 'MD_THROTTLING_COOLER_LV_COMMAND == TMC_CTRL_CMD_COOLER_LV'),
            ('lost-modem-off', 'return -CCCI_ERR_MD_INDEX_NOT_FOUND;', 'return 0;', 'modem-off', '== -CCCI_ERR_MD_INDEX_NOT_FOUND'),
            ('changed-message', 'ccci_h->data[1] = msg;', 'ccci_h->data[1] = MD_TX_POWER;', 'healthy-command-parity', 'sent.data[1] == MD_THROTTLING'),
            ('changed-payload', 'ccci_h->reserved = resv;', 'ccci_h->reserved = resv + 1;', 'healthy-command-parity', 'sent.reserved == commands[i]'),
            ('lost-pool-error', 'return -CCCI_ERR_ALLOCATE_MEMORY_FAIL;', 'return 0;', 'mutt-empty-pool', 'result == -CCCI_ERR_ALLOCATE_MEMORY_FAIL'),
            ('lost-ring-error', 'return -EBUSY;', 'return 0;', 'mutt-full-ring', 'result == (per.data_usb_bypass ? -ENOMEM : -EBUSY)'),
            ('lost-failure-free', 'if (ret)\n\t\t\t\tccci_free_skb(skb);', 'if (ret && 0)\n\t\t\t\tccci_free_skb(skb);', 'mutt-full-ring', 'frees == 1'),
        )
        for label, before, after, case, rejection in controls:
            mutant = compile_source(directory, label, replace_once(candidate, before, after))
            run(mutant, case, rejection)
    print(f'PASS {len(CASES)} exact candidate and baseline cases each; all255 other command classes preserved; {len(controls)} executed mutations; no whole-setter deadline or firmware ack')


if __name__ == '__main__':
    main()
