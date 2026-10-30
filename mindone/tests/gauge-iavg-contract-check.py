# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


CASES = ("valid-to-invalid", "invalid-to-valid", "io-error", "no-data", "status-read-error", "intervening-reader", "software-version", "disabled")
PROVIDER_CASES = ("signed-positive", "signed-negative", "valid-zero-fallback", "pre-write-error", "pre-read-error", "pre-timeout", "valid-read-error", "upper-read-error", "lower-read-error", "clear-write-error", "clear-read-error", "clear-timeout", "final-clear-error", "fallback-read-error")
PROVIDER_FUNCTIONS = ("pre_gauge_update", "post_gauge_update", "reg_to_current", "instant_current", "read_fg_hw_info_current_1", "average_current_get")
PROVIDER_DEFINES = ("RG_FGADC_CON1", "FG_LATCHDATA_ST_MASK", "FG_SW_CLEAR_MASK", "FG_SW_READ_PRE_MASK", "RG_FGADC_IAVG_CON0", "RG_FGADC_IAVG_CON1", "FG_IAVG_VLD_MASK", "FG_IAVG_VLD_SHIFT", "FG_IAVG_27_16_MASK", "FG_IAVG_27_16_SHIFT", "RG_FGADC_CUR_CON0", "FG_CURRENT_OUT_MASK", "FG_CURRENT_OUT_SHIFT", "UNIT_FGCURRENT", "UNIT_FG_IAVG", "DEFAULT_R_FG")


def command_block(source, name):
    start = source.index("\tcase " + name + ":")
    end = source.index("\n\tcase ", start + 1)
    return source[start:end]


def provider_controls(tree, fixtures, extract, folder):
    gauge = (tree / "mindone/modules/mt6358_battery/mt6358-gauge.c").read_text()
    constants = []
    for name in PROVIDER_DEFINES:
        lines = re.findall(r"^#define\s+" + name + r"\s+[^\n]+", gauge, re.MULTILINE)
        if len(lines) != 1:
            raise ValueError("Unknown provider constant contract: " + name)
        constants.append(lines[0])
    functions = {name: extract(gauge, name) for name in PROVIDER_FUNCTIONS}
    for name, source in functions.items():
        print(name + " sha256=" + hashlib.sha256(source.encode()).hexdigest())
    sign = "fg_iavg_ma = 0 - fg_iavg_ma;"
    fallback = "read_fg_hw_info_current_1(gauge_dev);"
    actual = functions["average_current_get"]
    if actual.count(sign) != 1 or actual.count(fallback) != 1:
        raise ValueError("Unknown healthy provider source contract")
    versions = (("actual-provider", actual, PROVIDER_CASES),
                ("lost-negative-sign", actual.replace(sign, "(void)fg_iavg_ma;"), ("signed-negative",)),
                ("lost-instant-fallback", actual.replace(fallback, "(void)gauge_dev;"), ("valid-zero-fallback",)))
    prefix = "\n".join(constants) + "\n" + (fixtures / "provider-stubs.h").read_text()
    prefix += "".join(functions[name] for name in PROVIDER_FUNCTIONS[:-1])
    suffix = (fixtures / "provider-cases.c").read_text()
    for name, getter, cases in versions:
        source = folder / (name + ".c")
        source.write_text(prefix + getter + suffix)
        binary = folder / name
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-but-set-variable", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(source), "-o", str(binary)], check=True, timeout=30)
        for case in cases:
            result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
            if name == "actual-provider" and result.returncode != 0 or name != "actual-provider" and result.returncode == 0:
                raise RuntimeError(name + ": " + case + " did not match the observed provider contract: " + result.stderr)
            print(name + " " + case + " " + (result.stdout.strip() if name == "actual-provider" else "rejected"))


def main():
    parser = argparse.ArgumentParser(description="Observe exact legacy GM30 scalar/status pairing and suppressed property errors without asserting a production fix")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    fixtures = Path(__file__).with_name("gauge-iavg-contract")
    extract = runpy.run_path(str(args.kernel_tree / "mindone/tests/gauge-algo-check.py"))["function"]
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    battery = (base / "mtk_battery.c").read_text()
    daemon = (base / "mtk_battery_daemon.c").read_text()
    scalar = extract(battery, "gauge_get_int_property")
    helper = extract(daemon, "gauge_get_average_current")
    blocks = command_block(daemon, "FG_DAEMON_CMD_GET_FG_CURRENT_AVG") + command_block(daemon, "FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID")
    dispatcher = "static void dispatch(struct mtk_battery *gm, int command, struct reply *ret_msg) { switch (command) {\n" + blocks + "\n} }\n"
    before = "*valid = gm->gauge->fg_hw_info.current_avg_valid;"
    read = "iavg =\n\t\t\tgauge_get_int_property(GAUGE_PROP_AVERAGE_CURRENT);"
    if helper.count(before) != 1 or helper.count(read) != 1:
        raise ValueError("Unknown legacy pairing source contract")
    blind = helper.replace(before, "").replace(read, read + "\n\t\t\t" + before)
    for name, text in (("scalar", scalar), ("helper", helper), ("wire", blocks)):
        print(name + " sha256=" + hashlib.sha256(text.encode()).hexdigest())
    with tempfile.TemporaryDirectory(prefix="gauge-iavg-contract-") as directory:
        folder = Path(directory)
        provider_controls(args.kernel_tree, fixtures, extract, folder)
        for name, actual in (("actual", helper), ("blind-fresh-valid", blind)):
            source = (fixtures / "stubs.h").read_text() + scalar + actual + dispatcher + (fixtures / "cases.c").read_text()
            path = folder / (name + ".c")
            path.write_text(source)
            binary = folder / name
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(path), "-o", str(binary)], check=True, timeout=30)
            for case in CASES if name == "actual" else CASES[:2]:
                result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
                if name == "actual" and result.returncode != 0 or name != "actual" and result.returncode == 0:
                    raise RuntimeError(name + ": " + case + " did not match the observed legacy contract: " + result.stderr)
                print(name + " " + case + " " + ("observed" if name == "actual" else "rejected"))
    print("Legacy observations and fresh-valid negative controls complete; no production correction or hardware acceptance claimed")


if __name__ == "__main__":
    main()
