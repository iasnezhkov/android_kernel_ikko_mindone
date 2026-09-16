#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""build-all-modules.py -- build the WHOLE module set from one place, with gates and a
manifest (PLAN-CLEANUP-AGENT.md section 4.1, Part C).

Parameters -- environment variables (the defaults are derived from where this script lives,
so a clean checkout works without setting anything):
  KERNEL_SRC  kernel tree                 the repository this script is in
  KOUT        kernel build directory (O=) <kernel-tree>/../k612-out   (Module.symvers, include/config/kernel.release)
  MODULES_DIR module tree                 <kernel-tree>/mindone/modules
  TOOLCHAIN   LLVM                        /opt/toolchains/llvm-19.1.4-aarch64
  JOBS        8
Required: --out <result directory>, and either --load <modules.load> (names and order of
the shipped set; the source directory is found by obj-m/directory name/alias/overrides)
OR --all.

Steps: 1) make M=<directory> (each directory once) 2) llvm-strip --strip-debug -> OUT/<name>
3) kocheck.py for each against KOUT/Module.symvers, providers = OUT (modules.load is copied
there) 4) extpathcheck.py + tree-lint.py (reports) 5) OUT/build-manifest.txt (kernel: release,
module_layout, git HEAD of the kernel and of the module tree, toolchain; modules: name, sha256, size, kocheck,
directory) + OUT/build-summary.json.

Reproducibility (criterion 5.6 "two runs = the same sha"): build-env.sh (timestamp/.version)
+ KCFLAGS -fmacro-prefix-map/-fdebug-prefix-map (build paths do not leak into .rodata, F3421).

The module_layout reference is NOT hardcoded: kocheck takes it from KOUT/Module.symvers
(F3420: after a full rebuild with CFI/BTF the layout differs -- the gate compares against
THAT kernel, the one it was built against).

Run (re-execs itself in the VM from macOS):
  python3 tools/scripts/build-all-modules.py --out <work-dir><date> \
      --load <work-dir>/shipped-modules/modules.load [--no-kocheck] [--only a,b]
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

# NOTE (published copy): the original mind_one lab copy of this script re-execs itself inside
# a separate Linux VM when run from macOS. That hack is intentionally removed
# here — this script needs a real Linux host (aarch64 or x86_64) with the toolchain below; run
# it directly on/inside whatever Linux environment you use to build the kernel.

REPO_SELF = Path(__file__).resolve().parent.parent.parent  # tools/scripts/../.. = the module treemods checkout
# Defaults come from the checkout itself rather than from any particular machine's layout:
# REPO_SELF is <kernel tree>/mindone/modules, so its grandparent is the kernel tree, and the
# build directory defaults to the ../k612-out that docs/BUILD.md uses.
_KERNEL_TREE = REPO_SELF.parent.parent
KERNEL_SRC = Path(os.environ.get('KERNEL_SRC', str(_KERNEL_TREE)))
KOUT = Path(os.environ.get('KOUT', str(_KERNEL_TREE.parent / 'k612-out')))
MODULES_DIR = Path(os.environ.get('MODULES_DIR', os.environ.get('MODULES_DIR', str(REPO_SELF))))
TOOLCHAIN = os.environ.get('TOOLCHAIN', '/opt/toolchains/llvm-19.1.4-aarch64')
JOBS = int(os.environ.get('JOBS', '8'))
SCRIPT_DIR = Path(__file__).resolve().parent

# KERNEL_SRC and KOUT MUST be the same kernel, and this is checked rather than assumed.
#    Pointing one at a tree of one version and the other at a build directory of another is
#    an easy mistake when two kernel versions are in play, and it produced dozens of failures
#    like "fatal error: 'asm/cpucaps.h' file not found" here. That is the BEST case: the worst
#    is a module that builds against the wrong headers and ends up in the set unnoticed.
def _assert_same_kernel():
    rel_f = KOUT / 'include' / 'config' / 'kernel.release'
    mk = KERNEL_SRC / 'Makefile'
    if not rel_f.exists() or not mk.exists():
        return
    rel = rel_f.read_text().strip()
    ver = {}
    for line in mk.read_text(errors='replace').splitlines()[:10]:
        for key in ('VERSION', 'PATCHLEVEL'):
            if line.startswith(key):
                ver[key] = line.split('=', 1)[1].strip()
    if 'VERSION' in ver and 'PATCHLEVEL' in ver:
        want = f"{ver['VERSION']}.{ver['PATCHLEVEL']}."
        if not rel.startswith(want):
            raise SystemExit(
                f"!! KERNEL_SRC and KOUT are DIFFERENT kernels: tree {KERNEL_SRC} gives {want}x, "
                f"while KOUT {KOUT} was built as {rel}.\n"
                f"   Set both variables, e.g.: KERNEL_SRC=<kernel-tree> KOUT=<kernel-out>")

