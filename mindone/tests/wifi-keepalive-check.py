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
        raise ValueError("Ambiguous source extraction: " + pattern)
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
    parser = argparse.ArgumentParser(description="Exercise exact WLAN keepalive handlers and OID with synthetic netlink, allocation and firmware-boundary controls")
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--baseline-dir", type=Path)
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tree = Path(__file__).resolve().parents[2]
    base = tree / "mindone/modules/wlan_drv_gen4m_6789"
    if args.baseline_dir:
        base = args.baseline_dir
    paths = ("os/linux/gl_vendor.c", "os/linux/include/gl_vendor.h",
             "common/wlan_oid.c", "os/linux/gl_init.c")
    sources = {path: (base / path).read_text() for path in paths}
    vendor = sources[paths[0]]
    header = sources[paths[1]]
    fragments = {
        "attributes": extract(header, r"^enum WIFI_MKEEP_ALIVE_ATTRIBUTE \{.*?^\};"),
        "request": extract(header, r"^struct PARAM_PACKET_KEEPALIVE_T \{.*?^\};"),
        "policy": extract(vendor, r"^const struct nla_policy nla_parse_offloading_policy\[.*?^\};"),
        "start": extract(vendor, r"^int mtk_cfg80211_vendor_packet_keep_alive_start\(.*?^\}"),
        "stop": extract(vendor, r"^int mtk_cfg80211_vendor_packet_keep_alive_stop\(.*?^\}"),
        "oid": extract(sources[paths[2]], r"^uint32_t\nwlanoidPacketKeepAlive\(.*?^\}"),
    }
    target = extract((tree / "mindone/modules/BUILD.bazel").read_text(),
                     r'ddk_module\(\n    name = "wlan_drv_gen4m_6789",.*?^\)')
    for name in ("os/linux/gl_vendor.c", "os/linux/gl_init.c", "common/wlan_oid.c"):
        if '"wlan_drv_gen4m_6789/' + name + '"' not in target:
            raise ValueError("Handler is not selected by active target: " + name)
    entries = re.findall(r"\.doit = mtk_cfg80211_vendor_packet_keep_alive_(?:start|stop)\s*,\s*\.policy = nla_parse_offloading_policy,\s*\.maxattr = ([^\n]+)", sources[paths[3]])
    if len(entries) != 2:
        raise ValueError("Missing active keepalive command registration")
    if not args.baseline_dir and entries != ["MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC"] * 2:
        raise ValueError("Keepalive policy maxattr exceeds its table")
    fixture = Path(__file__).with_name("wifi-keepalive")
    fixture_paths = (Path(__file__), fixture / "prelude.h", fixture / "cases.c")
    fixture_before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixture_paths}
    prefix = (fixture / "prelude.h").read_text()
    prefix += "\n" + "\n".join(fragments[x] for x in ("attributes", "request", "policy"))
    prefix += '''
static struct PARAM_PACKET_KEEPALIVE_T captured;
static uint32_t wlanSendSetQueryCmd(struct ADAPTER *a, unsigned cmd, bool set,
    bool response, bool oid, void *done, void *timeout, unsigned size,
    uint8_t *data, void *info, unsigned length)
{
    (void)a; (void)done; (void)timeout; (void)info; (void)length;
    assert(cmd == CMD_ID_WFC_KEEP_ALIVE && set && !response && oid);
    assert(size == sizeof(captured));
    memcpy(&captured, data, size);
    sends++;
    return send_status;
}
'''
    bridge = '''
static uint32_t kalIoctl(struct GLUE_INFO *g,
    uint32_t (*handler)(struct ADAPTER *, void *, uint32_t, uint32_t *),
    void *data, unsigned len, bool read, bool wait, bool cmd, uint32_t *info)
{
    assert(g == &glue && !read && !wait && cmd);
    ioctl_calls++;
    return ioctl_status ? ioctl_status : handler(&adapter, data, len, info);
}
'''
    variants = {"baseline" if args.baseline_dir else "candidate": fragments.copy()}
    if args.fault_controls:
        mutations = {
            "missing_length_limit": ("start", "u2IpPktLen > sizeof(prPkt->pIpPkt)", "0"),
            "missing_payload_equality": ("start", "nla_len(attr[MKEEP_ALIVE_ATTRIBUTE_IP_PKT]) != u2IpPktLen", "0"),
            "missing_required_packet": ("start", "!attr[MKEEP_ALIVE_ATTRIBUTE_IP_PKT] ||", ""),
            "missing_start_cleanup": ("start", "i4Status = -EFAULT;\n\t\tgoto nla_put_failure;", "return -EFAULT;"),
            "missing_stop_cleanup": ("stop", "i4Status = -EFAULT;\n\t\tgoto nla_put_failure;", "return -EFAULT;"),
            "missing_stop_id": ("stop", "!attr[MKEEP_ALIVE_ATTRIBUTE_ID]", "0"),
            "missing_oid_limit": ("oid", "(uint16_t)prPacket->u2IpPktLen > sizeof(prPacket->pIpPkt)", "0"),
            "missing_oid_positive": ("oid", "prPacket->u2IpPktLen <= 0", "0"),
        }
        for name, (part, before, after) in mutations.items():
            if fragments[part].count(before) != 1:
                raise ValueError("Mutation target missing: " + name)
            variant = fragments.copy()
            variant[part] = variant[part].replace(before, after)
            variants[name] = variant
    records = {}
    with tempfile.TemporaryDirectory(prefix="mindone-keepalive-") as temporary:
        output = Path(temporary)
        for name, parts in variants.items():
            code = prefix + "\n" + parts["oid"] + "\n" + bridge
            code += parts["start"] + "\n" + parts["stop"]
            code += "\n" + (fixture / "cases.c").read_text()
            source = output / (name + ".c")
            binary = output / name
            source.write_text(code)
            compiled = execute([args.compiler, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                                "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                str(source), "-o", str(binary)])
            if compiled["returncode"]:
                raise ValueError("Compile failed: " + name + ": " + compiled["stderr"])
            modes = ("signed-copy", "short-copy", "missing-start", "missing-stop", "oid-length", "not-ready") if args.baseline_dir else ("all",)
            records[name] = {}
            for mode in modes:
                result = execute([str(binary), mode])
                expected_failure = name != "candidate"
                result["expected_failure"] = expected_failure
                result["passed"] = (result["returncode"] != 0 if expected_failure else result["returncode"] == 0)
                if expected_failure:
                    result["passed"] &= "Assertion failed" in result["stderr"] or "BOUNDARY_REJECT:" in result["stderr"]
                records[name][mode] = result
    unchanged = all((base / name).read_text() == value for name, value in sources.items())
    if not unchanged:
        raise ValueError("Source changed during validation")
    fixture_after = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixture_paths}
    if fixture_before != fixture_after:
        raise ValueError("Fixture changed during validation")
    report = {"status": "pass" if all(r["passed"] for rows in records.values() for r in rows.values()) else "fail",
              "sources": {name: hashlib.sha256(value.encode()).hexdigest() for name, value in sources.items()},
              "fragments": {name: hashlib.sha256(value.encode()).hexdigest() for name, value in fragments.items()},
              "input_hashes_unchanged": unchanged,
              "fixture_sha256_before": fixture_before, "fixture_sha256_after": fixture_after,
              "fixture_hashes_unchanged": True,
              "selected_target_sha256": hashlib.sha256(target.encode()).hexdigest(),
              "policy_maxattr": entries, "results": records,
              "limits": "Exact START/STOP/OID functions, ABI struct and policy; synthetic strict scalar/binary netlink parser, intercepted allocator/copy/ioctl/firmware submission. Copies are checked against allocation and attribute bounds before memory access; no out-of-bounds copy is performed. No actual cfg80211 concurrency, firmware acknowledgement, native module or device acceptance."}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    raise SystemExit(report["status"] != "pass")


if __name__ == "__main__":
    main()
