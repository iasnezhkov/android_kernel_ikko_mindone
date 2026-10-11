import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def block(source, expression, semicolon=False):
    found = re.search(expression, source, re.M)
    if not found:
        raise ValueError("Missing source block: " + expression)
    opening = source.index("{", found.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if semicolon:
        end = source.index(";", end) + 1
    return source[found.start():end] + "\n"


def function(source, name):
    return block(source, r"^(?:static )?(?:int|void) " + re.escape(name) + r"\(")


def mutate(source, before, after, name):
    if source.count(before) != 1:
        raise ValueError("Ambiguous mutation: " + name)
    return source.replace(before, after, 1)


def main():
    parser = argparse.ArgumentParser(description="Execute exact CCMNI private commands, ACK ioctl dispatch and native/compat ABI controls")
    parser.add_argument("kernel_tree", type=Path)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    root = args.kernel_tree.resolve()
    paths = (
        "mindone/modules/ccmni/ccmni.c",
        "mindone/modules/include/drivers/misc/mediatek/ccmni/ccmni.h",
        "mindone/modules/include/drivers/misc/mediatek/include/mt-plat/mtk_ccci_common.h",
        "include/uapi/linux/if.h", "include/linux/compat.h", "include/linux/netdevice.h",
        "include/uapi/linux/sockios.h", "net/core/dev_ioctl.c", "net/socket.c",
        "mindone/modules/BUILD.bazel",
    )
    sources = {path: (root / path).read_text() for path in paths}
    driver, header, common, native, compat, device, sockios, core, socket, build = (sources[p] for p in paths)
    if not re.search(r'name = "ccmni",\s*srcs = \[\s*"ccmni/ccmni.c",', build):
        raise ValueError("CCMNI compiled source is not bound")
    if not re.search(r"case SIOCDEVPRIVATE \.\.\. SIOCDEVPRIVATE \+ 15:\s*return dev_siocdevprivate", core):
        raise ValueError("ACK private dispatch changed")
    prefix = (Path(__file__).with_name("ccmni-private") / "prelude.h").read_text()
    prefix += block(common, r"^enum MD_SYS \{", True)
    for name in ("ifmap", "if_settings", "ifreq"):
        prefix += block(native, r"^struct " + name + r" \{", True)
    for name in ("compat_ifmap", "compat_if_settings", "compat_ifreq"):
        prefix += block(compat, r"^struct " + name + r" \{", True)
    for name in ("SIOCDEVPRIVATE",):
        prefix += re.search(r"^#define\s+" + name + r"\s+[^\n]+", sockios, re.M).group(0) + "\n"
    for name in ("SIOCSTXQSTATE", "SIOCCCMNICFG", "SIOCFWDFILTER", "SIOCACKPRIO", "SIOPUSHPENDING",
                 "CCMNI_FLT_NUM", "CCMNI_NETDEV_WDT_TO", "IPV4_VERSION"):
        prefix += re.search(r"^#define\s+" + name + r"\s+[^\n]+", header, re.M).group(0) + "\n"
    prefix += re.search(r"^#define DEV_OPEN\s+[^\n]+", driver, re.M).group(0) + "\n"
    prefix += block(header, r"^enum \{\s*CCMNI_FLT_ADD", True)
    for name in ("ccmni_ch", "ccmni_fwd_filter", "ccmni_flt_act", "ccmni_instance", "ccmni_ccci_ops", "ccmni_ctl_block"):
        prefix += block(header, r"^struct " + name + r" \{", True)
    names = ("ndo_open", "ndo_stop", "ndo_start_xmit", "ndo_tx_timeout", "ndo_do_ioctl",
             "ndo_siocdevprivate", "ndo_change_mtu", "ndo_select_queue")
    device_ops = block(device, r"^struct net_device_ops \{", True)
    prefix += "struct net_device_ops {\n"
    for name in names:
        declaration = re.search(r"^[^\n]*\(\*" + name + r"\)[^;]+;", device_ops, re.M).group(0)
        prefix += declaration + "\n"
    prefix += "};\n"
    fixture = Path(__file__).with_name("ccmni-private")
    prefix += (fixture / "stubs.h").read_text()
    prefix += function(socket, "get_user_ifreq") + function(socket, "put_user_ifreq")
    prefix += function(core, "dev_siocdevprivate")
    handlers = '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'
    handlers += function(driver, "ccmni_ioctl")
    handlers += "#pragma GCC diagnostic pop\n" + function(driver, "ccmni_siocdevprivate")
    handlers += block(driver, r"^static const struct net_device_ops ccmni_netdev_ops =", True)
    handlers += function(driver, "get_queued_pkts")
    cases = (fixture / "cases.c").read_text()
    mutations = (
        ("missing-new-callback", ".ndo_siocdevprivate = ccmni_siocdevprivate,", ".ndo_siocdevprivate = NULL,"),
        ("raw-compat-filter-pointer", "request.ifr_data = data;", "request.ifr_data = data ? ifr->ifr_data : NULL;"),
        ("corrupt-scalar-union", "if (cmd == SIOCFWDFILTER)\n\t\trequest.ifr_data = data;", "if (cmd == SIOCFWDFILTER)\n\t\trequest.ifr_data = data;\n\telse\n\t\trequest.ifr_ifru.ifru_ivalue = 0;"),
        ("hide-legacy-error", "return ccmni_ioctl(dev, &request, cmd);", "ccmni_ioctl(dev, &request, cmd);\n\treturn 0;"),
        ("missing-target-guard", "if (!target || !target->ccci_ops)", "if (false)"),
        ("missing-pending-callback-guard", "if (cmd == SIOPUSHPENDING &&\n\t\t!ctlb->ccci_ops->ccci_handle_port_list)", "if (false)"),
        ("partial-ack-update", "if (!ctlb->ccmni_inst[i])\n\t\t\t\treturn -ENODEV;", "if (!ctlb->ccmni_inst[i])\n\t\t\t\tbreak;"),
        ("skip-work-synchronization", "\t\tflush_delayed_work(&ccmni->pkt_queue_work);", "\t\tif (false)\n\t\t\tflush_delayed_work(&ccmni->pkt_queue_work);"),
        ("accept-unknown-private", "\tdefault:\n\t\treturn -EOPNOTSUPP;", "\tdefault:\n\t\treturn 0;"),
    )
    results = []
    with tempfile.TemporaryDirectory(prefix="ccmni-private-") as directory:
        for name, text in [("actual", handlers)] + [(name, mutate(handlers, old, new, name)) for name, old, new in mutations]:
            path = Path(directory) / name
            path.with_suffix(".c").write_text(prefix + text + cases)
            compile_result = subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-but-set-variable",
                "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined", str(path.with_suffix(".c")), "-o", str(path)
            ], capture_output=True, text=True, timeout=30)
            if compile_result.returncode:
                raise RuntimeError("Compilation failed: " + name + "\n" + compile_result.stderr)
            result = subprocess.run([str(path)], capture_output=True, text=True, timeout=10)
            if name == "actual":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end="")
            elif not result.returncode or not re.search(r"Assertion failed:|runtime error:|ERROR: AddressSanitizer", result.stderr):
                raise RuntimeError("Mutation did not fail at runtime: " + name + "\n" + result.stdout + result.stderr)
            else:
                print("PASS rejected " + name)
            results.append({"name": name, "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
    evidence = {
        "source_sha256": {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sources.items()},
        "fixture_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__).resolve(), *fixture.glob("*")]},
        "results": results,
        "scope": "Extracted unmodified ACK/CCMNI bodies and header layouts; external user-copy, netdevice and work completion boundaries simulated. No device, native build, concurrent teardown, modem or energy acceptance.",
    }
    if args.evidence:
        args.evidence.write_text(json.dumps(evidence, indent=2) + "\n")
    print("PASS five command groups and " + str(len(mutations)) + " executed mutation controls")


if __name__ == "__main__":
    main()
