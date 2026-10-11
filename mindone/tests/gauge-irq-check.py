import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


MUTATIONS = (
    ("accept-invalid-index", "(unsigned int)irq >= GAUGE_IRQ_MAX", "false"),
    ("hide-mapping-errno", "return number ? number : -ENXIO;", "return -ENXIO;"),
    ("accept-zero-mapping", "return number ? number : -ENXIO;", "return number;"),
    ("accept-missing-descriptor", "return *desc ? 0 : -ENODEV;", "return 0;"),
    ("ignore-nested-disable", "if (desc->depth > 1)", "if (false)"),
    ("skip-disable", "if (desc->depth == 0)", "if (false)"),
)


def main():
    parser = argparse.ArgumentParser(description="Exercise exact gauge IRQ helpers and required failure controls")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    source = (base / "mtk_battery.c").read_text()
    header = (base / "mtk_gauge.h").read_text()
    enumeration = re.search(r"enum gauge_irq\s*\{.*?\};", header, re.S).group(0)
    extracted = "\n".join(function(source, name) for name in (
        "gauge_irq_desc", "validate_gauge_irq", "enable_gauge_irq", "disable_gauge_irq"))
    fixture = Path(__file__).with_name("gauge-irq")
    stubs = (fixture / "stubs.h").read_text()
    cases = (fixture / "cases.c").read_text()
    variants = [("actual", extracted)]
    for name, before, after in MUTATIONS:
        if extracted.count(before) != 1:
            raise ValueError("Ambiguous control: " + name)
        variants.append((name, extracted.replace(before, after, 1)))
    with tempfile.TemporaryDirectory(prefix="gauge-irq-") as directory:
        for name, functions in variants:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(enumeration + "\n" + stubs + functions + cases)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
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
