# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description='Run host register-model checks of the C ETA6965 charger operations')
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = args.tree.resolve()
    tests = root / 'mindone/tests/eta-operations'
    driver = root / 'mindone/modules/eta6965_charger'
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare', '-g',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', str(tests / 'include'), '-I', str(driver)]
    with tempfile.TemporaryDirectory() as work:
        binary = Path(work) / 'cases'
        subprocess.run([args.cc, *flags, '-o', str(binary), str(tests / 'cases.c'), str(driver / 'operations.c')], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
