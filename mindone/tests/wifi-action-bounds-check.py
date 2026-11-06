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


def read(tree, path, reference):
    if reference:
        return subprocess.run(
            ["git", "show", reference + ":" + path], cwd=tree,
            env={**os.environ, "GIT_NO_LAZY_FETCH": "1"}, capture_output=True,
            text=True, check=True, timeout=20).stdout
    return (tree / path).read_text()


def extract(source, pattern):
    matches = list(re.finditer(pattern, source, re.S | re.M))
    if len(matches) != 1:
        raise ValueError("Ambiguous source fragment: " + pattern)
    return matches[0].group(0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--reference")
    parser.add_argument("--expect-trap", action="store_true")
    args = parser.parse_args()
    base = "mindone/modules/wlan_drv_gen4m_6789/"
    source = read(args.tree, base + "mgmt/p2p_func.c", args.reference)
    mac = read(args.tree, base + "include/nic/mac.h", args.reference)
    definitions = read(args.tree, base + "include/nic/wlan_def.h", args.reference)
    function = extract(source, r"^void p2pFuncValidateRxActionFrame\([^\n]*(?:\n.*?)*?^\}")
    structures = "\n".join(extract(mac, r"struct " + name + r" \{.*?\} __KAL_ATTRIB_PACKED__;")
                           for name in ("WLAN_ACTION_FRAME", "WLAN_PUBLIC_VENDOR_ACTION_FRAME"))
    macro = extract(definitions, r"^#define WLAN_GET_FIELD_BE24[^\n]*(?:\n[^\n]*\\)*\n[^\n]*\}")
    fixture = Path(__file__).with_name("wifi-action-bounds")
    code = (fixture / "prelude.h").read_text() + structures + "\n" + macro + "\n"
    code += (fixture / "stubs.h").read_text() + function + "\n" + (fixture / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="wifi-action-bounds-") as tmp:
        path = Path(tmp) / "check"
        path.with_suffix(".c").write_text(code)
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-fstrict-flex-arrays=3",
                        "-fsanitize=address,undefined", "-fsanitize-trap=bounds",
                        "-fno-sanitize-recover=all", str(path.with_suffix(".c")),
                        "-o", str(path)], check=True, timeout=40)
        result = subprocess.run([str(path)] + (["baseline"] if args.expect_trap else []),
                                capture_output=True, text=True, timeout=15)
        passed = result.returncode in (-signal.SIGILL, -signal.SIGTRAP) if args.expect_trap else result.returncode == 0
        print(json.dumps({"passed": passed, "reference": args.reference,
                          "expected_bounds_trap": args.expect_trap,
                          "function_sha256": hashlib.sha256(function.encode()).hexdigest(),
                          "returncode": result.returncode, "stdout": result.stdout,
                          "stderr": result.stderr}, indent=2))
        raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
