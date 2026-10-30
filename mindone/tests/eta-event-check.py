# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def clean(text):
    pattern = r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*'
    return re.sub(pattern, lambda m: "" if m.group().startswith("/") else m.group(), text, flags=re.S)


def function(source, name):
    source = clean(source)
    match = re.search(r"^(?:static\s+)?(?:inline\s+)?(?:int|void|u32)\s+" + re.escape(name) + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError("Missing function: " + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def replace_one(source, before, after):
    if source.count(before) != 1:
        raise ValueError("Mutation anchor changed: " + before)
    return source.replace(before, after, 1)


def run(args, paths, variants, fixture, before):
    digest = lambda: {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    results = []
    compiler = subprocess.run([args.cc, "--version"], check=True, capture_output=True, text=True, timeout=10).stdout
    with tempfile.TemporaryDirectory(prefix=Path(__file__).stem + "-") as directory:
        build = Path(directory)
        for name, source in variants:
            text = fixture(source)
            path, binary = build / (name + ".c"), build / name
            path.write_text(text)
            command = [args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-g",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all", str(path), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if compiled.returncode:
                raise RuntimeError(name + " compile failed: " + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            results.append({"variant": name, "fixture_sha256": hashlib.sha256(text.encode()).hexdigest(),
                            "compile_command": command, "compile_rc": compiled.returncode,
                            "run_rc": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
            if name == "actual" and result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            if name != "actual" and (result.returncode == 0 or "CHECK failed" not in result.stderr):
                raise RuntimeError("Mutation survived or failed outside a check: " + name + result.stderr)
            print(name + ": " + (result.stdout.strip() if name == "actual" else "runtime rejection PASS"))
    after = digest()
    if before != after:
        raise RuntimeError("Inputs changed while testing")
    print("PASS input hash continuity: " + str(len(paths)) + " files")
    if args.output:
        args.output.write_text(json.dumps({"status": "PASS", "compiler_version": compiler,
            "inputs_before": before, "inputs_after": after, "tests": results,
            "limits": "Host stubs only; no kernel build, hardware, MMIO ordering or concurrent lifetime validation."}, indent=2) + "\n")


def arguments(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--tree", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main():
    args = arguments("Exercise extracted ETA event callback, ops dispatch and basic charger enable path")
    tree = args.tree.resolve()
    here = Path(__file__).resolve().parent
    paths = [tree / "mindone/modules/eta6965_charger/eta6965_charger.c",
             tree / "mindone/modules/mtk_charger_framework/mtk_basic_charger.c",
             tree / "mindone/modules/charger_class/charger_class.c",
             here / "eta-event/stubs.h", here / "eta-event/cases.c", Path(__file__).resolve()]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    source, basic, charger_class, stubs, cases = [p.read_text() for p in paths[:5]]
    source = clean(source)
    table_pattern = r"static const struct charger_ops eta6965_chg_ops = \{.*?^};"
    table = re.search(table_pattern, source, re.M | re.S).group()
    event_entry = re.search(r"\.event\s*=\s*eta_op_event\s*,", table).group()
    mutations = [
        ("drop_event_entry", event_entry, ""),
        ("missing_binding_success", "int ret = -ENODEV;", "int ret = 0;"),
        ("skip_changed", "power_supply_changed(binding->psy);", "(void)0;"),
    ]
    variants = [("actual", source)]
    for name, old, new in mutations:
        if name != "drop_event_entry":
            body = function(source, "eta_op_event").rstrip()
            variant = replace_one(source, body, replace_one(body, old, new))
        else:
            variant = replace_one(source, old, new)
        variants.append((name, variant))

    def fixture(text):
        table = re.search(table_pattern, text, re.M | re.S).group()
        entries = re.findall(r"\.event\s*=\s*[^,]+,", table)
        projected = "static const struct charger_ops eta6965_chg_ops = {" + "".join(entries) + "};\n"
        functions = function(text, "eta_op_event") + projected
        functions += function(charger_class, "charger_dev_do_event") + function(basic, "enable_charging")
        return stubs.replace("@FUNCTIONS@", functions) + cases

    run(args, paths, variants, fixture, before)


if __name__ == "__main__":
    main()