_assert_same_kernel()
REPO_ROOT = SCRIPT_DIR.parent.parent
OVERRIDES = REPO_ROOT / 'firmware' / 'k6-module-overrides.txt'
ALIASES = {'haptic_nv.ko': 'haptic_nv'}          # documented renames (tree-audit.py)
KNOWN_UPSTREAM = {'rfkill.ko', 'libarc4.ko'}       # built by the kernel, not out of tree


def norm(name):
    """Canonical module name -- with UNDERSCORES (the project's single format,
    tools/scripts/modreg.py).

    MINDONE: before 07.09 normalization went the other way, to hyphens. For matching
    purposes this made no difference (both sides were normalized the same way), but in
    --all mode the normalized key name became the OUTPUT FILE NAME -- and the set would
    land on disk as `clk-mt6789-mmsys.ko`, while the load lists and the kernel itself call
    this module `clk_mt6789_mmsys`. Any comparison of such a set against the load list
    produced a false gap: on 07.09 we got estimates of "167 missing", "77" and "11" in a
    row when the true number was 11 -- the difference was ONLY in spelling.
    """
    return name.replace('-', '_')


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True, **kw)


def ko_index():
    """{canonical .ko name: [directory, ...]} from obj-m/obj-y of every Makefile (like
    tree-audit.py).

    MINDONE: simple assignments are expanded. Six connectivity modules (wmt_drv,
    wmt_chrdev_wifi, wlan_drv_gen4m_6789, bt_drv_connac1x, gps_drv_stp,
    fmradio_drv_mt6631_6635) are declared as `obj-m += $(MODULE_NAME).o`. The old parser
    simply dropped tokens containing "$", so in --all mode they never made it into the
    list at all -- and the 6.12 set silently built without connectivity, which looked like
    "the module is not needed for boot" (07.09).
    """
    idx = {}
    var = re.compile(r'^\s*([A-Za-z_]\w*)\s*[:?]?=\s*(\S+)\s*$', flags=re.MULTILINE)
    for mk in sorted(MODULES_DIR.glob('*/Makefile')):
        flat = re.sub(r'[ \t]*\\\r?\n[ \t]*', ' ', mk.read_text(errors='replace'))
        env = {m.group(1): m.group(2) for m in var.finditer(flat)}
        # MINDONE: one pass is not enough. The Wi-Fi driver declares
        #   WLAN_CHIP_ID := 6789
        #   MODULE_NAME  := wlan_drv_gen4m_$(WLAN_CHIP_ID)
        # so MODULE_NAME still contains a "$" after the first pass and used to be discarded
        # with it -- the whole directory then never entered the --all list, and a set built
        # from a clean clone silently had no Wi-Fi driver at all (found 16.09 by building the
        # published snapshot end to end). Resolve variables against each other until stable,
        # then drop whatever is still unresolved.
        for _ in range(4):
            progressed = False
            for k, v in list(env.items()):
                if '$' not in v:
                    continue
                nv = v
                for n2, v2 in env.items():
                    if '$' in v2:
                        continue
                    nv = nv.replace(f'$({n2})', v2).replace(f'${{{n2}}}', v2)
                if nv != v:
                    env[k] = nv
                    progressed = True
            if not progressed:
                break
        env = {k: v for k, v in env.items() if '$' not in v}
        for m in re.finditer(r'^\s*obj-[my]\s*[:+]?=\s*(.+)$', flat, flags=re.MULTILINE):   # +=, :=, =
            for tok in m.group(1).split():
                for name, val in env.items():
                    tok = tok.replace(f'$({name})', val).replace(f'${{{name}}}', val)
                if tok.endswith('.o') and '$' not in tok and '/' not in tok:
                    idx.setdefault(norm(tok[:-2] + '.ko'), []).append(mk.parent.name)
    return {k: sorted(set(v)) for k, v in idx.items()}


def resolve_dirs(ko, idx, overrides):
    """-> [(dir, how), ...] in preference order (see the comment in main)."""
    cands = []
    if ko in overrides:
        p = Path(overrides[ko])
        cands.append((p.parent.name if p.suffix == '.ko' else p.name, 'override'))
    if ko in ALIASES:
        cands.append((ALIASES[ko], 'alias'))
    obj = idx.get(norm(ko), [])
    same = [c for c in obj if norm(c + '.ko') == norm(ko)]
    cands += [(c, 'obj-m') for c in same] + [(c, 'obj-m') for c in obj if c not in same]
    for d in sorted(MODULES_DIR.iterdir()):
        if d.is_dir() and (d / 'Makefile').is_file() and norm(d.name + '.ko') == norm(ko):
            cands.append((d.name, 'directory name'))
    seen, out = set(), []
    for d, how in cands:
        if d not in seen and (MODULES_DIR / d / 'Makefile').is_file():
            seen.add(d); out.append((d, how))
    return out


