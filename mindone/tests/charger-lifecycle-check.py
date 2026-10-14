import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def block(source, pattern, semicolon=False):
    match = re.search(pattern, source, re.M)
    if not match:
        raise ValueError(pattern)
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if semicolon:
        end = source.index(';', end) + 1
    return source[match.start():end] + '\n'


def main():
    parser = argparse.ArgumentParser(description='Exercise exact charger cooler probe, failure cleanup and removal with host fault injection')
    parser.add_argument('kernel_tree', type=Path)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--mutate-remove-order', action='store_true')
    parser.add_argument('--base', default='d6e46b206103')
    args = parser.parse_args()
    root = args.kernel_tree.resolve()
    relative = 'mindone/modules/charger_cooling/charger_cooling.c'
    source = (root / relative).read_text()
    if args.baseline:
        source = subprocess.run(['git', '-C', str(root), 'show', args.base + ':' + relative],
                                capture_output=True, text=True, check=True, timeout=20,
                                env=dict(os.environ, GIT_NO_LAZY_FETCH='1')).stdout
    header = (root / 'mindone/modules/charger_cooling/charger_cooling.h').read_text()
    build = (root / 'mindone/modules/BUILD.bazel').read_text()
    assert re.search(r'name = "charger_cooling",\s*srcs = \[\s*"charger_cooling/charger_cooling.c"', build)
    declarations = ''
    for name in ('CHARGER_COOLING_UNLIMITED_STATE', 'CHARGER_STATE_NUM', 'MAX_CHARGER_COOLER_NAME_LEN'):
        declarations += re.search(r'^#define ' + name + r'[^\n]+', header, re.M)[0] + '\n'
    for name in ('enum charger_type', 'struct charger_cooling_device', 'struct charger_cooling_platform_data'):
        declarations += block(header, '^' + name + r' \{', True)
    declarations += block(source, r'^struct charger_cooler_info \{', True)
    bodies = ''.join(block(source, r'^static (?:int|void) ' + name + r'\(')
                     for name in ('get_charger_type', 'charger_cooling_probe', 'charger_cooling_remove'))
    if args.mutate_remove_order:
        old = '\tthermal_cooling_device_unregister(charger_cdev->cdev);\n\tsysfs_remove_group(kernel_kobj, &charger_cooler_attr_group);'
        assert bodies.count(old) == 1
        bodies = bodies.replace(old, '\tsysfs_remove_group(kernel_kobj, &charger_cooler_attr_group);\n\tthermal_cooling_device_unregister(charger_cdev->cdev);')
    fixture = Path(__file__).with_name('charger-lifecycle')
    with tempfile.TemporaryDirectory(prefix='charger-lifecycle-') as temporary:
        work = Path(temporary)
        (work / 'types.h').write_text(declarations)
        (work / 'functions.h').write_text(bodies)
        executable = work / 'check'
        subprocess.run(['clang', '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                        '-fsanitize=address,undefined',
                        '-I', str(work), str(fixture / 'cases.c'), '-o', str(executable)], check=True, timeout=60)
        return subprocess.run([str(executable)], timeout=30).returncode


if __name__ == '__main__':
    raise SystemExit(main())
