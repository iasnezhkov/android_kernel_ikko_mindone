# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'ois-aw86006'
DRIVER = 'mindone/modules/ois_aw86006'
SOURCES = ('aw86006.h', 'aw86006_rom.c', 'ois_aw86006.c')
HEADERS = ('i2c-mt65xx.h', 'imgsensor-power.h')
HEADER_DIR = 'mindone/modules/include/mindone'
IMGSENSOR = 'mindone/modules/imgsensor_isp6s/common/v1_1/imgsensor_hw.c'
I2C = 'mindone/modules/i2c_mt65xx/i2c_mt65xx.c'
BUILD = 'mindone/modules/BUILD.bazel'
OVERLAY = 'mindone/dts/mindone-overlay.dtso'
PWR_SEQ = 'mindone/modules/imgsensor_isp6s/common/v1_1/imgsensor_pwr_seq.c'
PIN_NAMES = {'PDN': 'pnd', 'RST': 'rst', 'AVDD': 'vcama', 'AVDD1': 'vcama1', 'AFVDD': 'vcamaf', 'DVDD': 'vcamd',
             'DOVDD': 'vcamio', 'SensorMCLK': 'mclk'}
LOAD_LISTS = ('mindone/vendor_boot.modules.load', 'mindone/vendor_dlkm.modules.load',
              'mindone/vendor_boot.modules.load.recovery', 'mindone/modules/modules.load')
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ['defaults', 'params', 'identify-blank', 'identify-sync', 'hold-late', 'identify-late', 'identify-gate',
         'bus-dt', 'identify-dump', 'update-blank', 'update-max',
         'update-refused', 'update-verify-fail', 'update-write-fail', 'update-replace', 'guards', 'runtime-centre',
         'runtime-stop', 'runtime-power-off', 'runtime-refused', 'rom-errors', 'remove']
IMGSENSOR_PARTS = ('imgsensor_power_register_notifier', 'imgsensor_power_unregister_notifier', 'imgsensor_power_notify',
                   'imgsensor_hw_power_step', 'imgsensor_hw_power_sequence', 'imgsensor_hw_power_held',
                   'imgsensor_hw_power')
