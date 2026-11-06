# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description='Run host lifecycle checks of the C mindone_ufs_screen module')
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    tests = args.tree.resolve() / 'mindone/tests/ufs-screen'
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-g',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(tests / 'include')]
    with tempfile.TemporaryDirectory() as work:
        binary = Path(work) / 'cases'
        subprocess.run([args.cc, *flags, '-o', str(binary), str(tests / 'cases.c')], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
