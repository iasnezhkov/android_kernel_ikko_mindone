#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""tree-lint.py -- source-tree cleanliness gate for a published tree.

Three rules:
  1. No references to source trees outside this repository: neither the names of the
     devices modules were ported from, nor absolute home paths (/home/<user>/... and
     the macOS equivalent). A published tree should read as its own material, not as a
     record of where each file was collected.
  2. No Cyrillic in code (.c/.h/.S/Makefile/Kbuild/Kconfig/.dts*) -- English only.
  3. Our comments are few and to the point: a comment block with a MINDONE marker longer
     than MAX_MINDONE_LINES lines is a violation (rewrite it briefly or move it to docs/).

Run (host or VM, read-only):
  python3 tools/scripts/tree-lint.py [--modules DIR] [--kernel DIR] [--json OUT] [--fail]
  --fail  -> rc=1 on any violation (final-gate mode); without it -- report + rc=0.
  --list <rule> -> list files by rule (words|cyrillic|comments).

The kernel tree is NOT scanned in full (upstream ACK legitimately contains "external-tree" --
ext4 move_extent, etc.), only OUR files: those that mention mindone/MINDONE, plus files
from the bring-up commits (git diff <first commit>..HEAD).
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

CODE_EXT = {'.c', '.h', '.S', '.s', '.dts', '.dtsi', '.rs', '.ld', '.lds', '.inc'}
CODE_NAMES = {'Makefile', 'Kbuild', 'Kconfig'}
# Forbidden words (case-insensitive, on a word boundary where that makes sense).
# Generic rules only. Any Russian word -- including the one that names this class of
# problem -- is already caught by the Cyrillic rule below, so nothing is lost by not
# spelling it out here.
FORBIDDEN = [
    (r'\bdonors?\b', 'external-tree-reference'),
    (r'/(?:home|Users)/[a-z0-9_]+/', 'home-path'),
]

# Extra patterns can be added without putting them in the repository: one regex per line in
# extra-forbidden.txt next to this script, '#' for comments. Useful for names that must never
# appear in the tree but have no business being published in the checker either.
_extra = Path(__file__).with_name('extra-forbidden.txt')
if _extra.exists():
    for _l in _extra.read_text(encoding='utf-8').splitlines():
        _l = _l.strip()
        if _l and not _l.startswith('#'):
            FORBIDDEN.append((_l, 'extra'))
# Written as escapes rather than literal characters so this file passes its own check.
CYRILLIC = re.compile('[\u0400-\u04ff]')
MAX_MINDONE_LINES = 8
SKIP_DIRS = {'.git', '.tmp_versions', 'vendor-headers.bak'}


def is_code(p: Path):
    return p.suffix in CODE_EXT or p.name in CODE_NAMES


def iter_files(root: Path):
    for dp, dns, fns in os.walk(root):
        dns[:] = [d for d in dns if d not in SKIP_DIRS]
        for fn in fns:
            p = Path(dp) / fn
            if is_code(p) and not p.is_symlink():
                yield p


def kernel_our_files(kernel: Path):
    """Our files in the kernel tree: mindone marker OR from bring-up commits on top of base."""
    files = set()
    try:
        out = subprocess.run(['git', '-C', str(kernel), 'grep', '-Il', '-i', 'mindone', '--',
                              '*.c', '*.h', '*.S', 'Makefile', 'Kbuild', 'Kconfig', '*.dts', '*.dtsi'],
                             capture_output=True, text=True).stdout
        files.update(l.strip() for l in out.splitlines() if l.strip())
        first = subprocess.run(['git', '-C', str(kernel), 'rev-list', '--max-parents=0', 'HEAD'],
                               capture_output=True, text=True).stdout.split()
        if first:
            out = subprocess.run(['git', '-C', str(kernel), 'diff', '--name-only', first[0], 'HEAD'],
                                 capture_output=True, text=True).stdout
            files.update(l.strip() for l in out.splitlines() if l.strip())
    except OSError:
        pass
    return [kernel / f for f in sorted(files) if (kernel / f).is_file() and is_code(kernel / f)]


def mindone_comment_blocks(text):
    """Lengths of comment blocks (/* ... */ or consecutive //) containing MINDONE/mindone."""
    blocks = []
    for m in re.finditer(r'/\*.*?\*/', text, flags=re.S):
        if re.search(r'mindone', m.group(0), flags=re.I):
            blocks.append(m.group(0).count('\n') + 1)
    run = 0
    tagged = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('//') or s.startswith('#') and 'MINDONE' in s and not s.startswith('#include'):
            run += 1
            tagged = tagged or bool(re.search(r'mindone', s, flags=re.I))
        else:
            if run and tagged:
                blocks.append(run)
            run = 0
            tagged = False
    if run and tagged:
        blocks.append(run)
    return blocks


BACKUP_RE = re.compile(r'\.(bak[\w-]*|[\w-]*orig[\w-]*|OFF|rej|old|save|drift-bak|pre-[\w-]+|fixed|v1clean_working|KEEP-\d+)$')
# A source file with anything appended after its real extension is a kept-aside variant.
# mtk_drm_crtc.c.with-f2744 sat in the tree this way: a superseded copy of a driver, 410
# lines behind the real one, indistinguishable from an alternative you are meant to choose.
VARIANT_RE = re.compile(r'\.(c|h|S|dts|dtsi|py|sh|mk)\.[\w.-]+$')


