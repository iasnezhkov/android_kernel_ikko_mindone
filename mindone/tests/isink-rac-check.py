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
    match = re.search(r"^(?:static\s+)?(?:inline\s+)?(?:int|void)\s+" + re.escape(name) + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError("Missing function: " + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def define(source, name):
    match = re.search(r"^#define[ \t]+" + re.escape(name) + r"[ \t]+[^\n]+", source, re.M)
    if not match:
        raise ValueError("Missing constant: " + name)
    return clean(match.group()) + "\n"


def structure(source, name):
    return clean(re.search(r"^struct " + re.escape(name) + r" \{.*?^};", source, re.M | re.S).group()) + "\n"


def replace_one(source, name, before, after):
    original = function(source, name).rstrip()
    if original.count(before) != 1:
        raise ValueError("Mutation anchor changed: " + name + " " + before)
    stripped = clean(source)
    return stripped.replace(original, original.replace(before, after, 1), 1)


def main():
    parser = argparse.ArgumentParser(description="Exercise actual MT6358 ISINK/RAC cleanup with exact-source extraction and injected failures")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-source", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    relative = Path("mindone/modules/mt635x_auxadc/mt635x_auxadc.c")
    tree = args.kernel_tree.resolve()
    source_path = tree / relative
    paths = [source_path,
             tree / "mindone/modules/mt6358_regulator/linux/mfd/mt6358/registers.h",
             tree / "mindone/modules/mt635x_auxadc/dt-bindings/iio/mt635x-auxadc.h",
             tree / "include/linux/iio/types.h", here / "isink-rac/stubs.h",
             here / "isink-rac/cases.c", Path(__file__).resolve()]
    baseline_path = args.baseline_source.resolve() if args.baseline_source else None
    if baseline_path:
        paths.append(baseline_path)
    digest = lambda: {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = digest()
    source, registers, binding, iio, stubs, cases = [p.read_text() for p in paths[:6]]
    compiler_version = subprocess.run(["cc", "--version"], check=True, capture_output=True, text=True, timeout=10).stdout
    baseline_control = {"status": "NOT_REQUESTED", "source": None, "unchanged_functions": []}
    if baseline_path:
        original = baseline_path.read_text()
        unchanged = ("mt6358_imp_conv", "mt6358_imp_stop", "mt6358_imp_sample", "mt6358_imp_cleanup",
                     "enable_dummy_load", "disable_dummy_load", "auxadc_get_imp_ibat")
        for name in unchanged:
            if function(source, name) != function(original, name):
                raise ValueError("Out-of-scope function changed from baseline: " + name)
        dispatch = "\tif (adc_dev->info->imp_conv == mt6358_imp_conv)\n\t\treturn mt6358_get_rac(adc_dev);\n"
        if function(source, "auxadc_get_rac").replace(dispatch, "", 1) != function(original, "auxadc_get_rac"):
            raise ValueError("Legacy RAC body changed from baseline")
        baseline_control = {"status": "PASS", "source": str(baseline_path),
                            "unchanged_functions": list(unchanged), "legacy_rac_body_unchanged": True}
    constants = "".join(define(source, n) for n in ("VOLT_FULL", "IMP_VOLT_FULL", "IMIX_R_MIN_MOHM", "IMIX_R_CALI_CNT"))
    constants += "".join(define(registers, n) for n in ("MT6358_ISINK0_CON1", "MT6358_ISINK1_CON1", "MT6358_ISINK_EN_CTRL_SMPL"))
    constants += "".join(define(binding, n) for n in ("AUXADC_IMP", "AUXADC_IMIX_R", "AUXADC_IMP_SAMPLE"))
    constants += "".join(define(iio, n) for n in ("IIO_VAL_INT", "IIO_VAL_INT_MULTIPLE", "IIO_VAL_FRACTIONAL"))
    constants += clean(re.search(r"enum iio_chan_info_enum \{.*?\n};", iio, re.S).group()) + "\n"
    structures = structure(source, "mt635x_auxadc_device") + structure(source, "auxadc_info")
    arrays = ""
    for name in ("mt6358_en_isink_setting", "mt6358_dis_isink_setting"):
        arrays += clean(re.search(r"^static const unsigned int " + name + r"\[\]\[3\] = \{.*?^};", source, re.S | re.M).group()) + "\n"
    symbols = ("auxadc_conv_imp_vbat", "enable_dummy_load", "disable_dummy_load",
               "mt6358_disable_dummy_load", "mt6358_recover_dummy_load", "mt6358_enable_dummy_load",
               "mt6358_rac_voltage", "mt6358_get_rac", "auxadc_get_rac", "auxadc_bootstrap_imix_r", "auxadc_cali_imix_r",
               "mt635x_auxadc_read_raw", "mt635x_auxadc_suspend_late")
    mutations = [
        ("late_pending", "mt6358_enable_dummy_load", "adc_dev->load_cleanup_pending = true;", "adc_dev->load_cleanup_pending = false;"),
        ("omit_second_off", "mt6358_disable_dummy_load", "i < adc_dev->info->num_dis_isink_setting", "i + 1 < adc_dev->info->num_dis_isink_setting"),
        ("discard_off_errno", "mt6358_disable_dummy_load", "return ret ? ret : step;", "return step;"),
        ("accept_sticky_readback", "mt6358_disable_dummy_load", "if (!step && (value & 0x303))", "if (false)"),
        ("clear_pending_on_failed_readback", "mt6358_disable_dummy_load", "adc_dev->load_cleanup_pending = !!step;", "adc_dev->load_cleanup_pending = false;"),
        ("skip_legacy_channel_guard", "mt635x_auxadc_read_raw", "if (chan->channel == AUXADC_IMP || chan->channel == AUXADC_IMP_SAMPLE) {\n\t\tret = mt6358_recover_dummy_load", "if (chan->channel == AUXADC_IMP_SAMPLE) {\n\t\tret = mt6358_recover_dummy_load"),
        ("skip_checked_channel_guard", "mt635x_auxadc_read_raw", "if (chan->channel == AUXADC_IMP || chan->channel == AUXADC_IMP_SAMPLE) {\n\t\tret = mt6358_recover_dummy_load", "if (chan->channel == AUXADC_IMP) {\n\t\tret = mt6358_recover_dummy_load"),
        ("disable_before_loaded_sample", "mt6358_get_rac", "msleep(50);", "msleep(50); mt6358_recover_dummy_load(adc_dev);"),
        ("discard_first_measurement_error", "mt6358_get_rac", "if (ret)\n\t\t\treturn ret < 0 ? ret : -EIO;", "if (false)\n\t\t\treturn ret < 0 ? ret : -EIO;"),
        ("discard_second_measurement_error", "mt6358_get_rac", "ret = adc_dev->info->imp_conv(adc_dev, &raw, &ibat_2);", "ret = adc_dev->info->imp_conv(adc_dev, &raw, &ibat_2); ret = 0;"),
        ("early_soc_commit", "auxadc_cali_imix_r", "if (!checked)\n\t\tpre_uisoc = cur_uisoc;", "pre_uisoc = cur_uisoc;"),
        ("early_rac_cache", "auxadc_cali_imix_r", "imix_r_avg += rac_val[i];", "imix_r_avg += rac_val[i]; adc_dev->imix_r = rac_val[i];"),
        ("discard_cali_errno", "auxadc_cali_imix_r", "return checked ? rac_val[i] : -EIO;", "return -EIO;"),
        ("skip_suspend_recovery", "mt635x_auxadc_suspend_late", "load_ret = mt6358_recover_dummy_load(adc_dev);", "load_ret = 0;"),
        ("accept_pending_suspend", "mt635x_auxadc_suspend_late", "return ret;", "return 0;"),
        ("narrow_voltage_multiply", "mt6358_rac_voltage", "(s64)raw *", "raw *"),
        ("narrow_current_delta", "mt6358_get_rac", "current_delta = (s64)ibat_2 - ibat_1;", "current_delta = ibat_2 - ibat_1;"),
        ("skip_rac_recovery", "mt6358_get_rac", "ret = mt6358_recover_dummy_load(adc_dev);", "ret = 0;"),
        ("reject_current_boundary", "mt6358_get_rac", "current_delta >= 700", "current_delta > 700"),
        ("accept_voltage_outside", "mt6358_get_rac", "voltage_delta <= 2000", "voltage_delta <= 2001"),
        ("reject_rac_boundary", "mt6358_get_rac", "if (ret < 50)", "if (ret <= 50)"),
        ("extra_retry", "mt6358_get_rac", "++retries < 3", "++retries < 4"),
    ]
    variants = [("actual", source)] + [(name, replace_one(source, symbol, old, new)) for name, symbol, old, new in mutations]
    results = []
    with tempfile.TemporaryDirectory(prefix="isink-rac-") as directory:
        build = Path(directory)
        for name, variant in variants:
            functions = "".join(function(variant, symbol) for symbol in symbols)
            text = stubs.replace("@CONSTANTS@", constants).replace("@STRUCTURES@", structures).replace("@ARRAYS@", arrays).replace("@FUNCTIONS@", functions) + cases
            path = build / (name + ".c")
            binary = build / name
            path.write_text(text)
            command = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
                       "-pthread", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-ftrivial-auto-var-init=pattern", str(path), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            entry = {"variant": name, "fixture_sha256": hashlib.sha256(text.encode()).hexdigest(), "compile_command": command, "compile_rc": compiled.returncode, "compile_stderr": compiled.stderr}
            if compiled.returncode:
                results.append(entry)
                raise RuntimeError(name + " compile failed: " + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            entry.update(run_rc=result.returncode, stdout=result.stdout, stderr=result.stderr)
            results.append(entry)
            if name == "actual" and result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            if name != "actual" and (result.returncode == 0 or not any(s in result.stderr for s in ("CHECK failed", "runtime error:"))):
                raise RuntimeError("Mutation was not rejected by a runtime check: " + name + result.stderr)
            print(name + ": " + (result.stdout.strip() if name == "actual" else "runtime rejection PASS"))
    after = digest()
    if before != after:
        raise RuntimeError("Inputs changed while testing")
    print("PASS input hash continuity: " + str(len(paths)) + " files")
    output = {"status": "PASS_EXACT_SOURCE_ISINK_RAC", "source": str(source_path),
              "compiler_version": compiler_version, "inputs_before": before, "inputs_after": after,
              "baseline_control": baseline_control, "tests": results,
              "limits": ["Host register, measurement and lock models only; no physical ISINK/current acceptance.",
              "Legacy IMP and CURRENT_NOW producers are outside the producer models; hidden read failures are not made detectable.",
              "Actual load helpers, RAC calculation, cache decisions and external IIO/suspend dispatch are extracted from kernel_tree source.",
              "Optional baseline comparison checks unchanged legacy functions and the non-MT6358 RAC body; absent a baseline, that source control is not asserted."]}
    if args.output:
        args.output.write_text(json.dumps(output, indent=2) + "\n")



if __name__ == "__main__":
    main()
