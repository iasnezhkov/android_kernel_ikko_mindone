# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re

CHECK = 'imx766-pd-vc'
SOURCE = 'mindone/modules/imgsensor_isp6s/common/v1_1/imx766_mipi_raw/imx766mipiraw_Sensor.c'
SCENARIOS = ('CAMERA_PREVIEW', 'CAMERA_CAPTURE_JPEG', 'VIDEO_PREVIEW', 'HIGH_SPEED_VIDEO', 'SLIM_VIDEO', 'CUSTOM1',
             'CUSTOM2', 'CUSTOM3')
IMAGE = (0x00, 0x2B)
BINNING = {(0x01, 0x22, 0x08, 0x41, 0x41): '2x2', (0x01, 0x44, 0x08, 0x43, 0x43): '4x4',
           (0x00, 0x11, 0x0A, 0x00, 0x00): 'none'}
FRAMES = (((0x3066, 0x3067), (0x3068, 0x3069)), ((0x3077, 0x3078), (0x3079, 0x307A)),
          ((0x3087, 0x3088), (0x3089, 0x308A)))
PD_REGISTERS = {0x30B4, 0x33D0, 0x33D1, 0x0112, 0x0113, 0x034C, 0x034D, 0x034E, 0x034F, 0x0900, 0x0901, 0x0902,
                0x3200, 0x3201} | {register for frame in FRAMES for pixel in frame for register in pixel}
NEXT_VC = ('\n\t\t},\n\t\t1\n\t},\n\t{\n\t\t0x02, 0x0a, 0x00, 0x08, 0x40, 0x00,\n\t\t{\n'
           '\t\t\t{VC_STAGGER_NE, 0x00, 0x2b, 0x1000, 0x900},')
MUTANTS = (
    ('pix2-shares-pd-dt', (('imx766_preview_setting', '\t0x3069, 0x31,', '\t0x3069, 0x30,'),),
     'CAMERA_PREVIEW: VC 0 DT 0x30 carries 2560x1536'),
    ('pix2-shares-pd-dt-video', (('imx766_normal_video_setting', '\t0x3069, 0x31,', '\t0x3069, 0x30,'),),
     'VIDEO_PREVIEW: VC 0 DT 0x30 carries 2560x1152'),
    ('pix2-shares-2nd-frame', (('imx766_custom2_setting', '\t0x307A, 0x31,', '\t0x307A, 0x30,'),),
     'frame 2 PIX_1 and PIX_2 share VC 1 DT 0x30'),
    ('pd-declared-both-pixels', ((None, '0x0A00, 0x0300},' + NEXT_VC, '0x0A00, 0x0600},' + NEXT_VC),),
     'CAMERA_PREVIEW: VC 0 DT 0x30 carries 2560x768 per frame, VC_INFO2[0] declares 2560x1536'),
    ('capture-video-swap', ((None, 'CAPTURE_JPEG:\n\t\tmemcpy((void *)pvcinfo2, (void *)&SENSOR_VC_INFO2[2],',
                             'CAPTURE_JPEG:\n\t\tmemcpy((void *)pvcinfo2, (void *)&SENSOR_VC_INFO2[1],'),
                            (None, 'VIDEO_PREVIEW:\n\t\tmemcpy((void *)pvcinfo2, (void *)&SENSOR_VC_INFO2[1],',
                             'VIDEO_PREVIEW:\n\t\tmemcpy((void *)pvcinfo2, (void *)&SENSOR_VC_INFO2[2],')),
     'CAMERA_CAPTURE_JPEG: VC 0 DT 0x2b carries 4096x3072 per frame, VC_INFO2[1] declares 4096x2304'),
    ('hs-pd-width', ((None, '0x30, 0x0500, 0x0120}', '0x30, 0x0A00, 0x0120}'),),
     'HIGH_SPEED_VIDEO: VC 0 DT 0x30 carries 1280x288 per frame, VC_INFO2[4] declares 2560x288'),
    ('custom1-pd-claim', ((None, '0x2000, 0x1800},', '0x2000, 0x1800}, {VC_PDAF_STATS, 0x00, 0x30, 0xA00, 0x240},'),),
     'CUSTOM1: VC_INFO2[3] declares VC 0 DT 0x30, which the sensor does not send'),
    ('video-pd-off', (('imx766_normal_video_setting', '\t0x30B4, 0x01,', '\t0x30B4, 0x00,'),),
     'VIDEO_PREVIEW: PDAF capacity 1 without PD output'),
    ('slim-writes-hs', ((None, '\treturn preview_setting();\n', '\treturn hs_video_setting();\n'),),
     'SLIM_VIDEO: VC 0 DT 0x2b carries 2048x1152 per frame, VC_INFO2[0] declares 4096x3072'),
    ('dol-on', (('imx766_custom3_setting', '\t0x33D0, 0x00,', '\t0x33D0, 0x01,'),),
     'CUSTOM3: imx766_custom3_setting enables DOL'),
)


