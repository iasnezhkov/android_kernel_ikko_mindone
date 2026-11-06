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
    return re.sub(pattern, lambda match: "" if match.group().startswith("/") else match.group(), text, flags=re.S)


def function(source, name):
    source = clean(source)
    pattern = r"^(?:static\s+)?(?:int|void|bool)\s+" + re.escape(name) + r"\([^;{}]*\)\s*\{"
    match = re.search(pattern, source, re.M)
    if not match:
        raise ValueError("Missing function body: " + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def structure(source, name):
    match = re.search(r"^struct " + re.escape(name) + r" \{.*?^};", source, re.M | re.S)
    if not match:
        raise ValueError("Missing structure: " + name)
    return clean(match.group()) + "\n"


def define(source, name):
    match = re.search(r"^#define[ \t]+" + re.escape(name) + r"(?:\([^\n]*?\))?[ \t]+[^\n]*(?:\\\n[^\n]*)*", source, re.M)
    if not match:
        raise ValueError("Missing macro: " + name)
    lines = source[match.start():].splitlines(keepends=True)
    result = lines[0]
    index = 1
    while result.rstrip().endswith("\\"):
        result += lines[index]
        index += 1
    return clean(result).rstrip() + "\n"


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("Mutation anchor changed: " + before)
    return text.replace(before, after, 1)


def daemon_handler(source):
    body = function(source, "mtk_battery_daemon_handler")
    start = body.index("\tswitch (msg->fgd_cmd) {\n") + len("\tswitch (msg->fgd_cmd) {\n")
    end = body.rindex("\t}\n}")
    branches = re.split(r"(?=^\t(?:case |default:))", body[start:end], flags=re.M)
    selected = ("FG_DAEMON_CMD_GET_PTIM_SAMPLE_V1", "FG_DAEMON_CMD_GET_PTIM_VBAT",
                "FG_DAEMON_CMD_GET_PTIM_I", "FG_DAEMON_CMD_GET_RAC", "FG_DAEMON_CMD_GET_INIT_FLAG")
    branches = [branch for branch in branches if any(branch.startswith("\tcase " + name + ":") for name in selected)
                or branch.startswith("\tdefault:")]
    if len(branches) != len(selected) + 1:
        raise ValueError("Missing daemon dispatch branch")
    return (body[:start] + "".join(branches) + body[end:]).replace("\tint int_value;\n", "")


def sources(tree, fixture):
    paths = {
        "adc": tree / "mindone/modules/mt635x_auxadc/mt635x_auxadc.c",
        "gauge": tree / "mindone/modules/mt6358_battery/mt6358-gauge.c",
        "algorithm": tree / "mindone/modules/mt6358_battery/mtk_battery_algo.c",
        "header": tree / "mindone/modules/mt6358_battery/mtk_gauge.h",
        "bindings": tree / "mindone/modules/mt635x_auxadc/dt-bindings/iio/mt635x-auxadc.h",
        "registers": tree / "mindone/modules/mt6358_regulator/linux/mfd/mt6358/registers.h",
        "iio": tree / "include/linux/iio/types.h",
        "daemon": tree / "mindone/modules/mt6358_battery/mtk_battery_daemon.c",
        "daemon_header": tree / "mindone/modules/mt6358_battery/mtk_battery_daemon.h",
        "battery_header": tree / "mindone/modules/mt6358_battery/mtk_battery.h",
    }
    text = {name: path.read_text() for name, path in paths.items()}
    common = (fixture / "common.h").read_text()
    iio_enum = re.search(r"enum iio_chan_info_enum \{.*?\n};", text["iio"], re.S)
    if not iio_enum:
        raise ValueError("Missing iio_chan_info_enum")
    common += clean(iio_enum.group()) + "\n"
    common += "".join(define(text["iio"], name) for name in (
        "IIO_VAL_INT", "IIO_VAL_FRACTIONAL", "IIO_VAL_INT_MULTIPLE"))
    adc_constants = "".join(define(text["adc"], name) for name in (
        "VOLT_FULL", "AUXADC_TIMEOUT_US", "IMP_POLL_DELAY_US",
        "MT6358_IMP_CK_SW_MASK", "MT6358_IMP_AUTORPT_EN_MASK",
        "MT6358_IMP_CLR_MASK", "MT6358_IMP_IRQ_RDY_BIT", "MT6358_IMP_FGADC_R_CON0"))
    adc_constants += "".join(define(text["bindings"], name) for name in (
        "AUXADC_IMP", "AUXADC_IMIX_R", "AUXADC_IMP_SAMPLE"))
    adc_constants += "".join(define(text["registers"], name) for name in (
        "MT6358_AUXADC_DCM_CON", "MT6358_AUXADC_IMP0", "MT6358_AUXADC_IMP1", "MT6358_AUXADC_ADC28"))
    adc_body = define(text["adc"], "auxadc_imp_poll_timeout")
    adc_body += "".join(function(text["adc"], name) for name in (
        "mt6358_imp_cleanup", "mt6358_imp_sample", "mt6358_imp_conv",
        "mt6358_imp_stop", "mt635x_auxadc_read_raw"))
    sample = structure(text["header"], "gauge_ptim_sample")
    gauge_constants = "".join(define(text["gauge"], name) for name in ("UNIT_FGCURRENT", "DEFAULT_R_FG"))
    gauge_body = function(text["gauge"], "reg_to_current") + function(text["gauge"], "gauge_read_ptim_sample")
    consumer_body = structure(text["algorithm"], "fgr_vboot_sample")
    consumer_body += "".join(function(text["algorithm"], name) for name in (
        "fgr_read_vboot_sample", "fgr_temp_c_int_handler", "do_fg_algo"))
    dispatch = function(text["algorithm"], "do_fg_algo")
    constants = sorted(set(re.findall(r"\bFG_INTR_[A-Z0-9_]+\b", dispatch)))
    consumer_constants = "enum { " + ", ".join(constants) + " };\n"
    other_calls = sorted(set(re.findall(r"\b(fgr_[a-z0-9_]+)\(", dispatch)) - {"fgr_temp_c_int_handler", "fgr_int_end_flow"})
    consumer_constants += "".join("#define " + name + "(...) unsupported()\n" for name in other_calls)
    legacy_enum = clean(re.search(r"enum fg_daemon_cmds \{.*?\n};", text["battery_header"], re.S).group()) + "\n"
    if hashlib.sha256("".join(legacy_enum.split()).encode()).hexdigest() != "63d3049bfa6801d3a2fa330b39054f838a8e1c5563633e82514829881cc70121":
        raise ValueError("Legacy daemon enumeration changed")
    daemon_constants = "typedef uint32_t __u32;\ntypedef int32_t __s32;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n"
    daemon_constants += "".join(define(text["daemon_header"], name) for name in (
        "FGD_NL_LEN", "FGD_NL_HDR_LEN", "FGD_NL_MSG_MAX_LEN", "FGD_NL_MAGIC",
        "FG_DAEMON_CMD_GET_PTIM_SAMPLE_V1", "FGD_PTIM_SAMPLE_VALID"))
    daemon_constants += structure(text["daemon_header"], "fgd_nl_msg_t")
    daemon_constants += structure(text["daemon_header"], "fgd_ptim_sample_v1")
    daemon_body = function(text["daemon"], "fg_daemon_get_ptim_sample") + daemon_handler(text["daemon"])
    daemon_body += function(text["daemon"], "fg_daemon_init_complete")
    daemon_body += function(text["daemon"], "mtk_battery_netlink_handler")
    adc_reset = function((fixture / "adc-cases.c").read_text(), "reset").replace("void reset(", "void reset_adc(")
    assembled = {
        "adc": common + adc_constants + (fixture / "adc.h").read_text() + adc_body + (fixture / "adc-cases.c").read_text(),
        "gauge": common + gauge_constants + sample + (fixture / "gauge.h").read_text() + gauge_body + (fixture / "gauge-cases.c").read_text(),
        "vboot": common + sample + structure(text["algorithm"], "fgr_vboot_sample") + (fixture / "vboot.h").read_text() + function(text["algorithm"], "fgr_read_vboot_sample") + function(text["algorithm"], "fgr_construct_vboot_sample") + (fixture / "vboot-cases.c").read_text(),
        "consumer": common + sample + consumer_constants + (fixture / "consumer.h").read_text() + consumer_body + (fixture / "consumer-cases.c").read_text(),
        "daemon": common + adc_constants + (fixture / "adc.h").read_text() + adc_body + adc_reset + sample + gauge_constants + daemon_constants + legacy_enum + (fixture / "daemon.h").read_text() + gauge_body + daemon_body + (fixture / "daemon-cases.c").read_text(),
    }
    bindings = {str(path.relative_to(tree)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths.values()}
    bindings["mindone/tests/ptim-sample-check.py"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    for path in sorted(fixture.iterdir()):
        if path.is_file():
            bindings[str(path.relative_to(tree))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return assembled, bindings


def mutations(assembled):
    adc = assembled["adc"]
    gauge = assembled["gauge"]
    consumer = assembled["consumer"]
    daemon = assembled["daemon"]
    early_current = replace_once(adc,
        "ret = regmap_read(adc_dev->regmap, MT6358_AUXADC_ADC28, &voltage);\nstop:",
        "ret = regmap_read(adc_dev->regmap, MT6358_AUXADC_ADC28, &voltage);\n\tif (!ret) ret = regmap_read(adc_dev->regmap, MT6358_IMP_FGADC_R_CON0, &current_raw);\nstop:")
    early_current = replace_once(early_current,
        "\tret = regmap_read(adc_dev->regmap, MT6358_IMP_FGADC_R_CON0, &current_raw);\n\tif (ret)\n\t\treturn ret;\n\t*vbat",
        "\t*vbat")
    return {
        "discard_cleanup_failure": replace_once(adc, "if (ret || cleanup)\n\t\treturn ret ? ret : cleanup;", "if (ret)\n\t\treturn ret;"),
        "accept_stale_ready": replace_once(adc, "if (ready & MT6358_IMP_IRQ_RDY_BIT)\n\t\treturn -EIO;", "if (false)\n\t\treturn -EIO;"),
        "skip_legacy_pending_guard": replace_once(adc, "if (adc_dev->imp_cleanup_pending) {\n\t\tret = mt6358_imp_cleanup(adc_dev);\n\t\tif (ret)\n\t\t\treturn ret;\n\t}", ""),
        "unlock_before_calibration": replace_once(adc, "ret = auxadc_chan->cali_fn(adc_dev, auxadc_out, 10);", "mutex_unlock(&adc_dev->lock);\n\t\t\tret = auxadc_chan->cali_fn(adc_dev, auxadc_out, 10);\n\t\t\tmutex_lock(&adc_dev->lock);"),
        "current_from_voltage_register": replace_once(adc, "regmap_read(adc_dev->regmap, MT6358_IMP_FGADC_R_CON0, &current_raw)", "regmap_read(adc_dev->regmap, MT6358_AUXADC_ADC28, &current_raw)"),
        "read_current_before_cleanup": early_current,
        "advance_sequence_on_failure": replace_once(gauge, "out:\n\tmutex_unlock(&gauge->ops_lock);", "out:\n\tif (ret) gauge->ptim_sequence++;\n\tmutex_unlock(&gauge->ops_lock);"),
        "reverse_ptim_polarity": replace_once(gauge, "value = -div_s64(value * next.car_tune_value, 1000);", "value = div_s64(value * next.car_tune_value, 1000);"),
        "reject_profile_fallback": replace_once(consumer, "ret < 0 && ret != -ENODATA", "ret < 0"),
        "allow_profile_for_qmax_zero": replace_once(consumer, "gm->fg_cust_data.qmax_sel != 1", "gm->fg_cust_data.qmax_sel == 2"),
        "hide_rac_io_error": replace_once(consumer, "ret < 0 && ret != -ENODATA", "false"),
        "log_preliminary_vboot": replace_once(assembled["vboot"], "table_idx == 255 ? algo->vboot : algo->vboot_c", "vboot_t"),
        "skip_rac_seed": replace_once(assembled["vboot"], "if (rac > 0)", "if (false)"),
        "run_end_flow_after_sample_failure": replace_once(consumer, "if (!fgr_temp_c_int_handler(gm))\n\t\t\tfgr_int_end_flow(gm, FG_INTR_BAT_TMP_HT);", "fgr_temp_c_int_handler(gm);\n\t\tfgr_int_end_flow(gm, FG_INTR_BAT_TMP_HT);"),
        "daemon_suppress_errno": replace_once(daemon, "if (reply->status)\n\t\treturn;", "if (reply->status)\n\t\treply->status = 0;"),
        "daemon_stale_validity": replace_once(daemon, "if (reply->status)\n\t\treturn;", "if (reply->status) {\n\t\treply->flags = FGD_PTIM_SAMPLE_VALID;\n\t\treturn;\n\t}"),
        "daemon_reverse_current": replace_once(daemon, "reply->current_01ma = sample.current_01ma;", "reply->current_01ma = -sample.current_01ma;"),
        "daemon_bypass_reply_size": replace_once(daemon, "fgd_msg->fgd_ret_data_len != sizeof(struct fgd_ptim_sample_v1)", "false"),
        "daemon_bypass_transport_size": replace_once(daemon, "skb->len != NLMSG_HDRLEN + FGD_NL_HDR_LEN", "false"),
        "daemon_discard_cleanup_failure": replace_once(daemon, "if (ret || cleanup)\n\t\treturn ret ? ret : cleanup;", "if (ret)\n\t\treturn ret;"),
        "daemon_advance_failed_sequence": replace_once(daemon, "out:\n\tmutex_unlock(&gauge->ops_lock);", "out:\n\tif (ret) gauge->ptim_sequence++;\n\tmutex_unlock(&gauge->ops_lock);"),
    }


def execute(cc, directory, name, source, expected_failure=False, arguments=()):
    path = directory / (name + ".c")
    binary = directory / name
    path.write_text(source)
    command = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               "-Wno-unused-function", "-Wno-unused-but-set-variable", "-Wno-sign-compare", "-O1", "-g",
               "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined",
               str(path), "-o", str(binary)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=45)
    if compiled.returncode:
        raise RuntimeError(name + " compilation failed:\n" + compiled.stderr)
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=15)
    if expected_failure:
        if result.returncode == 0 or "CHECK failed:" not in result.stderr:
            raise RuntimeError(name + " was not rejected by an assertion:\n" + result.stdout + result.stderr)
    elif result.returncode or result.stderr:
        raise RuntimeError(name + " failed:\n" + result.stdout + result.stderr)
    return {"name": name, "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
            "compile_rc": compiled.returncode, "run_rc": result.returncode,
            "stdout": result.stdout.strip(), "stderr": result.stderr.strip(),
            "expected_rejection": expected_failure}


def main():
    parser = argparse.ArgumentParser(description="Exercise extracted PTIM acquisition, publication and consumer functions with bounded fault controls")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-mutations", action="store_true")
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    fixture = Path(__file__).resolve().with_name("ptim-sample")
    assembled, bindings = sources(tree, fixture)
    report = {"status": "PASS_EXACT_PTIM_SOURCE_CONTROLS", "inputs": bindings, "checks": [], "mutations": [],
              "limits": ["Actual extracted function bodies and constants; PMIC, IIO, clocks and scheduling are deterministic host substitutes.",
                         "No native Kleaf build, ABI, actual hardware timing, simultaneous kernel threads or physical first-sample acceptance.",
                         "Consumer controls cover both temperature event dispatch paths; initialization is covered by its separate existing suite.",
                         "Calibration coefficients are snapshots used by this call, not an atomic calibration epoch."]}
    report["limits"].append("Daemon uses the extracted receive path, PTIM helper and selected unchanged switch branches; unrelated commands are omitted from the host translation unit.")
    with tempfile.TemporaryDirectory(prefix="ptim-sample-") as temporary:
        directory = Path(temporary)
        for name, source in assembled.items():
            result = execute(args.cc, directory, name, source)
            report["checks"].append(result)
            print(name + ": " + result["stdout"])
        result = execute(args.cc, directory, "vboot_c", assembled["vboot"], arguments=("254",))
        report["checks"].append(result)
        print("vboot_c: " + result["stdout"])
        if not args.no_mutations:
            for name, source in mutations(assembled).items():
                result = execute(args.cc, directory, name, source, expected_failure=True)
                report["mutations"].append(result)
                print(name + ": rejected")
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
