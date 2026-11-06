# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def main():
    parser = argparse.ArgumentParser(description="Exercise actual native initialization getters with PMIC and RTC NVMEM failures")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    gauge = (args.kernel_tree / "mindone/modules/mt6358_battery/mt6358-gauge.c").read_text()
    fixture = Path(__file__).with_name("gauge-init-io")
    text = (fixture / "stubs.h").read_text()
    constants = ("RG_SYSTEM_INFO_CON0", "RG_BATON_ANA_MON0", "AD_BATON_UNDET_MASK",
                 "AD_BATON_UNDET_SHIFT", "RG_AUXADC_CON19", "AUXADC_ADC_RDY_PWRON_CLR_MASK",
                 "AUXADC_ADC_RDY_PWRON_CLR_SHIFT")
    for name in constants:
        match = re.search(r"^#define " + name + r"[\t ]+(.+?)\s*$", gauge, re.M)
        if not match:
            raise ValueError("Missing constant: " + name)
        text += "#define " + name + " " + match.group(1).split("/*")[0].rstrip() + "\n"
    for name in ("read_rtc_spare_fg_value", "fgauge_get_info", "rtc_ui_soc_get", "battery_exist_get"):
        text += function(gauge, name)
    text += (fixture / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-init-io-") as directory:
        root = Path(directory)
        (root / "check.c").write_text(text)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern",
                        "-fsanitize=address,undefined", str(root / "check.c"),
                        "-o", str(root / "check")], check=True)
        subprocess.run([str(root / "check")], check=True)


if __name__ == "__main__":
    main()
