# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import copy
import csv
import hashlib
import importlib.util
from pathlib import Path
import tempfile


SPEC = importlib.util.spec_from_file_location("checker", Path(__file__).with_name("gauge-sample-check.py"))
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
BOOT = "8291904c-6194-4733-ac5e-4d52d50731c8"


def observations(mode):
    return [{"boottime_s": time, "soc_pct": 75 - time // 360, "counter_uah": 1500000 - time * 100,
             "current_ua": -360000, "full_uah": 2000000, "design_uah": 2200000,
             "temp_decic": 220, "car_100uah": 5000 - time, "charger_online": 0,
             "voltage_uv": 4000000, "battery_status": "Discharging", "boot_id": BOOT,
             "algo_active": int(mode == "kernel"), "fuelgauged_state": "running" if mode == "daemon" else "stopped",
             "fuelgauged_pids": "885" if mode == "daemon" else "", "learned_aging_errno": 61 if mode == "daemon" else 0,
             "learned_cycles_errno": 61 if mode == "daemon" else 0,
             "learned_aging_bp": "" if mode == "daemon" else 9875,
             "learned_cycles_x100": "" if mode == "daemon" else 2000} for time in range(1801)]


def run(rows, mode="daemon", metadata=True, mutate=None):
    with tempfile.TemporaryDirectory(prefix="gauge-sample-test-") as directory:
        samples = Path(directory) / "samples.tsv"
        with samples.open("w") as stream:
            writer = csv.DictWriter(stream, tuple(rows[0]), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)
        provenance = {"schema": "mindone-gauge-window-v1", "mode": mode, "boot_id": BOOT,
                      "samples_sha256": hashlib.sha256(samples.read_bytes()).hexdigest(),
                      "kernel_commit": "a" * 40, "module_sha256": "b" * 64, "sampler_sha256": "c" * 64,
                      "learned_aging_source_sha256": "d" * 64, "collection_valid": True, "final_identity_verified": True,
                      "calibration": {"r_fg_value": 100, "car_tune_value": 1010}}
        if mutate:
            mutate(provenance)
        return CHECKER.evaluate(samples, provenance if metadata else None)


def main():
    rows = observations("daemon")
    report = run(rows)
    assert report["status"] == "PASS_WINDOW", report
    assert report["measurement"]["raw_car_delta_uah"] == -180000
    assert report["measurement"]["integrated_current_uah"] == -180000
    assert report["measurement"]["integrated_energy_uwh"] == -720000
    assert run(observations("kernel"), mode="kernel")["status"] == "PASS_WINDOW"
    print("PASS: signed 30-minute daemon and kernel windows, CAR/current/energy units")
    assert run(rows, metadata=False)["status"] == "INCOMPLETE"
    old = [{key: value for key, value in row.items() if key not in CHECKER.PROOF_FIELDS} for row in rows]
    assert run(old)["status"] == "INCOMPLETE"
    assert run(rows, mutate=lambda p: p.pop("calibration"))["status"] == "INCOMPLETE"
    assert run(rows, mutate=lambda p: p.pop("final_identity_verified"))["status"] == "INCOMPLETE"
    assert run(rows, mutate=lambda p: p.pop("learned_aging_source_sha256"))["status"] == "INCOMPLETE"
    assert run(rows, mutate=lambda p: p.update(collection_valid=False))["status"] == "FAIL"
    assert run(rows, mutate=lambda p: p.update(problems=[{"error": "postflight module changed"}]))["status"] == "FAIL"
    assert run(rows[:100])["status"] == "INCOMPLETE"
    print("PASS: missing provenance, old collector fields, calibration and duration cannot pass")
    assert run(rows, mutate=lambda p: p.update(samples_sha256="0" * 64))["status"] == "FAIL"
    rebooted = copy.deepcopy(rows)
    rebooted[-1]["boot_id"] = "6291904c-6194-4733-ac5e-4d52d50731c8"
    assert run(rebooted)["status"] == "FAIL"
    switched = copy.deepcopy(rows)
    switched[900]["algo_active"] = 1
    assert run(switched)["status"] == "FAIL"
    writers = observations("kernel")
    writers[900]["fuelgauged_pids"] = "885"
    assert run(writers, mode="kernel")["status"] == "FAIL"
    print("PASS: mismatched artifact, reboot, actual mode switch and competing writer fail")
    jump = copy.deepcopy(rows)
    for row in jump[900:]:
        row["counter_uah"] += 20000
    assert run(jump)["status"] == "FAIL"
    gradual = copy.deepcopy(rows)
    for time, row in enumerate(gradual):
        row["counter_uah"] += time
    assert run(gradual)["status"] == "FAIL"
    reset = copy.deepcopy(rows)
    for row in reset[900:]:
        row["car_100uah"] += 3000
    assert run(reset)["status"] == "FAIL"
    print("PASS: UI re-anchor, cumulative counter drift and unrecorded raw CAR reset fail")
    gap = copy.deepcopy(rows)
    del gap[800:810]
    assert run(gap)["status"] == "INCOMPLETE"
    time = copy.deepcopy(rows)
    time[900]["boottime_s"] = time[899]["boottime_s"]
    assert run(time)["status"] == "FAIL"
    nonfinite = copy.deepcopy(rows)
    nonfinite[900]["current_ua"] = "nan"
    assert run(nonfinite)["status"] == "INCOMPLETE"
    online = copy.deepcopy(rows)
    online[900]["charger_online"] = 1
    assert run(online)["status"] == "FAIL"
    print("PASS: gaps, duplicate times, non-finite values and a powered sample never pass")


if __name__ == "__main__":
    main()
