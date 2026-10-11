import argparse
from pathlib import Path
import re
import subprocess
import tempfile


CASES = ("timer", "dlpt", "boot", "car", "cycles", "aging", "zcv", "eoc", "tracking", "shutdown-timer", "zcv-errors", "zcv-current-error", "zcv-car-error", "eoc-error", "learning-zero")


def function(source, name):
    match = re.search(r"^(?:static )?(?:(?:signed )?int|void|bool|ssize_t|irqreturn_t) " + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise ValueError(f"Missing function: {name}")
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def main():
    parser = argparse.ArgumentParser(description="Exercise exact kernel gauge functions with mocked clocks, PMIC values and profile conversion")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    algorithm = (base / "mtk_battery_algo.c").read_text()
    battery = (base / "mtk_battery.c").read_text()
    header = (base / "mtk_battery.h").read_text()
    minimum = re.search(r"^#define FGR_AGING_MIN_BP\s+(\d+)\s*$", header, re.M)
    if not minimum:
        raise ValueError("Missing FGR_AGING_MIN_BP")
    fixtures = Path(__file__).resolve().parent / "gauge-algo"
    source = "#define FGR_AGING_MIN_BP " + minimum.group(1) + "\n"
    source += (fixtures / "stubs.h").read_text()
    for name in ("set_fg_time", "get_d0_c_soc_cust", "get_uisoc_cust", "get_imix_r", "fg_adc_reset", "Set_fg_c_d0_by_ocv", "fgr_set_soc_by_vc_mode", "fgr_update_c_dod", "fgr_dod_init", "fgr_imix_error_calibration", "fgr_dlpt_sd_handler", "fgr_cycle_capacity", "fgr_sw_cycle_accu", "fgr_time_handler", "fgr_learn_aging_factor", "fgr_zcv_recal_handler", "fgr_chr_full_int_handler"):
        if name == "fgr_update_c_dod" and "static void fgr_update_c_dod_car(" in algorithm:
            source += function(algorithm, "fgr_update_c_dod_car")
        if name == "fgr_sw_cycle_accu" and "void fgr_sw_cycle_accu_car(" in algorithm:
            source += function(algorithm, "fgr_sw_cycle_accu_car")
        source += function(algorithm, name)
    source += function(battery, "power_misc_handler")
    source += (fixtures / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-algo-") as directory:
        path = Path(directory)
        (path / "check.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-but-set-variable", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined", str(path / "check.c"), "-o", str(path / "check")], check=True)
        for case in args.case or CASES:
            subprocess.run([str(path / "check"), case], check=True)


if __name__ == "__main__":
    main()
