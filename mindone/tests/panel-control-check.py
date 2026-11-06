# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import tempfile

PANELS = ['panel_ch13721c_fhd_dsi_vdo_amoled_ocp2133', 'panel_sd5302h_fhd1240_dsi_vdo_amoled_ocp2133_61']


def main():
    parser = argparse.ArgumentParser(description='Run host checks of the C panel operation controller and both init tables')
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = args.tree.resolve()
    tests = root / 'mindone/tests/panel-control'
    modules = root / 'mindone/modules'
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-g',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', str(tests / 'include'), '-I', str(modules / 'panel_control')]
    with tempfile.TemporaryDirectory() as work:
        binary = Path(work) / 'cases'
        subprocess.run([args.cc, *flags, '-o', str(binary), str(tests / 'cases.c'),
                        str(modules / 'panel_control/panel_control.c')], check=True)
        subprocess.run([str(binary)], check=True)
        for panel in PANELS:
            subprocess.run([args.cc, *flags, '-fsyntax-only', str(modules / panel / 'board.c')], check=True)
            print(f'PASS {panel}/board.c compiles')


if __name__ == '__main__':
    main()
