# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import sys


GKI_MODULES = ('libarc4.ko', 'rfkill.ko', 'zram.ko', 'zsmalloc.ko')
LISTS = (('vendor_boot', 'mindone/vendor_boot.modules.load'),
         ('vendor_dlkm', 'mindone/vendor_dlkm.modules.load'),
         ('recovery', 'mindone/vendor_boot.modules.load.recovery'))
HEADER = ('# SPDX-License-Identifier: GPL-2.0\n'
          '# SPDX-FileCopyrightText: 2026 The LineageOS Project\n'
          'module\tfile\tsource\tselected\tstages\tdirectories\n')


def read_list(path):
    return [line.strip() for line in path.read_text().splitlines() if line.strip()]


def generate(tree):
    build = (tree / 'mindone/modules/BUILD.bazel').read_text()
    targets = {}
    for block in re.findall(r'^ddk_module\(\n(.*?)\n\)\n', build, re.M | re.S):
        name = re.search(r'^    name = "([^"]+)"', block, re.M)[1]
        out = re.search(r'^    out = "([^"]+)"', block, re.M)[1]
        sources = re.search(r'^    srcs = \[(.*?)\]', block, re.M | re.S)
        directories = sorted({path.split('/')[0] for path in re.findall(r'"([^"]+)"', sources[1])}) if sources else []
        for directory in directories:
            if not (tree / 'mindone/modules' / directory).is_dir():
                raise ValueError(f'{name}: source directory mindone/modules/{directory} is missing')
        if name in targets or not directories:
            raise ValueError(f'{name}: duplicate target or no sources')
        targets[name] = (out, directories)
    group = re.search(r'^kernel_module_group\(\n    name = "mindone_modules",\n    srcs = \[(.*?)\]', build, re.M | re.S)
    selected = set(re.findall(r'":([^"]+)"', group[1]))
    if selected - set(targets):
        raise ValueError('mindone_modules names unknown targets: ' + ', '.join(sorted(selected - set(targets))))
    in_tree_block = re.search(r'^_MINDONE_IN_TREE_MODULES = \[(.*?)\]', (tree / 'mindone/BUILD.bazel').read_text(), re.M | re.S)
    in_tree = re.findall(r'"([^"]+\.ko)"', in_tree_block[1])
    files = {}
    for name, (out, directories) in targets.items():
        files[out] = [name, out, 'ddk', 'yes' if name in selected else 'no', [], ','.join(directories)]
    for out in in_tree:
        files.setdefault(out, [out[:-3], out, 'in-tree', 'yes', [], '-'])
    for out in GKI_MODULES:
        files.setdefault(out, [out[:-3], out, 'gki', '-', [], '-'])
    if len(files) != len(targets) + len(in_tree) + len(GKI_MODULES):
        raise ValueError('A module file is declared twice')
    lists = {}
    for stage, relative in LISTS:
        lists[stage] = read_list(tree / relative)
        unknown = sorted(set(lists[stage]) - set(files))
        if unknown:
            raise ValueError(f'{relative} names unknown modules: ' + ', '.join(unknown))
        if len(set(lists[stage])) != len(lists[stage]):
            raise ValueError(f'{relative} repeats a module')
        for out in lists[stage]:
            row = files[out]
            if row[2] == 'ddk' and row[3] != 'yes':
                raise ValueError(f'{relative} loads {out}, which mindone_modules does not select')
            row[4].append(stage)
    combined = read_list(tree / 'mindone/modules/modules.load')
    if set(combined) != set(lists['vendor_boot']) | set(lists['vendor_dlkm']) or len(set(combined)) != len(combined):
        raise ValueError('mindone/modules/modules.load is not the union of the vendor_boot and vendor_dlkm lists')
    for out in GKI_MODULES:
        if not files[out][4]:
            raise ValueError(f'{out} is listed as a GKI module but no load list names it')
    rows = sorted(files.values(), key=lambda row: (row[0], row[1]))
    return HEADER + ''.join('\t'.join([*row[:4], ','.join(row[4]) or '-', row[5]]) + '\n' for row in rows), rows


def main():
    parser = argparse.ArgumentParser(description='Generate mindone/modules/MODULES.tsv from BUILD.bazel and the load '
                                     'lists and fail when the committed table differs; --write replaces it')
    parser.add_argument('kernel_tree', type=Path, nargs='?', default=Path(__file__).resolve().parents[2])
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    table, rows = generate(tree)
    path = tree / 'mindone/modules/MODULES.tsv'
    counts = {kind: sum(row[2] == kind for row in rows) for kind in ('ddk', 'in-tree', 'gki')}
    summary = (f"{len(rows)} modules: {counts['ddk']} ddk targets ({sum(row[3] == 'yes' for row in rows if row[2] == 'ddk')} selected), "
               f"{counts['in-tree']} in-tree, {counts['gki']} GKI; "
               + ', '.join(f'{stage} {sum(stage in row[4] for row in rows)}' for stage, _ in LISTS))
    if args.write:
        path.write_text(table)
        print('wrote ' + str(path) + ': ' + summary)
        return 0
    if path.read_text() != table:
        print('FAIL MODULES.tsv differs from BUILD.bazel and the load lists; run modules-tsv-check.py --write', file=sys.stderr)
        return 1
    print('PASS MODULES.tsv: ' + summary)
    return 0


if __name__ == '__main__':
    sys.exit(main())
