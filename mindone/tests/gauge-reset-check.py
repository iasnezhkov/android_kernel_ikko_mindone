# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import runpy
import re
import subprocess
import tempfile

function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


CASES = ("kernel", "daemon", "origin", "retry", "read-errors", "write-error", "post-read-error", "no-cs", "disabled", "locked", "raw-sysfs", "wrappers", "initializing", "setup-tracking", "threshold-setup")


def main():
    parser = argparse.ArgumentParser(description="Exercise the exact reset pipeline, property wrappers, consumer lists and kernel CAR anchors")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    battery = (base / "mtk_battery.c").read_text()
    algorithm = (base / "mtk_battery_algo.c").read_text()
    gauge = (base / "mt6358-gauge.c").read_text()
    consumers = (base / "mtk_battery_coulomb_service.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-reset"
    source = (fixtures / "stubs.h").read_text()
    for name in ("gauge_get_property", "gauge_get_int_property", "gauge_set_property", "battery_set_property"):
        source += function(battery, name)
    source += function(gauge, "charge_counter_update")
    source += function(gauge, "reset_set").replace("static int reset_set(", "static int gauge_reset_set(")
    for name in ("gauge_coulomb_before_reset", "gauge_coulomb_after_reset"):
        source += function(consumers, name)
    source += function(consumers, "gauge_coulomb_start")
    for name in ("coulomb_int_gap_set", "uisoc_ht_int_gap_set", "uisoc_lt_int_gap_set"):
        source += function(battery, name)
    source += function(algorithm, "fgr_cycle_capacity")
    if "void fgr_sw_cycle_accu_car(" in algorithm:
        source += function(algorithm, "fgr_sw_cycle_accu_car")
        source += function(algorithm, "fgr_reset_car")
    source += function(algorithm, "fgr_sw_cycle_accu")
    if "static int battery_reset_charge_mode_locked(" in battery:
        source += function(battery, "battery_reset_charge_mode_locked")
    if "int battery_reset_charge_locked(" in battery:
        source += function(battery, "battery_reset_charge_locked")
    if "int battery_reset_charge_init_locked(" in battery:
        source += function(battery, "battery_reset_charge_init_locked")
    source += function(battery, "reset_set").replace("static int reset_set(", "static int battery_reset_set(")
    source += function(gauge, "gauge_sysfs_store")
    source += function(algorithm, "fg_adc_reset")
    for name in ("Set_fg_c_d0_by_ocv", "fgr_update_c_dod"):
        if name == "fgr_update_c_dod" and "static void fgr_update_c_dod_car(" in algorithm:
            source += function(algorithm, "fgr_update_c_dod_car")
        source += function(algorithm, name)
    source += (fixtures / "cases.c").read_text()
    controls = (
        ("discard-setup-write-error", "gauge_set_property", "if (setup_write && ret < 0)", "if (setup_write && ret < 0 && false)", "setup-tracking"),
        ("overwrite-first-setup-error", "gauge_set_property", "cmpxchg(&gauge->gm->kernel_setup_error, 0, ret);", "gauge->gm->kernel_setup_error = ret;", "setup-tracking"),
        ("discard-inflight-setup-error", "gauge_set_property", "if (setup_write && ret < 0)", "if (READ_ONCE(gauge->gm->kernel_setup_in_progress) && ret < 0)", "setup-tracking"),
        ("hide-before-reset-threshold-error", "gauge_coulomb_before_reset", "if (!ret)\n\t\tret = gauge_set_property", "if (true)\n\t\tret = gauge_set_property", "setup-tracking"),
        ("hide-consumer-threshold-error", "gauge_coulomb_start", "return ret;\n}", "return 0;\n}", "threshold-setup"),
        ("hide-coulomb-gap-error", "coulomb_int_gap_set", "ret = gauge_coulomb_start(gm, &gm->coulomb_plus, val);\n\tif (ret)", "ret = gauge_coulomb_start(gm, &gm->coulomb_plus, val);\n\tif (ret && false)", "threshold-setup"),
    )
    variants = [("actual", source, args.case or CASES)]
    if not args.case:
        for name, target, before, after, case in controls:
            body = function(consumers if target.startswith("gauge_coulomb_") else battery, target)
            if body.count(before) != 1:
                raise ValueError("Ambiguous runtime control: " + name)
            variants.append((name, source.replace(body, body.replace(before, after, 1), 1), (case,)))
    with tempfile.TemporaryDirectory(prefix="gauge-reset-") as directory:
        for name, text, cases in variants:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(text)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-but-set-variable", "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path)], check=True, timeout=30)
            for case in cases:
                result = subprocess.run([str(path), case], capture_output=True, text=True, timeout=10)
                if name == "actual":
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    print(result.stdout, end="")
                elif not result.returncode or not re.search(r"Assertion failed:|runtime error:|ERROR: AddressSanitizer", result.stderr):
                    raise RuntimeError("Control did not fail at runtime: " + name + "\n" + result.stdout + result.stderr)
                else:
                    print("PASS rejected " + name)


if __name__ == "__main__":
    main()