def backup_files(root: Path):
    """Rule 4: orphan backup copies (.bak/.orig/.OFF/.rej/.old) and kept-aside variants
    (name.c.something) in the tree -- history lives
    in git, they have no place in the code (AUDIT-TREE-0901: 46 such files in the module tree;
    kernel: *.orig-restart-probe)."""
    out = []
    for dp, dns, fns in os.walk(root):
        dns[:] = [d for d in dns if d not in SKIP_DIRS]
        out += [str(Path(dp, f).relative_to(root)) for f in fns
                if BACKUP_RE.search(f) or VARIANT_RE.search(f)]
    return sorted(out)


def scan(files, rel_root):
    res = {'words': {}, 'cyrillic': {}, 'comments': {}, 'encoding': {}, 'crlf': {}, 'implicit': {}}
    for p in files:
        try:
            raw = p.read_bytes()
        except OSError:
            continue
        if p.name == 'Makefile' and b'-Wno-error=implicit-function-declaration' in raw:
            res['implicit'][str(p.relative_to(rel_root)) if rel_root in p.parents else str(p)] = 1
        if b'\r\n' in raw:
            res['crlf'][str(p.relative_to(rel_root)) if rel_root in p.parents else str(p)] = raw.count(b'\r\n')
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError as e:
            text = raw.decode('utf-8', errors='replace')
            res['encoding'][str(p.relative_to(rel_root)) if rel_root in p.parents else str(p)] = e.start
        rel = str(p.relative_to(rel_root)) if rel_root in p.parents else str(p)
        hits = {}
        for pat, tag in FORBIDDEN:
            n = len(re.findall(pat, text, flags=re.I))
            if n:
                hits[tag] = n
        if hits:
            res['words'][rel] = hits
        cy = sum(1 for l in text.splitlines() if CYRILLIC.search(l))
        if cy:
            res['cyrillic'][rel] = cy
        long_blocks = [n for n in mindone_comment_blocks(text) if n > MAX_MINDONE_LINES]
        if long_blocks:
            res['comments'][rel] = long_blocks
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modules',
                    default=str(Path(__file__).resolve().parent.parent.parent),
                    help='module tree to check (default: the one this script lives in)')
    ap.add_argument('--kernel', default=None,
                    help='optional second kernel tree to check as well')
    ap.add_argument('--json')
    ap.add_argument('--fail', action='store_true')
    ap.add_argument('--list', choices=['words', 'cyrillic', 'comments', 'encoding', 'crlf', 'implicit'])
    ap.add_argument('--no-kernel', action='store_true')
    ap.add_argument('--only-kernel', action='store_true')
    args = ap.parse_args()

    k6 = Path(args.modules)
    report = {}
    if k6.is_dir() and not args.only_kernel:
        report['modules'] = scan(iter_files(k6), k6)
    kr = Path(args.kernel) if args.kernel else None
    if not args.no_kernel and kr is not None and kr.is_dir():
        report['kernel'] = scan(kernel_our_files(kr), kr)

    roots = {'modules': k6, 'kernel': kr}
    total = 0
    for tree, res in report.items():
        res['backups'] = backup_files(roots[tree])
        w, c, m = res['words'], res['cyrillic'], res['comments']
        # split words: vendor-headers banner (mechanically cleared) vs everything else
        banner_only = [f for f in w if '/vendor-headers/' in f]
        other = [f for f in w if '/vendor-headers/' not in f]
        tags = {}
        for f, hits in w.items():
            for t, n in hits.items():
                tags[t] = tags.get(t, 0) + n
        print(f'== {tree} ==')
        print(f'  1. forbidden words: files {len(w)} '
              f'(in vendor-headers/ {len(banner_only)}, outside -- {len(other)}); by tag: '
              + ', '.join(f'{t}={n}' for t, n in sorted(tags.items())))
        print(f'  2. Cyrillic in code: files {len(c)}, lines {sum(c.values())}')
        print(f'  3. long MINDONE comments (>{MAX_MINDONE_LINES} lines): files {len(m)}, '
              f'blocks {sum(len(v) for v in m.values())}')
        print(f'  4. orphan backups and kept-aside variants: {len(res["backups"])}')
        print(f'  5. files not in UTF-8 (GBK/cp1252 in comments): {len(res["encoding"])}')
        print(f'  6. CRLF line endings: files {len(res["crlf"])}')
        print(f'  7. Makefiles with -Wno-error=implicit-function-declaration (hides unresolved imports, F3442; report only, not counted in total): {len(res["implicit"])}')
        # Rule 3 (long MINDONE comments) is a metric, not a violation: a comment that explains
        # why a vendor driver had to change is the point of this tree, and gating on its
        # length would push people to write less of it. Reported, not counted.
        total += len(w) + len(c) + len(res['backups']) + len(res['encoding']) + len(res['crlf'])
        if args.list:
            items = res[args.list]
            for f in sorted(items):
                print(f'     {f}  {items[f] if isinstance(items, dict) else ""}')
    if args.json:
        Path(args.json).write_text(json.dumps(report, ensure_ascii=False, indent=1))
    if args.fail and total:
        print(f'GATE FAILED: {total} files with violations')
        return 1
    print('clean' if not total else f'(report; add --fail for gate mode)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
