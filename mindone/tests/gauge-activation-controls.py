import argparse
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


MUTATIONS = (
    ("ignore-dod-initialization-error", "mtk_battery_algo.c", "battery_algo_init",
     "ret = fgr_dod_init(gm);\n\tif (ret)\n\t\tgoto restore;", "ret = fgr_dod_init(gm);\n\tif (ret && false)\n\t\tgoto restore;", "gauge-activation-audit.py", ()),
    ("omit-model-rollback", "mtk_battery_algo.c", "battery_algo_init",
     "gm->algo = saved;", "(void)saved;", "gauge-activation-audit.py", ()),
    ("truncate-profile-rollback", "mtk_battery_algo.c", "battery_algo_init",
     "memcpy(profile, saved_profile, 100 * sizeof(*profile));", "memcpy(profile, saved_profile, sizeof(*profile));", "gauge-activation-audit.py", ()),
    ("repeat-initialization", "mtk_battery.c", "battery_enable_kernel_algo",
     "if (!gm->algo.active)", "if (true)", "gauge-activation-audit.py", ()),
    ("publish-mode-before-preparation", "mtk_battery.c", "battery_enable_kernel_algo",
     "ret = battery_kernel_setup_ready(gm);", "smp_store_release(&gm->algo.active, true);\n\t\tret = battery_kernel_setup_ready(gm);", "gauge-activation-audit.py", ()),
    ("reject-positive-iio-success", "mtk_battery_algo.c", "battery_algo_init",
     "ret = gauge_get_property(GAUGE_PROP_PTIM_RESIST, &rac);\n\tif (ret < 0)", "ret = gauge_get_property(GAUGE_PROP_PTIM_RESIST, &rac);\n\tif (ret != 0)", "gauge-activation-audit.py", ()),
    ("ignore-native-con0-read-error", "mt6358-gauge.c", "fgauge_get_info",
     "if (ret)", "if (ret && false)", "gauge-init-io-check.py", ()),
    ("hide-native-rtc-read-error", "mt6358-gauge.c", "read_rtc_spare_fg_value",
     "return PTR_ERR(buf);", "return 0;", "gauge-init-io-check.py", ()),
    ("initialize-through-existing-mode-reset", "mtk_battery.c", "battery_reset_charge_init_locked",
     "battery_reset_charge_mode_locked(gm, true)", "battery_reset_charge_mode_locked(gm, false)", "gauge-reset-check.py", ("--case", "initializing")),
    ("failed-presence-becomes-software-plugout", "mtk_battery_daemon.c", "sw_check_bat_plugout",
     "ret = gauge_get_property(GAUGE_PROP_BATTERY_EXIST, &is_bat_exist);", "ret = 0;\n\t\tis_bat_exist = gauge_get_int_property(GAUGE_PROP_BATTERY_EXIST);", "gauge-presence-check.py", ()),
    ("failed-presence-becomes-irq-plugout", "mtk_battery_daemon.c", "bat_plugout_irq",
     "ret = gauge_get_property(GAUGE_PROP_BATTERY_EXIST, &is_bat_exist);", "ret = 0;\n\tis_bat_exist = gauge_get_int_property(GAUGE_PROP_BATTERY_EXIST);", "gauge-presence-check.py", ()),
    ("discard-sysfs-initialization-errno", "mtk_battery_daemon.c", "BAT_EC_store",
     "return ret < 0 ? ret : size;", "return size;", "gauge-activation-audit.py", ()),
)


def main():
    parser = argparse.ArgumentParser(description="Require exact-source activation and presence fixtures to reject transaction regressions")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    relative = Path("mindone/modules/mt6358_battery")
    filenames = ("mtk_battery.c", "mtk_battery_algo.c", "mtk_battery_daemon.c",
                 "mt6358-gauge.c", "mtk_battery_coulomb_service.c")
    sources = {name: (args.kernel_tree / relative / name).read_text() for name in filenames}
    tests = Path(__file__).resolve().parent
    for name, filename, target, before, after, runner, extra in MUTATIONS:
        source = sources[filename]
        original = function(source, target)
        if original.count(before) != 1:
            raise ValueError("Ambiguous mutation: " + name)
        mutated = source.replace(original, original.replace(before, after, 1), 1)
        with tempfile.TemporaryDirectory(prefix="gauge-activation-control-") as directory:
            tree = Path(directory)
            (tree / relative).mkdir(parents=True)
            for path, content in sources.items():
                (tree / relative / path).write_text(mutated if path == filename else content)
            result = subprocess.run([sys.executable, str(tests / runner), str(tree), *extra],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            evidence = re.search(r"^.*Assertion failed:.*$", result.stderr, re.M)
            if not result.returncode or not evidence or "error: " in result.stderr:
                raise RuntimeError("Mutation did not fail an actual runtime assertion: " + name + "\n" + result.stdout + result.stderr)
            print("PASS rejected " + name + ": " + evidence.group(0))


if __name__ == "__main__":
    main()
