import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def main():
    parser = argparse.ArgumentParser(description="Exercise exact ZCV getters and conversion with checked regmap errors")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--case", choices=("voltage-error", "current-error", "valid"))
    args = parser.parse_args()
    gauge = (args.kernel_tree / "mindone/modules/mt6358_battery/mt6358-gauge.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-zcv-io"
    source = "#define GENMASK(high, low) (((~0U) >> (31 - (high))) & ((~0U) << (low)))\n"
    for name in ("RG_FGADC_ZCV_CON1", "RG_FG_ZCV_CURR_MASK", "RG_FG_ZCV_CURR_SHIFT", "RG_AUXADC_ADC_OUT_FGADC_PCHR", "AUXADC_ADC_OUT_FGADC_PCHR_MASK", "AUXADC_ADC_OUT_FGADC_PCHR_SHIFT", "UNIT_FGCURRENT", "DEFAULT_R_FG", "VOLTAGE_FULL_RANGES", "R_VAL_TEMP_3", "ADC_PRECISE"):
        match = re.search(r"^#define " + name + r"[\t ]+(.+?)\s*$", gauge, re.M)
        if not match:
            raise ValueError("Missing constant: " + name)
        source += "#define " + name + " " + match.group(1).split("/*")[0].rstrip() + "\n"
    source += (fixtures / "stubs.h").read_text()
    for name in ("reg_to_mv_value", "zcv_current_get", "zcv_get"):
        source += function(gauge, name)
    source += (fixtures / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-zcv-io-") as directory:
        path = Path(directory)
        (path / "check.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(path / "check.c"), "-o", str(path / "check")], check=True)
        for case in (args.case,) if args.case else ("voltage-error", "current-error", "valid"):
            subprocess.run([str(path / "check"), case], check=True)


if __name__ == "__main__":
    main()
