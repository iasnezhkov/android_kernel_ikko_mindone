import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'^(?:static )?int ' + name + r'\(', source, re.M)
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


parser = argparse.ArgumentParser(description='Check shutdown voltage sampling against provider failures')
parser.add_argument('tree', type=Path)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
base = args.tree / 'mindone/modules/mt6358_battery'
battery = (base / 'mtk_battery.c').read_text()
gauge = (base / 'mt6358-gauge.c').read_text()
header = (base / 'mtk_battery.h').read_text()
table = (base / 'mtk_battery_table.h').read_text()
names = ('BAT_VOLTAGE_LOW_BOUND', 'LOW_TMP_BAT_VOLTAGE_LOW_BOUND',
         'SHUTDOWN_TIME', 'AVGVBAT_ARRAY_SIZE', 'LOW_TEMP_THRESHOLD',
         'LOW_TEMP_DISABLE_LOW_BAT_SHUTDOWN')
defines = '\n'.join(re.search(r'^#define\s+' + name + r'\s+[^\n]+', header + '\n' + table, re.M)[0]
                    for name in names)
fixture = Path(__file__).with_name('shutdown-voltage').joinpath('cases.c').read_text()
fixture = fixture.replace('@DEFINES@', defines)
fixture = fixture.replace('@PROVIDER@', function(gauge, 'bat_vol_get'))
fixture = fixture.replace('@FUNCTIONS@', '\n'.join(function(battery, name) for name in
                          ('gauge_get_int_property', 'next_waketime', 'shutdown_event_handler')))
with tempfile.TemporaryDirectory(prefix='shutdown-voltage-') as directory:
    work = Path(directory)
    (work / 'check.c').write_text(fixture)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', str(work / 'check.c'), '-o', str(work / 'check')],
                   check=True, timeout=60)
    result = subprocess.run([str(work / 'check')], capture_output=True, text=True, timeout=30)
    print(result.stdout, end='')
    print(result.stderr, end='')
    if (result.returncode != 0) != args.baseline:
        raise SystemExit(result.returncode or 'Baseline unexpectedly passed')
