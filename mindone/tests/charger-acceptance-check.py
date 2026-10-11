import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def block(source, expression, semicolon=True):
    found = re.search(expression, source, re.M)
    if not found:
        raise ValueError("Missing source block: " + expression)
    opening = source.index("{", found.start())
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if semicolon:
        end = source.index(";", end) + 1
    return source[found.start():end] + "\n"


def function(source, name):
    return block(source, r"^(?:static )?(?:int|ssize_t) " + re.escape(name) + r"\(", False)


def mutate(source, old, new, name):
    if source.count(old) != 1:
        raise ValueError("Ambiguous mutation: " + name)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description="Reproduce exact charger cooler/provider acceptance and retry contracts without hardware writes")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--base", default="e6fa454ff9035dffb3420846714377feddc97047")
    args = parser.parse_args()
    root = args.kernel_tree.resolve()
    paths = (
        "mindone/modules/charger_cooling/charger_cooling.c",
        "mindone/modules/board_temp/charger_cooling.h",
        "mindone/modules/mtk_charger_framework/mtk_charger.c",
        "mindone/modules/mtk_charger_framework/mtk_charger_intf.c",
        "mindone/modules/mtk_charger_framework/mtk_charger.h",
        "mindone/modules/mtk_charger_framework/mtk_charger_algorithm_class.h",
        "include/linux/power_supply.h", "drivers/power/supply/power_supply_core.c",
        "mindone/modules/BUILD.bazel",
        "mindone/modules/charger_class/charger_class.h",
        "mindone/modules/charger_class/charger_class.c",
    )
    sources = {path: (root / path).read_text() for path in paths}
    if (root / "mindone/modules/charger_cooling/charger_cooling.h").resolve() != (root / paths[1]).resolve():
        raise ValueError("Compiled cooler header target changed")
    if args.baseline:
        for path in paths[:3]:
            sources[path] = subprocess.run(["git", "show", args.base + ":" + path], cwd=root,
                                           capture_output=True, text=True, check=True, timeout=10).stdout
    originals = dict(sources)
    fixture = Path(__file__).with_name("charger-acceptance")
    cooler, header, framework, interface, provider_header, algo_header, supply_header, core, build, class_header, class_source = (sources[p] for p in paths)
    if (root / "mindone/modules/mtk_charger_framework/charger_class.h").resolve() != (root / paths[-2]).resolve():
        raise ValueError("Compiled charger class header target changed")
    for module, source in (("charger_cooling", "charger_cooling/charger_cooling.c"),
                           ("mtk_charger_framework", "mtk_charger_framework/mtk_charger.c")):
        match = re.search(r'name = "' + module + r'",\s*srcs = \[([^]]+)\]', build)
        if not match or '"' + source + '"' not in match[1]:
            raise ValueError("Compiled source binding missing: " + module)
    for expression in (
        r'info->psy_desc1.name = "mtk-master-charger";',
        r'info->psy_desc1.properties = charger_psy_properties;',
        r'info->psy_desc1.set_property = psy_charger_set_property;',
        r'info->psy_desc1.property_is_writeable =\s*psy_charger_property_is_writeable;',
    ):
        if not re.search(expression, framework):
            raise ValueError("Master provider registration changed: " + expression)
    prefix = "#define CHECKED_REQUESTS " + str(int(not args.baseline)) + "\n"
    prefix += (fixture / "prelude.h").read_text()
    prefix += block(supply_header, r"^enum power_supply_property \{")
    prefix += block(supply_header, r"^union power_supply_propval \{")
    prefix += block(supply_header, r"^enum \{\s*POWER_SUPPLY_STATUS_UNKNOWN")
    configuration = block(algo_header, r"^enum charger_configuration \{")
    configuration = configuration.replace("SINGLE_CHARGER", "PROVIDER_SINGLE_CHARGER")
    prefix += configuration
    prefix += re.search(r"^#define CHARGER_STATE_NUM[^\n]+", header, re.M)[0] + "\n"
    prefix += re.search(r"^#define MAX_CHARGER_COOLER_NAME_LEN[^\n]+", header, re.M)[0] + "\n"
    for expression in (r"^enum charger_type \{", r"^struct charger_cooling_device \{",
                       r"^struct charger_cooling_platform_data \{"):
        prefix += block(header, expression)
    prefix += block(provider_header, r"^struct charger_data \{")
    prefix += block(provider_header, r"^enum chg_data_idx_enum \{")
    operations = block(class_header, r"^struct charger_ops \{")
    prefix += "struct charger_ops {\n"
    for name in ("get_charging_current", "get_input_current"):
        prefix += re.search(r"^[^\n]*\(\*" + name + r"\)[^;]+;", operations, re.M)[0] + "\n"
    prefix += "};\n"
    prefix += (fixture / "stubs.h").read_text()
    bodies = ""
    for name in ("charger_dev_get_charging_current", "charger_dev_get_input_current"):
        bodies += function(class_source, name)
    bodies += '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wpointer-sign"\n'
    for name in ("get_charger_charging_current", "get_charger_input_current"):
        bodies += function(interface, name)
    bodies += "#pragma GCC diagnostic pop\n"
    for name in ("psy_charger_property_is_writeable", "psy_charger_set_property", "psy_charger_get_property"):
        bodies += function(framework, name)
    bodies += block(framework, r"^static enum power_supply_property charger_psy_properties\[\] =")
    bodies += function(core, "power_supply_set_property")
    bodies += block(cooler, r"^struct charger_cooler_info \{")
    bodies += "static struct charger_cooler_info charger_cl_data;\n"
    if not args.baseline:
        bodies += "static DEFINE_MUTEX(charger_cooler_lock);\n"
    for name in ("master", "slave"):
        bodies += block(cooler, r"^static const int " + name + r"_charger_state_to_current_limit\[CHARGER_STATE_NUM\] =")
    for name in ("charger_throttle", "charger_cooling_get_cur_state", "charger_cooling_set_cur_state", "cooling_state_to_charger_limit_v1"):
        bodies += function(cooler, name)
    if not args.baseline:
        bodies += function(cooler, "charger_curr_show")
    cases = (fixture / "cases.c").read_text()
    mutations = (
        ("drop-cc-request", "info->chg_data[idx].thermal_charging_current_limit = val->intval;", "info->chg_data[idx].thermal_charging_current_limit = -1;"),
        ("drop-iin-request", "info->chg_data[idx].thermal_input_current_limit = val->intval;", "info->chg_data[idx].thermal_input_current_limit = -1;"),
        ("drop-hv-disable", "info->enable_hv_charging = false;", "info->enable_hv_charging = true;"),
        ("drop-worker-wake", "\t_wake_up_charger(info);", "\tif (false) _wake_up_charger(info);"),
        ("unregister-cc-property", "\tPOWER_SUPPLY_PROP_TEMP,\n\tPOWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,", "\tPOWER_SUPPLY_PROP_TEMP,\n\tPOWER_SUPPLY_PROP_CAPACITY,"),
        ("hide-callback-first-error", 'pr_notice("set bat curr fail\\n");\n\t\treturn ret;', 'pr_notice("set bat curr fail\\n");\n\t\treturn 0;'),
        ("ignore-dead-provider", "atomic_read(&psy->use_cnt) <= 0 || !psy->desc->set_property", "(atomic_read(&psy->use_cnt) <= 0 && false) || !psy->desc->set_property"),
        ("drop-secondary-request", "prop_s_bat_chr.intval = slave_charger_state_to_current_limit[chg->target_state];", "prop_s_bat_chr.intval = -1;"),
        ("fake-current-readback", ("ret = " if not args.baseline else "val->intval = ") + "get_charger_charging_current(info, chg);", ("ret = " if not args.baseline else "val->intval = ") + "info->chg_data[0].thermal_charging_current_limit;"),
        ("missing-ops-guard", "if (chg_dev != NULL && chg_dev->ops != NULL &&\n\t    chg_dev->ops->get_charging_current)", "if (chg_dev->ops->get_charging_current)"),
        ("wrong-current-callback", "return chg_dev->ops->get_charging_current(chg_dev, uA);", "return chg_dev->ops->get_input_current(chg_dev, uA);"),
    )
    if not args.baseline:
        mutations += (
            ("hide-throttle-error", "ret = charger_cdev->pdata->state_to_charger_limit(charger_cdev);\n\tif (ret)", "ret = charger_cdev->pdata->state_to_charger_limit(charger_cdev);\n\tif (false && ret)"),
            ("skip-dirty-old-state-retry", "charger_cdev->target_state == state && !charger_cdev->request_dirty", "charger_cdev->target_state == state"),
            ("publish-wrong-accepted-request", "\t*state = charger_cdev->target_state;", "\t*state = 0;"),
            ("mark-failed-request-clean", "charger_cdev->request_dirty = true;", "charger_cdev->request_dirty = false;"),
            ("publish-failed-target", "charger_cdev->target_state = previous;", "charger_cdev->target_state = state + (previous - previous);"),
            ("publish-dirty-current", "if (charger_cl_data.request_dirty)", "if (false && charger_cl_data.request_dirty)"),
            ("missing-slave-preflight", "if (chg->type == DUAL_CHARGER && IS_ERR_OR_NULL(chg->s_chg_psy))", "if (false && chg->type == DUAL_CHARGER && IS_ERR_OR_NULL(chg->s_chg_psy))"),
            ("hide-readback-errno", "ret = get_charger_charging_current(info, chg);\n\t\tif (ret < 0)", "ret = get_charger_charging_current(info, chg);\n\t\tif (false && ret < 0)"),
            ("unlocked-request-sequence", "mutex_lock(&charger_cooler_lock);\n\tif (charger_cdev->target_state", "if (charger_cdev->target_state"),
            ("unlocked-request-readback", "mutex_lock(&charger_cooler_lock);\n\t*state = charger_cdev->target_state;\n\tmutex_unlock(&charger_cooler_lock);", "*state = charger_cdev->target_state;"),
            ("unlocked-dirty-diagnostic", "mutex_lock(&charger_cooler_lock);\n\tif (charger_cl_data.request_dirty)", "if (charger_cl_data.request_dirty)"),
        )
    results = []
    with tempfile.TemporaryDirectory(prefix="charger-acceptance-") as directory:
        for name, text in [("actual", bodies)] + [(name, mutate(bodies, old, new, name)) for name, old, new in mutations]:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(prefix + text + cases)
            compiled = subprocess.run([
                "cc", "-std=c11", "-pthread", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-variable",
                "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path),
            ], capture_output=True, text=True, timeout=30)
            if compiled.returncode:
                raise RuntimeError("Compilation failed: " + name + "\n" + compiled.stderr)
            result = subprocess.run([str(path)], capture_output=True, text=True, timeout=10)
            if name == "actual":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end="")
            elif not result.returncode or not re.search(r"Assertion failed:|runtime error:|ERROR: AddressSanitizer", result.stderr):
                raise RuntimeError("Mutation not rejected at runtime: " + name + "\n" + result.stdout + result.stderr)
            else:
                print("PASS rejected " + name)
            results.append({"name": name, "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
    evidence = {
        "source_sha256": {name: hashlib.sha256(text.encode()).hexdigest() for name, text in originals.items()},
        "candidate_source_sha256": {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sources.items()} if not args.baseline else None,
        "source_paths": {name: str((root / name).resolve()) for name in paths},
        "fixture_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__).resolve(), *sorted(fixture.glob("*"))]},
        "results": results,
        "checked_requests": not args.baseline,
        "baseline_commit": args.base if args.baseline else None,
        "scope": "Actual current cooler/framework/core functions and selected headers; baseline mode reads three original production files from a pinned local commit. External PSY callbacks, worker wake and hardware getter boundaries simulated; extracted cooler mutex operations use host pthread mutex with deterministic pending-request reader/writer interleavings. No worker I2C transaction, kernel lockdep, native ABI layout, concurrent probe/remove lifecycle, device or thermal runtime acceptance.",
    }
    if args.evidence:
        args.evidence.write_text(json.dumps(evidence, indent=2) + "\n")
    print("PASS " + ("seven" if args.baseline else "eight") + " contract groups and " + str(len(mutations)) + " executed mutation controls")


if __name__ == "__main__":
    main()
