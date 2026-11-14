# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
HARNESS = HERE / 'pwrap-mt6789'
FORK = 'mindone/modules/mtk_pmic_wrap/mtk_pmic_wrap.c'
COPY = 'mindone/modules/mtk_pmic_wrap_ack/mtk-pmic-wrap.c'
DTSI = 'mindone/dts/mt6789-pmic.dtsi'
ACK_C = 'drivers/soc/mediatek/mtk-pmic-wrap.c'
WACS = ('PWRAP_INIT_DONE2', 'PWRAP_WACS2_CMD', 'PWRAP_SWINF_2_WDATA_31_0', 'PWRAP_SWINF_2_RDATA_31_0',
        'PWRAP_WACS2_VLDCLR', 'PWRAP_WACS2_RDATA')
TYPE_FIELDS = ('regs', 'type', 'arb_en_all', 'int_en_all', 'int1_en_all', 'spi_w', 'wdt_src', 'caps',
               'init_reg_clock', 'init_soc_specific')
CAPS_DELTA = ('PWRAP_CAP_ARB | PWRAP_CAP_ULPOSC_CLK', 'PWRAP_CAP_ARB')
INSERTIONS = [
    ('before', 'static const int mt6795_regs[] = {',
     ['static const int mt6789_regs[] = {', '[PWRAP_INIT_DONE2] =\t\t0x0,', '[PWRAP_TIMER_EN] =\t\t0x3E4,',
      '[PWRAP_INT_EN] =\t\t0x450,', '[PWRAP_WACS2_CMD] =\t\t0x880,', '[PWRAP_SWINF_2_WDATA_31_0] =\t0x884,',
      '[PWRAP_SWINF_2_RDATA_31_0] =\t0x894,', '[PWRAP_WACS2_VLDCLR] =\t\t0x8A4,',
      '[PWRAP_WACS2_RDATA] =\t\t0x8A8,', '};']),
    ('after', 'PWRAP_MT6779,', ['PWRAP_MT6789,']),
    ('before', 'case PWRAP_MT6873:', ['case PWRAP_MT6789:']),
    ('before', 'static const struct pmic_wrapper_type pwrap_mt6795 = {',
     ['static const struct pmic_wrapper_type pwrap_mt6789 = {', '.regs = mt6789_regs,', '.type = PWRAP_MT6789,',
      '.arb_en_all = 0x777f,', '.int_en_all = 0x180000,', '.int1_en_all = 0,',
      '.spi_w = PWRAP_MAN_CMD_SPI_WRITE,', '.wdt_src = PWRAP_WDT_SRC_MASK_ALL,', '.caps = PWRAP_CAP_ARB,',
      '.init_reg_clock = pwrap_common_init_reg_clock,', '.init_soc_specific = NULL,', '};']),
    ('after', '{ .compatible = "mediatek,mt6779-pwrap", .data = &pwrap_mt6779 },',
     ['{ .compatible = "mediatek,mt6789-pwrap", .data = &pwrap_mt6789 },']),
]
CFLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
          '-Wno-sign-compare', '-Wno-missing-field-initializers', '-Wno-unused-variable',
          '-Wno-unused-but-set-variable', '-Wno-unused-const-variable',
          '-Wno-gnu-statement-expression-from-macro-expansion', '-Wno-initializer-overrides', '-Wno-pointer-sign',
          '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g']
