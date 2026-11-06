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
    parser = argparse.ArgumentParser(description="Exercise native battery presence errors through actual publication and plugout callers")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    battery = (base / "mtk_battery.c").read_text()
    daemon = (base / "mtk_battery_daemon.c").read_text()
    fixture = Path(__file__).with_name("gauge-presence")
    text = (fixture / "stubs.h").read_text()
    text += function(battery, "battery_update")
    for name in ("sw_check_bat_plugout", "bat_plugout_irq"):
        text += function(daemon, name)
    match = re.search(r"case FG_DAEMON_CMD_IS_BAT_EXIST:\s*\{([\s\S]*?)\n\t\}\s*break;", daemon)
    if not match:
        raise ValueError("Missing native battery presence reply")
    text += "void presence_reply(struct mtk_battery *gm, struct message *ret_msg)\n{\n" + match.group(1) + "\n}\n"
    text += (fixture / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-presence-") as directory:
        root = Path(directory)
        (root / "check.c").write_text(text)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern",
                        "-fsanitize=address,undefined", str(root / "check.c"),
                        "-o", str(root / "check")], check=True)
        subprocess.run([str(root / "check")], check=True)


if __name__ == "__main__":
    main()
