# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'tkcore-spi-clk'
PERIDEV = 'mindone/modules/tkcore/peridev.c'
TZ_DRV = 'mindone/modules/tkcore_drv/tee_tz_drv.c'
BASE_DTS = 'mindone/dts/mindone.dts'
BUS_DTS = 'mindone/dts/mt6789-bus.dtsi'
OVERLAY_DTS = 'mindone/dts/mindone-overlay.dtso'
BUILD = 'mindone/modules/BUILD.bazel'
LOADS = (
    'mindone/vendor_boot.modules.load',
    'mindone/vendor_dlkm.modules.load',
    'mindone/vendor_boot.modules.load.recovery',
    'mindone/modules/modules.load',
)
HARNESS = Path(__file__).resolve().parent / CHECK
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(message):
    raise SystemExit(f'{CHECK}: {message}')


def check_bindings(tree, tz_src):
    if (tree / 'mindone/modules/spi_mt65xx').exists():
        fail('modules/spi_mt65xx directory still exists')
    if 'spi_mt65xx' in (tree / BUILD).read_text():
        fail('spi_mt65xx still referenced in modules/BUILD.bazel')
    for rel in LOADS:
        if 'spi_mt65xx.ko' in (tree / rel).read_text().split():
            fail(f'spi_mt65xx.ko still listed in {rel}')

    base = (tree / BASE_DTS).read_text()
    tk = re.search(r'tkcore_drv:\s*tkcore_drv\s*\{(.*?)\};', base, re.S)
    if not tk:
        fail('tkcore_drv node missing in mindone.dts')
    body = tk.group(1)
    for req in (
        'compatible = "trustkernel,tzdrv";',
        'clocks = <&infracfg_ao_clk CLK_IFRAO_SPI0>;',
        'clock-names = "spi0";',
        'assigned-clocks = <&topckgen_clk CLK_TOP_SPI_SEL>;',
        'assigned-clock-parents = <&topckgen_clk CLK_TOP_MSDCPLL_D2>;',
    ):
        if req not in body:
            fail(f'tkcore_drv missing {req!r}')

    bus = (tree / BUS_DTS).read_text()
    spi0 = re.search(r'spi0:\s*spi0@1100a000\s*\{(.*?)\};', bus, re.S)
    if not spi0 or 'status = "disabled";' not in spi0.group(1):
        fail('spi0@1100a000 is not disabled in mt6789-bus.dtsi')

    overlay = (tree / OVERLAY_DTS).read_text()
    if re.search(r'&spi0\b', overlay):
        fail('&spi0 reference still present in mindone-overlay.dtso')

    if 'tz_0_plt_device' in tz_src:
        fail('synthetic tz_0_plt_device still present in tee_tz_drv.c')
    if 'MODULE_DEVICE_TABLE(of, tz_tee_match);' not in tz_src:
        fail('MODULE_DEVICE_TABLE(of, tz_tee_match) missing in tee_tz_drv.c')
    probe = tz_src[tz_src.index('static int tz_tee_probe'):tz_src.index('static void tz_tee_remove')]
    clk_pos = probe.find('devm_clk_get(dev, "spi0")')
    alloc_pos = probe.find('tee_core_alloc(')
    reg_pos = probe.find('tee_clkmgr_register1("spi", 0, tkcore_spi_clk_enable,')
    add_pos = probe.find('tee_core_add(tee)')
    if not (0 <= clk_pos < alloc_pos < reg_pos < add_pos):
        fail('tz_tee_probe ordering must be devm_clk_get -> tee_core_alloc -> tee_clkmgr_register1 -> tee_core_add')
    if 'tee_clkmgr_unregister("spi", 0);' not in probe:
        fail('tz_tee_probe error paths must call tee_clkmgr_unregister("spi", 0)')
    rem = tz_src[tz_src.index('static void tz_tee_remove'):tz_src.index('static const struct of_device_id tz_tee_match')]
    if 'tee_clkmgr_unregister("spi", 0);' not in rem:
        fail('tz_tee_remove must call tee_clkmgr_unregister("spi", 0)')


def extract_unit(peridev, tz_src):
    p_start = peridev.index('struct clkmgr_handle {')
    p_end = peridev.index('EXPORT_SYMBOL(tee_clkmgr_unregister);')
    en_start = tz_src.index('static DEFINE_MUTEX(tkcore_spi_clk_lock);')
    en_end = tz_src.index('static int tz_tee_probe(')
    return '\n'.join(['#include "stubs.h"', peridev[p_start:p_end], tz_src[en_start:en_end], '#include "cases.c"']) + '\n'


def build_and_run(directory, name, text):
    path = directory / (name + '.c')
    path.write_text(text)
    binary = directory / name
    res = subprocess.run(FLAGS + ['-I', str(HARNESS), str(path), '-o', str(binary)],
                         capture_output=True, text=True, timeout=300)
    if res.returncode:
        return res
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description='Verify spi_mt65xx removal and tkcore_drv SPI0 TEE clock registration')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    peridev = (tree / PERIDEV).read_text()
    tz_src = (tree / TZ_DRV).read_text()
    check_bindings(tree, tz_src)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        res = build_and_run(directory, 'candidate', extract_unit(peridev, tz_src))
        if res.returncode:
            fail(f'candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('wrong-enable-sig',
             'static void tkcore_spi_clk_enable(const void *clk)',
             'static void tkcore_spi_clk_enable(struct clk *clk)'),
            ('drop-disable-call',
             'clk_disable_unprepare((struct clk *)clk);',
             '(void)clk;'),
            ('drop-enable-call',
             'ret = clk_prepare_enable((struct clk *)clk);',
             'ret = 0; (void)clk;'),
            ('drop-disable-underflow-guard',
             'if (!tkcore_spi_clk_count) {',
             'if (0 && !tkcore_spi_clk_count) {'),
        )
        for name, before, after in mutants:
            assert tz_src.count(before) == 1
            mutated = tz_src.replace(before, after, 1)
            mres = build_and_run(directory, 'mutant-' + name, extract_unit(peridev, mutated))
            if mres.returncode == 0:
                fail(f'mutant {name} was not caught')
            print(f'PASS mutant {name}')
    print(f'{CHECK}: PASS bindings, kCFI callback dispatch, and {len(mutants)} mutants caught')


if __name__ == '__main__':
    sys.exit(main())
