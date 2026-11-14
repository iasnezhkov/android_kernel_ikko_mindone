# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


CORE = 'mindone/modules/mali_kbase_r54/mali_kbase_core_linux.c'
RING = 'mindone/modules/mali_kbase_r54/backend/gpu/mali_kbase_jm_rb.c'
CASES = ('unmapped', 'empty', 'short', 'mapped')
PRELUDE = '''#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IS_ENABLED(x) 1
#define pr_info(...) ((void)0)
#define STUB_PHYS 0x7a000000u
typedef unsigned long long phys_addr_t;
struct v1_data { unsigned int version, ctx, frame, job, freq; };
struct kbase_device { struct v1_data *v1; struct { phys_addr_t phyaddr; } job_status_addr; };
enum { GPU_MEM_ID = 3 };
static struct kbase_device device;
static unsigned char memory[64] __attribute__((aligned(8)));
static unsigned long long stub_size;
static phys_addr_t stub_virt;
static int setups;
static size_t setup_size;
static phys_addr_t sspm_reserve_mem_get_phys(int id) { assert(id == GPU_MEM_ID); return STUB_PHYS; }
static phys_addr_t sspm_reserve_mem_get_virt(int id) { assert(id == GPU_MEM_ID); return stub_virt; }
static unsigned long long sspm_reserve_mem_get_size(int id) { assert(id == GPU_MEM_ID); return stub_size; }
static void MTKGPUQoS_setup(struct v1_data *v1, phys_addr_t phyaddr, size_t size)
{
	assert(v1 && phyaddr == STUB_PHYS);
	setups++;
	setup_size = size;
}
static phys_addr_t rec_phys_addr, rec_virt_addr;
static unsigned long long rec_size;
static struct v1_data *gpu_info_ref;
'''


def function(source, signature):
    start = source.index(signature)
    depth, index = 0, source.index('{', start)
    while True:
        depth += (source[index] == '{') - (source[index] == '}')
        index += 1
        if depth == 0:
            break
    lines, depth = [], 0
    for line in source[start:index].split('\n'):
        if line.startswith('#if'):
            depth += 1
        elif line.startswith('#endif'):
            if depth == 0:
                continue
            depth -= 1
        lines.append(line)
    return '\n'.join(lines) + '\n'


def bm_writes_guarded(ring):
    blocks = re.findall(r'#if IS_ENABLED\(CONFIG_MALI_MTK_GPU_BM_2\)(.*?)#endif', ring, re.S)
    writes = [block for block in blocks if 'kbdev->v1->' in block]
    if len(writes) != 2:
        raise ValueError('Expected two bandwidth monitor write blocks in jm_rb, found ' + str(len(writes)))
    for block in writes:
        if not re.search(r'if \(js == 0 && kbdev->v1\) \{\s*kbdev->v1->ctx', block):
            return False
    return True


def probe_contained(core):
    return re.search(r'err = mtk_bandwith_resource_init\(kbdev\);\n\t\tif \(err\)\n\t\t\tpr_info\([^;]*\);\n\t\terr = 0;\n',
                     core) is not None


def run_cases(directory, name, core):
    source = PRELUDE + function(core, 'static void get_rec_addr(void)') + \
        function(core, 'static int mtk_bandwith_resource_init(struct kbase_device *kbdev)') + \
        (Path(__file__).with_name('gpu-bm') / 'cases.c').read_text()
    path = directory / (name + '.c')
    path.write_text(source)
    binary = directory / name
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(path), '-o', str(binary)],
                   check=True, timeout=60)
    return {case: subprocess.run([str(binary), case], capture_output=True, text=True, timeout=30) for case in CASES}


def main():
    parser = argparse.ArgumentParser(description='Check that a failed Mali bandwidth monitor setup leaves the GPU '
                                     'probed and the job path without a NULL v1 write')
    parser.add_argument('kernel_tree', type=Path)
    tree = parser.parse_args().kernel_tree.resolve()
    core, ring = (tree / CORE).read_text(), (tree / RING).read_text()
    build = (tree / 'mindone/modules/BUILD.bazel').read_text()
    if '"CONFIG_MALI_MTK_GPU_BM_2=1",' not in build or '"mali_kbase_r54/backend/gpu/mali_kbase_jm_rb.c",' not in build:
        raise ValueError('mali_kbase_mt6789 no longer builds the bandwidth monitor this check binds')
    if not bm_writes_guarded(ring):
        raise ValueError('A bandwidth monitor write in jm_rb is not guarded by kbdev->v1')
    if not probe_contained(core):
        raise ValueError('A bandwidth monitor setup failure still fails the Mali probe')
    with tempfile.TemporaryDirectory(prefix='gpu-bm-') as temporary:
        directory = Path(temporary)
        for case, result in run_cases(directory, 'actual', core).items():
            if result.returncode:
                raise RuntimeError(case + ': ' + result.stdout + result.stderr)
            print(result.stdout.strip())
        mutants = (
            ('accept-empty-region', core.replace('rec_virt_addr && rec_size >= sizeof(struct v1_data)', 'rec_virt_addr', 1),
             ('empty', 'short')),
            ('accept-unmapped-region', core.replace('rec_virt_addr && rec_size >= sizeof(struct v1_data)',
                                                    'rec_size >= sizeof(struct v1_data)', 1), ('unmapped',)),
        )
        for name, mutant, expected in mutants:
            if mutant == core:
                raise ValueError('Mutation did not apply: ' + name)
            results = run_cases(directory, name, mutant)
            for case in expected:
                result = results[case]
                if not result.returncode or not re.search(r'Assertion failed:|Assertion `[^\n]*\x27 failed|runtime error:|'
                                                          r'ERROR: AddressSanitizer', result.stderr):
                    raise RuntimeError(f'Mutation {name} escaped case {case}: {result.stdout}{result.stderr}')
            print('PASS rejected ' + name)
        if bm_writes_guarded(ring.replace('if (js == 0 && kbdev->v1) {', 'if (js == 0) {', 1)):
            raise RuntimeError('Guard check accepted an unguarded write')
        if probe_contained(core.replace('\t\terr = 0;\n', '', 1)):
            raise RuntimeError('Probe check accepted a failing probe')
        print('PASS rejected unguarded-write and failing-probe text controls')
    print(f'PASS gpu-bm: {len(CASES)} setup cases, 2 executed mutations, 2 text controls')


if __name__ == '__main__':
    main()
