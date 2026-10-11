import os
import re
import resource
import signal
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ETA = ROOT / 'mindone/modules/eta6965_charger/eta6965_charger.c'
FRAMEWORK = ROOT / 'mindone/modules/mtk_charger_framework/mtk_charger.c'
BASELINE = 'b6976ecd388f31e678d5403cf202375d293141a6'

def function(source, name):
    match = re.search('^static (?:int|ssize_t) ' + name + '\\([^;]*?\\)\\s*\\{', source, re.M)
    if match is None:
        raise RuntimeError(f'missing source function: {name}')
    depth = 1
    end = match.end()
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[match.start():end]


def extract(eta, names):
    registers = "\n".join(re.findall(r"^#define\s+(?:ETA6965_REG_|REG|CV_)\S+[^\n]*", eta, re.M))
    structure = re.search(r"struct eta6965_device \{.*?\n\};", eta, re.S).group()
    return "\n".join((registers, structure, *(function(eta, name) for name in names)))


def compile_fixture(folder, name, fixture, source, baseline=False):
    (folder / "eta_source.h").write_text(source)
    binary = folder / name
    arguments = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-pthread", "-I", str(folder)]
    if baseline:
        arguments.append("-Wno-sign-compare")
    subprocess.run([*arguments, str(fixture), "-o", str(binary)], check=True, timeout=30)
    return binary


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    eta = ETA.read_text()
    framework = FRAMEWORK.read_text()
    names = (
        "eta6965_read_byte_unlocked", "eta6965_read_byte", "eta6965_update_bits_unlocked",
        "eta6965_update_bits", "eta6965_update_bits_checked", "eta6965_read_safety_state",
        "safety_timer_status_show", "eta_op_enable_safety_timer", "eta_op_is_safety_timer_enabled",
        "eta6965_hw_init", "eta6965_suspend", "eta6965_resume",
        "eta_op_set_cv", "eta6965_get_cv_uV",
    )
    source = extract(eta, names)
    proc = "\n".join((function(framework, "mtk_chg_en_safety_timer_show"),
                       function(framework, "mtk_chg_en_safety_timer_write")))
    with tempfile.TemporaryDirectory(prefix="eta-safety-") as temporary:
        folder = Path(temporary)
        (folder / "proc_source.h").write_text(proc)
        fixture = Path(__file__).with_name("eta_safety_fixture.c")
        binary = compile_fixture(folder, "fixture", fixture, source)
        subprocess.run([str(binary)], check=True, timeout=30)
        environment = dict(os.environ, GIT_NO_LAZY_FETCH="1")
        baseline = subprocess.run(["git", "show", f"{BASELINE}:{ETA.relative_to(ROOT)}"],
                                  cwd=ROOT, env=environment, check=True, capture_output=True,
                                  text=True, timeout=30).stdout
        timer_fixture = Path(__file__).with_name("eta_timer_init_fixture.c")
        baseline_names = ("eta6965_read_byte", "eta6965_write_byte",
                          "eta6965_update_bits", "eta6965_hw_init")
        binary = compile_fixture(folder, "baseline", timer_fixture,
                                 extract(baseline, baseline_names), baseline=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        if result.returncode != 1 or "REG05=0xc3, expected timer bits 0x0c" not in result.stdout:
            raise RuntimeError(f"baseline did not fail the timer contract: {result}")
        print(f"ETA negative control baseline {BASELINE[:12]}: PASS (timer disabled detected)")
        init_names = ("eta6965_read_byte_unlocked", "eta6965_update_bits_unlocked",
                      "eta6965_update_bits", "eta6965_update_bits_checked", "eta6965_hw_init")
        binary = compile_fixture(folder, "candidate-init", timer_fixture, extract(eta, init_names))
        subprocess.run([str(binary)], check=True, timeout=30)
        initializer = r"\{ ETA6965_REG_05, REG05_SAFETY_TIMER_MASK, REG05_SAFETY_TIMER_MASK \}"
        mutant, count = re.subn(initializer, "{ ETA6965_REG_05, REG05_EN_TIMER_MASK, 0 }", eta)
        if count != 1:
            raise RuntimeError(f"expected one timer initializer, found {count}")
        binary = compile_fixture(folder, "mutant", fixture, extract(mutant, names))
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        if result.returncode != -signal.SIGABRT or "client.regs[5] == 0xcf" not in result.stderr:
            raise RuntimeError(f"isolated initializer mutant was not detected: {result}")
        print("ETA negative control initializer-only revert: PASS (full fixture rejected mutant)")


if __name__ == "__main__":
    main()
