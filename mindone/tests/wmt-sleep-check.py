import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


CASES = ("banks", "missing", "core", "native", "osc", "library", "consumers")
MUTATIONS = (
    ("ignore-bank-error", "mtk_wcn_consys_sleep_info_read_all_ctrl", "ret = read_ret;", "ret = 0;", "banks"),
    ("replace-first-bank-error", "mtk_wcn_consys_sleep_info_read_all_ctrl", "if (ret == 0 && read_ret != 0)", "if (read_ret != 0)", "banks"),
    ("publish-partial-banks", "mtk_wcn_consys_sleep_info_read_all_ctrl", "if (ret != 0)\n\t\t\t\tgoto unlock;", "if (ret == INT_MIN)\n\t\t\t\tgoto unlock;", "banks"),
    ("skip-last-bank", "mtk_wcn_consys_sleep_info_read_all_ctrl", "i < WMT_SLEEP_COUNT_MAX", "i < WMT_SLEEP_COUNT_MAX - 1", "banks"),
    ("hide-missing-provider", "mtk_wcn_consys_sleep_info_read_all_ctrl", "ret = -EOPNOTSUPP;", "ret = 0;", "missing"),
    ("hide-operation-error", "opfunc_get_consys_state", "return ret;", "return 0;", "core"),
    ("replace-first-operation-error", "opfunc_get_consys_state", "if (ret == 0 && dump_ret != MTK_WCN_BOOL_TRUE)", "if (dump_ret != MTK_WCN_BOOL_TRUE)", "core"),
    ("fail-positive-success", "opfunc_get_consys_state", "if (ret == 0 && dump_ret != MTK_WCN_BOOL_TRUE)", "if (ret == 0)", "core"),
    ("ignore-register-gating", "opfunc_get_consys_state", "if (mtk_consys_check_reg_readable() == 0)", "if (mtk_consys_check_reg_readable() == INT_MIN)", "core"),
    ("fake-unavailable-zero", "plat_resume_handler", "CONSYS state measurement unavailable", "TOP:0,0;MCU:0,0;BT:0,0;WIFI:0,0;GPS:0,0", "consumers"),
    ("accept-proc-error-as-eof", "wmt_dev_proc_for_dump_info_read", "retval = -EIO;", "retval = 0;", "consumers"),
    ("accept-unmapped-osc", "consys_dump_osc_state", "return MTK_WCN_BOOL_FALSE;", "return MTK_WCN_BOOL_TRUE;", "osc"),
    ("publish-failed-library-result", "wmt_lib_dmp_consys_state", "if (bRet == MTK_WCN_BOOL_FALSE)", "if (bRet == INT_MIN)", "library"),
    ("accept-operation-result", "wmt_lib_put_act_op", "bRet = (pOp->result) ? MTK_WCN_BOOL_FALSE : MTK_WCN_BOOL_TRUE;", "bRet = MTK_WCN_BOOL_TRUE;", "library"),
    ("hide-completion-result", "osal_op_raise_signal", "pOp->result = result;", "pOp->result = 0;", "library"),
)


def function(source, name):
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27',
                    lambda match: re.sub(r"[^\n]", " ", match.group(0)), source, flags=re.S)
    pattern = r"^(?:static\s+)?(?:INT32|UINT32|VOID|MTK_WCN_BOOL|void|int|ssize_t)\s+" + re.escape(name) + r"\s*\([^;{]*\)\s*\{"
    matches = list(re.finditer(pattern, masked, re.M))
    if len(matches) != 1:
        raise ValueError("Ambiguous definition: " + name)
    start = matches[0].start()
    opening = masked.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    return source[start:end] + "\n"


def declaration(source, pattern):
    matches = list(re.finditer(pattern, source, re.S))
    if len(matches) != 1:
        raise ValueError("Ambiguous declaration: " + pattern)
    return matches[0].group(0) + "\n"


