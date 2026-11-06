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
    args = arguments("Exercise extracted MTK watchdog reset mode, adoption, start, stop and DT property reads")
    tree = args.tree.resolve()
    here = Path(__file__).resolve().parent
    paths = [tree / "mindone/modules/mtk_wdt/mtk_wdt.c", here / "mtk-wdt-mode/stubs.h",
             here / "mtk-wdt-mode/cases.c", Path(__file__).resolve()]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    source, stubs, cases = [p.read_text() for p in paths[:3]]
    source = clean(source)
    mutations = [
        ("drop_irq_clear", "WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN", "WDT_MODE_DUAL_EN"),
        ("drop_dual_clear", "WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN", "WDT_MODE_IRQ_EN"),
        ("drop_exrst_clear", "reg &= ~WDT_MODE_EXRST_EN;", "reg |= 0;"),
        ("drop_adopt_write", "writel(mtk_wdt_reset_mode(mtk_wdt, readl(wdt_base + WDT_MODE)),\n\t\t       wdt_base + WDT_MODE);", "(void)0;"),
        ("drop_key", "return reg | WDT_MODE_KEY;", "return reg;"),
        ("drop_start_en", "iowrite32(reg | WDT_MODE_EN, wdt_base + WDT_MODE);", "iowrite32(reg, wdt_base + WDT_MODE);"),
    ]
    variants = [("actual", source)] + [(n, replace_one(source, old, new)) for n, old, new in mutations]

    def fixture(text):
        constants = "\n".join(re.findall(r"^#define[ \t]+WDT_[^\n]+", text, re.M))
        probe = function(text, "mtk_wdt_probe")
        reads = re.findall(r'mtk_wdt->(?:disable_wdt_extrst|reset_by_toprgu)\s*=\s*of_property_read_bool\([^;]+;', probe)
        if len(reads) != 2:
            raise ValueError("Missing watchdog DT property assignments")
        properties = "static void read_properties(struct mtk_wdt_dev *mtk_wdt, struct device *dev) {\n" + "\n".join(reads) + "\n}\n"
        names = ("mtk_wdt_reset_mode", "mtk_wdt_ping", "mtk_wdt_set_timeout", "mtk_wdt_init", "mtk_wdt_start", "mtk_wdt_stop")
        return stubs.replace("@CONSTANTS@", constants).replace("@FUNCTIONS@", properties + "".join(function(text, n) for n in names)) + cases

    run(args, paths, variants, fixture, before)


if __name__ == "__main__":
    main()
