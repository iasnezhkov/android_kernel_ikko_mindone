#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Static module check BEFORE loading (rule: compute it, don't try it).

Checks numerically:
  1. vermagic -- does it match the kernel;
  2. module_layout CRC -- the kernel's binary ABI agreement;
  3. GOT relocations -- the arm64 module loader does not support them, must be 0;
  4. unresolved symbols -- with WHO provides them (kernel, a module in the set, or NOBODY).

MINDONE: the tool checks itself on a known-good input: if `_printk` is not found in
Module.symvers, the parse is considered broken and the run aborts (empty output has
twice in this project meant a broken tool, not the absence of what was searched for).

Usage:
  python3 kocheck.py <module.ko> <Module.symvers> <directory-with-the-.ko-set>
"""
import subprocess, struct, sys, os, glob, re
TMP = f"/tmp/_kocheck_{os.getpid()}"   # MINDONE: unique per process: build-all runs kocheck across 8 threads, a shared /tmp/_v.bin produced garbage

TOOLCHAIN = os.environ.get("TOOLCHAIN", "/opt/toolchains/llvm-19.1.4-aarch64")
NM = f"{TOOLCHAIN}/bin/llvm-nm"
OBJDUMP = f"{TOOLCHAIN}/bin/llvm-objdump"
OBJCOPY = f"{TOOLCHAIN}/bin/llvm-objcopy"
REF_LAYOUT = 0xf4d8bdf7

def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout

def syms(ko):
    # MINDONE F3038 (29.08): a symbol's provider is ONLY what the module EXPORTS (an
    # __ksymtab_<name> entry), not everything it defines in symtab (defined != exported).
    # Duplicate exports are caught below.
    # (mtk_irtx_pwm: it exports mtk_pwm symbols, CRC/license match, yet insmod returns
    # -ENOENT -- cause OPEN, F3038; in first-stage init any insmod failure is a bootloop.)
    out = run(NM, ko)
    undef, exported = set(), set()
    for l in out.splitlines():
        p = l.split()
        if len(p) >= 2 and p[-2] == "U":
            undef.add(p[-1])
        elif len(p) >= 3 and p[-1].startswith("__ksymtab_"):
            exported.add(p[-1][len("__ksymtab_"):])
    return undef, exported

def export_crcs(ko):
    # CRCs of the provider module's exports. On arm64 (REL CRCs) the symbol __crc_<name>
    # is an OFFSET within ITS OWN section (__kcrctab for EXPORT_SYMBOL, __kcrctab_gpl for
    # EXPORT_SYMBOL_GPL), a u32 array; we read the u32 at that offset from whichever
    # section the symbol is defined in (objdump -t gives the section name). Before 30.08
    # we only read __kcrctab -- every GPL export produced a false mismatch (TODO from
    # 29.08 closed, F3116).
    secs = {}
    for sec in ("__kcrctab", "__kcrctab_gpl"):
        tmp = f"{TMP}_kcrc_{sec}.bin"
        if os.path.exists(tmp): os.remove(tmp)
        subprocess.run([OBJCOPY, "-O", "binary", f"--only-section={sec}", ko, tmp], capture_output=True)
        secs[sec] = open(tmp, "rb").read() if os.path.exists(tmp) else b""
        if os.path.exists(tmp): os.remove(tmp)
    res = {}
    for l in run(OBJDUMP, "-t", ko).splitlines():
        p = l.split()
        if len(p) >= 5 and p[-1].startswith("__crc_"):
            try: off = int(p[0], 16)
            except ValueError: continue
            name = p[-1][len("__crc_"):]
            sec = p[-3]
            tab = secs.get(sec, b"")
            if tab and off + 4 <= len(tab):
                res[name] = struct.unpack("<I", tab[off:off+4])[0]
            elif sec == "*ABS*":
                res[name] = off & 0xffffffff   # absolute CRC (a non-REL build)
    return res

def versions(ko):
    tmp = f"{TMP}_v.bin"
    subprocess.run([OBJCOPY, "-O", "binary", "--only-section=__versions", ko, tmp],
                   capture_output=True)
    if not os.path.exists(tmp): return {}
    d = open(tmp, "rb").read(); os.remove(tmp)
    res = {}
    for off in range(0, len(d) - 63, 64):
        crc = struct.unpack("<Q", d[off:off+8])[0] & 0xffffffff
        name = d[off+8:off+64].split(b"\x00")[0].decode(errors="replace")
        if name: res[name] = crc
    return res

def vermagic_of(path):
    try:
        out = subprocess.run(["strings", path], capture_output=True, text=True, errors="replace").stdout
    except FileNotFoundError:
        return None
    m = re.search(r"vermagic=(\S+(?: \S+)*)", out)
    return m.group(1) if m else None


def main():
    if len(sys.argv) != 4 or sys.argv[1] in ("-h", "--help"):
        print("usage: kocheck.py <module.ko> <Module.symvers> <dir with the other .ko files>\n"
              "Checks one module against the kernel ABI: vermagic, module_layout, and that every\n"
              "symbol it imports is exported by the kernel or by a module in the given directory.",
              file=sys.stderr)
        return 2
    ko, symvers, kodir = sys.argv[1], sys.argv[2], sys.argv[3]
    print(f"== {os.path.basename(ko)} ==")

    # --- self-check of the tool on a known-good input ---
    # MINDONE: Module.symvers lists both exports of the BUILTIN kernel (provider vmlinux)
    # and exports of in-tree MODULES (provider mm/zsmalloc, drivers/mmc/host/cqhci ...).
    # The latter must not be counted as "the kernel": zsmalloc.ko itself would then look
    # like a "duplicate export with the kernel", and its importers would look blocked by
    # the kernel, even though the module should be in the set (01.09, build-all #2:
    # 6 false failures).
    kernel_exports, intree_exports = {}, {}
    for l in open(symvers):
        p = l.rstrip("\n").split("\t")
        if len(p) >= 3 and p[2] != "vmlinux":
            intree_exports[p[1]] = p[2]
        elif len(p) >= 2:
            kernel_exports[p[1]] = p[0]
    if "_printk" not in kernel_exports:
        print("PARSE BROKEN: _printk not found in Module.symvers -- do not trust the result"); sys.exit(3)
    if not run(NM, ko).strip():
        print(f"PARSE BROKEN: {NM} produced no symbols"); sys.exit(3)
    print(f"   self-check passed: kernel exports {len(kernel_exports)}")
    # MINDONE: the module_layout reference is taken from the Module.symvers PASSED IN
    # (01.09): on a full kernel rebuild (CFI/BTF/level 202404, F3420) the layout changes,
    # and the gate must compare against THAT kernel, whose symvers it was given, not
    # against a hardcoded constant. Any difference from the historical 0xf4d8bdf7 is
    # printed.
    global REF_LAYOUT
    ml_symvers = kernel_exports.get("module_layout")
    if ml_symvers:
        ref = int(ml_symvers, 16)
        if ref != REF_LAYOUT:
            print(f"   module_layout reference from symvers: {hex(ref)} (historical 6.1 displayfix kernel: {hex(REF_LAYOUT)})")
        REF_LAYOUT = ref

    info = run("modinfo", ko)
    vm = [l.split(":",1)[1].strip() for l in info.splitlines() if l.startswith("vermagic")]
    print(f"1. vermagic: {vm[0] if vm else 'NONE'}")

    v = versions(ko)
    ml = v.get("module_layout")
    ok = (ml == REF_LAYOUT)
    print(f"2. module_layout: {hex(ml) if ml else 'NONE'} -- {'matches' if ok else 'MISMATCH'} (reference {hex(REF_LAYOUT)})")

    got = sum(1 for l in run(OBJDUMP, "-r", ko).splitlines() if "GOT" in l)
    print(f"3. GOT relocations: {got} -- {'OK' if got==0 else 'KERNEL WILL REJECT'}")

    undef, my_exports = syms(ko)
    providers = {}
    # MINDONE F3038: a duplicate export (the kernel or another module in the set already
    # exports the same name) -- insmod replies "exports duplicate symbol ... (owned by
    # ...)", Exec format error (mtk_tee_gpapi/tkcore).
    dup_exports = {}
    for s in my_exports:
        if s in kernel_exports: dup_exports[s] = "kernel"
    # MINDONE F2926: symbol providers are ONLY modules that are actually present in the
    # image (<directory>/modules.load), and only with the same vermagic as the module
    # being checked. Without this, stray files in the directory (including stock ones
    # built for 5.10) would satisfy symbols that are not present on the device, and the
    # gate would give a false "clean" (imgsensor_isp6s / clk-common).
    loadlist = None
    lf = os.path.join(kodir, "modules.load")
    if os.path.exists(lf):
        loadlist = set(l.strip() for l in open(lf) if l.strip())
    my_vm = vermagic_of(ko)
    skipped_foreign = []
    for other in glob.glob(os.path.join(kodir, "*.ko")):
        if loadlist is not None and os.path.basename(other) not in loadlist:
            continue
        ovm = vermagic_of(other)
        if my_vm and ovm and ovm.split()[0] != my_vm.split()[0]:
            skipped_foreign.append(os.path.basename(other)); continue
        if os.path.samefile(other, ko) if os.path.exists(other) else False: continue
        if os.path.basename(other) == os.path.basename(ko): continue  # own copy in the set -- not a duplicate
        _, d = syms(other)
        for s in d & undef:
            providers.setdefault(s, os.path.basename(other))
        for s in d & my_exports:
            dup_exports.setdefault(s, os.path.basename(other))
    # MINDONE F724/F3038: versions of symbols taken from MODULES in the set. With
    # modversions the kernel requires the importer's __versions to have a CRC entry for
    # every such symbol, and that CRC must match the provider's __crc_<name>; otherwise
    # "no symbol version for X" / "disagrees about version" -> Unknown symbol.
    ver_bad = []; ver_noentry = []
    prov_crc_cache = {}
    for s, who in providers.items():
        if s not in v:
            # the kernel FORGIVES this ("no symbol version for X", warn_once, still loads)
            # -- vcp loaded that way too; just a warning
            ver_noentry.append(s); continue
        if who not in prov_crc_cache:
            prov_crc_cache[who] = export_crcs(os.path.join(kodir, who))
        pc = prov_crc_cache[who].get(s)
        if pc is not None and pc != v[s]:
            ver_bad.append(f"{s}: importer CRC {hex(v[s])} != provider {who} {hex(pc)}")
    nobody = []
    from_kernel = from_mod = from_intree = 0
    intree_needed = set()
    for s in sorted(undef):
        if s in kernel_exports: from_kernel += 1
        elif s in providers: from_mod += 1
        elif s in intree_exports: from_intree += 1; intree_needed.add(os.path.basename(intree_exports[s]) + ".ko")
        else: nobody.append(s)
    print(f"4. symbols required {len(undef)}: kernel provides {from_kernel}, modules in set {from_mod}, "
          f"in-tree kernel modules {from_intree}{' (' + ' '.join(sorted(intree_needed)) + ')' if intree_needed else ''}, NOBODY -- {len(nobody)}")
    for s in nobody[:15]:
        print(f"     no provider: {s}")
    if loadlist is not None:
        print(f"  providers limited to the image's modules.load: {len(loadlist)} modules")
    if skipped_foreign:
        print(f"  skipped foreign vermagic in the directory: {len(skipped_foreign)} ({', '.join(skipped_foreign[:4])}...)")
    mods = sorted(set(providers.values()))
    if mods: print(f"   need to be loaded: {' '.join(mods)}")
    print(f"5. module exports {len(my_exports)}, duplicates with kernel/set -- {len(dup_exports)}"
          + ("" if not dup_exports else " insmod WILL REFUSE (Exec format error)"))
    for s, who in sorted(dup_exports.items())[:10]:
        print(f"     duplicate export: {s} (already owned by {who})")
    # 30.08 (F3116): the exporter's CRC is taken from the section of the symbol
    # __crc_<name> (__kcrctab or __kcrctab_gpl, objdump -t) -- false mismatches for
    # EXPORT_SYMBOL_GPL are gone; a mismatch is still a FAILURE (insmod will give -EINVAL
    # "disagrees about version of symbol").
    print(f"6. symbol versions from modules in the set: {len(providers)} checked, mismatches -- {len(ver_bad)}")
    for s in ver_bad[:6]:
        print(f"     {s}")
    if ver_noentry:
        print(f"   no __versions entry (kernel forgives this, warn_once): {len(ver_noentry)} -- {' '.join(sorted(ver_noentry)[:6])}")
    sys.exit(0 if (ok and got == 0 and not nobody and not dup_exports and not ver_bad) else 1)

main()
