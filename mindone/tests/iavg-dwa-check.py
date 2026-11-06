# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


helpers = runpy.run_path(str(Path(__file__).with_name("ptim-sample-check.py")))
clean = helpers["clean"]
define = helpers["define"]
structure = helpers["structure"]
replace_once = helpers["replace_once"]
NAMES = ("pre_gauge_update", "post_gauge_update", "enable_dwa", "dwa_convert_current",
         "dwa_latch_clear", "read_iavg_dwa_sample", "iavg_check", "iavg_workaround", "event_set",
         "battery_meter_meta_tool_cali_car_tune")
CONSTANTS = ("RG_FGADC_CON1", "FG_LATCHDATA_ST_MASK", "FG_SW_CLEAR_MASK", "FG_SW_READ_PRE_MASK",
             "RG_FGADC_ANA_TEST_CON0", "FG_DWA_RST_SW_MASK", "FG_DWA_RST_SW_SHIFT",
             "FG_DWA_RST_MODE_MASK", "FG_DWA_RST_MODE_SHIFT", "RG_FGADC_ANA_CON0",
             "RG_SPARE_MASK", "RG_SPARE_SHIFT", "RG_FGADC_CUR_CON3", "RG_FGADC_OFFSET_CON0",
             "RG_FGADC_IAVG_CON1", "RG_FGADC_IAVG_CON0", "FG_IAVG_VLD_MASK", "FG_IAVG_VLD_SHIFT",
             "FG_IAVG_27_16_MASK", "FG_IAVG_15_00_MASK", "UNIT_FGCURRENT", "UNIT_FG_IAVG", "DEFAULT_R_FG")
MUTATIONS = (
    ("skip-preflight", "read_iavg_dwa_sample", "\tret = dwa_latch_clear(gauge);", "\tret = 0;"),
    ("discard-cleanup-error", "read_iavg_dwa_sample", "if (ret || clear_ret)", "if (ret)"),
    ("accept-dirty-latch", "dwa_latch_clear", "if (value & (FG_LATCHDATA_ST_MASK | FG_SW_READ_PRE_MASK | FG_SW_CLEAR_MASK))",
     "if (false)"),
    ("publish-before-read", "read_iavg_dwa_sample", "\tret = dwa_latch_clear(gauge);",
     "\tgauge->fg_hw_info.current_2 = 0;\n\tret = dwa_latch_clear(gauge);"),
    ("hide-outer-status", "event_set", "return iavg_workaround(gauge, event);",
     "iavg_workaround(gauge, event);\n\treturn 0;"),
    ("ignore-first-dwa-write", "enable_dwa", "if (ret)\n\t\treturn ret;\n\tret = regmap_update_bits",
     "if (false)\n\t\treturn ret;\n\tret = regmap_update_bits"),
    ("discard-invalid-fallback", "read_iavg_dwa_sample", "next.iavg = next.cic2;", "next.iavg = 0;"),
    ("change-average-polarity", "read_iavg_dwa_sample", "value = -value;", "value += 0;"),
    ("sum-in-int", "iavg_check", "sum = (s64)sample.iavg + sample.offset;", "sum = sample.iavg + sample.offset;"),
    ("remove-factory-lock", "battery_meter_meta_tool_cali_car_tune", "mutex_lock(&gm->gauge->ops_lock);", ""),
)


def function(source, name):
    return helpers["function"](re.sub(r"^signed int ", "int ", source, flags=re.M), name)


def main():
    parser = argparse.ArgumentParser(description="Exercise exact IAVG/DWA read, cleanup, conversion and event failure boundaries")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    source_path = tree / "mindone/modules/mt6358_battery/mt6358-gauge.c"
    fixture = Path(__file__).resolve().with_name("iavg-dwa")
    inputs = [source_path, Path(__file__).resolve(), Path(__file__).resolve().with_name("ptim-sample-check.py"),
              fixture / "stubs.h", fixture / "cases.c"]
    inputs_before = {str(path.relative_to(tree)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    source = source_path.read_text()
    prefix = "".join(define(source, name) for name in CONSTANTS)
    prefix += (fixture / "stubs.h").read_text()
    prefix += structure(source, "gauge_dwa_sample")
    functions = {name: function(source, name) for name in NAMES}
    suffix = (fixture / "cases.c").read_text()
    variants = [("actual", functions)]
    for label, target, before, after in MUTATIONS:
        changed = replace_once(functions[target], before, after)
        variants.append((label, {**functions, target: changed}))
    results = []
    with tempfile.TemporaryDirectory(prefix="iavg-dwa-") as directory:
        for label, selected in variants:
            path = Path(directory) / label
            path.with_suffix(".c").write_text(prefix + "\n".join(selected.values()) + suffix)
            command = ["clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                       "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-sign-compare",
                       "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", str(path.with_suffix(".c")), "-o", str(path)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if compiled.returncode:
                raise RuntimeError(label + " compile failed:\n" + compiled.stderr)
            result = subprocess.run([str(path)], capture_output=True, text=True, timeout=15)
            if label == "actual":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end="")
            elif result.returncode == 0 or not re.search(r"CHECK failed:|runtime error:|ERROR: AddressSanitizer", result.stderr):
                raise RuntimeError("Control did not fail at runtime: " + label + "\n" + result.stdout + result.stderr)
            else:
                print("PASS rejected " + label)
            results.append({"variant": label, "compile_rc": compiled.returncode, "run_rc": result.returncode,
                            "stdout": result.stdout.strip(), "stderr": result.stderr.strip(),
                            "extracted_fixture_sha256": hashlib.sha256(path.with_suffix(".c").read_bytes()).hexdigest()})
    inputs_after = {str(path.relative_to(tree)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    if inputs_before != inputs_after:
        raise RuntimeError("Input hashes changed during the test run")
    print("PASS input hash continuity: " + str(len(inputs)) + " files")
    report = {"status": "PASS", "tests": results,
              "inputs_before": inputs_before, "inputs_after": inputs_after,
              "limitations": ["Host functions use deterministic regmap, locks and calibration stubs; no PMIC or native kernel execution.",
                              "One checked software transaction does not establish hardware sample synchrony or calibration accuracy.",
                              "Legacy scalar/current caches and legacy latch failure behavior are not accepted by these tests.",
                              "DWA errors stop later writes and preserve status; prior writes may remain applied, with no rollback or state proof.",
                              "DWA acknowledged-unapplied writes are explicitly unverified, unlike checked latch cleanup readback."]}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
