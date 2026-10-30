# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


BASELINE = "446b82f88877f8346811ec549b59f77bb0b9b048"
SOURCE = "mindone/modules/md_cooling_all/src/md_cooling.c"
CASES = ("normal-levels", "unchanged", "equal-aggregate", "peer-aggregate", "sparse-aggregate", "failure-state", "inflight-state", "failure-retry", "release-retry", "peer-failure", "real-errors", "modem-off", "scg-off", "validation", "locking")
HEALTHY = ("normal-levels", "unchanged", "equal-aggregate", "peer-aggregate", "sparse-aggregate", "modem-off", "scg-off", "validation", "locking")
FUNCTIONS = ("get_md_cooling_status", "send_throttle_msg", "get_md_cdev", "find_max_mutt_state", "state_to_md_cooling_status", "mutt_throttle", "md_cooling_get_max_state", "md_cooling_get_cur_state", "md_cooling_set_cur_state", "md_cooling_state_to_mutt_lv")


def function(source, name):
    match = re.search(r"^(?:static )?[A-Za-z_][\w\s*]*\b" + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise ValueError("Missing function: " + name)
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def enum(source, name):
    match = re.search(r"^enum " + name + r" \{.*?^\};", source, re.M | re.S)
    if not match:
        raise ValueError("Missing enum: " + name)
    return match.group(0) + "\n"


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError("Mutation does not match exactly once: " + before)
    return source.replace(before, after, 1)


def assemble(tree, source):
    fixtures = Path(__file__).resolve().parent / "md-mutt"
    header_path = tree / "mindone/modules/md_cooling_all/md_cooling.h"
    if header_path.resolve() != (tree / "mindone/modules/board_temp/md_cooling.h").resolve():
        raise ValueError("Unexpected actual md_cooling header")
    ccci = (tree / "mindone/modules/include/drivers/misc/mediatek/include/mt-plat/mtk_ccci_common.h").read_text()
    result = (fixtures / "stubs.h").read_text()
    for name in ("MD_SYS", "MD_STATE_FOR_USER", "KERN_FUNC_ID"):
        result += enum(ccci, name)
    lines = iter(ccci.splitlines())
    for line in lines:
        if line.startswith("#define CCCI_ERR"):
            result += line + "\n"
            while line.endswith("\\"):
                line = next(lines)
                result += line + "\n"
    result += header_path.read_text().replace("#include <linux/thermal.h>", "")
    for declaration in ("static struct md_cooling_data *mdc;", "static DEFINE_MUTEX(md_cdev_list_lock);", "static DEFINE_MUTEX(mdc_data_lock);", "static DEFINE_MUTEX(send_tmc_msg_lock);", "static LIST_HEAD(md_cdev_list);"):
        if declaration not in source:
            raise ValueError("Missing actual declaration: " + declaration)
        result += declaration + "\n"
    result += "static void trace_md_mutt_limit(struct md_cooling_device *, enum md_cooling_status);\n"
    for name in FUNCTIONS:
        result += function(source, name)
    platform = re.search(r"^static const struct md_cooling_platform_data md_cooling_pdata = \{.*?^\};", source, re.M | re.S)
    if not platform:
        raise ValueError("Missing actual platform data")
    result += platform.group(0) + "\n"
    return result + (fixtures / "cases.c").read_text()


def compile_source(directory, name, source):
    path = directory / (name + ".c")
    path.write_text(source)
    binary = directory / name
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined", str(path), "-o", str(binary)], check=True)
    return binary


def run(binary, case, expected_failure=None):
    result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
    if expected_failure is None:
        if result.returncode:
            raise RuntimeError(f"{binary.name}/{case} exited {result.returncode}: {result.stderr}")
        print(result.stdout.strip())
    else:
        if not result.returncode or expected_failure not in result.stderr or "Assertion" not in result.stderr:
            raise RuntimeError(f"Control did not reach its expected assertion: {binary.name}/{case}: {result.stderr}")
        print(f"PASS rejected {binary.name}/{case}: {expected_failure}")


def main():
    parser = argparse.ArgumentParser(description="Exercise exact MUTT dispatch state with CCCI failure, readback, recovery and mutation controls")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--baseline", default=BASELINE)
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    source = (tree / SOURCE).read_text()
    baseline = subprocess.run(["git", "-C", str(tree), "show", args.baseline + ":" + SOURCE], check=True, capture_output=True, text=True, env={**os.environ, "GIT_NO_LAZY_FETCH": "1"}).stdout
    for path in ("mindone/modules/board_temp/md_cooling.h", "mindone/modules/include/drivers/misc/mediatek/include/mt-plat/mtk_ccci_common.h"):
        original = subprocess.run(["git", "-C", str(tree), "show", args.baseline + ":" + path], check=True, capture_output=True, text=True, env={**os.environ, "GIT_NO_LAZY_FETCH": "1"}).stdout
        if original != (tree / path).read_text():
            raise ValueError("Actual header changed from the baseline: " + path)
    build = (tree / "mindone/modules/BUILD.bazel").read_text()
    if not re.search(r'name = "md_cooling_all",\s+srcs = \[\s+"md_cooling_all/src/md_cooling.c",\s+"md_cooling_all/md_cooling_debugfs.c",', build):
        raise ValueError("Actual DDK source binding changed")
    for name in ("send_throttle_msg", "get_md_cdev", "get_md_cooling_status", "state_to_md_cooling_status", "md_cooling_get_max_state", "md_cooling_get_cur_state", "md_cooling_set_cur_state", "md_cooling_state_to_mutt_lv", "tx_pwr_throttle", "scg_off_throttle"):
        if function(source, name) != function(baseline, name):
            raise ValueError("Unrelated actual function changed: " + name)
    with tempfile.TemporaryDirectory(prefix="md-mutt-") as temporary:
        directory = Path(temporary)
        actual = assemble(tree, source)
        candidate = compile_source(directory, "candidate", actual)
        for case in args.case or CASES:
            run(candidate, case)
        if args.case:
            return
        old = compile_source(directory, "baseline", assemble(tree, baseline))
        for case in HEALTHY:
            run(old, case)
        for case, assertion in (("failure-state", "value == second"), ("inflight-state", "value == observed_targets[i]"), ("failure-retry", "dispatches == 2")):
            run(old, case, assertion)
        controls = (
            ("premature-target", "final_state = find_max_mutt_state(md_cdev, state);", "md_cdev->target_state = state;\n\tfinal_state = find_max_mutt_state(md_cdev, state);", "inflight-state", "value == observed_targets[i]"),
            ("drop-equal-commit", "md_cdev->target_state = state;\n\t\tmutex_unlock(&mdc_data_lock);", "mutex_unlock(&mdc_data_lock);", "equal-aggregate", "value == second"),
            ("drop-success-commit", "md_cdev->target_state = state;\n\tmdc->status = new_status;", "mdc->status = new_status;", "normal-levels", "value == second"),
            ("lost-error", "if (ret) {\n\t\tmutex_unlock(&mdc_data_lock);\n\t\treturn ret;", "if (ret) {\n\t\tmutex_unlock(&mdc_data_lock);\n\t\treturn 0;", "failure-state", "== -EIO"),
            ("wrong-pa", "mutt_lv_to_tmc_msg(md_cdev->pa_id,", "mutt_lv_to_tmc_msg(0,", "normal-levels", "messages[state - 1] == expected_message"),
            ("ignore-peer", "final_state = max(final_state,\n\t\t\t\tmd_cdev == requested ? state : md_cdev->target_state);", "final_state = ((void)requested, state);", "peer-aggregate", "data.mutt_state == aggregate"),
            ("ignore-pending", "md_cdev == requested ? state : md_cdev->target_state", "requested ? md_cdev->target_state : state", "normal-levels", "messages[state - 1] == expected_message"),
            ("wrong-disable", "? TMC_COOLER_LV_DISABLE_MSG", "? TMC_COOLER_LV_ENABLE_MSG", "release-retry", "messages[0] == 0x103"),
            ("drop-data-lock", "\tmutex_lock(&mdc_data_lock);\n\n\tif (is_md_off(status))", "\tif (is_md_off(status))", "locking", "mdc_data_lock.held && send_tmc_msg_lock.held"),
        )
        for name, before, after, case, assertion in controls:
            mutant = compile_source(directory, name, replace_once(actual, before, after))
            run(mutant, case, assertion)
    print(f"PASS {len(CASES)} candidate scenarios, {len(HEALTHY)} unchanged baseline controls, 3 baseline failures and {len(controls)} executed mutations")


if __name__ == "__main__":
    main()