REGS_BLOCK = '\n\t'.join(INSERTIONS[0][2][:-1]) + '\n};'
REG_MUTATIONS = [
    ('TIMER_EN at the MT6873 offset', '[PWRAP_TIMER_EN] =\t\t0x3E4,', '[PWRAP_TIMER_EN] =\t\t0x3E0,'),
    ('INT_EN at the MT6873 offset', '[PWRAP_INT_EN] =\t\t0x450,', '[PWRAP_INT_EN] =\t\t0x448,'),
    ('WACS2_CMD at the MT6873 offset', '[PWRAP_WACS2_CMD] =\t\t0x880,', '[PWRAP_WACS2_CMD] =\t\t0xC80,'),
    ('write data register shifted', '[PWRAP_SWINF_2_WDATA_31_0] =\t0x884,', '[PWRAP_SWINF_2_WDATA_31_0] =\t0x888,'),
    ('read data register shifted', '[PWRAP_SWINF_2_RDATA_31_0] =\t0x894,', '[PWRAP_SWINF_2_RDATA_31_0] =\t0x898,'),
    ('VLDCLR register shifted', '[PWRAP_WACS2_VLDCLR] =\t\t0x8A4,', '[PWRAP_WACS2_VLDCLR] =\t\t0x8A0,'),
    ('WACS2_RDATA register shifted', '[PWRAP_WACS2_RDATA] =\t\t0x8A8,', '[PWRAP_WACS2_RDATA] =\t\t0x8AC,'),
    ('INIT_DONE2 register moved', '[PWRAP_INIT_DONE2] =\t\t0x0,', '[PWRAP_INIT_DONE2] =\t\t0x4,'),
]
TYPE_BLOCK = '\n\t'.join(INSERTIONS[3][2][:-1]) + '\n};'
TYPE_MUTATIONS = [
    ('MT6873 interrupt mask', '.int_en_all = 0x180000,', '.int_en_all = BIT(4) | BIT(5),'),
    ('arbiter mask of MT6779', '.arb_en_all = 0x777f,', '.arb_en_all = 0xfbb7f,'),
    ('without the arbiter capability', '.caps = PWRAP_CAP_ARB,', '.caps = 0,'),
    ('watchdog sources without STAUPD', '.wdt_src = PWRAP_WDT_SRC_MASK_ALL,', '.wdt_src = PWRAP_WDT_SRC_MASK_NO_STAUPD,'),
    ('SPI write command of the new format', '.spi_w = PWRAP_MAN_CMD_SPI_WRITE,', '.spi_w = PWRAP_MAN_CMD_SPI_WRITE_NEW,'),
]
MUTATIONS = [(COPY, label, REGS_BLOCK, REGS_BLOCK.replace(old, new)) for label, old, new in REG_MUTATIONS] + [
    (COPY, label, TYPE_BLOCK, TYPE_BLOCK.replace(old, new)) for label, old, new in TYPE_MUTATIONS] + [
    (COPY, 'generic code changed: poll timeout', '#define PWRAP_POLL_TIMEOUT_US\t10000',
     '#define PWRAP_POLL_TIMEOUT_US\t1000'),
    (DTSI, 'PMIC without the MT6358 fallback', 'compatible = "mediatek,mt6366", "mediatek,mt6358";',
     'compatible = "mediatek,mt6366";'),
    (DTSI, 'wrap clock before spi', 'clocks = <&infracfg_ao_clk CLK_IFRAO_PMIC_AP &infracfg_ao_clk CLK_IFRAO_PMIC_TMR',
     'clocks = <&infracfg_ao_clk CLK_IFRAO_PMIC_TMR &infracfg_ao_clk CLK_IFRAO_PMIC_AP'),
    (DTSI, 'pwrap compatible of MT6873', 'compatible = "mediatek,mt6789-pwrap";', 'compatible = "mediatek,mt6873-pwrap";'),
]


def fail(message):
    raise SystemExit('pwrap-mt6789: ' + message)


def ack_source(tree, path):
    for candidate in (tree / path, tree / 'common' / path):
        if candidate.exists():
            return candidate.read_text()
    base = (tree / 'mindone/ack-base').read_text().split()
    for repo in (tree, tree / 'ack-6.18'):
        if (repo / '.git').exists() or (repo / 'HEAD').exists():
            result = subprocess.run(['git', '-C', str(repo), 'show', '%s:%s' % (base[1], path)], capture_output=True,
                                    text=True, env=dict(os.environ, GIT_NO_LAZY_FETCH='1'))
            if result.returncode == 0:
                return result.stdout
    fail('no ACK source for %s: not in %s, %s/common or a repository holding %s' % (path, tree, tree, base[1]))


