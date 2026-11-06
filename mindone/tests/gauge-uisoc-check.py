# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


POSITIVE_CASES = (
    (1, 20000, 6200, 100, 100, 100),
    (100, 20000, 6200, 100, 100, 100),
    (101, 20000, 6200, 100, 200, 12277),
    (6300, 20000, 6200, 100, 200, 196),
    (10000, 20000, 6200, 100, 200, 124),
    (6300, 20000, 0, 100, 200, 20),
    (6300, 1000, 0, 100, 15, 15),
)
ZERO_CASES = (
    (0, 20000, 6200, 100, 100, 100),
    (0, 20000, 0, 100, 100, 100),
    (0, 1000, 0, 100, 15, 15),
    (0, 1000, 6200, 100, 15, 15),
)
MUTATIONS = (
    ("admit-zero-divisor", "if (algo->ui_soc > 0)", "if (algo->ui_soc >= 0)"),
    ("change-positive-divisor", "/ algo->ui_soc;", "/ (algo->ui_soc + 1);"),
    ("lose-one-percent-low-gap", "if (algo->ui_soc <= 100)\n\t\talgo->lt_gap =", "if (algo->ui_soc < 100)\n\t\talgo->lt_gap ="),
    ("lose-minimum-low-gap", "if (algo->lt_gap < CAR_MIN_GAP)", "if (false)"),
)


def compile_check(root, source, minimum):
    fixture = Path(__file__).with_name("gauge-uisoc")
    stubs = (fixture / "stubs.h").read_text()
    cases = (fixture / "cases.c").read_text()
    (root / "check.c").write_text(stubs + "#define CAR_MIN_GAP " + minimum + "\n" + source + cases)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern",
                    "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                    str(root / "check.c"), "-o", str(root / "check")], check=True)


def run_case(root, values):
    return subprocess.run([str(root / "check"), *map(str, values)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser(description="Exercise the actual UI-SOC threshold function through the zero-charge boundary")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--expect-zero-fault", action="store_true")
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    algorithm = (args.kernel_tree / "mindone/modules/mt6358_battery/mtk_battery_algo.c").read_text()
    actual = function(algorithm, "fgr_update_uisoc_threshold")
    match = re.search(r"^#define CAR_MIN_GAP\s+(\d+)\s*$", algorithm, re.M)
    if not match:
        raise ValueError("Missing source minimum CAR gap")
    minimum = match.group(1)
    with tempfile.TemporaryDirectory(prefix="gauge-uisoc-") as directory:
        root = Path(directory)
        compile_check(root, actual, minimum)
        for values in POSITIVE_CASES:
            result = run_case(root, values)
            if result.returncode:
                raise RuntimeError("Positive UI threshold changed: " + result.stderr)
        print("PASS unchanged positive UI threshold and minimum-gap controls")
        for values in ZERO_CASES:
            result = run_case(root, values)
            if args.expect_zero_fault:
                if not result.returncode or "runtime error: division by zero" not in result.stderr:
                    raise RuntimeError("Expected an actual UI-zero division fault: " + result.stderr)
            elif result.returncode:
                raise RuntimeError("UI-zero threshold must remain safe: " + result.stderr)
        print("REPRODUCED UI-zero division fault in actual threshold source" if args.expect_zero_fault else "PASS UI-zero threshold with remaining charge and minimum gaps")
        if args.mutations:
            for name, before, after in MUTATIONS:
                if actual.count(before) != 1:
                    raise ValueError("Ambiguous mutation: " + name)
                compile_check(root, actual.replace(before, after, 1), minimum)
                failures = [run_case(root, values) for values in POSITIVE_CASES + ZERO_CASES]
                evidence = next((row.stderr for row in failures if row.returncode and
                                 ("runtime error: division by zero" in row.stderr or "Assertion failed:" in row.stderr)), None)
                if not evidence:
                    raise RuntimeError("Mutation did not fail a runtime control: " + name)
                print("PASS rejected " + name + ": " + evidence.splitlines()[0])


if __name__ == "__main__":
    main()
