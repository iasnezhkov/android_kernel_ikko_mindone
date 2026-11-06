# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import runpy
import sys
import uuid


MEASUREMENT_FIELDS = ("boottime_s", "soc_pct", "counter_uah", "current_ua", "full_uah", "design_uah",
                      "temp_decic", "car_100uah", "charger_online", "voltage_uv", "battery_status")
PROOF_FIELDS = ("boot_id", "algo_active", "fuelgauged_state", "fuelgauged_pids", "learned_aging_errno",
                "learned_cycles_errno", "learned_aging_bp", "learned_cycles_x100")
LIMITATIONS = ["This checks one off-cable interval, not complete GM30/kernel parity, shutdown or persistence.",
               "Current and CAR share calibration; their agreement does not establish absolute physical capacity.",
               "Per-sample mode and process observations cannot exclude a writer between observations.",
               "Source provenance and actual mode observations must be collected from the device; a module parameter is not actual mode."]


def evaluate(samples, provenance=None):
    missing, failures = [], []
    result = {"status": "INCOMPLETE", "scope": "single off-cable counter and mode observation window",
              "missing": missing, "failures": failures, "limitations": LIMITATIONS}
    with samples.open() as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        fields = tuple(reader.fieldnames or ())
        missing.extend("sample field: " + field for field in MEASUREMENT_FIELDS + PROOF_FIELDS if field not in fields)
        raw = list(reader)
    if len(set(fields)) != len(fields) or any(None in row or any(value is None for value in row.values()) for row in raw):
        missing.append("complete rows and unique column names")
        return result
    if not raw:
        missing.append("samples")
    if any(field not in fields for field in MEASUREMENT_FIELDS) or not raw:
        return result
    rows = []
    try:
        for row in raw:
            values = {field: float(row[field]) for field in MEASUREMENT_FIELDS if field != "battery_status"}
            if not all(math.isfinite(value) for value in values.values()):
                raise ValueError("non-finite numeric value")
            if any(value != int(value) for field, value in values.items() if field != "boottime_s"):
                raise ValueError("non-integral device property")
            rows.append({**row, **values})
    except (KeyError, TypeError, ValueError) as error:
        missing.append("valid measurement rows: " + str(error))
        return result
    durations = [right["boottime_s"] - left["boottime_s"] for left, right in zip(rows, rows[1:])]
    if any(duration <= 0 for duration in durations):
        failures.append("sample time is not strictly increasing")
        result["status"] = "FAIL"
        return result
    duration = rows[-1]["boottime_s"] - rows[0]["boottime_s"]
    charge = sum((left["current_ua"] + right["current_ua"]) / 2 * seconds / 3600
                 for left, right, seconds in zip(rows, rows[1:], durations))
    energy = sum((left["current_ua"] * left["voltage_uv"] + right["current_ua"] * right["voltage_uv"]) / 2 * seconds / 3.6e9
                 for left, right, seconds in zip(rows, rows[1:], durations))
    counter = rows[-1]["counter_uah"] - rows[0]["counter_uah"]
    car = (rows[-1]["car_100uah"] - rows[0]["car_100uah"]) * 100
    result["measurement"] = {"samples": len(rows), "seconds": duration, "counter_delta_uah": counter,
                             "raw_car_delta_uah": car, "integrated_current_uah": charge,
                             "integrated_energy_uwh": energy, "counter_car_difference_uah": counter - car,
                             "car_current_difference_uah": car - charge,
                             "model_full_uah_range": [min(row["full_uah"] for row in rows), max(row["full_uah"] for row in rows)],
                             "nominal_design_uah_range": [min(row["design_uah"] for row in rows), max(row["design_uah"] for row in rows)]}
    if duration < 1800:
        missing.append("at least 1800 seconds of observations")
    if any(seconds > 2.5 for seconds in durations):
        missing.append("continuous approximately one-second sample cadence")
    if any(row["charger_online"] != 0 or row["battery_status"] != "Discharging" for row in rows):
        failures.append("physical off-cable discharge is not confirmed throughout the window")
    if any(not 0 <= row["soc_pct"] <= 100 or row["full_uah"] <= 0 or row["design_uah"] <= 0
           or not 2500000 <= row["voltage_uv"] <= 5000000 or not -200 <= row["temp_decic"] <= 600 for row in rows):
        failures.append("measurement value is outside the valid property range")
    if any(row["counter_uah"] < 0 or row["counter_uah"] > row["full_uah"] for row in rows):
        failures.append("reported remaining charge exceeds the model bounds")
    if any(row["counter_uah"] in (0, row["full_uah"]) for row in rows):
        missing.append("window free of model-boundary counter clipping")
    if any(abs((right["counter_uah"] - left["counter_uah"]) -
               (right["car_100uah"] - left["car_100uah"]) * 100) > 200
           for left, right in zip(rows, rows[1:])):
        failures.append("counter changes do not preserve raw CAR changes")
    if abs(counter - car) > 200:
        failures.append("counter window delta does not preserve raw CAR window delta")
    if abs(car - charge) > max(1000, abs(charge) * 0.05):
        failures.append("raw CAR disagrees with the signed current integral beyond 1 mAh or five percent")
    if car >= 0 or charge >= 0:
        failures.append("there is no measured net battery discharge")
    if provenance is None:
        missing.append("device and sample provenance")
    else:
        if provenance.get("schema") != "mindone-gauge-window-v1":
            missing.append("supported provenance schema")
        if provenance.get("collection_valid") is not True:
            if "collection_valid" not in provenance:
                missing.append("collector quality result")
            else:
                failures.append("collector marked the observations invalid")
        if provenance.get("final_identity_verified") is not True:
            missing.append("final device and binary identity verification")
        if provenance.get("problems"):
            failures.append("collector reported unresolved observation problems")
        if provenance.get("gaps"):
            missing.append("collector reported observation gaps")
        if provenance.get("samples_sha256") != hashlib.sha256(samples.read_bytes()).hexdigest():
            failures.append("sample hash does not match provenance")
        for key in ("kernel_commit", "module_sha256", "sampler_sha256", "learned_aging_source_sha256"):
            size = 40 if key == "kernel_commit" else 64
            if not re.fullmatch(r"[0-9a-f]{" + str(size) + r"}", str(provenance.get(key, ""))):
                missing.append("provenance: " + key)
        calibration = provenance.get("calibration", {})
        if not isinstance(calibration, dict) or any(not isinstance(calibration.get(key), int) or calibration[key] <= 0
                                                   for key in ("r_fg_value", "car_tune_value")):
            missing.append("measured active gauge calibration")
        mode = provenance.get("mode")
        if mode not in ("daemon", "kernel"):
            missing.append("provenance mode")
        if all(field in fields for field in PROOF_FIELDS):
            boot_ids = {row["boot_id"] for row in rows}
            try:
                for boot in boot_ids:
                    uuid.UUID(boot)
            except ValueError:
                missing.append("valid boot identity")
            if len(boot_ids) != 1 or provenance.get("boot_id") not in boot_ids:
                failures.append("boot identity changed or differs from provenance")
            try:
                for row in rows:
                    active = int(row["algo_active"])
                    aging_error, cycles_error = int(row["learned_aging_errno"]), int(row["learned_cycles_errno"])
                    pids = [int(pid) for pid in row["fuelgauged_pids"].split(",")] if row["fuelgauged_pids"] else []
                    if any(pid <= 0 for pid in pids):
                        raise ValueError("invalid daemon PID")
                    if mode == "daemon" and (active != 0 or aging_error != 61 or cycles_error != 61 or
                                              row["fuelgauged_state"] != "running" or len(pids) != 1):
                        failures.append("daemon mode or its exclusive writer is not confirmed at each sample")
                        break
                    if mode == "kernel":
                        if active != 1 or aging_error != 0 or cycles_error != 0 or row["fuelgauged_state"] != "stopped" or pids:
                            failures.append("kernel mode or absence of daemon writer is not confirmed at each sample")
                            break
                        if not 7000 <= int(row["learned_aging_bp"]) <= 10000 or int(row["learned_cycles_x100"]) < 0:
                            failures.append("kernel learned state is outside its permitted range")
                            break
            except (TypeError, ValueError):
                missing.append("valid per-sample actual mode, errno and process observations")
    result["status"] = "FAIL" if failures else "INCOMPLETE" if missing else "PASS_WINDOW"
    return result


def main():
    parser = argparse.ArgumentParser(description="Check a saved off-cable TSV window, or run synthetic host regressions when given a kernel tree; missing mode, provenance or raw measurements never pass")
    parser.add_argument("samples", type=Path)
    parser.add_argument("--provenance", type=Path)
    args = parser.parse_args()
    if args.samples.is_dir() and (args.samples / "mindone/tests/gauge-sample-check-test.py").is_file() and args.provenance is None:
        runpy.run_path(str(args.samples / "mindone/tests/gauge-sample-check-test.py"))["main"]()
        return 0
    try:
        provenance = json.loads(args.provenance.read_text()) if args.provenance else None
        if provenance is not None and not isinstance(provenance, dict):
            raise ValueError("provenance must be a JSON object")
        result = evaluate(args.samples, provenance)
    except (OSError, ValueError, csv.Error) as error:
        result = {"status": "INCOMPLETE", "missing": [str(error)], "failures": [], "limitations": LIMITATIONS}
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "PASS_WINDOW" else 1


if __name__ == "__main__":
    sys.exit(main())
