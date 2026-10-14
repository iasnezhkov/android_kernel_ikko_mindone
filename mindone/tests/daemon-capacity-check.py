import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'^(?:static )?(?:int|void) ' + name + r'\(', source, re.M)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def case(source, name):
    start = source.index('\tcase ' + name + ':')
    end = source.index('\n\tcase ', start + 1)
    return source[start:end]


def clean(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)


parser = argparse.ArgumentParser(description='Check exact daemon capacity reporting and packet boundaries')
parser.add_argument('tree', type=Path)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
base = args.tree / 'mindone/modules/mt6358_battery'
battery = (base / 'mtk_battery.c').read_text()
daemon = (base / 'mtk_battery_daemon.c').read_text()
header = (base / 'mtk_battery_daemon.h').read_text()
fixture = Path(__file__).parent / 'daemon-capacity'
types = '\n'.join(re.findall(r'struct fgd_(?:nl_msg_t|cmd_param_t_[48]) \{.*?\n\};', header, re.S))
functions = function(battery, 'mtk_battery_get_learned_q_max')
if 'static int mtk_battery_get_full_uah(' in battery:
    functions += '\n' + function(battery, 'mtk_battery_get_full_uah')
functions += '\n' + function(battery, 'mtk_battery_get_charge_now_uah')
functions += '\n' + function(battery, 'mtk_battery_time_to_full')
cases = case(daemon, 'FG_DAEMON_CMD_SET_BATTERY_CAPACITY')
cases += '\n' + case(daemon, 'FG_DAEMON_CMD_SET_DAEMON_PID')
report = case(battery, 'POWER_SUPPLY_PROP_CHARGE_FULL')
wire = function(daemon, 'mtk_battery_netlink_handler')
wire = wire[:wire.index('\n\tif (fgd_msg->identity')]
wire += '\n\t(void)uid; (void)pid; (void)seq; (void)size; (void)fgd_ret_msg; (void)fgd_msg;\n\taccepted++;\n}'
source = (fixture / 'cases.c').read_text()
source = source.replace('@TYPES@', clean(types)).replace('@FUNCTIONS@', clean(functions))
source = source.replace('@CASES@', clean(cases)).replace('@REPORT@', clean(report)).replace('@WIRE@', clean(wire))
with tempfile.TemporaryDirectory(prefix='daemon-capacity-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True, timeout=60)
    result = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=60)
    print(result.stdout, end='')
    print(result.stderr, end='')
    if (result.returncode != 0) != args.baseline:
        raise SystemExit(result.returncode or 'Baseline unexpectedly passed')
