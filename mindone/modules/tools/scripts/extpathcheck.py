#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Gate against external-tree-tree contamination in the build (owner's policy, 01.09.2026,
PLAN-A16-LOS23 section 4.6).

Rule: no NEW or rebuilt module may gain a new `-I`/source path pointing at
`a path outside this repository`. The existing debt (253 modules as of 01.09.2026) is known, named,
recorded in tools/external-tree-debt-baseline.txt, and must MONOTONICALLY NOT GROW. Whatever is
needed from a external-tree gets copied into OUR tree with a provenance banner (example:
mindone/modules/musb_hdrc_recon/vendor-headers/).

Run (host or VM, only needs access to the module tree):
  python3 tools/scripts/extpathcheck.py        # defaults to the tree it lives in

Return code: 0 -- no new offenders (debt <= baseline);
             1 -- NEW external-tree paths appeared (list is printed) -- gate failed;
             2 -- environment error (tree/baseline missing).

Clearing a module's debt: once its paths are cleaned up, rerun with --rebaseline; the
script rewrites the baseline ONLY if the debt has decreased (growing the baseline by
hand is not allowed).
"""
import argparse
import re
import sys
from pathlib import Path

# MINDONE: ANY occurrence of the reference-tree path, not just after -I/:=/= : on the
# night of 01.09 we found `-include <absolute path outside the tree>/mtk_sip_svc.h` (3 modules) and
# `ifeq (<absolute path outside the tree>,)` (wmt_drv), which a narrow pattern would have missed --
# the build was silently depending on ~/external trees in the VM.
# Any absolute path into a user's home: a module Makefile must never reach outside
# this repository, whatever the directory beyond it happens to be called.
PATTERN = re.compile(r'/(?:home|Users)/[^\s/]+/')


def scan(modules_dir: Path):
    offenders = set()
    for mk in sorted(modules_dir.glob('*/Makefile')):
        try:
            text = mk.read_text(errors='replace')
        except OSError as e:
            print(f'!! cannot read {mk}: {e}', file=sys.stderr)
            continue
        if PATTERN.search(text):
            offenders.add(mk.parent.name)
    return offenders


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modules',
                    default=str(Path(__file__).resolve().parent.parent.parent),
                    help='module tree to check (default: the one this script lives in)')
    ap.add_argument('--baseline',
                    default=str(Path(__file__).resolve().parent.parent
                                / 'external-tree-debt-baseline.txt'))
    ap.add_argument('--rebaseline', action='store_true',
                    help='rewrite the baseline, ONLY if the debt has decreased')
    args = ap.parse_args()

    modules_dir = Path(args.modules)
    if not modules_dir.is_dir():
        print(f'!! tree not found: {modules_dir}', file=sys.stderr)
        return 2
    baseline_path = Path(args.baseline)
    if not baseline_path.is_file():
        print(f'!! no baseline at {baseline_path} -- create it deliberately '
              f'(this records the debt, it is not a formality)', file=sys.stderr)
        return 2

    baseline = {l.strip() for l in baseline_path.read_text().splitlines()
                if l.strip()}
    now = scan(modules_dir)

    new = sorted(now - baseline)
    cleaned = sorted(baseline - now)

    print(f'external-tree debt: {len(now)} modules '
          f'(baseline {len(baseline)}, at first import = 253)')
    if cleaned:
        print(f'cleaned up since the baseline ({len(cleaned)}): '
              + ', '.join(cleaned))
    if new:
        print(f'NEW external-tree paths ({len(new)}) -- gate FAILED:')
        for name in new:
            print(f'   {name}  ({modules_dir / name / "Makefile"})')
        print('   fix: copy the needed headers into the module tree '
              '(vendor-headers/ with a provenance banner, example '
              'musb_hdrc_recon), remove the absolute path')
        return 1

    if args.rebaseline and cleaned:
        baseline_path.write_text('\n'.join(sorted(now)) + '\n')
        print(f'baseline rewritten: {len(baseline)} -> {len(now)}')
    print('no new external-tree paths')
    return 0


if __name__ == '__main__':
    sys.exit(main())
