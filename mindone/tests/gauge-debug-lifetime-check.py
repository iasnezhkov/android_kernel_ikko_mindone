import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


function = runpy.run_path(str(Path(__file__).with_name("gauge-algo-check.py")))["function"]


def main():
    parser = argparse.ArgumentParser(description="Check actual gauge debug lookup across failed-probe object lifetimes")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    source = (args.kernel_tree / "mindone/modules/mt6358_battery/mtk_battery.c").read_text()
    actual = function(source, "bat_get_debug_level")
    fixture = Path(__file__).with_name("gauge-debug-lifetime")
    stubs = (fixture / "stubs.h").read_text()
    cases = (fixture / "cases.c").read_text()
    cached = (fixture / "cached.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-debug-lifetime-") as directory:
        for name, body in (("actual", actual), ("stale-static-cache", cached)):
            path = Path(directory) / name
            path.with_suffix(".c").write_text(stubs + body + cases)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path)],
                           check=True, timeout=30)
            result = subprocess.run([str(path)], capture_output=True, text=True, timeout=10)
            if name == "actual":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end="")
            elif not result.returncode or not re.search(r"Assertion failed:|ERROR: AddressSanitizer", result.stderr):
                raise RuntimeError("Lifetime control did not fail at runtime: " + result.stderr)
            else:
                print("PASS rejected " + name)


if __name__ == "__main__":
    main()
