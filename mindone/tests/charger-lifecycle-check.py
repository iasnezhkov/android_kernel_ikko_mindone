# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from baseline_source import require

FIX = (
    ('\n\tif (of_property_read_u32(node, "charger_configuration", &val))\n\t\treturn 0;\n\telse\n\t\treturn val;\n}\n',
     '\n\tof_property_read_u32(node, "charger_configuration", &val);\n\tof_node_put(node);\n\treturn val;\n}\n'),
    ('\t\tpr_info("Couldn\'t get chg_psy\\n");\n\t\treturn -EINVAL;\n\t}\n',
     '\t\tpr_info("Couldn\'t get chg_psy\\n");\n\t\treturn charger_cdev->chg_psy ? PTR_ERR(charger_cdev->chg_psy) : -EPROBE_DEFER;\n\t}\n'),
    ('\t\t\tpr_info("Couldn\'t get s_chg_psy\\n");\n\t\t\treturn -EINVAL;\n\t\t}\n',
     '\t\t\tpr_info("Couldn\'t get s_chg_psy\\n");\n\t\t\tret = charger_cdev->s_chg_psy ? PTR_ERR(charger_cdev->s_chg_psy) : -EPROBE_DEFER;\n\t\t\tgoto put_master;\n\t\t}\n'),
    ('\t\tdev_info(&pdev->dev, "failed to create charger cooler sysfs, ret=%d!\\n", ret);\n\t\treturn ret;\n\t}\n',
     '\t\tdev_info(&pdev->dev, "failed to create charger cooler sysfs, ret=%d!\\n", ret);\n\t\tgoto put_slave;\n\t}\n'),
    ('\t}\n\tcharger_cl_data.cur_state = 0;\n',
     '\t}\n\tmutex_lock(&charger_cooler_lock);\n\tcharger_cl_data.cur_state = 0;\n'),
    ('\tcharger_cl_data.num_state = CHARGER_STATE_NUM;\n\n',
     '\tcharger_cl_data.num_state = CHARGER_STATE_NUM;\n\tcharger_cl_data.request_dirty = false;\n\tmutex_unlock(&charger_cooler_lock);\n\n'),
    ('\t\tcharger_cdev, &charger_cooling_ops);\n\tif (IS_ERR(cdev))\n\t\treturn -EINVAL;\n\tcharger_cdev->cdev = cdev;\n',
     '\t\tcharger_cdev, &charger_cooling_ops);\n\tif (IS_ERR(cdev)) {\n\t\tret = PTR_ERR(cdev);\n\t\tgoto remove_group;\n\t}\n\tcharger_cdev->cdev = cdev;\n'),
    ('\treturn 0;\n}\n',
     '\treturn 0;\n\nremove_group:\n\tsysfs_remove_group(kernel_kobj, &charger_cooler_attr_group);\nput_slave:\n\tif (charger_cdev->type == DUAL_CHARGER)\n\t\tpower_supply_put(charger_cdev->s_chg_psy);\nput_master:\n\tpower_supply_put(charger_cdev->chg_psy);\n\treturn ret;\n}\n'),
    ('\tthermal_cooling_device_unregister(charger_cdev->cdev);\n\tplatform_set_drvdata(pdev, NULL);\n',
     '\tthermal_cooling_device_unregister(charger_cdev->cdev);\n\tsysfs_remove_group(kernel_kobj, &charger_cooler_attr_group);\n\tif (charger_cdev->type == DUAL_CHARGER)\n\t\tpower_supply_put(charger_cdev->s_chg_psy);\n\tpower_supply_put(charger_cdev->chg_psy);\n\tplatform_set_drvdata(pdev, NULL);\n'),
)
BASELINE_PINS = {
    'get_charger_type': '108fcf0abcfd44c832f2a7bf3abba2f6f29b7f517d5daddc1443b3430c58df3a',
    'charger_cooling_probe': 'bd1c2afa6503241fb05d2b84624edbb440ec6e4fd5be185526718f7beae9a163',
    'charger_cooling_remove': 'd70e78b935d1958f76b06015f65088f26945d452d0191ea8a05dc6478a194508',
}


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Lifecycle change is not in the source exactly once: ' + before.strip().splitlines()[0])
    return source.replace(before, after)


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
    args = parser.parse_args()
    root = args.kernel_tree.resolve()
    relative = 'mindone/modules/charger_cooling/charger_cooling.c'
    source = (root / relative).read_text()
    if args.baseline:
        for before, after in reversed(FIX):
            source = replace_once(source, after, before)
        for name, pin in BASELINE_PINS.items():
            require('Function ' + name + ' without the lifecycle change', block(source, r'^static (?:int|void) ' + name + r'\('), pin)
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
