# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def main():
    parser = argparse.ArgumentParser(description="Exercise exact ZCV IRQ checks and daemon/kernel dispatch with measurement faults")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--case", choices=("errors", "rest", "busy", "disabled"))
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    battery = (base / "mtk_battery.c").read_text()
    daemon = (base / "mtk_battery_daemon.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-zcv-irq"
    source = (fixtures / "stubs.h").read_text()
    for name in ("is_algo_active", "fg_interrupt_check", "wakeup_fg_algo_cmd", "wakeup_fg_algo"):
        source += function(battery, name)
    source += function(daemon, "zcv_irq")
    source += (fixtures / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-zcv-irq-") as directory:
        path = Path(directory)
        (path / "check.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(path / "check.c"), "-o", str(path / "check")], check=True)
        for case in (args.case,) if args.case else ("errors", "rest", "busy", "disabled"):
            subprocess.run([str(path / "check"), case], check=True)


if __name__ == "__main__":
    main()