def build_dir(d, logdir):
    env = dict(os.environ)
    env['PATH'] = f'{TOOLCHAIN}/bin:' + env.get('PATH', '')
    env.setdefault('KBUILD_BUILD_TIMESTAMP', 'Thu Jan  1 00:00:00 UTC 1970')
    env.setdefault('KBUILD_BUILD_VERSION', '1')
    env['KCFLAGS'] = (env.get('KCFLAGS', '') +
                      f' -fmacro-prefix-map={MODULES_DIR}/= -fdebug-prefix-map={MODULES_DIR}/= -fmacro-prefix-map={KERNEL_SRC}/=').strip()
    # MINDONE 04.09: this is a SHARED source tree for 6.1 and 6.12, and object files sit
    #    directly inside the module directories. So building the set against a different
    #    kernel reuses the previous .o: make sees the source has not changed and does not
    #    rebuild. The result is a module with the PREVIOUS kernel's struct layout and the
    #    NEW kernel's vermagic -- and no gate catches this, because the vermagic is
    #    correct. On 6.12 this put garbage into platform_driver's probe field (the struct
    #    is one field shorter on 6.12 than on 6.1) and caused a crash at a garbage
    #    address. So: remember which kernel a directory was built for, and clean it fully
    #    on a switch.
    _dir = MODULES_DIR / d
    _stamp = _dir / '.mindone-built-for'
    _want = (KOUT / 'include' / 'config' / 'kernel.release').read_text().strip() \
        if (KOUT / 'include' / 'config' / 'kernel.release').exists() else ''
    if _want and (not _stamp.exists() or _stamp.read_text().strip() != _want):
        for _pat in ('*.o', '*.ko', '.*.cmd', '*.mod', '*.mod.c', 'Module.symvers', 'modules.order'):
            for _f in _dir.glob(_pat):
                try:
                    _f.unlink()
                except OSError:
                    pass
        try:
            _stamp.write_text(_want + "\n")
        except OSError:
            pass

    cmd = ['make', '-C', str(KERNEL_SRC), f'O={KOUT}', f'M={MODULES_DIR / d}', 'ARCH=arm64', 'LLVM=1',
           'LLVM_IAS=1', 'CROSS_COMPILE=aarch64-linux-gnu-', 'KBUILD_MODPOST_WARN=1', f'-j{JOBS}', 'modules']
    with open(logdir / f'build-{d}.log', 'w') as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode


def find_ko(d, ko):
    hits = [p for p in (MODULES_DIR / d).rglob('*.ko') if norm(p.name) == norm(ko)]
    if not hits:
        # the directory builds a .ko under a DIFFERENT name (haptic_nv -> aw_haptic_nv.ko,
        # haptic_nv.ko in the image): if there is exactly one .ko, that is it
        allko = list((MODULES_DIR / d).rglob('*.ko'))
        if len(allko) == 1:
            hits = allko
    return hits[0] if hits else None


