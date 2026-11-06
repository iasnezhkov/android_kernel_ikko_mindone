# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def activation_case(source):
    body = function(source, "exec_BAT_EC")
    match = re.search(r"^\tcase 805:", body, re.M)
    if not match:
        raise ValueError("Missing BAT_EC805")
    opening = body.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (body[end] == "{") - (body[end] == "}")
        end += 1
    return body[match.start():end] + "\n\tbreak;\n"


def main():
    parser = argparse.ArgumentParser(description="Exercise the exact kernel activation helper and BAT_EC805 branch without synthetic interrupts")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--case", choices=("fresh", "repeat", "already-active"))
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    battery = (base / "mtk_battery.c").read_text()
    daemon = (base / "mtk_battery_daemon.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-enable"
    source = (fixtures / "stubs.h").read_text()
    if "battery_enable_kernel_algo(" in battery:
        source += function(battery, "battery_enable_kernel_algo")
    source += "\nstatic int activate(struct mtk_battery *gm) { int cmd = 805; int ret; switch (cmd) {\n"
    source += activation_case(daemon)
    source += "default: abort(); } return 0; }\n"
    source += (fixtures / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-enable-") as directory:
        path = Path(directory)
        (path / "check.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-variable", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(path / "check.c"), "-o", str(path / "check")], check=True)
        subprocess.run([str(path / "check")] + ([args.case] if args.case else []), check=True)


if __name__ == "__main__":
    main()
