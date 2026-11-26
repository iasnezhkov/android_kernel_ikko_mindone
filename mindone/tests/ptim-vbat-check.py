# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECK = 'ptim-vbat'
GAUGE = 'mindone/modules/mt6358_battery/mt6358-gauge.c'
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-parameter',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']

STUBS = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define IIO_VAL_INT 1
#define IIO_VAL_INT_PLUS_MICRO 2
#define IIO_VAL_INT_MULTIPLE 4
#define IIO_CHAN_INFO_PROCESSED 2

struct iio_channel {
	int next_rc;
	int next_vbat;
	int next_ibat;
};

struct mtk_gauge {
	struct iio_channel *chan_ptim_bat_voltage;
};

struct mtk_gauge_sysfs_field_info {
	int dummy;
};

static bool IS_ERR(const void *p)
{
	return (uintptr_t)p >= (uintptr_t)-4095L;
}

static void *ERR_PTR(long err)
{
	return (void *)(uintptr_t)err;
}

static int err_logs;
#define bm_err(fmt, ...) do { err_logs++; } while (0)

static int iio_read_channel_attribute(struct iio_channel *chan, int *val,
				      int *val2, int info)
{
	assert(info == IIO_CHAN_INFO_PROCESSED);
	assert(val != NULL && val2 != NULL);
	if (chan->next_rc < 0)
		return chan->next_rc;
	*val = chan->next_vbat;
	*val2 = chan->next_ibat;
	return chan->next_rc;
}

static int iio_read_channel_processed(struct iio_channel *chan, int *val)
{
	int val2 = 0;
	int ret = iio_read_channel_attribute(chan, val, &val2,
					     IIO_CHAN_INFO_PROCESSED);
	if (ret < 0)
		return ret;
	if (ret == IIO_VAL_INT)
		return 0;
	return -EINVAL;
}
"""

CASES = r"""
int main(void)
{
	struct iio_channel ch = { 0 };
	struct mtk_gauge g = { .chan_ptim_bat_voltage = &ch };
	struct mtk_gauge_sysfs_field_info attr = { 0 };
	int val = -1;

	ch.next_rc = IIO_VAL_INT_MULTIPLE;
	ch.next_vbat = 42150;
	ch.next_ibat = -4032;
	err_logs = 0;
	assert(ptim_battery_voltage_get(&g, &attr, &val) == 0);
	assert(val == 42150 && err_logs == 0);

	ch.next_rc = IIO_VAL_INT;
	ch.next_vbat = 38500;
	ch.next_ibat = 0;
	val = -1;
	assert(ptim_battery_voltage_get(&g, &attr, &val) == 0);
	assert(val == 38500 && err_logs == 0);

	ch.next_rc = -EIO;
	val = 99;
	err_logs = 0;
	assert(ptim_battery_voltage_get(&g, &attr, &val) == -EIO);
	assert(err_logs == 1);

	ch.next_rc = IIO_VAL_INT_PLUS_MICRO;
	assert(ptim_battery_voltage_get(&g, &attr, &val) == -EINVAL);

	g.chan_ptim_bat_voltage = ERR_PTR(-ENODEV);
	err_logs = 0;
	assert(ptim_battery_voltage_get(&g, &attr, &val) == -EOPNOTSUPP);
	assert(err_logs == 1);

	printf("PASS ptim-vbat\n");
	return 0;
}
"""


def extract_fn(source):
    m = re.search(r'^static int ptim_battery_voltage_get\(.*?\n\}\n', source, re.M | re.S)
    if not m:
        raise SystemExit(f'{CHECK}: missing ptim_battery_voltage_get in {GAUGE}')
    return m.group(0)


def compile_and_run(directory, name, fn_src):
    path = directory / f'{name}.c'
    binary = directory / name
    path.write_text(STUBS + '\n' + fn_src + '\n' + CASES)
    res = subprocess.run(FLAGS + [str(path), '-o', str(binary)],
                         capture_output=True, text=True, timeout=60)
    if res.returncode:
        return res
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser(
        description='Verify ptim_battery_voltage_get handles IIO_VAL_INT_MULTIPLE on Linux 6.18')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    fn_src = extract_fn((tree / GAUGE).read_text())

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        directory = Path(tmp)
        res = compile_and_run(directory, 'candidate', fn_src)
        if res.returncode:
            raise SystemExit(f'{CHECK}: candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('revert-processed-helper',
             'iio_read_channel_attribute(\n\t\t\tgauge->chan_ptim_bat_voltage, val, &ibat,\n\t\t\tIIO_CHAN_INFO_PROCESSED)',
             'iio_read_channel_processed(gauge->chan_ptim_bat_voltage, val)'),
            ('leak-positive-iio-val',
             'else if (ret == IIO_VAL_INT || ret == IIO_VAL_INT_MULTIPLE)\n\t\t\tret = 0;',
             'else if (ret == IIO_VAL_INT)\n\t\t\tret = 0;'),
            ('accept-unexpected-iio-val',
             'else\n\t\t\tret = -EINVAL;',
             'else\n\t\t\tret = 0;'),
        )
        for name, before, after in mutants:
            if fn_src.count(before) != 1:
                raise SystemExit(f'{CHECK}: mutant {name} anchor count={fn_src.count(before)}')
            mres = compile_and_run(directory, 'mutant-' + name, fn_src.replace(before, after, 1))
            if mres.returncode == 0:
                raise SystemExit(f'{CHECK}: mutant {name} survived')
            print(f'PASS mutant-killed {name}')

    print(f'{CHECK}: PASS (5 cases, {len(mutants)} mutants)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
