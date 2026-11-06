# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile


def extract(source, pattern):
    matches = re.findall(pattern, source, re.M | re.S)
    if len(matches) != 1:
        raise ValueError("Ambiguous source: " + pattern)
    return matches[0]


def execute(argv):
    process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=40)
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    return {"returncode": process.returncode, "stdout": stdout, "stderr": stderr}


def main():
    parser = argparse.ArgumentParser(description="Check exact roaming input admission before blacklist mutation using bounded host interceptors")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tree = Path(__file__).resolve().parents[2]
    base = tree / "mindone/modules/wlan_drv_gen4m_6789"
    paths = {"vendor": args.baseline or base / "os/linux/gl_vendor.c",
             "header": base / "os/linux/include/gl_vendor.h",
             "config": base / "include/config.h",
             "mac": base / "include/nic/mac.h",
             "netlink": tree / "include/net/netlink.h",
             "registration": base / "os/linux/gl_init.c",
             "build": tree / "mindone/modules/BUILD.bazel"}
    sources = {key: path.read_text() for key, path in paths.items()}
    function = extract(sources["vendor"], r"^int mtk_cfg80211_vendor_config_roaming\(.*?^\}")
    fragments = {"handler": function,
                 "attributes": extract(sources["header"], r"^enum WIFI_ATTRIBUTE \{.*?^\};"),
                 "shared_limit": extract(sources["config"], r"^#define CFG_FW_ROAMING_BLACKLIST_SIZE[^\n]*"),
                 "limit": extract(sources["header"], r"^#define MAX_FW_ROAMING_BLACKLIST_SIZE[^\n]*"),
                 "copy_mac": extract(sources["mac"], r"^#define COPY_MAC_ADDR\([^\n]*\n[^\n]*"),
                 "policy": extract(sources["vendor"], r"^const struct nla_policy nla_parse_wifi_attribute\[.*?^\};")}
    for name in ("nla_type", "nla_len", "nla_ok", "nla_next"):
        fragments[name] = extract(sources["netlink"], r"^static inline [^\n]*\b" + name + r"\(.*?^\}")
    fragments["iteration"] = extract(sources["netlink"], r"^#define nla_for_each_attr\([^\n]*\n(?:[^\n]*\\\n)*[^\n]*")
    target = extract(sources["build"], r'ddk_module\(\n    name = "wlan_drv_gen4m_6789",.*?^\)')
    if '"wlan_drv_gen4m_6789/os/linux/gl_vendor.c"' not in target:
        raise ValueError("Handler not selected by target")
    if ".doit = mtk_cfg80211_vendor_config_roaming" not in sources["registration"]:
        raise ValueError("Handler not registered")
    variants = {"baseline" if args.baseline else "candidate": function}
    if args.fault_controls:
        substitutions = {
            "skip_policy_validation": ("if (nla_validate(data, data_len, WIFI_ATTRIBUTE_MAX,\n\t\t\t nla_parse_wifi_attribute, NULL) < 0)", "if (0)"),
            "missing_count_presence": ("!fgCountSeen", "0"),
            "missing_count_equality": ("u4BssidCount != u4BlacklistCount", "0"),
            "missing_count_uniqueness": ("if (fgCountSeen)", "if (0)"),
            "missing_record_capacity": ("u4BssidCount >= MAX_FW_ROAMING_BLACKLIST_SIZE", "0"),
            "early_refresh": ("if (nla_validate(data,", "aisRefreshFWKBlacklist(prGlueInfo->prAdapter);\n\tif (nla_validate(data,"),
            "reject_zero_count": ("if (remaining > 0 ||", "if (u4BssidCount == 0 || remaining > 0 ||"),
            "corrupt_copied_bssid": ("u4BssidCount++;", "aucBssid[u4BssidCount][0] ^= 1; u4BssidCount++;"),
            "ignore_enabled_status": ("if (ret)\n\t\treturn ret;", "if (0)\n\t\treturn ret;"),
            "ignore_disabled_roaming": ("if (!enabled)", "if (0)"),
        }
        for name, (before, after) in substitutions.items():
            if function.count(before) != 1:
                raise ValueError("Missing mutation target: " + name)
            variants[name] = function.replace(before, after)
    fixture = Path(__file__).with_name("wifi-roaming")
    fixture_paths = (Path(__file__), fixture / "prelude.h", fixture / "cases.c")
    fixture_before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixture_paths}
    prefix = (fixture / "prelude.h").read_text() + "\n"
    prefix += "\n".join(value for key, value in fragments.items() if key != "handler") + "\n"
    cases = (fixture / "cases.c").read_text()
    records = {}
    with tempfile.TemporaryDirectory(prefix="mindone-roaming-") as temporary:
        directory = Path(temporary)
        for name, body in variants.items():
            source = directory / (name + ".c")
            binary = directory / name
            source.write_text(prefix + body + "\n" + cases)
            compatibility = ["-Wno-sign-compare"] if args.baseline else []
            result = execute([args.compiler, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                              "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-const-variable",
                              "-Wno-unused-but-set-variable", "-fsanitize=address,undefined",
                              "-fno-sanitize-recover=all", *compatibility, str(source), "-o", str(binary)])
            if result["returncode"]:
                raise ValueError("Compile failed: " + name + ": " + result["stderr"])
            modes = ("missing-count", "wrong-count", "duplicate-count") if args.baseline else ("all",)
            records[name] = {}
            for mode in modes:
                result = execute([str(binary), mode])
                if name == "candidate":
                    result["passed"] = result["returncode"] == 0
                else:
                    result["passed"] = (result["returncode"] in (2, 42) and
                                        ("BOUNDARY_REJECT:" in result["stderr"] or "CHECK_FAIL:" in result["stderr"]))
                records[name][mode] = result
    if any(path.read_text() != sources[name] for name, path in paths.items()):
        raise ValueError("Source changed during checks")
    fixture_after = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixture_paths}
    if fixture_before != fixture_after:
        raise ValueError("Fixture changed during checks")
    result = {"status": "pass" if all(row["passed"] for rows in records.values() for row in rows.values()) else "fail",
              "source_sha256": {key: hashlib.sha256(value.encode()).hexdigest() for key, value in sources.items()},
              "fragment_sha256": {key: hashlib.sha256(value.encode()).hexdigest() for key, value in fragments.items()},
              "fixture_sha256_before": fixture_before, "fixture_sha256_after": fixture_after,
              "fixture_hashes_unchanged": True,
              "target_sha256": hashlib.sha256(target.encode()).hexdigest(), "input_hashes_unchanged": True,
              "results": records,
              "limits": "Exact driver handler and local kernel iterator helpers; policy validator is a bounded scalar/binary host model. Reads and mutation boundaries are intercepted before access; baseline cases contain complete allocated attributes and stop before mutation. Owned-submit boundary is intercepted; real AIS allocation rollback and slot races have separate exact-source gates. No out-of-bounds copy, live kernel/network traffic, firmware or device evidence."}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(result["status"] != "pass")


if __name__ == "__main__":
    main()
