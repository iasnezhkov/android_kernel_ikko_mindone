# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from baseline_source import strip_comments

CHECK = 'tcpci-rt1711h'
HARNESS = Path(__file__).resolve().parent / CHECK
SOURCE = 'mindone/modules/tcpci_rt1711h/tcpci_rt1711h.c'
ACK_DRIVER = 'drivers/usb/typec/tcpm/tcpci_rt1711h.c'
ACK_TCPCI = 'drivers/usb/typec/tcpm/tcpci.c'
ACK_HEADER = 'include/linux/usb/tcpci.h'
CASES = ('exhaustive', 'injected', 'quiet')
CALL = '\treturn rt1711h_alert(chip);\n'
ACK_CALL = '\treturn tcpci_irq(chip->tcpci);\n'
SHUTDOWN_LINE = '\t.shutdown = rt1711h_shutdown,\n'
MUTANTS = (
    ('mask-without-power-status', ' | \\\n\t TCPC_ALERT_POWER_STATUS)', ')'),
    ('no-reloop', '\tif (status & mask)\n\t\tgoto process_status;\n', ''),
    ('rx-cleared-before-read', '\t\tregmap_read(regmap, TCPC_RX_BYTE_CNT, &cnt);\n',
     '\t\trt1711h_write16(chip, TCPC_ALERT, TCPC_ALERT_RX_STATUS);\n\t\tregmap_read(regmap, TCPC_RX_BYTE_CNT, &cnt);\n'),
    ('payload-off-by-one', 'payload_cnt = cnt - (1 + sizeof(msg.header));', 'payload_cnt = cnt - sizeof(msg.header);'),
    ('tx-failed-first', '\tif (status & TCPC_ALERT_TX_SUCCESS)\n\t\ttcpm_pd_transmit_complete(port, TCPC_TX_SUCCESS);\n'
     '\telse if (status & TCPC_ALERT_TX_DISCARDED)',
     '\tif (status & TCPC_ALERT_TX_FAILED)\n\t\ttcpm_pd_transmit_complete(port, TCPC_TX_FAILED);\n'
     '\telse if (status & TCPC_ALERT_TX_DISCARDED)'),
    ('no-tcpc-reset', '\t\tif (raw == 0xff)\n\t\t\ttcpm_tcpc_reset(port);\n\t\telse\n\t\t\ttcpm_vbus_change(port);\n',
     '\t\ttcpm_vbus_change(port);\n'),
    ('rx-status-cleared-early', '\tif (status & ~TCPC_ALERT_RX_STATUS)\n\t\trt1711h_write16(chip, TCPC_ALERT,\n'
     '\t\t\t\tstatus & ~TCPC_ALERT_RX_STATUS);\n', '\trt1711h_write16(chip, TCPC_ALERT, status);\n'),
    ('vsafe0v-ignored', '\tif (chip->data.vbus_vsafe0v)\n\t\tmask |= TCPC_ALERT_EXTENDED_STATUS;\n', ''),
    ('handled-always', '\treturn IRQ_RETVAL(irq_ret);\n', '\treturn IRQ_HANDLED;\n'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-Wno-gnu-statement-expression-from-macro-expansion', '-Wno-format', '-Wno-unused-label', '-Wno-unused-but-set-variable',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


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


def function(source, name, label):
    found = list(re.finditer(r'^(?:static )?(?:inline )?\w[^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{',
                             source, re.M))
    if len(found) != 1:
        fail(f'{name} has {len(found)} definitions in {label}')
    start = found[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def block(source, opener, label):
    start = source.find(opener)
    if start < 0 or source.find(opener, start + 1) >= 0:
        fail(f'expected one {opener!r} in {label}')
    return source[start:closing(source, source.index('{', start))] + ';\n'


def define(source, name, label):
    found = re.findall(r'^#define ' + re.escape(name) + r'\b.*?(?<!\\)\n', source, re.M | re.S)
    if len(found) != 1:
        fail(f'expected one #define {name} in {label}')
    return found[0]


def normalized(text):
    lines, blank = [], True
    for line in strip_comments(text).split('\n'):
        line = line.rstrip()
        if not line.strip():
            if not blank:
                lines.append('')
            blank = True
            continue
        blank = False
        lines.append(line)
    while lines and not lines[-1]:
        lines.pop()
    return '\n'.join(lines)


def reverted(source):
    for part in (define(source, 'RT1711H_TCPCI_ALERT_MASK', SOURCE), function(source, 'rt1711h_alert', SOURCE),
                 function(source, 'rt1711h_shutdown', SOURCE)):
        if source.count(part) != 1:
            fail('an added part is not unique in ' + SOURCE)
        source = source.replace(part + '\n', '', 1) if (part + '\n') in source else source.replace(part, '', 1)
    for before, after in ((CALL, ACK_CALL), (SHUTDOWN_LINE, '')):
        if source.count(before) != 1:
            fail(f'expected one {before.strip()!r} in {SOURCE}')
        source = source.replace(before, after, 1)
    return source


def copy_check(source, ack_driver):
    mine, theirs = normalized(reverted(source)), normalized(ack_driver)
    if mine != theirs:
        left, right = mine.split('\n'), theirs.split('\n')
        for index, (a, b) in enumerate(zip(left, right)):
            if a != b:
                fail(f'{SOURCE} differs from ACK {ACK_DRIVER} outside the alert dispatch and shutdown at line '
                     f'{index + 1}: {a!r} != {b!r}')
        fail(f'{SOURCE} differs from ACK {ACK_DRIVER} in length outside the alert dispatch and shutdown')
    irq = function(source, 'rt1711h_irq', SOURCE)
    if not irq.endswith(CALL + '}\n'):
        fail('rt1711h_irq does not end in rt1711h_alert(chip)')
    probe = function(source, 'rt1711h_probe', SOURCE)
    if 'IRQF_ONESHOT | IRQF_TRIGGER_LOW' not in probe or 'enable_irq_wake(client->irq);' not in probe:
        fail('rt1711h_probe no longer requests a oneshot level-low wake interrupt')
    if 'tcpci_irq' in source:
        fail('tcpci_irq is referenced; r13 does not export it')


def unit(source, ack_tcpci, header):
    defines = ''.join(re.findall(r'^#define TCPC_\w+\s.*\n', header, re.M))
    struct_tcpci = block(ack_tcpci, 'struct tcpci {', ACK_TCPCI)
    ack_irq = function(ack_tcpci, 'tcpci_irq', ACK_TCPCI).replace('irqreturn_t tcpci_irq(', 'irqreturn_t ack_tcpci_irq(', 1)
    init = function(ack_tcpci, 'tcpci_init', ACK_TCPCI)
    start = init.find('\treg = TCPC_ALERT_TX_SUCCESS')
    end = init.find('\ttcpci->alert_mask = reg;\n')
    if start < 0 or end < 0:
        fail('tcpci_init no longer builds alert_mask from TCPC_ALERT_TX_SUCCESS')
    mask = ('static int ack_alert_mask(struct tcpci *tcpci)\n{\n\tunsigned int reg;\n\tint ret;\n\n'
            + init[start:end + len('\ttcpci->alert_mask = reg;\n')] + '\treturn 0;\n}\n')
    parse = function(ack_tcpci, 'tcpci_parse_config', ACK_TCPCI)
    if '\ttcpci->controls_vbus = true;' not in parse:
        fail('tcpci_parse_config no longer sets controls_vbus unconditionally')
    parts = ['#include "stubs.h"', defines,
             'struct tcpci_data {\n\tstruct regmap *regmap;\n\tunsigned char TX_BUF_BYTE_x_hidden:1;\n'
             '\tunsigned char auto_discharge_disconnect:1;\n\tunsigned char vbus_vsafe0v:1;\n};\n',
             struct_tcpci,
             'static void ack_parse_config(struct tcpci *tcpci)\n{\n\ttcpci->controls_vbus = true;\n}\n',
             'struct tcpm_port *tcpci_get_tcpm_port(struct tcpci *tcpci)\n{\n\treturn tcpci->port;\n}\n',
             function(ack_tcpci, 'tcpci_read16', ACK_TCPCI), function(ack_tcpci, 'tcpci_write16', ACK_TCPCI),
             mask, ack_irq,
             block(source, 'struct rt1711h_chip_info {', SOURCE), block(source, 'struct rt1711h_chip {', SOURCE),
             function(source, 'rt1711h_read16', SOURCE), function(source, 'rt1711h_write16', SOURCE),
             define(source, 'RT1711H_TCPCI_ALERT_MASK', SOURCE), function(source, 'rt1711h_alert', SOURCE),
             '#include "cases.c"']
    return '\n'.join(parts) + '\n'


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
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Check that modules/tcpci_rt1711h equals ACK tcpci_rt1711h.c apart '
                                     'from its own alert dispatch and shutdown, and that the dispatch makes the '
                                     'same register accesses, tcpm calls and return values as ACK tcpci_irq() on a '
                                     'TCPCI register model for every alert combination; every mutation must be '
                                     'caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    copy_check(source, ack_source(tree, ACK_DRIVER))
    ack_tcpci, header = ack_source(tree, ACK_TCPCI), ack_source(tree, ACK_HEADER)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', unit(source, ack_tcpci, header))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                fail(f'FAIL {case} (exit {result.returncode})\n{result.stdout[-4000:]}{result.stderr[-4000:]}')
            print(result.stdout.rstrip())
        for name, before, after in MUTANTS:
            if source.count(before) != 1:
                fail(f'mutant {name}: expected one {before!r}')
            mutated = source.replace(before, after, 1)
            binary = build(directory, 'mutant-' + name, unit(mutated, ack_tcpci, header))
            caught = [case for case in CASES if run(binary, case).returncode]
            if not caught:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}: caught by {", ".join(caught)}')
    print(f'{CHECK}: PASS copy equals ACK {ACK_DRIVER} outside the dispatch and shutdown, {len(CASES)} case sets '
          f'equal ACK tcpci_irq, {len(MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