def code_lines(text):
    out = []
    i = 0
    n = len(text)
    buf = []
    while i < n:
        if text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if text[i] in '"\'':
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == '\\' else 1
            buf.append(text[i:j + 1])
            i = j + 1
            continue
        buf.append(text[i])
        i += 1
    return [line.strip() for line in ''.join(buf).split('\n') if line.strip()]


def fidelity(tree, copy):
    upstream = code_lines(ack_source(tree, ACK_C))
    actual = code_lines(copy.read_text())
    for where, anchor, lines in INSERTIONS:
        head = lines[0]
        if actual.count(head) != 1:
            fail('%s has %d copies of %r' % (copy.name, actual.count(head), head))
        start = actual.index(head)
        end = start + 1 if len(lines) == 1 else actual.index('};', start) + 1
        neighbour = end if where == 'before' else start - 1
        if neighbour < 0 or neighbour >= len(actual) or actual[neighbour] != anchor:
            fail('%s: %r is not %s %r' % (copy.name, head, where, anchor))
        del actual[start:end]
    if actual != upstream:
        for index, (a, b) in enumerate(zip(actual, upstream)):
            if a != b:
                fail('%s line %d differs from %s outside the MT6789 data: %r != %r' % (copy.name, index, ACK_C, a, b))
        fail('%s length differs from %s outside the MT6789 data' % (copy.name, ACK_C))


def table(source, name):
    match = re.search(r'static (?:const )?int %s\[\] = \{(.*?)\n\};' % name, source, re.S)
    if not match:
        fail('no %s table' % name)
    return {key: int(value, 16) for key, value in re.findall(r'\[(\w+)\]\s*=\s*(0x[0-9a-fA-F]+),', match.group(1))}


def wrapper_type(source):
    match = re.search(r'static (?:const )?struct pmic_wrapper_type pwrap_mt6789 = \{(.*?)\n\};', source, re.S)
    if not match:
        fail('no pwrap_mt6789')
    fields = dict(re.findall(r'\.(\w+) = ([^,]+),', match.group(1)))
    if set(fields) != set(TYPE_FIELDS):
        fail('pwrap_mt6789 fields %s' % sorted(fields))
    return fields


def static_compare(fork, copy):
    regs_fork, regs_copy = table(fork, 'mt6789_regs'), table(copy, 'mt6789_regs')
    if regs_fork != regs_copy:
        fail('mt6789_regs differ: fork %s, copy %s' % (sorted(set(regs_fork.items()) - set(regs_copy.items())),
                                                       sorted(set(regs_copy.items()) - set(regs_fork.items()))))
    type_fork, type_copy = wrapper_type(fork), wrapper_type(copy)
    for field in TYPE_FIELDS:
        a, b = type_fork[field], type_copy[field]
        if a != b and (field, a, b) != ('caps',) + CAPS_DELTA:
            fail('pwrap_mt6789.%s differs: fork %s, copy %s' % (field, a, b))
    return regs_fork


def devicetree(dtsi):
    text = dtsi.read_text()
    node = re.search(r'pwrap: pwrap@10026000 \{(.*?)\n\t\t(\w+): (\w+) \{\n\t\t\tcompatible = ([^;]*);', text, re.S)
    if not node:
        fail('no pwrap node with a PMIC child in ' + DTSI)
    body = node.group(1)
    compatible = re.findall(r'"([^"]+)"', re.search(r'compatible = ([^;]*);', body).group(1))
    clocks = re.search(r'clocks = <([^>]*)>;', body).group(1).split('&')[1:]
    clocks = ['%s:%s' % tuple(item.split()) for item in clocks]
    names = re.findall(r'"([^"]+)"', re.search(r'clock-names = ([^;]*);', body).group(1))
    irq = int(re.search(r'interrupts = <GIC_SPI (\d+) ', body).group(1))
    reg_name = re.search(r'reg-names = "([^"]+)";', body).group(1)
    pmic = re.findall(r'"([^"]+)"', node.group(4))
    return compatible, clocks, names, pmic, irq, reg_name


