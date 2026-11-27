# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import sys

CHECK = 'wmt-pinctrl'
POS_GEN = 'mindone/modules/wmt_drv/common_main/platform/mt6789_pos_gen.c'
CONSYS_HW = 'mindone/modules/wmt_drv/common_main/platform/mtk_wcn_consys_hw.c'
PINCTRL_DTSI = 'mindone/dts/mt6789-pinctrl.dtsi'
CONN_DTSI = 'mindone/dts/mt6789-connectivity.dtsi'


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def main():
    parser = argparse.ArgumentParser(description='Verify wmt_drv configures consys GPIOs via pinctrl instead of direct GPIO/IOCFG_RT register writes')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()

    pos = (tree / POS_GEN).read_text()
    hw = (tree / CONSYS_HW).read_text()
    pinctrl_dt = (tree / PINCTRL_DTSI).read_text()
    conn_dt = (tree / CONN_DTSI).read_text()

    forbidden = (
        'CONSYS_GEN_GPIO_BASE_ADDR',
        'CONSYS_GEN_IOCFG_RT_BASE_ADDR',
        'CONSYS_GEN_GPIO_MODE22_OFFSET_ADDR',
        'CONSYS_GEN_GPIO_MODE23_OFFSET_ADDR',
        'CONSYS_GEN_DRV_CFG1_OFFSET_ADDR',
        'CONSYS_GEN_GPIO_DIR5_SET_OFFSET_ADDR',
        'CONSYS_GEN_GPIO_DOUT5_SET_OFFSET_ADDR',
    )
    for token in forbidden:
        if token in pos:
            fail(f'direct GPIO/IOCFG register access still present in {POS_GEN}: {token}')

    for state in ('consys_if_active', 'consys_if_sleep', 'consys_wifi_ctrl_gpio', 'consys_wifi_ctrl_conn'):
        if f'"{state}"' not in pos:
            fail(f'missing pinctrl state selection {state!r} in {POS_GEN}')
        if f'"{state}"' not in conn_dt:
            fail(f'missing pinctrl-names entry {state!r} in {CONN_DTSI}')

    if 'pinctrl_select_state(' not in pos:
        fail(f'missing pinctrl_select_state() call in {POS_GEN}')

    if 'of_parse_phandle(pdev->dev.of_node, "pinctrl-1"' in hw or 'of_parse_phandle(pdev->dev.of_node, "pinctrl-4"' in hw:
        fail(f'hardcoded pinctrl-1/pinctrl-4 LNA lookup remains in {CONSYS_HW}')
    if 'of_property_match_string(pdev->dev.of_node' not in hw:
        fail(f'missing named pinctrl lookup for LNA in {CONSYS_HW}')
    if 'if (iRet == -EPROBE_DEFER)' not in hw:
        fail(f'-EPROBE_DEFER propagation missing for devm_pinctrl_get in {CONSYS_HW}')

    m_active = re.search(r'consys_pins_active:\s*consys-active\s*\{(.*?)\n\t\t\};', pinctrl_dt, re.S)
    if not m_active:
        fail(f'consys_pins_active missing from {PINCTRL_DTSI}')
    active_body = m_active.group(1)
    for pin, func in (
        (177, 'CONN_TOP_CLK'), (178, 'CONN_TOP_DATA'), (179, 'CONN_BT_CLK'),
        (180, 'CONN_BT_DATA'), (181, 'CONN_HRST_B'), (182, 'CONN_WB_PTA'),
        (183, 'CONN_WF_CTRL0'), (184, 'CONN_WF_CTRL1'), (185, 'CONN_WF_CTRL2'),
    ):
        if f'PINMUX_GPIO{pin}__FUNC_{func}' not in active_body:
            fail(f'consys_pins_active missing GPIO{pin} {func}')
    if 'drive-strength = <1>;' not in active_body or 'drive-strength = <0>;' not in active_body:
        fail('consys_pins_active must encode raw drive-strength <1> (GPIO177-180) and <0> (GPIO181-185)')
    if 'GPIO176' in active_body or 'GPIO59' in active_body or 'GPIO156' in active_body:
        fail('consys_pins_active must not touch audio/MSDC/PA pins (GPIO176/59/156)')

    m_sleep = re.search(r'consys_pins_sleep:\s*consys-sleep\s*\{(.*?)\n\t\t\};', pinctrl_dt, re.S)
    if not m_sleep:
        fail(f'consys_pins_sleep missing from {PINCTRL_DTSI}')
    for pin in range(177, 186):
        if f'PINMUX_GPIO{pin}__FUNC_GPIO{pin}' not in m_sleep.group(1):
            fail(f'consys_pins_sleep missing GPIO{pin} GPIO mode')

    if '"default"' in conn_dt:
        fail(f'consys node in {CONN_DTSI} must not auto-select active pinmux as "default" at probe')

    print(f'{CHECK}: PASS (0 direct GPIO/IOCFG writes, 4 pinctrl states, LNA & EPROBE_DEFER guarded)')


if __name__ == '__main__':
    sys.exit(main())
