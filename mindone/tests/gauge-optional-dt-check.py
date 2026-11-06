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
    parser = argparse.ArgumentParser(description="Exercise actual optional boot and table DT reads with absent, malformed and supplied properties")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    source = (base / "mtk_battery.c").read_text()
    table = (base / "mtk_battery_table.h").read_text()
    prefix = function(source, "fg_custom_init_from_dts").split('\tfg_read_dts_val(np, "Q_MAX_L_CURRENT",', 1)[0]
    prefix += "\tobserved_multi = multi_battery;\n\tobserved_active = active_table;\n}\n"
    text = (Path(__file__).with_name("gauge-optional-dt") / "stubs.h").read_text()
    for name in ("MULTI_BATTERY", "ACTIVE_TABLE"):
        text += re.search(r"^#define " + name + r"\s+[^\n]+", table, re.M).group(0) + "\n"
    text += function(source, "fg_read_dts_val")
    cases = (Path(__file__).with_name("gauge-optional-dt") / "cases.c").read_text()
    mutants = [("actual", prefix)]
    for field, prop in (("ptim_lk_v", "fg_swocv_v"), ("ptim_lk_i", "fg_swocv_i"), ("pl_shutdown_time", "shutdown_time")):
        before = f'fg_read_dts_val(np, "{prop}", &gm->{field}, 1);'
        after = f'int unchecked; fg_read_dts_val(np, "{prop}", &unchecked, 1); gm->{field} = unchecked;'
        if prefix.count(before) != 1:
            raise ValueError("Missing unique boot-state mutation site: " + field)
        mutants.append((field, prefix.replace(before, after, 1)))
    for field, default in (("multi_battery", "MULTI_BATTERY"), ("active_table", "ACTIVE_TABLE")):
        before = field + " = " + default
        if prefix.count(before) != 1:
            raise ValueError("Missing unique table-default mutation site: " + field)
        mutants.append((field, prefix.replace(before, field, 1)))
    before = "if (gm->ptim_lk_i == 0)"
    if prefix.count(before) != 1:
        raise ValueError("Missing unique independent current-seed boundary")
    mutants.append(("coupled-current-seed", prefix.replace(before, "if (gm->ptim_lk_v == 0)", 1)))
    with tempfile.TemporaryDirectory(prefix="gauge-optional-dt-") as directory:
        root = Path(directory)
        for name, variant in mutants:
            c = root / (name + ".c")
            binary = root / name
            c.write_text(text + variant + cases)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                            "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined",
                            str(c), "-o", str(binary)], check=True, timeout=30)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            if name == "actual":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end="")
            elif not result.returncode or "Assertion failed:" not in result.stderr:
                raise RuntimeError("Mutation escaped runtime checks: " + name + result.stderr)
            else:
                print("PASS rejected " + name)


if __name__ == "__main__":
    main()