def oracle(regs, dt):
    compatible, clocks, names, pmic, irq, reg_name = dt
    lines = ['#define ORACLE_REG_NAME "%s"' % reg_name, '#define ORACLE_IRQ %d' % irq,
             '#define ORACLE_CLOCKS %d' % len(clocks)]
    for name in WACS:
        lines.append('#define ORACLE_%s 0x%x' % (name[len('PWRAP_'):], regs[name]))
    for name, values in (('oracle_clocks', clocks), ('oracle_clock_names', names),
                         ('oracle_pwrap_compatible', compatible), ('oracle_pmic_compatible', pmic)):
        lines.append('static const char *const %s[] = { %s };' % (name, ', '.join('"%s"' % v for v in values)))
    return '\n'.join(lines) + '\n'


def build(work, source, name, oracle_text):
    unit = work / name
    unit.mkdir()
    (unit / 'oracle.h').write_text(oracle_text)
    fake = unit / 'fake'
    for include in re.findall(r'^#include <([^>]+)>', source.read_text(), re.M):
        path = fake / include
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    flags = CFLAGS + ['-include', str(HARNESS / 'stubs.h'), '-I', str(unit), '-I', str(HARNESS), '-I', str(fake)]
    objects = []
    for index, path in enumerate([HARNESS / 'model.c', source]):
        target = unit / ('%d.o' % index)
        subprocess.run(['cc'] + flags + ['-c', str(path), '-o', str(target)], check=True)
        objects.append(str(target))
    binary = unit / 'bin'
    subprocess.run(['cc', '-fsanitize=address,undefined'] + objects + ['-o', str(binary)], check=True)
    return binary


def run(binary, mode):
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
    result = subprocess.run([str(binary), mode], capture_output=True, text=True, env=environment, timeout=120)
    if result.returncode:
        sys.stderr.write(result.stderr)
        raise RuntimeError('%s exited with %d' % (binary, result.returncode))
    return result.stdout.splitlines()


def write_test(lines):
    if 'POPULATE pwrap' not in lines or 'RESULT 0' not in lines:
        return lines, []
    start, end = lines.index('POPULATE pwrap') + 1, lines.index('RESULT 0')
    return lines[:start] + lines[end:], lines[start:end]


def compare(fork, ack):
    for label, lines in (('fork', fork), ('ack', ack)):
        bad = [line for line in lines if line.startswith('VIOLATION ') or
               line.startswith('VIOLATIONS') and line != 'VIOLATIONS 0']
        if bad:
            fail('%s run: %s' % (label, bad[0]))
        if 'RESULT 0' not in lines:
            fail('%s probe failed: %s' % (label, [line for line in lines if line.startswith('RESULT')]))
    fork, test = write_test(fork)
    if fork != ack:
        first = next((i for i, (a, b) in enumerate(zip(fork, ack)) if a != b), min(len(fork), len(ack)))
        fail('traces differ at step %d: fork %s, ack %s' % (first, fork[first:first + 3], ack[first:first + 3]))
    expected = ['W 0x884 0x0000a55a', 'W 0x880 0x20000410', 'PMIC write 0x0410=0xa55a', 'R 0x8a8', 'W 0x880 0x00000410',
                'PMIC read 0x0410', 'R 0x8a8', 'R 0x894', 'W 0x8a4 0x00000001']
    probe = [line for line in test if not line.startswith('R 0x8a8')]
    if probe != [line for line in expected if line != 'R 0x8a8']:
        fail('the fork write test is not the DEW_WRITE_TEST access it should be: %s' % test)
    return fork, test


def gate(name, function, verdicts):
    try:
        result = function()
    except (SystemExit, RuntimeError) as error:
        verdicts.append('%s: %s' % (name, str(error).splitlines()[0].replace('pwrap-mt6789: ', '')[:110]))
        return None
    return result