def read_sources(tree, reference=None):
    base = "mindone/modules/wmt_drv/common_main/"
    paths = {"platform": base + "platform/mt6789.c", "hardware": base + "platform/mtk_wcn_consys_hw.c",
             "core": base + "core/wmt_core.c", "library": base + "core/wmt_lib.c",
             "osal": base + "linux/osal.c", "proc": base + "linux/wmt_proc_dbg.c", "alarm": base + "linux/wmt_alarm.c",
             "registers": base + "platform/include/mt6789.h",
             "plat_header": "mindone/modules/include/drivers/misc/mediatek/connectivity/common/common_main/include/wmt_plat.h",
             "core_header": "mindone/modules/include/drivers/misc/mediatek/connectivity/common/common_main/core/include/wmt_core.h"}
    result = {}
    for key, path in paths.items():
        if reference:
            result[key] = subprocess.run(["git", "show", reference + ":" + path], cwd=tree,
                                         env={**os.environ, "GIT_NO_LAZY_FETCH": "1"}, capture_output=True,
                                         text=True, check=True, timeout=20).stdout
        else:
            result[key] = (tree / path).read_text()
    return result


def components(sources):
    defs = declaration(sources["plat_header"], r"typedef enum\s*\{\s*WMT_SLEEP_COUNT_TOP.*?\}\s*WMT_SLEEP_COUNT_TYPE;")
    defs += declaration(sources["plat_header"], r"struct consys_sw_state\s*\{.*?\};")
    defs += declaration(sources["plat_header"], r"typedef struct consys_state\s*\{.*?\}\s*CONSYS_STATE,\s*\*P_CONSYS_STATE;")
    defs += declaration(sources["core_header"], r"#define WMT_CORE_DMP_CPUPCR_NUM[^\n]*")
    defs += declaration(sources["core_header"], r"enum wmt_consys_dump_status\s*\{.*?\};")
    defs += declaration(sources["core_header"], r"typedef struct consys_state_dmp_info\s*\{.*?\}\s*CONSYS_STATE_DMP_INFO,\s*\*P_CONSYS_STATE_DMP_INFO;")
    defs += declaration(sources["core_header"], r"typedef struct consys_state_dmp_op\s*\{.*?\}\s*CONSYS_STATE_DMP_OP,\s*\*P_CONSYS_STATE_DMP_OP;")
    functions = {}
    for key, names in (("platform", ("consys_sleep_info_is_enable", "consys_sleep_info_read_ctrl", "consys_dump_osc_state", "consys_dump_gating_state")),
                       ("hardware", ("mtk_wcn_consys_sleep_info_read_all_ctrl", "plat_resume_handler")),
                       ("core", ("opfunc_get_consys_state",)), ("osal", ("osal_op_raise_signal",)),
                       ("library", ("wmt_lib_put_act_op", "wmt_lib_dmp_consys_state")),
                       ("proc", ("wmt_dev_proc_for_dump_info_read",)), ("alarm", ("wmt_alarm_dmp_info_handler",))):
        for name in names:
            functions[name] = function(sources[key], name)
    functions["wmt_alarm_dmp_info_handler"] = functions["wmt_alarm_dmp_info_handler"].replace("g_dmp_info", "alarm_dmp_info")
    names = sorted(set(re.findall(r"\bCONN_[A-Z0-9_]+\b", "\n".join(functions.values()))))
    for name in names:
        defs += declaration(sources["registers"], r"#define\s+" + name + r"\s+[^\n]*")
    return defs, functions