MUTANTS = (
    ('frame-checksum-range', 'aw86006_rom.c',
     (('buf[0] = aw86006_sum8(buf + 1, AW_ROM_HEAD - 1);', 'buf[0] = aw86006_sum8(buf + 2, AW_ROM_HEAD - 2);'),),
     'identify-blank', 'count_packets(0x01, 0x01) == 1'),
    ('image-sum-bytes', 'aw86006_rom.c',
     (('sum += get_unaligned_le32(p + 4 * i);', 'sum += p[i];'),),
     'update-blank', 'a->update == AW_UPDATE_DONE'),
    ('record-sum-offset', 'aw86006_rom.c',
     (('#define AW_RECORD_SUM_FROM\t12', '#define AW_RECORD_SUM_FROM\t20'),),
     'identify-blank', 'a->record_state == AW_AREA_VALID'),
    ('erase-limit-off-by-one', 'aw86006_rom.c',
     (('addr <= AW_FLASH_LIMIT - len;', 'addr <= AW_FLASH_LIMIT - len + AW_ROM_BLOCK;'),),
     'guards', 'aw86006_rom_erase(a, 0x0100ae00, 2) == -EPERM'),
    ('erase-one-block-more', 'aw86006_rom.c',
     (('DIV_ROUND_UP(size - AW_MOVE_LEN, AW_ROM_BLOCK)', 'DIV_ROUND_UP(size - AW_MOVE_LEN, AW_ROM_BLOCK) + 1'),),
     'update-max', 'a->update == AW_UPDATE_DONE'),
    ('no-readback', 'aw86006_rom.c',
     (('ret = aw86006_rom_pass(aw, img, size, true, end);', 'ret = 0;'),),
     'update-verify-fail', 'a->update == AW_UPDATE_FAILED'),
    ('header-not-last', 'aw86006_rom.c',
     (('\t\tif (off == AW_MOVE_LEN)\n\t\t\tcontinue;\n\t\tif (aw86006_stopping(aw))',
       '\t\tif (aw86006_stopping(aw))'),),
     'update-verify-fail', '!memchr_inv(m.flash + 0x2000, 0xff, 64)'),
    ('no-app-id-check', 'aw86006_rom.c',
     (('if (info->move_length != AW_MOVE_LEN || info->app_id != AW_APP_ID ||',
       'if (info->move_length != AW_MOVE_LEN ||'),),
     'update-refused', 'a->update == (kind == NO_FILE ? AW_UPDATE_NO_FILE : AW_UPDATE_BAD_IMAGE)'),
    ('read-as-write', 'aw86006_rom.c',
     (('#define AW_FLASH_READ\t\t0x11', '#define AW_FLASH_READ\t\t0x01'),),
     'identify-blank', 'only_read_packets()'),
    ('no-gap', 'ois_aw86006.c',
     (('\t\tusleep_range(wait_us, wait_us + 50);\n', ''),),
     'identify-blank', 'm.early_reads == 0'),
    ('no-slow-target', 'ois_aw86006.c',
     (('\tif (aw->slow_set || !aw_bus_hz)\n\t\treturn 0;\n\taw->slow_ret', '\treturn 0;\n\taw->slow_ret'),),
     'identify-blank', 'slow.sets == 1'),
    ('hold-window-8ms', 'ois_aw86006.c',
     (('#define AW_HOLD_END_US\t\t6000', '#define AW_HOLD_END_US\t\t8000'),),
     'hold-late', 'hold_bytes_in_window()'),
    ('identify-default-on', 'ois_aw86006.c',
     (('static unsigned int aw_identify;', 'static unsigned int aw_identify = 1;'),),
     'defaults', 'aw_identify == 0'),
    ('update-not-consumed', 'ois_aw86006.c',
     (('update = atomic_xchg(&aw_update, 0);', 'update = atomic_read(&aw_update);'),),
     'update-blank', '!strcmp(get_param("update"), "N\\n")'),
    ('stop-keeps-ois', 'ois_aw86006.c',
     (('\tret = aw86006_app_write(aw, AW_REG_OIS, 0);\n\tif (ret)\n\t\tret = aw86006_app_write(aw, AW_REG_OIS, 0);\n',
       '\tret = 0;\n'),),
     'runtime-stop', '!m.ois_on && m.ois_off_at - t <= 2000000'),
    ('power-off-without-halt', 'ois_aw86006.c',
     (('\taw86006_halt(aw, AW_RUN_POWER_OFF);\n', ''),),
     'runtime-power-off', '!m.ois_on_at_power_off'),
    ('runtime-without-deadline', 'ois_aw86006.c',
     (('\tif (left <= 0)\n', '\tif (left <= -1000000)\n'),),
     'runtime-centre', '!a->running && a->run == AW_RUN_DONE && !m.ois_on'),
    ('no-presence-wait', 'ois_aw86006.c',
     (('\t\tif (aw86006_stopping(aw) || at >= limit)\n', '\t\tif (aw86006_stopping(aw) || at >= limit / 1000)\n'),),
     'identify-late', 'a->answer_us >= 300000'),
    ('data-while-absent', 'ois_aw86006.c',
     (('\t} else if (!aw86006_presence(aw)) {\n\t\taw->mode = AW_MODE_ABSENT;\n\t} else {\n',
       '\t} else {\n\t\taw86006_presence(aw);\n'),),
     'identify-late', 'a->mode == AW_MODE_ABSENT'),
    ('rom-frames-to-program', 'ois_aw86006.c',
     (('\t\telse if (what)\n\t\t\taw86006_rom_identify(aw, what);',
       '\t\tif (aw->mode != AW_MODE_APP && what)\n\t\t\taw86006_rom_identify(aw, what);'),),
     'identify-gate', 'm.npackets == 0 && m.app_rom_frames == 0 && m.napp_writes == 0 && m.sys_data == 0'),
    ('app-probe-in-rom', 'ois_aw86006.c',
     (('\t\tif (!aw->sys_ret)\n\t\t\taw86006_app_probe(aw);\n\t\telse if (what)',
       '\t\taw86006_app_probe(aw);\n\t\tif (aw->mode != AW_MODE_APP && what)'),),
     'identify-blank', 'no_pointer_writes()'),
    ('sys-probe-once', 'ois_aw86006.c',
     (('#define AW_SYS_TRIES\t\t6', '#define AW_SYS_TRIES\t\t1'),),
     'identify-gate', 'a->sys_ret == 0'),
    ('bus-hz-zero-refused', 'ois_aw86006.c',
     (('\tif (hz && (hz < AW_BUS_MIN_HZ || hz > AW_BUS_MAX_HZ))', '\tif (hz < AW_BUS_MIN_HZ || hz > AW_BUS_MAX_HZ)'),),
     'params', '!set_param("bus_hz", "0")'),
    ('dump-skips-first-chunk', 'aw86006_rom.c',
     (('\tfor (off = 0; off < AW_FLASH_SIZE; off += AW_ROM_CHUNK) {',
       '\tfor (off = AW_ROM_CHUNK; off < AW_FLASH_SIZE; off += AW_ROM_CHUNK) {'),),
     'identify-dump', 'count_packets(0x03, 0x11) == reads + 1024'),
    ('dump-stops-at-refusal', 'aw86006_rom.c',
     (('\t\tif (!ret)\n\t\t\tcontinue;\n\t\tmemset(aw->flash', '\t\tif (ret)\n\t\t\treturn;\n\t\tcontinue;\n\t\tmemset(aw->flash'),),
     'identify-dump', 'a->flash_complete && a->flash_bad == 64'),
    ('trace-kept', 'ois_aw86006.c',
     (('\taw->ntrace = 0;\n', ''),),
     'identify-late', '!strstr(trace_text(), "0x61")'),
)
IMGSENSOR_MUTANTS = (
    ('pin-on-after-failed-set',
     (('\tif (ret == IMGSENSOR_RETURN_SUCCESS && device &&\n\t    status == IMGSENSOR_HW_POWER_STATUS_ON)',
       '\tif (device && status == IMGSENSOR_HW_POWER_STATUS_ON)'),),
     'count(EV_NOTIFY, IMGSENSOR_POWER_PIN_ON) == 5'),
    ('off-notified-late',
     (('\tif (pwr_status == IMGSENSOR_HW_POWER_STATUS_OFF)\n\t\timgsensor_power_notify(IMGSENSOR_POWER_OFF, sensor_idx,\n'
       '\t\t\t\t       IMGSENSOR_HW_PIN_NONE);\n', ''),
      ('notify:\n\tif (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON)\n\t\timgsensor_power_notify(',
       'notify:\n\t\timgsensor_power_notify(pwr_status == IMGSENSOR_HW_POWER_STATUS_OFF ? IMGSENSOR_POWER_OFF :')),
     'evs[0].kind == EV_NOTIFY && evs[0].action == IMGSENSOR_POWER_OFF'),
    ('failure-reports-on',
     (('ret == IMGSENSOR_RETURN_SUCCESS ?\n\t\t\t\t       IMGSENSOR_POWER_ON : IMGSENSOR_POWER_OFF',
       'IMGSENSOR_POWER_ON'),),
     'count(EV_NOTIFY, IMGSENSOR_POWER_ON) == 0'),
    ('busy-without-wait',
     (('\t\twait_event_timeout(phw->power_wait,\n\t\t\t\t   !imgsensor_hw_power_held(phw, sensor_idx),\n'
       '\t\t\t\t   msecs_to_jiffies(IMGSENSOR_HW_POWER_WAIT_MS));\n', ''),),
     'ret == IMGSENSOR_RETURN_SUCCESS && hw.power_owner == IMGSENSOR_SENSOR_IDX_SUB'),
    ('mutex-held-while-waiting',
     (('\t\tmutex_unlock(&phw->power_mutex);\n\t\twait_event_timeout(', '\t\twait_event_timeout('),
      ('IMGSENSOR_HW_POWER_WAIT_MS));\n\t\tmutex_lock(&phw->power_mutex);\n', 'IMGSENSOR_HW_POWER_WAIT_MS));\n')),
     'hw.power_wait.waiters == 1 && !hw.power_mutex.locked'),
    ('off-does-not-wake',
     (('\t\twake_up_all(&phw->power_wait);\n', ''),),
     'hook_ticks == 40 && released_ms > release_at'),
    ('no-refusal-after-wait',
     (('\tif (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON &&\n\t    imgsensor_hw_power_held(phw, sensor_idx)) {\n'
       '\t\tpr_info("%s: sensor %d refused', '\tif (0) {\n\t\tpr_info("%s: sensor %d refused'),),
     'imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_BUSY'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
         '-Wno-missing-field-initializers', '-Wno-unused-function', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all']


def skip(source, index):
    if source.startswith('//', index):
        return source.index('\n', index)
    if source.startswith('/*', index):
        return source.index('*/', index + 2) + 2
    if source[index] in '"\'':
        quote, index = source[index], index + 1
        while source[index] != quote:
            index += 2 if source[index] == '\\' else 1
        return index + 1
    return None


def closing(source, index):
    depth = 0
    while True:
        after = skip(source, index)
        if after is not None:
            index = after
            continue
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if not depth:
                return index + 1
        index += 1


def function(source, name, path):
    pattern = r'^(?:static )?\w[^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {name} has {len(found)} definitions in {path}')
    start = found[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def replace_once(text, before, after, label):
    if text.count(before) != 1:
        raise SystemExit(f'{CHECK}: {label}: expected one {before!r}')
    return text.replace(before, after)


def bindings(tree, sources):
    build = (tree / BUILD).read_text()
    block = re.search(r'^ddk_module\(\n    name = "ois_aw86006",\n(.*?)\n\)\n', build, re.M | re.S)
    if not block:
        raise SystemExit(f'{CHECK}: no ois_aw86006 ddk_module in {BUILD}')
    srcs = re.findall(r'"(ois_aw86006/[^"]+\.c)"', block.group(1))
    if sorted(srcs) != ['ois_aw86006/aw86006_rom.c', 'ois_aw86006/ois_aw86006.c'] or \
            'out = "ois_aw86006.ko"' not in block.group(1):
        raise SystemExit(f'{CHECK}: the ois_aw86006 target does not build both sources into ois_aw86006.ko')
    for dep in ('":mindone_headers"', '":i2c_mt65xx"', '":imgsensor_isp6s"'):
        if dep not in block.group(1):
            raise SystemExit(f'{CHECK}: the ois_aw86006 target lacks the dependency {dep}')
    group = re.search(r'^kernel_module_group\(\n    name = "mindone_modules",\n    srcs = \[(.*?)\]', build, re.M | re.S)
    if '":ois_aw86006"' not in group.group(1):
        raise SystemExit(f'{CHECK}: mindone_modules does not build ois_aw86006')
    for relative in LOAD_LISTS:
        if 'ois_aw86006.ko' in (tree / relative).read_text().split():
            raise SystemExit(f'{CHECK}: {relative} loads ois_aw86006.ko; the first tests load it by hand')
    overlay = (tree / OVERLAY).read_text()
    nodes = re.findall(r'^\tcamera_main_ois@69 \{\n(.*?)^\t\};', overlay, re.M | re.S)
    if len(nodes) != 1 or 'compatible = "mindone,aw86006-ois";' not in nodes[0] or 'reg = <0x69>;' not in nodes[0]:
        raise SystemExit(f'{CHECK}: {OVERLAY} needs one camera_main_ois@69 node with compatible '
                         f'"mindone,aw86006-ois" and reg <0x69>')
    fragment = overlay[:overlay.index('\tcamera_main_ois@69 {')]
    if fragment.rfind('&i2c4 {') < fragment.rfind('\n};'):
        raise SystemExit(f'{CHECK}: camera_main_ois@69 is not inside an &i2c4 fragment of {OVERLAY}')
    if '{ .compatible = "mindone,aw86006-ois" },' not in sources['ois_aw86006.c']:
        raise SystemExit(f'{CHECK}: the driver does not match "mindone,aw86006-ois"')
    imgsensor = (tree / IMGSENSOR).read_text()
    for name in ('imgsensor_power_register_notifier', 'imgsensor_power_unregister_notifier'):
        if f'EXPORT_SYMBOL_GPL({name});' not in imgsensor:
            raise SystemExit(f'{CHECK}: {IMGSENSOR} does not export {name}')
    if 'EXPORT_SYMBOL_GPL(mtk_i2c_set_slow_target);' not in (tree / I2C).read_text():
        raise SystemExit(f'{CHECK}: {I2C} does not export mtk_i2c_set_slow_target')
    if imx766_sequence((tree / PWR_SEQ).read_text()) != harness_sequence((HARNESS / 'cases.c').read_text()):
        raise SystemExit(f'{CHECK}: the imx766_on table of {CHECK}/cases.c differs from the IMX766 sequence of {PWR_SEQ}')


def imx766_sequence(text):
    block = re.search(r'^#if defined\(IMX766_MIPI_RAW\)\n\t\{\n\t\tSENSOR_DRVNAME_IMX766_MIPI_RAW,\n\t\t\{\n(.*?)\n\t\t\},\n'
                      r'\t\},\n#endif', text, re.M | re.S)
    if not block:
        raise SystemExit(f'{CHECK}: no IMX766 power sequence in {PWR_SEQ}')
    steps = re.findall(r'^\t\t\t\{(\w+), \w+, (\d+)\},?$', block.group(1), re.M)
    if len(steps) != block.group(1).count('\n') + 1 or any(pin not in PIN_NAMES for pin, _ in steps):
        raise SystemExit(f'{CHECK}: unexpected step in the IMX766 power sequence of {PWR_SEQ}')
    return [(PIN_NAMES[pin], int(ms)) for pin, ms in steps]


def harness_sequence(text):
    block = re.search(r'^\} imx766_on\[\] = \{\n(.*?)^\};', text, re.M | re.S)
    if not block:
        raise SystemExit(f'{CHECK}: no imx766_on table in {CHECK}/cases.c')
    return [(pin, int(ms)) for pin, ms in re.findall(r'\{ "(\w+)", (\d+) \}', block.group(1))]


def shims(directory, texts, target):
    for text in texts:
        for header in re.findall(r'^#include <((?:linux|asm)/[^>]+)>', text, re.M):
            path = directory / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f'#include "{target}"\n')


def ois_unit(directory, tree, sources):
    source_dir = directory / 'src'
    include_dir = directory / 'include'
    (include_dir / 'mindone').mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(exist_ok=True)
    headers = {name: (tree / HEADER_DIR / name).read_text() for name in HEADERS}
    for name, text in sources.items():
        (source_dir / name).write_text(text)
    for name, text in headers.items():
        (include_dir / 'mindone' / name).write_text(text)
    shims(directory / 'sys', list(sources.values()) + list(headers.values()), 'stubs.h')
    unit = source_dir / 'unit.c'
    unit.write_text('\n'.join(['#include "stubs.h"', '#include <mindone/i2c-mt65xx.h>',
                               '#include <mindone/imgsensor-power.h>', '#include "model.c"',
                               '#include "aw86006_rom.c"', '#include "ois_aw86006.c"', '#include "cases.c"']) + '\n')
    return unit, ['-I', str(directory / 'sys'), '-I', str(HARNESS), '-I', str(include_dir)]


def imgsensor_unit(directory, tree, imgsensor):
    names = re.search(r'^char \* const imgsensor_hw_pin_names\[\] = \{.*?^\};\n', imgsensor, re.M | re.S)
    head = re.findall(r'^static BLOCKING_NOTIFIER_HEAD\(imgsensor_power_chain\);$', imgsensor, re.M)
    bound = re.findall(r'^#define IMGSENSOR_HW_POWER_WAIT_MS \d+$', imgsensor, re.M)
    if not names or len(head) != 1 or len(bound) != 1:
        raise SystemExit(f'{CHECK}: {IMGSENSOR} lacks the pin names, the imgsensor_power_chain head or '
                         f'IMGSENSOR_HW_POWER_WAIT_MS')
    header = (tree / HEADER_DIR / 'imgsensor-power.h').read_text()
    include_dir = directory / 'include'
    (include_dir / 'mindone').mkdir(parents=True, exist_ok=True)
    (include_dir / 'mindone' / 'imgsensor-power.h').write_text(header)
    shims(directory / 'sys', [header], 'imgsensor.h')
    parts = ['#include "imgsensor.h"', '#include <mindone/imgsensor-power.h>', names.group(0), bound[0], head[0]]
    parts += [function(imgsensor, name, IMGSENSOR) for name in IMGSENSOR_PARTS]
    parts += ['#include "imgsensor-cases.c"']
    unit = directory / 'imgsensor-unit.c'
    unit.write_text('\n'.join(parts) + '\n')
    return unit, ['-I', str(directory / 'sys'), '-I', str(HARNESS), '-I', str(include_dir)]


def build(unit, includes, binary):
    subprocess.run(FLAGS + includes + [str(unit), '-o', str(binary)], check=True, timeout=300)
    return binary


def run(binary, case=None):
    return subprocess.run([str(binary)] + ([case] if case else []), capture_output=True, text=True, timeout=600)


def caught(result, assertion):
    return result.returncode and 'Assertion' in result.stderr and assertion in result.stderr


def main():
    parser = argparse.ArgumentParser(description='Compile ois_aw86006 (ROM boot loader protocol, update, runtime '
                                     'control, sysfs and parameters) against a model of the AW86006 ROM loader, '
                                     'flash, program registers and the i2c-4 timing with ASan/UBSan, and the '
                                     'imgsensor power notifier and the bounded wait for the shared supplies against '
                                     'a pin and wait-queue model: identification must send only '
                                     'read requests, the update must never touch 0x0100b000+ and must write the '
                                     'header last after a readback, bad images must be refused before any erase, '
                                     'every parameter must default to off, and every mutation must be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    sources = {name: (tree / DRIVER / name).read_text() for name in SOURCES}
    bindings(tree, sources)
    imgsensor = (tree / IMGSENSOR).read_text()
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        unit, includes = ois_unit(directory / 'candidate', tree, sources)
        candidate = build(unit, includes, directory / 'candidate' / 'unit')
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                raise SystemExit(f'{CHECK}: FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr}')
            print(result.stdout.rstrip())
        unit, includes = imgsensor_unit(directory / 'imgsensor', tree, imgsensor)
        result = run(build(unit, includes, directory / 'imgsensor' / 'unit'))
        if result.returncode:
            raise SystemExit(f'{CHECK}: FAIL imgsensor (exit {result.returncode})\n{result.stdout}{result.stderr}')
        print(result.stdout.rstrip())
        for name, file, edits, case, assertion in MUTANTS:
            mutated = dict(sources)
            for before, after in edits:
                mutated[file] = replace_once(mutated[file], before, after, 'mutant ' + name)
            unit, includes = ois_unit(directory / ('mutant-' + name), tree, mutated)
            result = run(build(unit, includes, directory / ('mutant-' + name) / 'unit'), case)
            if not caught(result, assertion):
                raise SystemExit(f'{CHECK}: mutant {name} was not caught by {case} at {assertion!r}\n'
                                 f'{result.stdout}{result.stderr}')
            print(f'PASS mutant {name}: {case} fails at {assertion}')
        for name, edits, assertion in IMGSENSOR_MUTANTS:
            mutated = imgsensor
            for before, after in edits:
                mutated = replace_once(mutated, before, after, 'mutant ' + name)
            unit, includes = imgsensor_unit(directory / ('mutant-' + name), tree, mutated)
            result = run(build(unit, includes, directory / ('mutant-' + name) / 'unit'))
            if not caught(result, assertion):
                raise SystemExit(f'{CHECK}: mutant {name} was not caught by imgsensor at {assertion!r}\n'
                                 f'{result.stdout}{result.stderr}')
            print(f'PASS mutant {name}: imgsensor fails at {assertion}')
    print(f'{CHECK}: PASS {len(CASES)} driver cases and the imgsensor notifier and wait cases, '
          f'{len(MUTANTS) + len(IMGSENSOR_MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
