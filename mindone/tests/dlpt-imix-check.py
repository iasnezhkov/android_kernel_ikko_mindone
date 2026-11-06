# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description="Exercise exact DLPT IMIX resistance retry, periodic dispatch and resume")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    here = Path(__file__).resolve().parent
    relative = "mindone/modules/mtk_dynamic_loading_throttling/mtk_dynamic_loading_throttling.c"
    paths = [tree / relative, here / "dlpt-imix/stubs.h", here / "dlpt-imix/cases.c",
             here / "isink-rac-check.py", Path(__file__).resolve()]
    digest = lambda: {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = digest()
    helpers = runpy.run_path(str(paths[3]))
    function, define, structure, replace_one = [helpers[n] for n in ("function", "define", "structure", "replace_one")]
    source, stubs, cases = [p.read_text() for p in paths[:3]]
    source = source.replace("__maybe_unused ", "")
    baseline = subprocess.run(["git", "show", "HEAD:" + relative], cwd=tree, check=True,
                              capture_output=True, text=True, timeout=30).stdout.replace("__maybe_unused ", "")
    expected = function(baseline, "dlpt_resume").replace(
        "\tmutex_unlock(&dlpt.notify_lock);\n\tupdate_dlpt_imix_r();",
        "\tupdate_dlpt_imix_r();\n\tmutex_unlock(&dlpt.notify_lock);")
    if function(source, "dlpt_resume") != expected:
        raise ValueError("Resume changed beyond moving the unconditional read under the cache lock")
    if "kthread_run(dlpt_notify_handler," not in function(source, "dlpt_notify_init"):
        raise ValueError("Periodic handler is no longer launched as a kthread")
    symbols = ("update_dlpt_imix_r", "retry_dlpt_imix_r", "dlpt_notify_handler", "dlpt_timer_func", "dlpt_resume")
    constants = "".join(define(source, n) for n in
                        ("DLPT_NOTIFY_FAST_UISOC", "DLPT_IMIX_R_RETRY_INTERVAL", "IMAX_MAX_VALUE"))
    mutations = [
        ("read_every_tick", "retry_dlpt_imix_r", "time_before(jiffies, dlpt.imix_r_retry_at)", "false"),
        ("overwrite_filled_cache", "retry_dlpt_imix_r", "if (dlpt.imix_r != 0)", "if (false)"),
        ("accept_zero", "update_dlpt_imix_r", "ret < 0 || imix_r <= 0", "ret < 0 || imix_r < 0"),
        ("log_every_failure", "update_dlpt_imix_r", "pr_notice_once(", "pr_notice("),
        ("retry_on_charger", "dlpt_notify_handler", "if (!dlpt_get_rgs_chrdet())\n\t\t\tretry_dlpt_imix_r();", "retry_dlpt_imix_r();"),
    ]
    variants = [("actual", source)] + [(n, replace_one(source, f, old, new)) for n, f, old, new in mutations]
    results = []
    with tempfile.TemporaryDirectory(prefix="dlpt-imix-") as directory:
        build = Path(directory)
        for name, variant in variants:
            fixture = stubs.replace("@CONSTANTS@", constants).replace("@STRUCTURE@", structure(source, "dlpt_priv"))
            fixture = fixture.replace("@FUNCTIONS@", "".join(function(variant, n) for n in symbols)) + cases
            path, binary = build / (name + ".c"), build / name
            path.write_text(fixture)
            command = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all", str(path), "-o", str(binary)]
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            results.append({"variant": name, "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
                            "run_rc": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
            if name == "actual" and result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            if name != "actual" and (result.returncode == 0 or "CHECK failed" not in result.stderr):
                raise RuntimeError("Mutation escaped runtime assertions: " + name + result.stderr)
            print(name + ": " + (result.stdout.strip() if name == "actual" else "runtime rejection PASS"))
    after = digest()
    if before != after:
        raise RuntimeError("Inputs changed while testing")
    print("PASS input hash continuity; resume source control; kthread dispatch source control")
    if args.output:
        args.output.write_text(json.dumps({"status": "PASS_EXACT_SOURCE_DLPT_IMIX", "inputs_before": before,
                                          "inputs_after": after, "tests": results,
                                          "limits": ["Host IIO, mutex, clock and computation models; no kernel build or device test."]}, indent=2) + "\n")


if __name__ == "__main__":
    main()