def execute(directory, name, defs, functions, fixture, cases):
    path = directory / name
    path.with_suffix(".c").write_text((fixture / "types.h").read_text() + defs +
                                    (fixture / "stubs.h").read_text() + "\n".join(functions.values()) +
                                    (fixture / "cases.c").read_text())
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-Wno-pointer-sign", "-Wno-format-zero-length", "-Wno-deprecated-declarations", "-ftrivial-auto-var-init=pattern",
                    "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path)],
                   check=True, timeout=30)
    results = []
    for case in cases:
        result = subprocess.run([str(path), case], capture_output=True, text=True, timeout=10)
        results.append({"case": case, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
    return results


def rejected(result):
    return result["returncode"] != 0 and re.search(r"Assertion failed:|runtime error:|ERROR: AddressSanitizer", result["stderr"])


def main():
    parser = argparse.ArgumentParser(description="Exercise exact WMT sample, operation, BOOL and consumer failure contracts with mocked hardware and queues")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--baseline", help="Run selected negative cases on the original source and require runtime rejection")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.unlink(missing_ok=True)
    sources = read_sources(args.kernel_tree)
    defs, functions = components(sources)
    fixture = Path(__file__).with_name("wmt-sleep")
    report = {"source_sha256": {name: hashlib.sha256(source.encode()).hexdigest() for name, source in sources.items()},
              "cases": [], "mutations": [], "baseline": [],
              "limits": ["Mocked MMIO/queues/clocks; native compilation and device execution remain separate gates.",
                         "No true concurrent worker, timeout or reset scheduling is reproduced by synchronous queue stubs.",
                         "PM counter clear and deferred/coalesced worker have no proven per-cycle epoch.",
                         "Global proc buffer is not a per-open snapshot across concurrent readers; worker teardown is outside this patch.",
                         "Raw gating mailbox without known magic is not decoded state; timer units and energy savings are unproved."]}
    with tempfile.TemporaryDirectory(prefix="wmt-sleep-") as temporary:
        directory = Path(temporary)
        results = execute(directory, "actual", defs, functions, fixture, CASES)
        for result in results:
            if result["returncode"]:
                raise RuntimeError(json.dumps(result, indent=2))
            print(result["stdout"], end="")
        report["cases"] = results
        for name, target, before, after, case in MUTATIONS:
            variant = dict(functions)
            count = variant[target].count(before)
            expected = 2 if name in ("skip-last-bank", "replace-first-operation-error", "fail-positive-success") else 1
            if count != expected:
                raise ValueError("Ambiguous mutation: " + name + ": " + str(count))
            variant[target] = variant[target].replace(before, after)
            result = execute(directory, name, defs, variant, fixture, (case,))[0]
            if not rejected(result):
                raise RuntimeError("Mutation did not fail at runtime: " + name + "\n" + json.dumps(result))
            report["mutations"].append({"name": name, **result})
            print("PASS rejected " + name)
        if args.baseline:
            old = read_sources(args.kernel_tree, args.baseline)
            old_defs, old_functions = components(old)
            for unchanged in ("consys_sleep_info_is_enable", "consys_sleep_info_read_ctrl", "consys_dump_gating_state", "wmt_lib_put_act_op", "wmt_lib_dmp_consys_state", "osal_op_raise_signal"):
                if old_functions[unchanged] != functions[unchanged]:
                    raise ValueError("Changed hardware/scheduling function: " + unchanged)
            for source_key, names in (("platform", ("consys_sleep_info_enable_ctrl", "consys_sleep_info_clear")),
                                      ("hardware", ("mtk_wmt_suspend", "mtk_wmt_resume", "connsys_pm_notifier_callback", "mtk_wcn_consys_sleep_info_clear", "mtk_wcn_consys_sleep_info_restore"))):
                for name in names:
                    if function(old[source_key], name) != function(sources[source_key], name):
                        raise ValueError("Changed PM/hardware sequence: " + name)
            report["unchanged_pm_hardware_sequences"] = True
            for result in execute(directory, "baseline", old_defs, old_functions, fixture, ("banks", "core", "osc", "library", "consumers")):
                if not rejected(result):
                    raise RuntimeError("Original source did not reproduce defect: " + json.dumps(result))
                report["baseline"].append(result)
                print("PASS original rejected " + result["case"])
    report["status"] = "PASS_HOST"
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
