import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]
FUNCTIONS = (
    ("mt6358-gauge.c", "hw_version_get"),
    ("mtk_battery.c", "gauge_irq_desc"),
    ("mtk_battery.c", "validate_gauge_irq"),
    ("mtk_battery.c", "request_gauge_irq"),
    ("mtk_battery.c", "release_gauge_irq"),
    ("mtk_battery.c", "release_gauge_irqs"),
    ("mtk_battery.c", "battery_kernel_setup_ready"),
    ("mtk_battery.c", "battery_enable_kernel_algo"),
    ("mtk_battery_daemon.c", "mtk_battery_request_irqs"),
    ("mtk_battery_daemon.c", "mtk_battery_setup_files"),
    ("mtk_battery_daemon.c", "mtk_battery_daemon_init"),
    ("mtk_battery_coulomb_service.c", "gauge_coulomb_service_init"),
    ("mtk_battery.c", "battery_init"),
    ("mt6358-gauge.c", "mt6358_gauge_probe"),
)
MUTATIONS = (
    ("auto-enable-before-ready", "request_gauge_irq", " | IRQF_NO_AUTOEN", ""),
    ("hide-request-errno", "request_gauge_irq", "if (ret)\n\t\treturn ret;\n\tgauge->irq_requested", "if (ret && false)\n\t\treturn ret;\n\tgauge->irq_requested"),
    ("free-existing-batch-member", "mtk_battery_request_irqs", "acquired[i] = !gauge->irq_requested[requests[i].irq];", "acquired[i] = true;"),
    ("skip-batch-unwind", "mtk_battery_request_irqs", "if (acquired[i])", "if (acquired[i] && false)"),
    ("ignore-hardware-version-error", "hw_version_get", "if (ret)", "if (ret && false)"),
    ("hide-files-error", "mtk_battery_daemon_init", "return ret;\n}", "return 0;\n}"),
    ("publish-coulomb-init-on-thread-error", "gauge_coulomb_service_init", "cs->init = false;", "cs->init = true;"),
    ("leak-coulomb-low-irq", "gauge_coulomb_service_init", "if (acquired_l)", "if (acquired_l && false)"),
    ("ignore-daemon-init-error", "battery_init", "ret = mtk_battery_daemon_init(pdev);\n\tif (ret)", "ret = mtk_battery_daemon_init(pdev);\n\tif (ret && false)"),
    ("hide-coulomb-init-error", "battery_init", "ret = gauge_coulomb_service_init(gm);\n\tif (ret)", "ret = gauge_coulomb_service_init(gm);\n\tif (ret && false)"),
    ("hide-battery-init-at-probe", "mt6358_gauge_probe", "ret = battery_init(pdev);\n\tif (ret)", "ret = battery_init(pdev);\n\tif (ret && false)"),
    ("leak-probe-netlink", "mt6358_gauge_probe", "netlink_kernel_release(gauge->gm->mtk_battery_sk);", "(void)gauge->gm->mtk_battery_sk;"),
    ("hide-battery-thread-error", "battery_init", "if (IS_ERR(battery_task))", "if (IS_ERR(battery_task) && false)"),
    ("hide-battery-sysfs-error", "battery_init", "ret = battery_sysfs_create_group(gm->bs_data.psy);\n\t\tif (ret)", "ret = battery_sysfs_create_group(gm->bs_data.psy);\n\t\tif (ret && false)"),
    ("ignore-service-readiness-error", "battery_kernel_setup_ready", "ret = READ_ONCE(gm->kernel_service_error);", "ret = 0;"),
    ("retry-dirty-hardware-setup", "battery_kernel_setup_ready", "if (READ_ONCE(gm->kernel_setup_error))", "if (READ_ONCE(gm->kernel_setup_error) && false)"),
    ("accept-unrequested-required-irq", "battery_kernel_setup_ready", "if (!gm->gauge->irq_requested[irq])", "if (!gm->gauge->irq_requested[irq] && false)"),
)


def main():
    parser = argparse.ArgumentParser(description="Exercise exact IRQ setup, init and probe fault lifecycles")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    sources = {name: (base / name).read_text() for name, _ in FUNCTIONS}
    header = (base / "mtk_gauge.h").read_text()
    enums = "\n".join(re.search(r"enum " + name + r"\s*\{.*?\};", header, re.S).group(0)
                        for name in ("gauge_irq", "gauge_hw_version"))
    extracted = {name: function(sources[filename], name) for filename, name in FUNCTIONS}
    fixture = Path(__file__).with_name("gauge-irq-setup")
    stubs = (fixture / "stubs.h").read_text()
    cases = (fixture / "cases.c").read_text()
    variants = [("actual", extracted)]
    for name, target, before, after in MUTATIONS:
        if extracted[target].count(before) != 1:
            raise ValueError("Ambiguous runtime control: " + name)
        variants.append((name, {**extracted, target: extracted[target].replace(before, after, 1)}))
    with tempfile.TemporaryDirectory(prefix="gauge-irq-setup-") as directory:
        for name, functions in variants:
            for pm in (False, True) if name == "actual" else (True,):
                path = Path(directory) / (name + ("-pm" if pm else "-no-pm"))
                path.with_suffix(".c").write_text(enums + "\n" + stubs + "\n".join(functions.values()) + cases)
                command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                           "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                           "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined"]
                if pm:
                    command.append("-DCONFIG_PM=1")
                subprocess.run([*command, str(path.with_suffix(".c")), "-o", str(path)], check=True, timeout=30)
                result = subprocess.run([str(path)], capture_output=True, text=True, timeout=10)
                if name == "actual":
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    print("CONFIG_PM=" + str(int(pm)))
                    print(result.stdout, end="")
                elif not result.returncode or not re.search(r"Assertion failed:|runtime error:|ERROR: AddressSanitizer", result.stderr):
                    raise RuntimeError("Control did not fail at runtime: " + name + "\n" + result.stdout + result.stderr)
                else:
                    print("PASS rejected " + name)


if __name__ == "__main__":
    main()
