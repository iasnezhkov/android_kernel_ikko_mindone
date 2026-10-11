import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]
NAMES = ("pre_gauge_update", "post_gauge_update", "mv_to_reg_12_value", "read_rtc_spare_fg_value",
         "set_rtc_spare_fg_value", "fgauge_set_info", "fgauge_get_info", "rtc_ui_soc_get", "rtc_ui_soc_set",
         "read_gauge_car_registers", "coulomb_interrupt_ht_set", "coulomb_interrupt_lt_set",
         "set_lbat2_en", "en_l_vbat_set", "vbat_lt_set", "bat_plugout_en_set")
MUTATIONS = (
    ("hide-handover-write-error", "fgauge_set_info", "else if (ginfo == GAUGE_PROP_IS_NVRAM_FAIL_MODE)\n\t\treturn regmap_update_bits", "else if (ginfo == GAUGE_PROP_IS_NVRAM_FAIL_MODE)\n\t\tregmap_update_bits"),
    ("hide-rtc-read-error", "rtc_ui_soc_set", "if (ret)", "if (ret && false)"),
    ("accept-short-rtc-write", "set_rtc_spare_fg_value", "if (ret != length)", "if (ret != length && false)"),
    ("discard-latch-read-error", "read_gauge_car_registers", "return ret ? ret : clear_ret;", "return 0 * (ret + clear_ret);"),
    ("publish-vbat-cache-before-setup", "en_l_vbat_set", "val = !!val;", "val = !!val;\n\tgauge->vbat_l_en = val;"),
)


def main():
    parser = argparse.ArgumentParser(description="Exercise exact native threshold and RTC handover failure paths")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    source = (base / "mt6358-gauge.c").read_text()
    header = (base / "mtk_gauge.h").read_text()
    enums = "\n".join(re.search(r"enum " + name + r"\s*\{.*?\};", header, re.S).group(0)
                        for name in ("gauge_irq", "gauge_property"))
    defines = "\n".join(re.findall(r"^#define[^\n]*", source, re.M))
    functions = {name: function(source, name) for name in NAMES}
    fixture = Path(__file__).with_name("gauge-setup-io")
    stubs = (fixture / "stubs.h").read_text()
    prefix, body = stubs.split("struct device", 1)
    stubs = prefix + enums + "\n" + defines + "\nstruct device" + body
    cases = (fixture / "cases.c").read_text()
    variants = [("actual", functions)]
    for name, target, before, after in MUTATIONS:
        if functions[target].count(before) != 1:
            raise ValueError("Ambiguous runtime control: " + name)
        variants.append((name, {**functions, target: functions[target].replace(before, after, 1)}))
    with tempfile.TemporaryDirectory(prefix="gauge-setup-io-") as directory:
        for name, selected in variants:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(stubs + "\n".join(selected.values()) + cases)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-ftrivial-auto-var-init=pattern",
                            "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path)], check=True, timeout=30)
            result = subprocess.run([str(path)], capture_output=True, text=True, timeout=10)
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
