# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'clk-disable-unused'
SOURCE = 'mindone/modules/clk_disable_unused/clk_disable_unused.c'
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ['empty', 'absent', 'pass', 'already-active', 'no-domain', 'get-errors', 'resume-fail', 'fail-sweep']
RESUME = ('\tretval = pm_runtime_resume_and_get(&pdev->dev);\n\tif (retval) {\n'
          '\t\tpr_notice("disable_unused dev: %s resume failed, ret=%d\\n",\n\t\t\t\tpdev->name, retval);\n'
          '\t\treturn 0;\n\t}\n')
PARENT_FAIL = ('pr_info("parent clk of clk %d clk_prepare_enable failed\\n", i);\n'
               '\t\t\t\tpr_info("return err code=%d\\n", retval);\n\t\t\t\tclk_put(clk);\n\t\t\t\tbreak;\n')
CLK_FAIL = '#if DUMP_UNUSED_CLKS\n\t\t\t\tclk_disable_unprepare(parent);\n#endif\n\t\t\t\tclk_put(clk);\n\t\t\t\tbreak;\n'
DONE = '#endif\n\t\t\tclk_put(clk);\n\t\t}\n\t}\n'
PUT = '\tpm_runtime_put_sync(&pdev->dev);\n'
MUTANTS = (
    ('unchecked get_sync (pre-fix)', ((RESUME, '\tpm_runtime_get_sync(&pdev->dev);\n'),),
     'resume-fail', 'domain_on()'),
    ('get_sync with the result check', ((RESUME, RESUME.replace('pm_runtime_resume_and_get', 'pm_runtime_get_sync')),),
     'resume-fail', 'pdev_model.dev.power.usage_count == 0'),
    ('get_sync, already active', ((RESUME, RESUME.replace('pm_runtime_resume_and_get', 'pm_runtime_get_sync')),),
     'already-active', 'pdev_model.dev.power.usage_count == 0'),
    ('resume result ignored', ((RESUME, '\tretval = pm_runtime_resume_and_get(&pdev->dev);\n'),),
     'resume-fail', 'domain_on()'),
    ('probe fails after a resume error', ((RESUME, RESUME.replace('\t\treturn 0;\n', '\t\treturn retval;\n')),),
     'resume-fail', 'ret == 0'),
    ('parent failure returns (pre-fix)',
     ((PARENT_FAIL, PARENT_FAIL.replace('\t\t\t\tclk_put(clk);\n\t\t\t\tbreak;\n', '\t\t\t\treturn 0;\n')),),
     'fail-sweep', 'pdev_model.dev.power.usage_count == 0'),
    ('clk failure returns (pre-fix)', ((CLK_FAIL, '\t\t\t\treturn 0;\n'),),
     'fail-sweep', 'pdev_model.dev.power.usage_count == 0'),
    ('parent left on after a clk failure', ((CLK_FAIL, '\t\t\t\tclk_put(clk);\n\t\t\t\tbreak;\n'),),
     'fail-sweep', 'cores[i]->enable_count == 0'),
    ('handle kept after a parent failure', ((PARENT_FAIL, PARENT_FAIL.replace('\t\t\t\tclk_put(clk);\n', '')),),
     'fail-sweep', 'handles_live == 0'),
    ('handle kept after a clk failure', ((CLK_FAIL, CLK_FAIL.replace('\t\t\t\tclk_put(clk);\n', '')),),
     'fail-sweep', 'handles_live == 0'),
    ('handle kept after the pass', ((DONE, DONE.replace('\t\t\tclk_put(clk);\n', '')),),
     'pass', 'handles_live == 0'),
    ('parent put instead of the handle', ((DONE, DONE.replace('clk_put(clk)', 'clk_put(parent)')),),
     'pass', 'clk->handle'),
    ('put_sync dropped', ((PUT, ''),), 'pass', 'pdev_model.dev.power.usage_count == 0'),
    ('put_noidle instead of put_sync', ((PUT, PUT.replace('put_sync', 'put_noidle')),),
     'pass', 'runtime_status == RPM_SUSPENDED'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


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


def function(source, name):
    pattern = r'^static [^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {name} has {len(found)} definitions in {SOURCE}')
    start = found[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def replace_once(text, before, after, label):
    if text.count(before) != 1:
        raise SystemExit(f'{CHECK}: {label}: expected one {before!r}')
    return text.replace(before, after)


def unit(source):
    defines = re.findall(r'^#define DUMP_UNUSED_CLKS\s+\d+$', source, re.M)
    if len(defines) != 1:
        raise SystemExit(f'{CHECK}: expected one DUMP_UNUSED_CLKS define in {SOURCE}')
    return '\n'.join(['#include "stubs.h"', defines[0], function(source, 'disable_unused_probe'),
                      '#include "cases.c"']) + '\n'


def bindings(source):
    if not re.search(r'^\t\.probe\t\t= disable_unused_probe,$', source, re.M):
        raise SystemExit(f'{CHECK}: disable_unused_probe is no longer the platform driver probe')
    if '.compatible = "mediatek,clk-disable-unused"' not in source:
        raise SystemExit(f'{CHECK}: the driver no longer matches mediatek,clk-disable-unused')


def build(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)], check=True, timeout=300)
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Compile disable_unused_probe() of clk_disable_unused.c with '
                                     'ASan/UBSan against a runtime PM, power domain and clock model: every case must '
                                     'end with the usage count at zero, the device suspended, every of_clk_get handle '
                                     'put and every clock count back where it started, no clock may be touched while '
                                     'the domain is off, the probe must return 0, and every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    bindings(source)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        candidate = build(directory, 'candidate', unit(source))
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                raise SystemExit(f'{CHECK}: FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr}')
            print(result.stdout.rstrip())
        for index, (name, edits, case, assertion) in enumerate(MUTANTS):
            mutated = source
            for before, after in edits:
                mutated = replace_once(mutated, before, after, 'mutant ' + name)
            result = run(build(directory, 'mutant%d' % index, unit(mutated)), case)
            if not result.returncode or 'Assertion failed' not in result.stderr or assertion not in result.stderr:
                raise SystemExit(f'{CHECK}: mutant {name} was not caught by {case} at {assertion!r}\n'
                                 f'{result.stdout}{result.stderr}')
            print(f'PASS mutant {name}: {case} fails at {assertion}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