def kocheck(ko_path, symvers, kodir):
    r = sh(['python3', str(SCRIPT_DIR / 'kocheck.py'), str(ko_path), str(symvers), str(kodir)])
    out = r.stdout
    ml = re.search(r'module_layout: (0x[0-9a-f]+)', out)
    nobody = re.search(r'NOBODY -- (\d+)', out)
    return r.returncode, (ml.group(1) if ml else '?'), (int(nobody.group(1)) if nobody else -1), out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--load', help='modules.load of the shipped set (names+order)')
    ap.add_argument('--all', action='store_true', help='every module directory with a Makefile')
    ap.add_argument('--only', help='restrict the list to these .ko names (comma-separated)')
    ap.add_argument('--no-kocheck', action='store_true')
    ap.add_argument('--prefer-intree', default='', help='.ko names for which to take the in-tree KOUT module instead of the out-of-tree directory (6.12: zram, zsmalloc, iio)')
    ap.add_argument('--no-strip', action='store_true')
    args = ap.parse_args()

    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    logdir = out / 'logs'; logdir.mkdir(exist_ok=True)
    symvers = KOUT / 'Module.symvers'
    if not symvers.is_file():
        print(f'!! {symvers} not found -- the kernel in KOUT is not built', file=sys.stderr); return 2
    krel = (KOUT / 'include' / 'config' / 'kernel.release').read_text().strip() if (KOUT / 'include/config/kernel.release').is_file() else '?'
    layout = next((l.split('\t')[0] for l in symvers.read_text().splitlines() if '\tmodule_layout\t' in l), '?')
    idx = ko_index()
    overrides = {}
    if OVERRIDES.is_file():
        for l in OVERRIDES.read_text().splitlines():
            p = l.split()
            if len(p) >= 2 and not l.startswith('#'):
                overrides[p[0]] = p[1]

    if args.load:
        names = [l.strip() for l in Path(args.load).read_text().splitlines() if l.strip()]
        shutil.copy2(args.load, out / 'modules.load')
    elif args.all:
        names = []
        for d in sorted(MODULES_DIR.iterdir()):
            if d.is_dir() and (d / 'Makefile').is_file():
                for k, dirs in idx.items():
                    if d.name in dirs and k not in names:
                        names.append(k)
    else:
        ap.error('need --load or --all')
    if args.only:
        keep = {n for n in args.only.split(',')}
        names = [n for n in names if n in keep or norm(n) in {norm(k) for k in keep}]

    plan = {}
    for ko in names:
        if ko in KNOWN_UPSTREAM:
            plan[ko] = []; continue
        plan[ko] = resolve_dirs(ko, idx, overrides)
    dirs = sorted({d for c in plan.values() for d, _ in c})
    print(f'== kernel {krel}  module_layout {layout}  modules {len(names)}  candidate directories {len(dirs)} ==')

    # MINDONE: a single .ko often has SEVERAL candidate directories (obj-m with the same
    # name in parallel variants, or a directory matching by name) -- and the first one
    # alphabetically is sometimes dead/unbuildable (AUDIT-TREE-0901: 23 "failed to build"
    # cases with a working sibling present). So candidates are tried IN ORDER
    # (override -> alias -> obj-m with a matching directory name -> other obj-m ->
    # directory by name); the first one that builds and produces a .ko wins. If none do,
    # we look for an in-tree kernel module in KOUT (cfg80211/mac80211 etc. are built by
    # the kernel, not out of tree).
    t0 = time.time()
    built = {}       # dir -> rc
    rows = []
    prefer_intree = {norm(x.strip()) for x in args.prefer_intree.split(',') if x.strip()}
    for i, ko in enumerate(names, 1):
        cands = [] if norm(ko) in prefer_intree else plan[ko]
        row = {'ko': ko, 'dir': None, 'tried': []}
        src = None
        for d, how in cands:
            if d not in built:
                built[d] = build_dir(d, logdir)
            row['tried'].append(f'{d}({how}:{"ok" if built[d] == 0 else "fail"})')
            if built[d] == 0:
                src = find_ko(d, ko)
                if src:
                    row['dir'] = d; row['how'] = how
                    break
        if not src:
            intree = [p for p in KOUT.rglob('*.ko') if norm(p.name) == norm(ko)]
            if intree:
                src = intree[0]; row['dir'] = f'KOUT:{src.relative_to(KOUT)}'; row['how'] = 'in-tree'
        if not src:
            row['status'] = 'no_source' if not cands else 'build_fail'
            rows.append(row)
            print(f'[{i}/{len(names)}] {ko}: {row["status"]}  ({", ".join(row["tried"]) or "no candidates"})', flush=True)
            continue
        dst = out / ko
        if args.no_strip:
            shutil.copyfile(src, dst)
        else:
            sh([f'{TOOLCHAIN}/bin/llvm-strip', '--strip-debug', str(src), '-o', str(dst)])
        row.update({'status': 'built', 'size': dst.stat().st_size,
                    'sha256': hashlib.sha256(dst.read_bytes()).hexdigest()})
        # F3558/F3562: pointer<->int and incompatible-pointer warnings are API mismatches that the module
        # Makefiles silence with -Wno-error; they cost two days (rq passed to cpu_util_cfs(int), EM callback
        # in 5.10 argument order). Surface them per module and count them as a gate failure.
        api_warn = []
        if row.get('how') != 'in-tree':
            logf = logdir / f'build-{row["dir"]}.log'
            if logf.exists():
                api_warn = sorted(set(re.findall(
                    r'^(\S+:\d+:\d+): warning: .*\[-W(?:int-conversion|incompatible-pointer-types|'
                    r'incompatible-function-pointer-types|implicit-function-declaration|int-to-pointer-cast|'
                    r'pointer-to-int-cast)\]', logf.read_text(errors='replace'), re.M)))
        row['api_warn'] = api_warn
        rows.append(row)
        print(f'[{i}/{len(names)}] {ko}: ok <- {row["dir"]}' + (f'  api-warn {len(api_warn)}: {api_warn[0]}' if api_warn else ''), flush=True)
    print(f'build: {time.time() - t0:.0f} s')

    if not args.no_kocheck:
        t1 = time.time()
        def chk(row):
            rc, ml, nobody, txt = kocheck(out / row['ko'], symvers, out)
            (logdir / f'kocheck-{row["ko"]}.txt').write_text(txt)
            row.update({'kocheck_rc': rc, 'module_layout': ml, 'nobody': nobody})
        with ThreadPoolExecutor(max_workers=JOBS) as ex:
            list(ex.map(chk, [r for r in rows if r['status'] == 'built']))
        print(f'kocheck: {time.time() - t1:.0f} s')

    gates = {}
    # 🔴 both gates default to this repository's own paths
    # with no arguments — pass THIS run's actual MODULES_DIR/KERNEL_SRC explicitly, or a build from
    # any other checkout (e.g. build/build.sh from a fresh clone) would silently gate the
    # production tree instead of the one it just built.
    gate_args = {'extpathcheck.py': ['--modules', str(MODULES_DIR)],
                 'tree-lint.py': ['--modules', str(MODULES_DIR), '--kernel', str(KERNEL_SRC)]}
    for name in ('extpathcheck.py', 'tree-lint.py'):
        r = sh(['python3', str(SCRIPT_DIR / name)] + gate_args[name])
        gates[name] = {'rc': r.returncode, 'tail': r.stdout.strip().splitlines()[-3:]}

    head_k = sh(['git', '-C', str(KERNEL_SRC), 'rev-parse', '--short', 'HEAD']).stdout.strip()
    head_m = sh(['git', '-C', str(MODULES_DIR), 'rev-parse', '--short', 'HEAD']).stdout.strip()
    cc = sh([f'{TOOLCHAIN}/bin/clang', '--version']).stdout.splitlines()[0] if Path(f'{TOOLCHAIN}/bin/clang').exists() else '?'
    with open(out / 'build-manifest.txt', 'w') as f:
        f.write(f'# build-all-modules  {time.strftime("%Y-%m-%d %H:%M")}\n'
                f'# kernel.release {krel}\n# module_layout {layout}\n# kernel {KERNEL_SRC} @ {head_k}\n'
                f'# modules {MODULES_DIR} @ {head_m}\n# toolchain {cc}\n# KOUT {KOUT}\n'
                f'# ko\tsha256\tsize\tkocheck_rc\tmodule_layout\tnobody\tdir\tstatus\n')
        for r in rows:
            f.write('\t'.join(str(r.get(k, '')) for k in ('ko', 'sha256', 'size', 'kocheck_rc', 'module_layout',
                                                          'nobody', 'dir', 'status')) + '\n')
    (out / 'build-summary.json').write_text(json.dumps({'kernel': krel, 'module_layout': layout,
                                                        'rows': rows, 'gates': gates}, ensure_ascii=False, indent=1))
    n_built = sum(1 for r in rows if r['status'] == 'built')
    n_fail = [r['ko'] for r in rows if r['status'] == 'build_fail']
    n_nosrc = [r['ko'] for r in rows if r['status'] == 'no_source']
    n_kobad = [r['ko'] for r in rows if r.get('kocheck_rc') not in (None, 0)]
    n_api = [r['ko'] for r in rows if r.get('api_warn')]
    print(f'\n== SUMMARY ==  built {n_built}/{len(rows)}  build failures {len(n_fail)}  no source {len(n_nosrc)}  '
          f'kocheck failures {len(n_kobad)}  api warnings {len(n_api)}')
    if n_api:
        print('  api-warn (pointer<->int / incompatible pointers -- F3558/F3562, gate): ' + ' '.join(n_api))
        for r in rows:
            for w in r.get('api_warn', []):
                print(f'     {r["ko"]}: {w}')
    for k, v in gates.items():
        print(f'  {k}: rc={v["rc"]}  {v["tail"][-1] if v["tail"] else ""}')
    if n_fail:
        print('  build failed: ' + ' '.join(n_fail))
    if n_nosrc:
        print('  no source: ' + ' '.join(n_nosrc))
    if n_kobad:
        print('  kocheck: ' + ' '.join(n_kobad))
    print(f'  manifest: {out / "build-manifest.txt"}')
    return 0 if not (n_fail or n_kobad or n_api) else 1


if __name__ == '__main__':
    sys.exit(main())
