# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = 'system-heap-rss'
SRC = 'mindone/modules/system_heap/system_heap.c'
HARNESS = Path(__file__).resolve().parent / CHECK
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def extract_unit(src):
    start = src.index('#define DMA_HEAP_RSS_SEEN_MAX')
    end = src.index('static int system_heap_create(void)')
    return '\n'.join(['#include "stubs.h"', src[start:end], '#include "cases.c"']) + '\n'


def build_and_run(work, name, src):
    cpath = work / (name + '.c')
    cpath.write_text(extract_unit(src))
    binary = work / name
    res = subprocess.run(FLAGS + ['-I', str(HARNESS), str(cpath), '-o', str(binary)],
                         capture_output=True, text=True, timeout=120)
    if res.returncode:
        return res
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Verify system_heap /proc/dma_heap/rss_pid memtrack interface')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    src = (tree / SRC).read_text()

    for dead in ('DO_DMA_BUFFER_COMMON_DIV', 'pid_name', 'tid_name'):
        if dead in src:
            fail(f'stale symbol {dead} still present in {SRC}')
    if 'system_heap_procfs_init()' not in src:
        fail('system_heap_create does not call system_heap_procfs_init()')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        work = Path(tmp)
        res = build_and_run(work, 'candidate', src)
        if res.returncode:
            fail(f'candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('drop-dedup',
             'if (acc->seen[i] == dmabuf)\n\t\t\t\treturn 0;',
             'if (0 && acc->seen[i] == dmabuf)\n\t\t\t\treturn 0;'),
            ('drop-is-dmabuf-check',
             'if (!is_dma_buf_file(file))\n\t\treturn 0;',
             'if (0 && !is_dma_buf_file(file))\n\t\treturn 0;'),
            ('drop-pss-div',
             'acc->pss_bytes += div_u64(dmabuf->size, (u32)refs);',
             'acc->pss_bytes += dmabuf->size;'),
            ('wrong-gid',
             'make_kgid(&init_user_ns, 1000)',
             'make_kgid(&init_user_ns, 0)'),
            ('drop-pid-reset',
             'dma_heap_rss_target_pid = 0;',
             '(void)pid;'),
            ('drop-end-marker',
             'seq_printf(s, "-----EGL memtrack data end\\n");',
             '(void)s;'),
        )
        for name, before, after in mutants:
            if src.count(before) != 1:
                fail(f'mutant {name} count={src.count(before)}')
            mres = build_and_run(work, 'mutant-' + name, src.replace(before, after, 1))
            if mres.returncode == 0:
                fail(f'mutant {name} survived')
            print(f'PASS mutant {name}')
    print(f'{CHECK}: PASS ({len(mutants)} mutants caught)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
