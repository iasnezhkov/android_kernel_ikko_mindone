import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"^(?:static )?int " + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise ValueError(f"Missing function: {name}")
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def command(source, name):
    marker = "\tcase " + name + ":"
    start = source.index(marker)
    end = source.index("\n\tbreak;", start) + len("\n\tbreak;")
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description="Exercise real GM30 charger-presence packets against independent physical ONLINE and battery STATUS")
    parser.add_argument("kernel_tree", type=Path)
    args = parser.parse_args()
    base = args.kernel_tree / "mindone/modules/mt6358_battery"
    daemon = (base / "mtk_battery_daemon.c").read_text()
    algorithm = (base / "mtk_battery_algo.c").read_text()
    fixtures = Path(__file__).resolve().parent / "gauge-charger"
    source = (fixtures / "stubs.h").read_text()
    source += function(algorithm, "get_charger_exist")
    source += "static void dispatch(struct mtk_battery *gm, int cmd, struct response *ret_msg)\n{\n\tswitch (cmd) {\n"
    source += command(daemon, "FG_DAEMON_CMD_IS_CHARGER_EXIST")
    source += command(daemon, "FG_DAEMON_CMD_GET_CHARGER_STATUS")
    source += "\tdefault: abort();\n\t}\n}\n"
    source += (fixtures / "cases.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gauge-charger-") as directory:
        path = Path(directory)
        (path / "check.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined,address", str(path / "check.c"), "-o", str(path / "check")], check=True)
        subprocess.run([str(path / "check")], check=True)


if __name__ == "__main__":
    main()
