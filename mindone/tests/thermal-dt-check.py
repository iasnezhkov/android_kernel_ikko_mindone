# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=20).stdout.strip()


def get(blob, node, prop, kind="u"):
    value = run("fdtget", "-t", kind, str(blob), node, prop)
    return value if kind == "s" else [int(cell) for cell in value.split()]


def children(blob, node):
    return run("fdtget", "-l", str(blob), node).split()


def require(actual, expected, location):
    if actual != expected:
        raise ValueError(f"{location}: {actual!r} != {expected!r}")


def validate(blob, merged):
    root = "/thermal-zones"
    targets = {}
    for name in ("cpu0", "cpu6", "mali"):
        path = get(blob, "/__symbols__", name, "s")
        require(get(blob, path, "#cooling-cells"), [2], path)
        targets[name] = get(blob, path, "phandle")[0]
    hot = ("hot", "hot", 115000, 2000, ())
    schema = {"soc_max": (1000, 0, [
        ("trip-point@0", "hot", 95000, 2000, ()),
        ("trip-point@1", "critical", 113500, 2000, ())])}
    for cluster, temp, cpu in (("little", 85000, "cpu0"), ("big", 80000, "cpu6")):
        for index in range(1, 5):
            trips = [("trip-point@0", "passive", temp, 5000, (cpu,)),
                     ("trip-point@1", "hot", 105000, 2000, ())]
            schema[f"cpu_{cluster}{index}"] = (1000, 250, trips + ([hot] if merged else []))
    for name in ("gpu1", "gpu2"):
        trips = [("trip-point@0", "passive", 85000, 5000, ("mali",))]
        schema[name] = (1000, 250, trips + ([hot] if merged and name == "gpu1" else []))
    schema["ap_ntc"] = (1000, 250, [
        hot if merged else ("hot", "hot", 42000, 1000, ()),
        ("trip-point@1", "passive", 46000, 1000, ("cpu0", "cpu6", "mali"))])
    for name in ("soc1", "soc2", "md", "ltepa_ntc", "pmic6366_pmu",
                 "pmic6366_bk1", "pmic6366_bk2", "pmic6366_bk3", "consys"):
        schema[name] = (0, 0, [hot] if merged else [])
    if merged:
        for name in ("camera_main", "camera_sub"):
            schema[name] = (1000, 0, [hot])
    require(sorted(children(blob, root)), sorted(schema), root)
    for name, (normal, passive, trips) in schema.items():
        zone = root + "/" + name
        nodes = children(blob, zone)
        require(get(blob, zone, "polling-delay"), [normal], zone + "/polling-delay")
        require(get(blob, zone, "polling-delay-passive"), [passive], zone + "/polling-delay-passive")
        actual_trips = children(blob, zone + "/trips") if "trips" in nodes else []
        expected_nodes = [trip[0] for trip in trips]
        if merged and name not in ("ap_ntc", "soc_max"):
            actual_trips = sorted(actual_trips)
            expected_nodes = sorted(expected_nodes)
        require(actual_trips, expected_nodes, zone + "/trip-order")
        expected_maps = {}
        for node, kind, temp, hyst, cooling in trips:
            path = zone + "/trips/" + node
            require(get(blob, path, "type", "s"), kind, path + "/type")
            require(get(blob, path, "temperature"), [temp], path + "/temperature")
            require(get(blob, path, "hysteresis"), [hyst], path + "/hysteresis")
            if cooling:
                phandle = get(blob, path, "phandle")[0]
                expected_maps[phandle] = sorted((targets[c], 4294967295, 4294967295) for c in cooling)
        actual_maps = {}
        maps = children(blob, zone + "/cooling-maps") if "cooling-maps" in nodes else []
        for node in maps:
            path = zone + "/cooling-maps/" + node
            trip = get(blob, path, "trip")
            require(len(trip), 1, path + "/trip")
            cells = get(blob, path, "cooling-device")
            require(len(cells) % 3, 0, path + "/cooling-device")
            require(get(blob, path, "contribution"), [1024], path + "/contribution")
            actual_maps.setdefault(trip[0], []).extend(tuple(cells[i:i + 3]) for i in range(0, len(cells), 3))
        require({k: sorted(v) for k, v in actual_maps.items()}, expected_maps, zone + "/maps")


def main():
    parser = argparse.ArgumentParser(description="Validate M22 base and retained-overlay thermal DTs")
    parser.add_argument("base", type=Path)
    parser.add_argument("overlay", type=Path)
    args = parser.parse_args()
    inputs = {name: path.read_bytes() for name, path in (("base", args.base), ("overlay", args.overlay))}
    with tempfile.TemporaryDirectory(prefix="m22-thermal-") as temporary:
        base = Path(temporary) / "base.dtb"
        overlay = Path(temporary) / "overlay.dtbo"
        merged = Path(temporary) / "merged.dtb"
        base.write_bytes(inputs["base"])
        overlay.write_bytes(inputs["overlay"])
        validate(base, False)
        run("fdtoverlay", "-i", str(base), "-o", str(merged), str(overlay))
        validate(merged, True)
        run("fdtput", "-t", "u", str(merged), "/thermal-zones/ap_ntc/trips/trip-point@1", "temperature", "47000")
        try:
            validate(merged, True)
        except ValueError as error:
            if "/ap_ntc/trips/trip-point@1/temperature:" not in str(error):
                raise
        else:
            raise RuntimeError("Negative control accepted an incorrect skin trip")
    print(json.dumps({"status": "PASS_M22_THERMAL_DT", "negative_control": "rejected_47000",
                      "sha256": {k: hashlib.sha256(v).hexdigest() for k, v in inputs.items()}}))


if __name__ == "__main__":
    main()