def evaluate(tree, work, name, copy, dtsi, fork_lines, regs):
    verdicts = []
    gate('fidelity', lambda: fidelity(tree, copy), verdicts)
    gate('table', lambda: static_compare((tree / FORK).read_text(), copy.read_text()), verdicts)
    dt = gate('dt', lambda: devicetree(dtsi), verdicts)
    ack = gate('build', lambda: run(build(work, copy, name, oracle(regs, dt)), 'warm'), verdicts) if dt else None
    result = gate('trace', lambda: compare(fork_lines, ack), verdicts) if ack else None
    return result, ack, verdicts


def main():
    parser = argparse.ArgumentParser(description='Compile the mtk_pmic_wrap fork and the ACK mtk-pmic-wrap copy with '
                                     'its MT6789 data against one pwrap register and PMIC model with ASan/UBSan, '
                                     'probe both as after the bootloader (INIT_DONE2 set), read and write the PMIC '
                                     'through the registered regmap and raise the interrupt: every register access '
                                     'must match apart from the fork write test. The register table and wrapper '
                                     'type must match the fork, the copy must equal the ACK source of ack-base plus '
                                     'the MT6789 data, and every mutation in MUTATIONS must be rejected.')
    parser.add_argument('tree', type=Path)
    parser.add_argument('--no-mutations', action='store_true')
    parser.add_argument('--trace', type=Path)
    args = parser.parse_args()
    tree = args.tree.resolve()
    fork_source = (tree / FORK).read_text()
    regs = table(fork_source, 'mt6789_regs')
    with tempfile.TemporaryDirectory(prefix='pwrap-mt6789-') as name:
        work = Path(name)
        dt = devicetree(tree / DTSI)
        fork_lines = run(build(work, tree / FORK, 'fork', oracle(regs, dt)), 'warm')
        result, ack, verdicts = evaluate(tree, work, 'ack', tree / COPY, tree / DTSI, fork_lines, regs)
        if verdicts:
            fail('; '.join(verdicts))
        lines, test = result
        if args.trace:
            args.trace.mkdir(parents=True, exist_ok=True)
            (args.trace / 'pwrap-fork.txt').write_text('\n'.join(fork_lines) + '\n')
            (args.trace / 'pwrap-ack.txt').write_text('\n'.join(ack) + '\n')
        writes = sum(1 for line in lines if line.startswith('W '))
        print('pwrap-mt6789: register table and wrapper type match the fork (caps %s -> %s, ULPOSC clocks through '
              'the clock bulk); %d register writes, %d accesses and the clock order match' % (
                  CAPS_DELTA[0], CAPS_DELTA[1], writes, len(lines)))
        print('  normalised: fork-only write test of DEW_WRITE_TEST 0x410 after populate (%d accesses)' % len(test))
        print('  PMIC compatible %s binds pmic_mt6358 in the copy, pmic_mt6366 in the fork' % dt[3])
        if args.no_mutations:
            print('pwrap-mt6789: PASS (mutations skipped)')
            return
        survivors = []
        for number, (path, label, old, new) in enumerate(MUTATIONS):
            scratch = work / ('mutant%d' % number)
            scratch.mkdir()
            copy = scratch / Path(COPY).name
            dtsi = scratch / Path(DTSI).name
            shutil.copy(tree / COPY, copy)
            shutil.copy(tree / DTSI, dtsi)
            target = copy if path == COPY else dtsi
            text = target.read_text()
            if text.count(old) != 1:
                fail('mutation "%s" does not apply once to %s' % (label, path))
            target.write_text(text.replace(old, new))
            verdicts = evaluate(tree, scratch, 'build', copy, dtsi, fork_lines, regs)[2]
            if not verdicts:
                survivors.append(label)
            print('  mutation %-36s %s' % (label, 'SURVIVED' if not verdicts else 'rejected by ' + ' | '.join(verdicts)))
        if survivors:
            fail('FAIL: mutations survived: ' + ', '.join(survivors))
    print('pwrap-mt6789: PASS (%d mutations rejected)' % len(MUTATIONS))


if __name__ == '__main__':
    main()
