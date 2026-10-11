import argparse
from pathlib import Path
import runpy
import re
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def main():
    parser = argparse.ArgumentParser(description="Audit actual kernel-gauge initialization through the one-shot activation entry")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    algorithm = (base / "mtk_battery_algo.c").read_text()
    battery = (base / "mtk_battery.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-algo"
    text = (fixtures / "stubs.h").read_text()
    text = text.replace("struct mtk_battery_algo {", "struct mtk_battery_algo {\n\tint fg_bat_int1_gap, prev_car_bat0, ht_gap, lt_gap, uisoc_ht_en, uisoc_lt_en, fg_vbat2_lt;\n\tbool active;\n\tint fg_bat_tmp_c_gap;\n\tint DC_ratio;\n\tint last_temp;", 1)
    text = text.replace("struct mtk_battery {", "struct mtk_gauge { struct mutex ops_lock; };\nstruct mtk_battery {\n\tstruct mtk_gauge *gauge;\n\tint soc;\n\tbool kernel_setup_in_progress, kernel_setup_mutated;\n\tint kernel_setup_error;\n\tint init_flag;\n\tunsigned int BAT_EC_cmd;\n\tunsigned int BAT_EC_param;", 1)
    text = text.replace("struct fuel_gauge_custom_data {", "struct fuel_gauge_custom_data {\n\tint vbat2_det_voltage1;", 1)
    text = text.replace("struct fuel_gauge_table_custom_data {\n\tint unused;", "struct fuel_gauge_table_custom_data {\n\tint temperature_tb1;", 1)
    text = text.replace("\tGAUGE_PROP_COUNT,", "\tGAUGE_PROP_BATTERY_EXIST,\n\tGAUGE_PROP_IS_NVRAM_FAIL_MODE,\n\tGAUGE_PROP_PTIM_RESIST,\n\tGAUGE_PROP_BATTERY_CURRENT,\n\tGAUGE_PROP_VBAT_LT_INTR_THRESHOLD,\n\tGAUGE_PROP_EN_LOW_VBAT_INTERRUPT,\n\tGAUGE_PROP_COUNT,", 1)
    text = text.replace("\tBAT_PROP_COUNT,", "\tBAT_PROP_INIT_DONE,\n\tBAT_PROP_COULOMB_INT_GAP,\n\tBAT_PROP_UISOC_HT_INT_GAP,\n\tBAT_PROP_UISOC_LT_INT_GAP,\n\tBAT_PROP_TEMP_TH_GAP,\n\tBAT_PROP_COUNT,", 1)
    text = text.replace("\tassert(gm->algo.quse_tb1 > 0);", "", 1)
    text = text.replace("\t*value = gauge_values[property];\n\treturn 0;", "\t*value = gauge_values[property];\n\treturn property == GAUGE_PROP_PTIM_BATTERY_VOLTAGE || property == GAUGE_PROP_PTIM_RESIST ? 1 : 0;", 1)
    text = text.replace("int gauge_get_property(int property, int *value);", (Path(__file__).with_name("gauge-activation") / "io.h").read_text() + "\nint gauge_get_property(int property, int *value);", 1)
    text = text.replace("\tgauge_reads[property]++;", "\tgauge_reads[property]++;\n\tint ret = io_step(false);\n\tif (ret) return ret;", 1)
    text = text.replace("\t*value = gauge_values[property];\n\treturn property", "\t*value = gauge_values[property];\n\tif (mismatch_property == property && gauge_writes[property]) (*value)++;\n\treturn property", 1)
    text = text.replace("\tbattery_values[property] = value;", "\tint ret = io_step(property != BAT_PROP_UISOC);\n\tif (ret) return ret;\n\tbattery_values[property] = value;", 1)
    text += (Path(__file__).with_name("gauge-activation") / "stubs.h").read_text()
    for name in ("fg_adc_reset", "get_d0_c_soc_cust", "get_uisoc_cust", "Set_fg_c_d0_by_ocv", "fgr_set_soc_by_vc_mode"):
        text += function(algorithm, name)
    if "static void fgr_update_c_dod_car(" in algorithm:
        text += function(algorithm, "fgr_update_c_dod_car")
    for name in ("fgr_update_c_dod", "fgr_dod_init", "set_kernel_soc", "set_fg_bat_tmp_c_gap", "fgr_update_uisoc_ht", "fgr_update_uisoc_lt", "fgr_set_int1", "battery_algo_init"):
        text += function(algorithm, name)
    text += function(battery, "battery_enable_kernel_algo")
    daemon = (base / "mtk_battery_daemon.c").read_text()
    activation = runpy.run_path(str(Path(__file__).with_name("gauge-enable-check.py")))["activation_case"]
    text += "static int exec_BAT_EC(int cmd, int param) { struct mtk_battery *gm = active_gm; int ret; switch (cmd) {\n"
    text += activation(daemon)
    text += "default: abort(); } return 0; }\n"
    text += function(daemon, "BAT_EC_store")
    text += (Path(__file__).with_name("gauge-activation") / "cases.c").read_text()
    mutations = (
        ("hide-threshold-setup-error", "battery_algo_init", "ret = fgr_set_int1(gm);\n\tif (ret)", "ret = fgr_set_int1(gm);\n\tif (ret && false)"),
        ("hide-handover-write-error", "battery_algo_init", "ret = gauge_set_property(GAUGE_PROP_IS_NVRAM_FAIL_MODE, 1);\n\tif (ret < 0)", "ret = gauge_set_property(GAUGE_PROP_IS_NVRAM_FAIL_MODE, 1);\n\tif (ret < 0 && false)"),
        ("accept-invalid-handover-readback", "battery_algo_init", "if (handoff != 1)", "if (handoff != 1 && false)"),
        ("accept-invalid-rtc-readback", "fgr_set_int1", "if (readback != (algo->ui_soc + 50) / 100)", "if (readback != (algo->ui_soc + 50) / 100 && false)"),
        ("accept-invalid-con0-readback", "fgr_set_int1", "return readback == con0_soc / 100 ? 0 : -EIO;", "return 0;"),
        ("retry-after-hardware-mutation", "battery_algo_init", "if (READ_ONCE(gm->kernel_setup_mutated))", "if (READ_ONCE(gm->kernel_setup_mutated) && false)"),
        ("ignore-concurrent-setup-error", "battery_algo_init", "ret = READ_ONCE(gm->kernel_setup_error);", "ret = 0;"),
        ("publish-active-before-setup", "battery_enable_kernel_algo", "ret = battery_kernel_setup_ready(gm);", "smp_store_release(&gm->algo.active, true);\n\t\tret = battery_kernel_setup_ready(gm);"),
    )
    variants = [("actual", text)]
    for name, target, before, after in mutations:
        body = function(text, target)
        if body.count(before) != 1:
            raise ValueError("Ambiguous runtime control: " + name)
        variants.append((name, text.replace(body, body.replace(before, after, 1), 1)))
    with tempfile.TemporaryDirectory(prefix="gauge-activation-audit-") as directory:
        for name, source in variants:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-Wno-unused-function",
                            "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                            "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined",
                            str(path.with_suffix(".c")), "-o", str(path)], check=True, timeout=30)
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