def skip(text, index):
    if text.startswith('//', index):
        return text.index('\n', index)
    if text.startswith('/*', index):
        return text.index('*/', index + 2) + 2
    if text[index] in '"\'':
        quote, index = text[index], index + 1
        while text[index] != quote:
            index += 2 if text[index] == '\\' else 1
        return index + 1
    return None


def code(text):
    out, index = [], 0
    while index < len(text):
        after = skip(text, index)
        if after is None:
            out.append(text[index])
            index += 1
            continue
        piece = text[index:after]
        out.append(piece if piece[0] in '"\'' else re.sub(r'[^\n]', ' ', piece))
        index = after
    return ''.join(out)


def block(text, start):
    depth = 0
    for index in range(text.index('{', start), len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if not depth:
                return text[text.index('{', start) + 1:index]
    raise SystemExit(f'{CHECK}: unbalanced braces at {start}')


def one(pattern, text, label, flags=re.M | re.S):
    found = list(re.finditer(pattern, text, flags))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: expected one {label}, found {len(found)}')
    return found[0]


def function(text, name):
    return block(text, one(r'^static [^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', text, name).start())


def number(token):
    return int(token, 0)


def tables(text):
    result = {}
    for match in re.finditer(r'^static kal_uint16 (imx766_\w+_setting)\[\] = \{(.*?)^\};', text, re.M | re.S):
        values = [number(token) for token in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', match.group(2))]
        if len(values) % 2:
            raise SystemExit(f'{CHECK}: {match.group(1)} has an odd number of values')
        result[match.group(1)] = list(zip(values[0::2], values[1::2]))
    return result


def vc_info(text):
    body = block(text, one(r'^static struct SENSOR_VC_INFO2_STRUCT SENSOR_VC_INFO2\[(\d+)\] = \{', text,
                           'SENSOR_VC_INFO2').start())
    entries, depth, start = [], 0, None
    for index, char in enumerate(body):
        if char == '{':
            depth += 1
            if depth == 1:
                start = index
        elif char == '}':
            depth -= 1
            if not depth:
                entries.append(body[start + 1:index])
    result = []
    for entry in entries:
        header = [number(token) for token in re.match(r'\s*((?:0x[0-9A-Fa-f]+\s*,\s*){6})', entry).group(1)
                  .replace(',', ' ').split()]
        channels = [(feature, number(vc), number(dt), number(width), number(height))
                    for feature, vc, dt, width, height in
                    re.findall(r'\{\s*(VC_\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\}', entry)]
        result.append((header, channels))
    return result


def switch_map(body, value_pattern):
    labels, result = [], {}
    for match in re.finditer(r'case MSDK_SCENARIO_ID_(\w+):|(default):|' + value_pattern, body):
        if match.group(1) or match.group(2):
            labels.append(match.group(1) or 'default')
            continue
        for label in labels:
            result[label] = number(match.group(match.lastindex))
        labels = []
    return result


def vc_index(text):
    body = function(text, 'get_vc_info_2')
    return switch_map(body, r'&SENSOR_VC_INFO2\[(\d+)\]')


def capacity(text):
    start = one(r'case SENSOR_FEATURE_GET_SENSOR_PDAF_CAPACITY:', text, 'PDAF capacity case').end()
    body = block(text, text.index('switch (*feature_data)', start))
    return switch_map(body, r'\*\(MUINT32 \*\)\(uintptr_t\)\(\*\(feature_data\+1\)\) = (\d+);')


def info(text):
    body = block(text, one(r'^static struct imgsensor_info_struct imgsensor_info = \{', text, 'imgsensor_info').start())
    result = {}
    for match in re.finditer(r'\.(\w+) = \{(.*?)\}', body, re.S):
        fields = dict(re.findall(r'\.(grabwindow_width|grabwindow_height) = (\d+)', match.group(2)))
        if len(fields) == 2:
            result[match.group(1)] = (int(fields['grabwindow_width']), int(fields['grabwindow_height']))
    return result


def setting_table(text, name, seen=()):
    if name in seen:
        raise SystemExit(f'{CHECK}: {name} calls itself')
    body = function(text, name)
    writes = re.findall(r'imx766_(?:seq|table)_write_cmos_sensor\((imx766_\w+_setting)\b', body)
    calls = re.findall(r'return (\w+_setting)\(\);', body)
    extra = [number(a) for a in re.findall(r'write_cmos_sensor_(?:8|16)\((0x[0-9A-Fa-f]+)', body)]
    if [a for a in extra if a in PD_REGISTERS]:
        raise SystemExit(f'{CHECK}: {name} writes PD registers outside its table: {extra}')
    if len(writes) == 1 and not calls:
        return writes[0]
    if len(calls) == 1 and not writes:
        return setting_table(text, calls[0], seen + (name,))
    raise SystemExit(f'{CHECK}: {name} does not write exactly one mode table: {writes} {calls}')


def scenario(text, name):
    control = function(text, 'control')
    handler = one(r'case MSDK_SCENARIO_ID_' + name + r':\s*return (\w+)\(', control, 'control case ' + name).group(1)
    body = function(text, handler)
    calls = sorted(set(re.findall(r'\b(\w+_setting)\(', body)))
    members = sorted(set(re.findall(r'imgsensor_info\.(\w+)\.pclk', body)))
    if len(calls) != 1 or len(members) != 1:
        raise SystemExit(f'{CHECK}: {handler} must call one *_setting and use one imgsensor_info mode: '
                         f'{calls} {members}')
    return setting_table(text, calls[0]), members[0]


def word(registers, address):
    return (registers[address] << 8) | registers[address + 1]


def sent(name, registers, errors, label):
    missing = sorted(a for a in PD_REGISTERS if a not in registers)
    if missing:
        raise SystemExit(f'{CHECK}: {name} does not set {", ".join(hex(a) for a in missing)}')
    if registers[0x33D0] & 1:
        errors.append(f'{label}: {name} enables DOL; this check models single-exposure output only')
    if (registers[0x0112], registers[0x0113]) != (0x0A, 0x0A):
        errors.append(f'{label}: {name} is not RAW10 output')
    for frame, (first, second) in enumerate(FRAMES, 1):
        one_pixel = (registers[first[0]], registers[first[1]])
        two_pixel = (registers[second[0]], registers[second[1]])
        if one_pixel == two_pixel:
            errors.append(f'{label}: {name} frame {frame} PIX_1 and PIX_2 share VC {one_pixel[0]} DT '
                          f'0x{one_pixel[1]:02x}')
    width, height = word(registers, 0x034C), word(registers, 0x034E)
    channels = {IMAGE: [width, height]}
    binning = tuple(registers[a] for a in (0x0900, 0x0901, 0x0902, 0x3200, 0x3201))
    pd = bool(registers[0x30B4] & 1)
    if pd:
        if binning not in BINNING:
            raise SystemExit(f'{CHECK}: {name} runs PD with binning {[hex(v) for v in binning]}, whose PD geometry '
                             f'is not attested')
        if width % 8 or height % 4:
            raise SystemExit(f'{CHECK}: {name} output {width}x{height} has no whole PD geometry')
        for pixel in FRAMES[0]:
            key = (registers[pixel[0]], registers[pixel[1]])
            if key == IMAGE:
                errors.append(f'{label}: {name} sends PD on the image channel')
                continue
            entry = channels.setdefault(key, [width * 5 // 8, 0])
            if entry[0] != width * 5 // 8:
                errors.append(f'{label}: {name} mixes line widths on VC {key[0]} DT 0x{key[1]:02x}')
            entry[1] += height // 4
    return channels, pd, (width, height), BINNING.get(binning, 'unknown')


def analyse(text):
    text = code(text)
    modes, indices, capacities, windows = tables(text), vc_index(text), capacity(text), info(text)
    vcs = vc_info(text)
    errors, rows = [], []
    for name in SCENARIOS:
        table, member = scenario(text, name)
        if table not in modes:
            raise SystemExit(f'{CHECK}: {table} is not a mode table')
        registers = dict(modes[table])
        channels, pd, size, binning = sent(table, registers, errors, name)
        if windows.get(member) != size:
            errors.append(f'{name}: imgsensor_info.{member} grab window {windows.get(member)} but {table} outputs '
                          f'{size[0]}x{size[1]}')
        index = indices.get(name, indices.get('default'))
        if index is None or index >= len(vcs):
            raise SystemExit(f'{CHECK}: {name} has no SENSOR_VC_INFO2 entry')
        header, declared = vcs[index]
        if header[0] != len(declared):
            errors.append(f'{name}: VC_INFO2[{index}] VC_Num {header[0]} but {len(declared)} channels')
        keys = [(vc, dt) for _, vc, dt, _, _ in declared]
        if len(set(keys)) != len(keys):
            errors.append(f'{name}: VC_INFO2[{index}] declares a channel twice')
        has_pd = False
        for feature, vc, dt, width, height in declared:
            if feature.startswith('VC_PDAF_STATS'):
                has_pd = True
                if (vc, dt) == IMAGE:
                    errors.append(f'{name}: VC_INFO2[{index}] declares PD on the image channel')
            elif feature == 'VC_STAGGER_NE':
                if (vc, dt) != IMAGE:
                    errors.append(f'{name}: VC_INFO2[{index}] image channel is VC {vc} DT 0x{dt:02x}')
            else:
                errors.append(f'{name}: VC_INFO2[{index}] declares {feature}, which no table sends')
            if (vc, dt) not in channels:
                errors.append(f'{name}: VC_INFO2[{index}] declares VC {vc} DT 0x{dt:02x}, '
                              f'which the sensor does not send')
                continue
            real = channels[(vc, dt)]
            if [width, height] != real:
                errors.append(f'{name}: VC {vc} DT 0x{dt:02x} carries {real[0]}x{real[1]} per frame, '
                              f'VC_INFO2[{index}] declares {width}x{height}')
        cap = capacities.get(name, capacities.get('default'))
        if cap and not (pd and has_pd):
            errors.append(f'{name}: PDAF capacity {cap} without PD output'
                          + ('' if has_pd else ' or a declared PD channel'))
        pd_text = ' '.join(f'VC{vc}/0x{dt:02x} {w}x{h}' for (vc, dt), (w, h) in sorted(channels.items())
                           if (vc, dt) != IMAGE)
        rows.append(f'{name:<20} {table:<28} {size[0]}x{size[1]} bin {binning:<4} VC_INFO2[{index}] '
                    f'PD {"on " + pd_text if pd else "off"} capacity {cap}')
    return errors, rows


def mutate(source, edits, label):
    for table, before, after in edits:
        if table is None:
            if source.count(before) != 1:
                raise SystemExit(f'{CHECK}: mutant {label}: expected one {before!r}')
            source = source.replace(before, after)
            continue
        match = one(r'^static kal_uint16 ' + table + r'\[\] = \{.*?^\};', source, table)
        body = match.group(0)
        if body.count(before) != 1:
            raise SystemExit(f'{CHECK}: mutant {label}: expected one {before!r} in {table}')
        source = source[:match.start()] + body.replace(before, after) + source[match.end():]
    return source


def main():
    parser = argparse.ArgumentParser(description=(
        'For every IMX766 scenario, follow control() to the mode table it writes and derive the CSI-2 channels the '
        'sensor sends: the RAW10 image on VC 0 DT 0x2B at the output size (0x034C/0x034E), and, when '
        'PHASE_PIX_OUT_EN 0x30B4 bit 0 is set, PD pixel types PIX_1 (VC/DT 0x3066/0x3067) and PIX_2 (0x3068/0x3069), '
        'each output_width*5/8 x output_height/4 (the PD geometry of the OnePlus/OPPO 20817 and MediaTek common v1_1 '
        'IMX766 drivers for these binning setups, and the pd buf size and binning (2, 4) the stock HAL logs). Every '
        'channel that get_vc_info_2 declares must carry exactly its declared size, PIX_1 and PIX_2 must not share a '
        'channel in any frame, PDAF capacity needs PD output and a PD channel, and the grab window must match the '
        'table; every mutation must fail'))
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    source = (tree / SOURCE).read_text()
    errors, rows = analyse(source)
    for row in rows:
        print(row)
    if errors:
        raise SystemExit(f'{CHECK}: FAIL\n' + '\n'.join(errors))
    for label, edits, expected in MUTANTS:
        found, _ = analyse(mutate(source, edits, label))
        if not any(expected in error for error in found):
            raise SystemExit(f'{CHECK}: mutant {label} was not caught at {expected!r}: {found}')
        print(f'PASS mutant {label}: {expected}')
    print(f'{CHECK}: PASS {len(rows)} scenarios match their VC_INFO2 channels, {len(MUTANTS)} mutants caught; '
          f'kernel build and device check pending')


if __name__ == '__main__':
    main()
