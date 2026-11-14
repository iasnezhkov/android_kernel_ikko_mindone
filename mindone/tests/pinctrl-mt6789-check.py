# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from baseline_source import strip_comments

CHECK = 'pinctrl-mt6789'
FIXTURE = Path(__file__).resolve().parent / CHECK
FORK_REV = '88e4a728341a5f8d98f6a6b805138f086cb73d5f'
FORK_DIR = 'modules/pinctrl_mediatek/'
FORK_FILES = {
    'mtk-eint.c': '201da4c596ce556ff33d978448f74ced69e9a8ea5700030e4368f114a472ac94',
    'mtk-eint.h': '44917769800e46d81fc86011c3a104a4b906f03a15daf28e743be1918d5f94e1',
    'pinctrl-core-api.h': '212617375af97cbc969b2e4c0a01ca0d1b7d1ae829729d0f3aa70648ee74e170',
    'pinctrl-mt6789.c': 'bd3f725f7a1e6c3f094edc7e012530725972db3558ae257de85bb9af6e756745',
    'pinctrl-mtk-common-v2.c': '5b3eae4b55661dcaf9256c6f69f09c4ced01b61bbd6acefdbf4fd0e342dc13ae',
    'pinctrl-mtk-common-v2.h': '755f4681b5fa7f0015c57c4a0cfcc71ce2680750ab2a78525b483e3ad986c097',
    'pinctrl-mtk-mt6789.h': '009d3fab4f5a221a8bad66163c74bde86a9960daed234a3aa516c61c84137cda',
    'pinctrl-paris.c': 'febb467d822c21db8ad489de7b3de766b5d0dbe0dee8dcea8f29897653a60743',
    'pinctrl-paris.h': 'f3b179d8ad605ca5c56b075cd09b588b4843160d383b9692d06c1b13775a6a82',
}
FORK_DT = {
    'dts/mt6789-pinctrl.dtsi': '75d095647c7b487b9276dee49c015a69d6e53468fbc6a76fd1f22194c49c4edd',
    'dts/mt6789-bus.dtsi': '9afbbd1e4b16e5fd4a64022465fb5ef8c387de9b23eb2a6ab1baf4b78b21bdb5',
    'dts/mindone-overlay.dtso': '11b0f17f601b520f1c535e32ba4235d732240b0ce14d1cf16a322d138cf02ee8',
}
ACK_FILES = {
    'drivers/pinctrl/mediatek/pinctrl-paris.c': '7023ac596d67185f668b26dbfc7e601753e9666a7e23ecea23ad99f172604bce',
    'drivers/pinctrl/mediatek/pinctrl-paris.h': '2785801e5113c50a0feec28b9d39a26f18cd16baa51af3d48a4dde9e1fdb3285',
    'drivers/pinctrl/mediatek/pinctrl-mtk-common-v2.c': 'f8615055a2df94dd69e57c8dc7e1be59ab7cceddd50163f6de0c5a239a1565e4',
    'drivers/pinctrl/mediatek/pinctrl-mtk-common-v2.h': '8691ca4ef77adbb488f1388562c4930affc911778a65933eb9549a92f8086ff4',
    'drivers/pinctrl/mediatek/mtk-eint.c': '4c8ba3e8b8ff1508f1b0b6366ccbbdbd2310174fde2b8cb75455c84158ea482b',
    'drivers/pinctrl/mediatek/mtk-eint.h': '05fe8048a7b0bb940b970d3447e39267de6fc1edeb923ffd3015bc464e30156d',
    'drivers/pinctrl/core.h': '337cf554cbeb5e803ce18cdb72bc5013379d698da4931151368a6fac8ceb61a4',
    'drivers/pinctrl/pinconf.h': '3a4c3f3e9817a0a8fc694327faa4115568f414e6515b784df7420f56a3d7be51',
    'drivers/pinctrl/pinmux.h': '9e56f8732405a7ce250a61c9bf6c31a8811c4c6e0523e5baa162bc6ff1509d4c',
    'drivers/pinctrl/pinctrl-utils.h': '9096c21fd4c82bad5f9f9e63cdd5a113aa2eea5773b4788ea7bda7cac4ac7be6',
    'drivers/pinctrl/pinconf-generic.c': '16a83e8a8c384d0cfb0451878ee8a7b83b9819103ed35c30842ee9cb57d68703',
    'include/linux/pinctrl/pinconf-generic.h': '40ba191c7f55665323538af014c8236687f2db286f6dac3aeb9715b411ac6f60',
    'include/dt-bindings/pinctrl/mt65xx.h': 'f65c0ac3ff79e3100adb89f17333792ad71998b108992c2734e0a81ba23c4b8f',
}
COPY_ROOT = 'mindone/modules/include/'
COPIES = ('drivers/pinctrl/mediatek/pinctrl-paris.h', 'drivers/pinctrl/mediatek/pinctrl-mtk-common-v2.h',
          'drivers/pinctrl/mediatek/mtk-eint.h', 'drivers/pinctrl/core.h', 'drivers/pinctrl/pinconf.h',
          'drivers/pinctrl/pinmux.h', 'drivers/pinctrl/pinctrl-utils.h')
CANDIDATE = 'mindone/modules/pinctrl_mt6789/'
NEW_DT = ('mindone/dts/mt6789-pinctrl.dtsi', 'mindone/dts/mt6789-bus.dtsi', 'mindone/dts/mindone-overlay.dtso')
PINFUNC = 'mindone/dts/include/dt-bindings/pinctrl/mt6789-pinfunc.h'
DT_MACROS = {'GIC_SPI': 0, 'GIC_PPI': 1, 'IRQ_TYPE_NONE': 0, 'IRQ_TYPE_EDGE_RISING': 1, 'IRQ_TYPE_EDGE_FALLING': 2,
             'IRQ_TYPE_EDGE_BOTH': 3, 'IRQ_TYPE_LEVEL_HIGH': 4, 'IRQ_TYPE_LEVEL_LOW': 8, 'GPIO_ACTIVE_HIGH': 0,
             'GPIO_ACTIVE_LOW': 1}
PARIS_FUNCTIONS = ('mtk_drv_adv_to_uA', 'mtk_drv_uA_to_adv', 'mtk_pinmux_gpio_request_enable',
                   'mtk_pinmux_gpio_set_direction', 'mtk_pinconf_get', 'mtk_pinconf_set',
                   'mtk_pctrl_find_function_by_pin', 'mtk_pctrl_is_function_valid', 'mtk_hw_get_value_wrap',
                   'mtk_pctrl_show_one_pin', 'mtk_pmx_set_mux', 'mtk_pconf_group_set')
PARIS_ARRAYS = ('mtk_drv_adv_uA', 'mtk_conf_items', 'mtk_gpio_functions', 'mtk_custom_bindings')
FORK_ONLY_EH = frozenset([*range(15, 23), *range(126, 130), 186, 187])
EXPECTED_FORK_CALLBACKS = {'bias_set_combo': 'mtk_pinconf_bias_set_combo', 'bias_get_combo': 'mtk_pinconf_bias_get_combo',
                           'adv_drive_set': 'mtk_pinconf_adv_drive_set', 'adv_drive_get': 'mtk_pinconf_adv_drive_get'}
EXPECTED_ACK_CALLBACKS = {'bias_set_combo': 'mtk_pinconf_bias_set_combo', 'bias_get_combo': 'mtk_pinconf_bias_get_combo',
                          'drive_set': 'mtk_pinconf_drive_set_raw', 'drive_get': 'mtk_pinconf_drive_get_raw',
                          'adv_drive_set': 'mtk_pinconf_adv_drive_set_raw',
                          'adv_drive_get': 'mtk_pinconf_adv_drive_get_raw'}
FORK_FLAGS = 0x3
GPIO_LABEL = 'pinctrl_paris'
DT_CHANGES = {
    'ts_scl_suspend': 'touch sleep keeps SCL0 in I2C mode (ts_scl_active): output-low drove the pull-up of the powered touch IC',
    'ts_sda_suspend': 'touch sleep keeps SDA0 in I2C mode (ts_sda_active): output-low drove the pull-up of the powered touch IC',
}
SD_PINS = 'not in gpio_init_default; the live DIR 0 under the MSDC1 function (SD card)'
MMC0_PINS = 'mmc0 is disabled, its states are never applied; the live DIR is 0'
AUDIO_PINS = 'gpio_init_default sets DIR 0 and no state or GPIO user sets DIR 1'
BOOT_DEPENDENT = {
    9: 'ts_int_active sets SMT 1 through input-schmitt-enable; gpio_init_default and the EINT request set SMT 1 too',
    130: 'EH bits 2:1 are 3 on all twenty I2C pins in the live dump (drive-strength-adv 0x7, 0x6 on 130 and 131)',
    131: 'EH bits 2:1 are 3 on all twenty I2C pins in the live dump (drive-strength-adv 0x7, 0x6 on 130 and 131)',
    **{pin: AUDIO_PINS for pin in (50, 51, 52, 53, 169, 171, 172, 173, 175, 176)},
    **{pin: MMC0_PINS for pin in (56, 58, 59, 60, 61, 62, 63, 64, 65)},
    **{pin: SD_PINS for pin in (72, 73, 74, 75, 76)},
}
CC_FLAGS = ['-std=gnu11', '-O1', '-w']


def usage():
    return ('usage: pinctrl-mt6789-check.py [TREE] [--ack-repo REPO] [--live DUMP] [--write-expected DIR]\n'
            '                               [--compare DEVICE] [--skip-mutations]\n'
            'Proves that the MT6789 data driver on the ACK pinctrl-paris, pinctrl-mtk-common-v2 and mtk-eint gives\n'
            'the same register fields, pins, functions, pull types, EINT map and debounce table as the BSP fork at\n'
            + FORK_REV[:12] + ', that every pinctrl state of the device tree writes the same registers through both\n'
            'drivers, and that the fork harness reproduces the live debugfs dump in ' + CHECK + '/live. TREE holds\n'
            'mindone/ and, in a combined test tree, the ACK sources; otherwise REPO (default TREE/ack-6.18) gives the\n'
            'ACK files at the commit of mindone/ack-base. DUMP (default ' + CHECK + '/live) is a fork dump: pins.txt,\n'
            'pinconf-pins.txt, pinmux-pins.txt, pingroups.txt, gpio-ranges.txt of the pinctrl debugfs, gpio.txt of\n'
            '/sys/kernel/debug/gpio and interrupts-eint.txt with the mtk-eint lines of /proc/interrupts. DIR receives\n'
            'the pins and pinconf-pins lines the ACK driver must print for that register state; DEVICE holds pins.txt\n'
            'and pinconf-pins.txt read from the ACK driver and is compared with them, input levels (DI) masked.')


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def run(argv, cwd=None):
    result = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(' '.join(map(str, argv)) + ' failed: ' + result.stderr.strip()[-2000:])
    return result.stdout


class Sources:
    def __init__(self, tree, ack_repo):
        self.tree = tree
        self.ack_repo = ack_repo
        self.cache = {}

    def device_git(self, rev, path):
        return run(['git', '-C', str(self.tree / 'mindone'), 'show', rev + ':' + path])

    def fork(self, name):
        key = ('fork', name)
        if key not in self.cache:
            text = self.device_git(FORK_REV, FORK_DIR + name)
            if sha(text) != FORK_FILES[name]:
                raise ValueError(f'fork {name} at {FORK_REV[:12]} differs from its pinned sha256')
            self.cache[key] = text
        return self.cache[key]

    def fork_dt(self, path):
        key = ('fork-dt', path)
        if key not in self.cache:
            text = self.device_git(FORK_REV, path)
            if sha(text) != FORK_DT[path]:
                raise ValueError(f'{path} at {FORK_REV[:12]} differs from its pinned sha256')
            self.cache[key] = text
        return self.cache[key]

    def ack(self, path):
        key = ('ack', path)
        if key not in self.cache:
            local = self.tree / path
            if local.is_file():
                text = local.read_text()
            else:
                base = (self.tree / 'mindone/ack-base').read_text().split()
                text = run(['git', '-C', str(self.ack_repo), 'show', base[1] + ':' + path])
            if sha(text) != ACK_FILES[path]:
                raise ValueError(f'ACK {path} differs from its pinned sha256 (r13)')
            self.cache[key] = text
        return self.cache[key]

    def current(self, path):
        return (self.tree / path).read_text()


def normalized(text):
    return [line.rstrip() for line in strip_comments(text).split('\n') if line.strip()]


def check_copies(sources, overrides=None):
    failures = []
    for path in COPIES:
        copy = (overrides or {}).get(path) or sources.current(COPY_ROOT + path)
        if normalized(copy) != normalized(sources.ack(path)):
            failures.append(f'{COPY_ROOT}{path} is not ACK {path} without comments')
    return failures


def matching(text, index, opening, closing):
    depth = 0
    for position in range(index, len(text)):
        if text[position] == opening:
            depth += 1
        elif text[position] == closing:
            depth -= 1
            if not depth:
                return position
    raise ValueError('unbalanced ' + opening)


def definition(text, name, array=False):
    pattern = (r'^(?:static\s+)?(?:const\s+)?[\w \t]*\b' + name + r'\s*\[\s*\]\s*=\s*\{' if array else
               r'^[A-Za-z_][\w \t\*]*\n?[\w \t\*]*\b' + name + r'\s*\(')
    for match in re.finditer(pattern, text, re.M):
        if array:
            end = matching(text, match.end() - 1, '{', '}')
            return text[match.start():text.index(';', end) + 1]
        close = matching(text, match.end() - 1, '(', ')')
        rest = text[close + 1:].lstrip()
        if rest.startswith('{'):
            start = text.index('{', close)
            return text[match.start():matching(text, start, '{', '}') + 1]
    return None


def defines(text):
    out, block = [], None
    for line in text.split('\n'):
        if block is not None:
            block.append(line)
            if not line.rstrip().endswith('\\'):
                out.append('\n'.join(block))
                block = None
            continue
        if re.match(r'\s*#\s*define\s', line):
            if line.rstrip().endswith('\\'):
                block = [line]
            else:
                out.append(line)
    return out


def paris_part(paris, fork):
    text = strip_comments(paris)
    pieces = []
    for name in PARIS_FUNCTIONS + PARIS_ARRAYS:
        body = definition(text, name, name in PARIS_ARRAYS)
        if body is not None:
            pieces.append((text.index(body), body))
    if not any('mtk_pconf_group_set' in body for _, body in pieces):
        raise ValueError('pinctrl-paris.c without mtk_pconf_group_set')
    head = ['#include "pinctrl-paris.h"']
    if fork:
        head.append('#include <mindone/compat-pinconf.h>')
    head += [d for d in defines(text) if '__PINCTRL' not in d]
    return '\n'.join(head) + '\n' + '\n\n'.join(body for _, body in sorted(pieces)) + '\n'


def prototypes(headers):
    found = {}
    for text in headers:
        text = strip_comments(text)
        for match in re.finditer(r'^([A-Za-z_][\w \t\*]*?\b(\w+)\s*\(((?:[^;{}()]|\([^()]*\))*)\))\s*;', text, re.M | re.S):
            whole, name = match.group(1), match.group(2)
            if whole.lstrip().startswith(('static', 'typedef', 'return', '#')) or name in found:
                continue
            found[name] = ('function', whole)
        for match in re.finditer(r'^extern\s+([^;()]*?\b(\w+)\s*(\[\s*\])?)\s*;', text, re.M):
            found.setdefault(match.group(2), ('data', match.group(1)))
    return found


def stubs(declared, sources):
    joined = '\n'.join(sources)
    lines = []
    for name, (kind, text) in sorted(declared.items()):
        if re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', joined) or re.search(r'\b' + name + r'\s*(\[\s*\])?\s*=', joined):
            continue
        if kind == 'data':
            lines.append(text.replace('[]', '[16]') + ';')
        elif re.match(r'\s*void\s+\w', text):
            lines.append(text + ' {}')
        else:
            lines.append(text + ' { return 0; }')
    return '\n'.join(lines) + '\n'


def pinconf_generic_gen(sources):
    header = strip_comments(sources.ack('include/linux/pinctrl/pinconf-generic.h'))
    core = strip_comments(sources.ack('drivers/pinctrl/pinconf-generic.c'))
    enum = re.search(r'enum pin_config_param \{.*?\};', header, re.S).group(0)
    packed = re.search(r'#define PIN_CONF_PACKED\(.*', header).group(0)
    dump = re.search(r'#define PCONFDUMP\(a, b, c, d\).*?\}\n', header, re.S).group(0)
    item = re.search(r'struct pin_config_item \{.*?\};', header, re.S).group(0)
    params = re.search(r'struct pinconf_generic_params \{.*?\};', header, re.S).group(0)
    helpers = re.findall(r'static inline [^\n]*pinconf_to_config_\w+\([^)]*\)\s*\{.*?\n\}', header, re.S)
    items = re.search(r'static const struct pin_config_item conf_items\[\] = \{.*?\};', core, re.S).group(0)
    return '\n'.join([enum, packed, *helpers, dump, item, params,
                      items.replace('conf_items[]', 'harness_conf_items[]')]) + '\n'


def dt_params(sources, paris):
    core = strip_comments(sources.ack('drivers/pinctrl/pinconf-generic.c'))
    body = re.search(r'dt_params\[\] = \{(.*?)\};', core, re.S).group(1)
    generic = [(name, param, int(default)) for name, param, default in
               re.findall(r'\{\s*"([^"]+)",\s*(\w+),\s*(\d+)\s*,?\s*\}', body)]
    custom_body = re.search(r'mtk_custom_bindings\[\] = \{(.*?)\};', strip_comments(paris), re.S).group(1)
    custom = [(name, param, int(default)) for name, param, default in
              re.findall(r'\{\s*"([^"]+)",\s*(\w+),\s*(\d+)\s*\}', custom_body)]
    return generic + custom


class Engine:
    def __init__(self, sources, variant, workdir, driver=None, nbase=7, name=None):
        self.variant = variant
        root = Path(workdir) / (name or variant)
        if root.exists():
            shutil.rmtree(root)
        mediatek = root / 'drv/pinctrl/mediatek'
        mediatek.mkdir(parents=True)
        inc = root / 'inc'
        gen = root / 'gen'
        gen.mkdir()
        if variant == 'fork':
            files = {name: sources.fork(name) for name in FORK_FILES}
            common, paris = files['pinctrl-mtk-common-v2.c'], files['pinctrl-paris.c']
            for name in ('pinctrl-paris.h', 'pinctrl-mtk-common-v2.h', 'mtk-eint.h', 'pinctrl-core-api.h',
                         'pinctrl-mt6789.c', 'pinctrl-mtk-mt6789.h'):
                (mediatek / name).write_text(files[name])
            headers = [files[name] for name in ('pinctrl-paris.h', 'pinctrl-mtk-common-v2.h', 'mtk-eint.h',
                                                'pinctrl-core-api.h')]
        else:
            common = sources.ack('drivers/pinctrl/mediatek/pinctrl-mtk-common-v2.c')
            paris = sources.ack('drivers/pinctrl/mediatek/pinctrl-paris.c')
            headers = []
            for path in COPIES:
                text = sources.current(COPY_ROOT + path)
                target = root / 'drv' / path[len('drivers/'):]
                target.parent.mkdir(parents=True, exist_ok=True)
                if '/mediatek/' in path:
                    target.write_text(text)
                    headers.append(text)
                else:
                    target.write_text('')
            for name, text in (driver or {}).items():
                (mediatek / name).write_text(text)
        (mediatek / 'common.c').write_text(common)
        part = paris_part(paris, variant == 'fork')
        (gen / 'paris-part.c').write_text(part)
        driver_text = (mediatek / 'pinctrl-mt6789.c').read_text()
        (gen / 'stubs-gen.h').write_text(stubs(prototypes(headers), [common, part, driver_text]))
        (gen / 'pinconf-generic-gen.h').write_text(pinconf_generic_gen(sources))
        texts = [common, paris, driver_text, part] + headers
        for text in texts:
            for header in re.findall(r'#\s*include\s*<([^>]+)>', text):
                target = inc / header
                target.parent.mkdir(parents=True, exist_ok=True)
                if header == 'dt-bindings/pinctrl/mt65xx.h':
                    target.write_text(sources.ack('include/dt-bindings/pinctrl/mt65xx.h'))
                elif header.startswith('mindone/'):
                    target.write_text(sources.current('mindone/modules/include/' + header))
                elif not target.exists():
                    target.write_text('#include "kernel.h"\n')
        for header in ('linux/version.h', 'linux/pinctrl/pinconf-generic.h'):
            target = inc / header
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                target.write_text('#include "kernel.h"\n')
        self.binary = root / 'engine'
        defines_ = ['-DHARNESS_ACK'] if variant == 'ack' else ['-DHARNESS_NBASE=' + str(nbase)]
        run(['cc', *CC_FLAGS, *defines_, '-DHARNESS_DRIVER="' + str(mediatek / 'pinctrl-mt6789.c') + '"',
             '-DHARNESS_COMMON="' + str(mediatek / 'common.c') + '"', '-I', str(gen), '-I', str(inc), '-I', str(mediatek),
             '-I', str(FIXTURE), str(FIXTURE / 'engine.c'), '-o', str(self.binary)])
        self.process = subprocess.Popen([str(self.binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def ask(self, line):
        self.process.stdin.write(line + '\n')
        self.process.stdin.flush()
        answer = self.process.stdout.readline()
        if not answer:
            raise RuntimeError(f'{self.variant} engine stopped on: {line}')
        return json.loads(answer)

    def close(self):
        self.process.stdin.close()
        self.process.wait()


def dts_tokens(text):
    text = strip_comments(text)
    text = re.sub(r'^\s*#\s*(?:include|define|undef|if|ifdef|ifndef|elif|else|endif)\b.*$', '', text, flags=re.M)
    return re.findall(r'"(?:\\.|[^"\\])*"|<[^>]*>|\[[^\]]*\]|[{};=]|/[\w-]+/|[^\s{};=<>"\[\]]+', text)


def cell(word, macros):
    word = word.strip()
    if word.startswith('&'):
        return word
    names = re.findall(r'(?<!\w)[A-Za-z_]\w*', word)
    if any(name not in macros for name in names):
        return word
    expression = re.sub(r'(?<!\w)[A-Za-z_]\w*', lambda m: str(macros[m.group(0)]), word)
    if not re.fullmatch(r'[\d\sxXa-fA-F()+\-*|<>&]+', expression):
        return word
    return int(eval(expression, {'__builtins__': {}}))


def dts_parse(text, macros):
    tokens = dts_tokens(text)
    roots = []

    def node(i):
        props, children = {}, []
        while tokens[i] != '}':
            label = None
            while tokens[i].endswith(':') and not tokens[i].startswith('"'):
                label = tokens[i][:-1]
                i += 1
            name = tokens[i]
            if tokens[i + 1] == '{':
                child, i = node(i + 2)
                child.update(name=name, label=label)
                children.append(child)
                continue
            if tokens[i + 1] == ';':
                props[name] = True
                i += 2
                continue
            if tokens[i + 1] != '=':
                raise ValueError('bad DT syntax near ' + ' '.join(tokens[i:i + 6]))
            j, values = i + 2, []
            while tokens[j] != ';':
                if tokens[j] != ',':
                    values.append(tokens[j])
                j += 1
            parsed = []
            for value in values:
                if value.startswith('<'):
                    parsed += [cell(word, macros) for word in re.findall(r'\([^()]*\)|\S+', value[1:-1])]
                elif value.startswith('"'):
                    parsed.append(value[1:-1])
                else:
                    parsed.append(value)
            props[name] = parsed
            i = j + 1
        return {'props': props, 'children': children}, i + 2

    i = 0
    while i < len(tokens):
        if tokens[i] in ('/dts-v1/', '/plugin/', ';'):
            i += 1
            continue
        label = None
        while tokens[i].endswith(':'):
            label = tokens[i][:-1]
            i += 1
        name = tokens[i]
        if tokens[i + 1] != '{':
            raise ValueError('bad DT top level near ' + ' '.join(tokens[i:i + 6]))
        tree, i = node(i + 2)
        tree.update(name=name, label=label)
        roots.append(tree)
    return roots


def walk(tree):
    yield tree
    for child in tree['children']:
        yield from walk(child)


def pinctrl_dt(texts, macros):
    pio, eint, states = None, None, []
    for text in texts:
        for root in dts_parse(text, macros):
            for item in walk(root):
                if item.get('label') == 'pio':
                    pio = item
                if item.get('label') == 'eint':
                    eint = item
            if root['name'] == '&pio':
                states += root['children']
    if pio is None:
        raise ValueError('no pio node')
    return {'pio': pio['props'], 'eint': eint['props'] if eint else None, 'states': pio['children'] + states}


def pinfunc_macros(text):
    text = re.sub(r'MTK_PIN_NO\((\d+)\)', r'(\1 << 8)', text)
    return {name: cell(value, {}) for name, value in
            re.findall(r'^#define\s+(PINMUX_GPIO\w+)\s+(\([^\n]*\)|\S+)', text, re.M)} | DT_MACROS


def state_actions(dt, params, ids):
    actions = {}
    for state in dt['states']:
        for sub in state['children']:
            configs = []
            for name, param, default in params:
                if name in sub['props']:
                    value = sub['props'][name]
                    configs.append((ids[param], default if value is True else value[0]))
            for value in sub['props'].get('pinmux', []):
                pin, func = value >> 8, value & 0xf
                actions.setdefault(pin, {}).setdefault(state['name'], []).append((func, configs))
    return actions


def reference_actions(old, new):
    def pins(actions, name):
        return {p: s[name] for p, s in actions.items() if name in s}
    names = {name for states in (*old.values(), *new.values()) for name in states}
    changed = {name for name in names if set(pins(old, name)) != set(pins(new, name))}
    reference = {}
    for actions, keep in ((old, lambda name: name not in DT_CHANGES), (new, lambda name: name in DT_CHANGES)):
        for pin, states in actions.items():
            for name, steps in states.items():
                if keep(name):
                    reference.setdefault(pin, {})[name] = steps
    return reference, changed


def param_ids(header):
    enum = re.search(r'enum pin_config_param \{(.*?)\};', strip_comments(header), re.S).group(1)
    ids, value = {}, -1
    for entry in enum.split(','):
        entry = entry.strip()
        if not entry:
            continue
        name, _, explicit = entry.partition('=')
        value = int(explicit, 0) if explicit.strip() else value + 1
        ids[name.strip()] = value
    for index, name in enumerate(('MTK_PIN_CONFIG_TDSEL', 'MTK_PIN_CONFIG_RDSEL', 'MTK_PIN_CONFIG_PU_ADV',
                                  'MTK_PIN_CONFIG_PD_ADV', 'MTK_PIN_CONFIG_DRV_ADV'), 1):
        ids[name] = ids['PIN_CONFIG_END'] + index
    return ids


def functions(funcs):
    return [tuple(f) for f in funcs or [] if f[1] is not None]


def compare_tables(fork, ack, fork_lookup, ack_lookup, fork_eint, ack_eint, fork_virt, ack_virt):
    errors, notes = [], []
    if (fork['npins'], fork['ngrps'], fork['nfuncs'], fork['gpio_m'], fork['ies_present']) != \
            (ack['npins'], ack['ngrps'], ack['nfuncs'], ack['gpio_m'], ack['ies_present']):
        errors.append('pin, group or function counts, gpio_m or ies_present differ')
    for a, b in zip(fork['pins'], ack['pins']):
        if a[:2] != b[:2]:
            errors.append(f'pin {a[0]} {a[1]} is {b[0]} {b[1]}')
            continue
        if a[3] != b[3]:
            errors.append(f'pin {a[0]} EINT {a[3]} is {b[3]}')
        if fork_virt[a[0]] == 0 and a[2] != b[2]:
            errors.append(f'pin {a[0]} EINT mode {a[2]} is {b[2]}')
        if a[4] != b[4]:
            errors.append(f'pin {a[0]} drv_n {a[4]} is {b[4]}')
        if functions(a[5]) != functions(b[5]):
            errors.append(f'pin {a[0]} functions {functions(a[5])} are {functions(b[5])}')
    if len(fork['pins']) != len(ack['pins']):
        errors.append('pin tables have different lengths')
    if fork_virt != ack_virt:
        errors.append('virtual GPIO pins differ: ' + str([i for i, (a, b) in enumerate(zip(fork_virt, ack_virt)) if a != b]))
    fork_pull, ack_pull = fork['pull_type'] or [], ack['pull_type'] or []
    bad_pull = [i for i in range(len(fork_virt))
                if i >= len(ack_pull) or ack_pull[i] != (0 if fork_virt[i] else fork_pull[i])]
    if bad_pull:
        errors.append('pull types differ at pins ' + str(bad_pull))
    notes.append(f'pull types equal on {fork_virt.count(0)} real pins; the {fork_virt.count(1)} virtual pins have 0 '
                 'where the fork array of 188 entries is read past its end')
    names = {name: name for name in ack_lookup} | {'DRV_ADV': 'DRV_EH'}
    compared = 0
    for name, fork_name in names.items():
        for pin, (a, b) in enumerate(zip(fork_lookup[fork_name], ack_lookup[name])):
            compared += 1
            if a == b:
                continue
            if name == 'DRV_ADV' and pin in FORK_ONLY_EH and a[0] == 0 and b == [-524]:
                continue
            errors.append(f'pin {pin} field {name}: fork {a} ack {b}')
    for name in ack_lookup:
        for pin, entry in enumerate(ack_lookup[name]):
            if entry[0] == 0 and entry[2] % 16:
                errors.append(f'pin {pin} field {name} sits at offset {entry[2]:#x}, outside the SET/CLR model')
    notes.append(f'{compared} pin fields compared')
    if fork['callbacks'] != {key: EXPECTED_FORK_CALLBACKS.get(key) for key in fork['callbacks']}:
        errors.append('fork callbacks are not the pinned ones: ' + str(fork['callbacks']))
    if fork['capability_flags'] != FORK_FLAGS:
        errors.append('fork capability flags are not RACE_FREE_ACCESS | DRIVE_SET_RAW')
    if ack['callbacks'] != {key: EXPECTED_ACK_CALLBACKS.get(key) for key in ack['callbacks']}:
        errors.append('candidate callbacks differ from raw drive, raw advanced drive and combo bias: ' + str(ack['callbacks']))
    if ack['driver']['probe'] != 'mtk_paris_pinctrl_probe' or not ack['driver']['pm'] or \
            ack['driver']['compatible'] != [['mediatek,mt6789-pinctrl', 1]]:
        errors.append('candidate driver is not bound to mediatek,mt6789-pinctrl with the paris probe and PM ops')
    if fork['driver']['compatible'] != [['mediatek,mt6789-pinctrl', 0]] or not fork['driver']['pm']:
        errors.append('fork driver binding is not the pinned one')
    if ack['grps'] or ack['funcs'] or ack['driver_data'] or ack['eint_regs'] or ack['eint_pin'] or ack['pin_rsel']:
        errors.append('candidate sets grps, funcs, driver_data, eint_regs, eint_pin or pin_rsel')
    eh = {pin for pin, mux in fork['eh']}
    if eh != set(range(130, 150)) or {mux for pin, mux in fork['eh']} != {1}:
        errors.append('fork EH pin list is not pins 130-149 with function 1')
    if fork_eint != ack_eint:
        errors.append('EINT to GPIO maps differ: ' + str([(a, b) for a, b in zip(fork_eint, ack_eint) if a != b][:5]))
    return errors, notes


def fork_debounce_table(text):
    body = re.search(r'static const unsigned int debounce_time\[\] = \{([^}]*)\}', strip_comments(text)).group(1)
    return [int(v) for v in body.replace('\n', ' ').split(',') if v.strip()]


def eint_regs(text):
    body = re.search(r'mtk_generic_eint_regs = \{(.*?)\};', strip_comments(text), re.S).group(1)
    return {name: int(value, 16) for name, value in re.findall(r'\.(\w+)\s*=\s*(0x[0-9a-fA-F]+)', body)}


def fork_debounce_index(table, value):
    for index, limit in enumerate(table):
        if value <= limit:
            return index
    return len(table) - 1


def ack_debounce_index(table, value):
    count = next(i for i, v in enumerate(table) if v == 0)
    for index in range(count):
        if value <= table[index]:
            return index
    return count


def compare_eint(sources, fork_dt, new_dt, ack):
    errors, notes = [], []
    old = fork_dt['eint']
    if not old or old['compatible'] != ['mediatek,mt6983-eint'] or 'mediatek,pins' in old:
        errors.append('fork DT EINT node is not the legacy mt6983-eint node without mediatek,pins')
        return errors, notes
    total = old['mediatek,total-pin-number'][0]
    hw = ack.get('eint_hw')
    if not hw:
        return ['candidate has no eint_hw'], notes
    if hw['ap_num'] != total or old['mediatek,instance-num'] != [1]:
        errors.append(f'ap_num {hw["ap_num"]} is not the fork total-pin-number {total} of one instance')
    if hw['db_cnt'] != 32:
        errors.append('db_cnt is not 32, the fork legacy rule debounce = (index < 32)')
    if any(((index >> 5) & hw['port_mask']) != index >> 5 for index in range(total)):
        errors.append('port_mask truncates an EINT port')
    if hw['ports'] != (total + 31) // 32:
        errors.append('ports is not the number of 32-bit EINT ports')
    fork_table = fork_debounce_table(sources.fork('mtk-eint.c'))
    if hw['db_time'] != fork_table + [0]:
        errors.append(f'debounce table {hw["db_time"]} is not the fork table {fork_table}')
    else:
        values = sorted({0, 1, *[v + d for v in fork_table for d in (-1, 0, 1)], 256000, 50000, 10000, 1000000})
        for value in values:
            a, b = fork_debounce_index(fork_table, value), ack_debounce_index(hw['db_time'], value)
            if a != b and value <= fork_table[-1]:
                errors.append(f'debounce {value} us selects {a} on the fork and {b} on ACK')
        notes.append(f'debounce selection equal for {len(values)} values up to {fork_table[-1]} us; above it the '
                     f'fork clamps to {len(fork_table) - 1}, ACK writes {len(fork_table)}')
    fork_regs, ack_regs = eint_regs(sources.fork('mtk-eint.c')), eint_regs(sources.ack('drivers/pinctrl/mediatek/mtk-eint.c'))
    if {k: v for k, v in fork_regs.items() if k != 'raw_stat'} != ack_regs:
        errors.append('generic EINT register offsets differ')
    reg, names = new_dt['pio'].get('reg', []), new_dt['pio'].get('reg-names', [])
    bases = [reg[i + 1] for i in range(0, len(reg), 4)]
    if names[:len(ack['base_names'] or [])] != ack['base_names'] or len(names) != len(bases):
        errors.append('candidate base_names are not the DT reg-names of pio')
    elif names[len(ack['base_names']):] != ['eint'] or bases[len(ack['base_names'])] != old['reg'][1]:
        errors.append('pio does not describe the EINT block after the pinctrl bases')
    if new_dt['pio'].get('interrupts') != old['interrupts']:
        errors.append('pio interrupts are not the EINT interrupt of the fork EINT node')
    if 'interrupt-controller' not in new_dt['pio'] or new_dt['pio'].get('#interrupt-cells') != [2]:
        errors.append('pio is not an interrupt controller with two cells')
    old_reg = fork_dt['pio']['reg']
    if [old_reg[i + 1] for i in range(0, len(old_reg), 4)] != bases[:len(old_reg) // 4]:
        errors.append('pinctrl register bases moved')
    if new_dt['pio'].get('compatible') != ['mediatek,mt6789-pinctrl']:
        errors.append('pio compatible changed')
    return errors, notes


def boot_images(fork_tables, live, gpio_init):
    images = {}
    for pin in range(fork_tables['npins']):
        image = dict(live.get(pin, {}))
        if pin in gpio_init:
            image.update(gpio_init[pin])
        images[pin] = image
    return images


def load(engine, words):
    engine.ask('zero')
    for chunk in range(0, len(words), 64):
        engine.ask('set ' + ' '.join(f'{b}:{o}:{v}' for b, o, v in words[chunk:chunk + 64]))


def pin_words(engine, pin, image):
    engine.ask('zero')
    for field, value in image.items():
        engine.ask(f'poke {pin} {field} {value}')
    return [tuple(w) for w in engine.ask('dump')]


def apply(engine, action):
    kind = action[0]
    if kind == 'state':
        _, pin, steps = action
        codes = []
        for func, _ in steps:
            codes.append(engine.ask(f'mux {pin} {func}'))
            if codes[-1]:
                return codes
        for _, configs in steps:
            if configs:
                codes.append(engine.ask(f'conf {pin} ' + ' '.join(f'{p}:{a}' for p, a in configs)))
                if codes[-1]:
                    return codes
        return codes
    return [engine.ask(' '.join(map(str, action[1:])) if kind == 'raw' else ' '.join(map(str, action)))]


def outcome(codes):
    return next((code for code in codes if code), 0)


def explore(fork_engine, ack_engine, pin, start, fork_actions, ack_actions, limit=4000):
    seen, queue, differences = {start}, [start], []
    while queue and len(seen) < limit:
        words = queue.pop()
        for name in fork_actions:
            load(fork_engine, list(words))
            load(ack_engine, list(words))
            fork_codes = apply(fork_engine, fork_actions[name])
            ack_codes = apply(ack_engine, ack_actions[name])
            fork_words = tuple(tuple(w) for w in fork_engine.ask('dump'))
            ack_words = tuple(tuple(w) for w in ack_engine.ask('dump'))
            if fork_words != ack_words or outcome(fork_codes) != outcome(ack_codes):
                differences.append((name, words, fork_words, ack_words, fork_codes, ack_codes))
                continue
            if fork_words not in seen:
                seen.add(fork_words)
                queue.append(fork_words)
    return differences, len(seen)


def gpio_users(text):
    users = {}
    for line in text.splitlines():
        match = re.match(r'\s*gpio-(\d+)\s+\(\s*\|(.*?)\s*\)\s+(in|out)\s+(\S+)\s*(IRQ)?', line)
        if match:
            users[int(match.group(1))] = (match.group(2), match.group(3), bool(match.group(5)))
    return users


def eint_users(text):
    return {int(m.group(1)): m.group(2).strip() for m in re.finditer(r'mtk-eint\s+(\d+)\s+\w+\s+(.*)', text)}


def action_pair(pin, old_actions, new_actions, gpios, eints, fork_eint):
    old = pin_actions(pin, old_actions, gpios, eints, fork_eint)
    new = dict(old)
    for name, steps in new_actions.get(pin, {}).items():
        new['state ' + name] = ('state', pin, steps)
    return old, new


def pin_actions(pin, dt_actions, gpios, eints, fork_eint):
    actions = {}
    for name, steps in dt_actions.get(pin, {}).items():
        actions['state ' + name] = ('state', pin, steps)
    if pin in gpios:
        actions['gpio request'] = ('raw', 'gpioreq', pin)
        actions['gpio input'] = ('raw', 'gpiodir', pin, 1)
        actions['gpio output'] = ('raw', 'gpiodir', pin, 0)
        actions['gpio low'] = ('raw', 'gpioset', pin, 0)
        actions['gpio high'] = ('raw', 'gpioset', pin, 1)
    for eint, err, gpio, virt in fork_eint:
        if not err and gpio == pin and eint in eints:
            actions[f'eint {eint} request'] = ('raw', 'eintreq', eint)
    return actions


def parse_live_pins(text):
    rows = {}
    for line in text.splitlines():
        match = re.match(r'pin (\d+) \((\w+)\) (\S+) ?(.*)$', line)
        if match:
            rows[int(match.group(1))] = (match.group(2), match.group(3), match.group(4))
    return rows


def decode_show(text, pull_type):
    match = re.fullmatch(r'(\d{3}): (\d)(\d)(\d)(\d)(\d\d|XX)(\d|X)(\d|X)(\d\d|XX)(?: \((\d) (\d)\)| \(rsel = (\d)\))?', text)
    if not match:
        return None
    g = match.groups()
    image = {'MODE': int(g[1]), 'DIR': int(g[2]), 'DO': int(g[3]), 'DI': int(g[4])}
    if g[5] != 'XX':
        image['DRV'] = int(g[5])
    if g[6] != 'X':
        image['SMT'] = int(g[6])
    if g[7] != 'X':
        image['IES'] = int(g[7])
    pull = g[8]
    if pull == 'XX':
        image.update(PU=1, PD=1)
    elif pull_type & 4:
        image.update(PUPD=1 - int(pull[1]), R1=int(g[9]), R0=int(g[10]))
    else:
        en, up = int(pull[0]), int(pull[1])
        image.update(PU=en & up, PD=en & (1 - up))
        if g[11] is not None:
            image['RSEL'] = int(g[11])
    return image


def live_images(live, pinconf, pull_types):
    images, eh = {}, {}
    for line in pinconf.splitlines():
        match = re.match(r'pin (\d+) \(\w+\): .*drive-strength-adv \(0x([0-9a-f]+)\)', line)
        if match:
            eh[int(match.group(1))] = int(match.group(2), 16)
    for pin, (_, _, shown) in live.items():
        image = decode_show(shown, pull_types[pin]) if shown else None
        if image is not None:
            if pin in eh:
                image['DRV_EH'] = eh[pin]
            images[pin] = image
    return images


def gpio_init_table(texts, macros):
    for text in texts:
        for root in dts_parse(text, macros):
            for item in walk(root):
                if 'gpio_init_default' in item['props']:
                    values = item['props']['gpio_init_default']
                    rows = [values[i:i + 7] for i in range(0, len(values), 7)]
                    return {row[0]: {'MODE': row[1], 'DIR': row[2], 'DO': row[3], 'SMT': row[6]} for row in rows}
    raise ValueError('no gpio_init_default in the DT')


def predicted(engine, pins, images, label):
    shown, conf = [], []
    for pin, (name, gpio, _) in sorted(pins.items()):
        load(engine, [])
        for field, value in images.get(pin, {}).items():
            engine.ask(f'poke {pin} {"DRV_ADV" if field == "DRV_EH" and engine.variant == "ack" else field} {value}')
        length, text = engine.ask(f'show {pin}')
        prefix = gpio if gpio == '0:?' else gpio.split(':')[0] + ':' + label
        shown.append(f'pin {pin} ({name}) {prefix} {text if length > 0 else ""}'.rstrip())
        conf.append(f'pin {pin} ({name}): {engine.ask(f"pinconf {pin}")}'.rstrip())
    return shown, conf


def main():
    parser = argparse.ArgumentParser(usage=usage())
    parser.add_argument('tree', type=Path, nargs='?', default=Path(__file__).resolve().parents[2])
    parser.add_argument('--ack-repo', type=Path)
    parser.add_argument('--write-expected', type=Path)
    parser.add_argument('--skip-mutations', action='store_true')
    parser.add_argument('--live', type=Path, default=FIXTURE / 'live')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    tree = args.tree.resolve()
    sources = Sources(tree, (args.ack_repo or tree / 'ack-6.18').resolve())
    if not shutil.which('cc'):
        print('FAIL needs cc on PATH', file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix='pinctrl-mt6789-') as work:
        result = verify(sources, work, args)
    errors, notes = result
    for note in notes:
        print('note: ' + note)
    if errors:
        for error in errors[:60]:
            print('FAIL ' + error, file=sys.stderr)
        if len(errors) > 60:
            print(f'FAIL ... {len(errors) - 60} more', file=sys.stderr)
        return 1
    print('PASS ' + notes[-1])
    return 0


def candidate_files(sources):
    return {name: sources.current(CANDIDATE + name) for name in ('pinctrl-mt6789.c', 'pinctrl-mtk-mt6789.h')}


def table_errors(sources, work, fork_bundle, driver, copies=None, new_dt=None, fork_dt=None):
    fork_tables, fork_lookup, fork_eint, fork_virt = fork_bundle
    errors = check_copies(sources, copies)
    if errors:
        return errors, [], None
    try:
        ack = Engine(sources, 'ack', work, driver)
    except RuntimeError as error:
        return ['candidate does not compile in the harness: ' + str(error)[:400]], [], None
    try:
        ack_tables = ack.ask('tables')
        ack_lookup = ack.ask('lookup')
        ack_eint = ack.ask(f'eintmap {len(fork_eint)}')
        ack_virt = ack.ask('virt')
    except (RuntimeError, ValueError) as error:
        ack.close()
        return ['candidate engine failed: ' + str(error)], [], None
    errors, notes = compare_tables(fork_tables, ack_tables, fork_lookup, ack_lookup, fork_eint, ack_eint, fork_virt, ack_virt)
    if new_dt is not None:
        more, extra = compare_eint(sources, fork_dt, new_dt, ack_tables)
        errors += more
        notes += extra
    return errors, notes, (ack, ack_tables)


def verify(sources, work, args):
    errors, notes = [], []
    macros = pinfunc_macros(sources.current(PINFUNC))
    fork_dt = pinctrl_dt([sources.fork_dt(p) for p in FORK_DT], macros)
    new_dt = pinctrl_dt([sources.current(p) for p in NEW_DT], macros)
    nbase = len(fork_dt['pio']['reg']) // 4
    fork = Engine(sources, 'fork', work, nbase=nbase)
    fork_tables = fork.ask('tables')
    fork_lookup = fork.ask('lookup')
    total = fork_dt['eint']['mediatek,total-pin-number'][0]
    fork_eint = fork.ask(f'eintmap {total}')
    fork_virt = fork.ask('virt')
    bundle = (fork_tables, fork_lookup, fork_eint, fork_virt)
    driver = candidate_files(sources)
    more, extra, ack_bundle = table_errors(sources, work, bundle, driver, new_dt=new_dt, fork_dt=fork_dt)
    errors += more
    notes += extra
    if ack_bundle is None:
        return errors, notes + ['tables not compared']
    ack, ack_tables = ack_bundle
    ids = param_ids(sources.ack('include/linux/pinctrl/pinconf-generic.h'))
    fork_params = dt_params(sources, sources.fork('pinctrl-paris.c'))
    fork_actions, changed = reference_actions(state_actions(fork_dt, fork_params, ids),
                                              state_actions(new_dt, fork_params, ids))
    ack_actions = state_actions(new_dt, dt_params(sources, sources.ack('drivers/pinctrl/mediatek/pinctrl-paris.c')), ids)
    unlisted, stale = sorted(changed - set(DT_CHANGES)), sorted(set(DT_CHANGES) - changed)
    if unlisted:
        errors.append('the new DT adds or drops pin states not listed in DT_CHANGES: ' + ', '.join(unlisted))
    if stale:
        errors.append('DT_CHANGES lists states the new DT neither adds nor drops: ' + ', '.join(stale))
    for name in sorted(changed & set(DT_CHANGES)):
        notes.append(f'state {name} is added or dropped on purpose: {DT_CHANGES[name]}')
    if set(fork_actions) != set(ack_actions) or any(set(fork_actions[p]) != set(ack_actions[p]) for p in fork_actions):
        errors.append('the new DT does not keep the pin states of the fork DT')
    live = parse_live_pins((args.live / 'pins.txt').read_text())
    pinconf = (args.live / 'pinconf-pins.txt').read_text()
    images = live_images(live, pinconf, fork_tables['pull_type'])
    gpios = gpio_users((args.live / 'gpio.txt').read_text())
    eints = eint_users((args.live / 'interrupts-eint.txt').read_text())
    gpio_init = gpio_init_table([sources.current(p) for p in NEW_DT], macros)
    boots = boot_images(fork_tables, images, gpio_init)
    for pin in (130, 131):
        boots[pin]['DRV_EH'] = 7
    notes.append(f'boot image: MODE, DIR, DO and SMT of the {len(gpio_init)} pins in gpio_init_default over the live '
                 'register decode; EH of GPIO130/131 is 7, the value of the eighteen I2C pins no kernel path writes')
    explored = 0
    conditional = {}
    for pin in range(fork_tables['npins']):
        fork_pin, ack_pin = action_pair(pin, fork_actions, ack_actions, gpios, eints, fork_eint)
        if not fork_pin:
            continue
        start = tuple(pin_words(fork, pin, boots[pin]))
        differences, count = explore(fork, ack, pin, start, fork_pin, ack_pin)
        explored += count
        for name, words, a, b, ca, cb in differences[:3]:
            errors.append(f'pin {pin}: {name} gives fork {a} rc {ca}, ACK {b} rc {cb} from {words}')
        for image in ({}, {f: 7 for f in ('MODE', 'DRV', 'RSEL', 'DRV_EH')} |
                      {f: 1 for f in ('DIR', 'DO', 'SMT', 'IES', 'PU', 'PD', 'PUPD', 'R0', 'R1')}):
            found, _ = explore(fork, ack, pin, tuple(pin_words(fork, pin, image)), fork_pin, ack_pin, limit=400)
            if found:
                conditional.setdefault(pin, set()).update(item[0] for item in found)
    notes.append(f'{explored} register images reached by DT states, GPIO and EINT requests, all equal from the boot image')
    if 'mediatek,eint' in new_dt['pio'] and new_dt['eint'] == fork_dt['eint']:
        errors += fork_keeps_new_dt(sources, work, fork, fork_actions, new_dt, ids, gpios, eints, fork_eint, boots, notes)
    else:
        notes.append('the new DT drops mediatek,eint or the BSP EINT node: the fork cannot run on it, so a rollback to '
                     'the fork needs the previous DT')
    unexpected = sorted(pin for pin in conditional if pin not in BOOT_DEPENDENT)
    if unexpected:
        errors.append('boot-dependent equivalence on pins not listed: ' +
                      ', '.join(f'{p} ({sorted(conditional[p])})' for p in unexpected))
    missing = sorted(p for p in BOOT_DEPENDENT if p not in conditional)
    if missing:
        errors.append('listed boot-dependent pins that no longer depend on the boot image: ' + str(missing))
    for reason in dict.fromkeys(BOOT_DEPENDENT.values()):
        pins = [p for p, r in BOOT_DEPENDENT.items() if r == reason]
        notes.append(f'equal only from the boot image on pins {pins}: {reason}')
    live_errors, live_notes = check_live(fork, ack, fork_tables, ack_tables, live, images, pinconf, gpios, eints,
                                         fork_eint, new_dt, args)
    errors += live_errors
    notes += live_notes
    if not args.skip_mutations and not errors:
        more, extra = mutations(sources, work, bundle, driver, new_dt, fork_dt, fork, fork_actions, gpios, eints,
                                fork_eint, boots)
        errors += more
        notes += extra
    fork.close()
    ack.close()
    notes.append(f'{len(fork_tables["pins"])} pins, {sum(len(v) for v in ack_actions.values())} pin states, '
                 f'{len(fork_eint)} EINTs and the live dump agree between the fork and the ACK data driver')
    return errors, notes


def fork_keeps_new_dt(sources, work, fork, fork_actions, new_dt, ids, gpios, eints, fork_eint, boots, notes):
    errors = []
    fork_new = Engine(sources, 'fork', work, nbase=len(new_dt['pio']['reg']) // 4, name='fork-new-dt')
    fork_new_actions = state_actions(new_dt, dt_params(sources, sources.fork('pinctrl-paris.c')), ids)
    kept = 0
    for pin in range(fork_new.ask('tables')['npins']):
        old_pin, new_pin = action_pair(pin, fork_actions, fork_new_actions, gpios, eints, fork_eint)
        if not old_pin:
            continue
        differences, count = explore(fork, fork_new, pin, tuple(pin_words(fork, pin, boots[pin])), old_pin, new_pin)
        kept += count
        for name, words, a, b, ca, cb in differences[:3]:
            errors.append(f'pin {pin}: the new DT changes the fork: {name} gives {a} rc {ca} instead of {b} rc {cb}')
    fork_new.close()
    notes.append(f'the fork driver reaches the same {kept} register images with the new DT as with the old one')
    return errors


def check_live(fork, ack, fork_tables, ack_tables, live, images, pinconf, gpios, eints, fork_eint, new_dt, args):
    errors, notes = [], []
    live_dir, out = args.live, args.write_expected
    names = [p[1] for p in ack_tables['pins']]
    if [live[p][0] for p in sorted(live)] != names or len(live) != len(names):
        errors.append('live pin names are not the candidate pin names')
    groups = re.findall(r'group: (\w+)\npin (\d+) \((\w+)\)', (live_dir / 'pingroups.txt').read_text())
    if [(g, int(p), n) for g, p, n in groups] != [(n, i, n) for i, n in enumerate(names)]:
        errors.append('live pin groups are not one group per candidate pin')
    ranges = (live_dir / 'gpio-ranges.txt').read_text()
    gpio_ranges = new_dt['pio'].get('gpio-ranges', [])
    match = re.search(r'PINS \[(\d+) - (\d+)\]', ranges)
    if not match or [int(match.group(1)), int(match.group(2)) + 1 - int(match.group(1))] != [gpio_ranges[2], gpio_ranges[3]]:
        errors.append('live GPIO range is not the DT gpio-ranges')
    chip = re.search(r'gpiochip\d+: (\d+) GPIOs', (live_dir / 'gpio.txt').read_text())
    if not chip or int(chip.group(1)) != ack_tables['npins']:
        errors.append('live gpiochip size is not the candidate pin count')
    by_pin = {gpio: eint for eint, err, gpio, virt in fork_eint if not err}
    irq_pins = {pin for pin, (_, _, irq) in gpios.items() if irq}
    if {by_pin.get(pin) for pin in irq_pins} != set(eints):
        errors.append('live EINT lines do not map to the GPIOs that live gpio lists with IRQ')
    pinmux = (live_dir / 'pinmux-pins.txt').read_text()
    for pin, func in re.findall(r'pin (\d+) \(\w+\): .* function func(\d+) group', pinmux):
        pin, func = int(pin), int(func)
        if images.get(int(pin), {}).get('MODE') != func:
            errors.append(f'live pinmux says pin {pin} func{func} but the live register decode differs')
        if not any(f[0] == func and f[1] for f in ack_tables['pins'][pin][5]):
            errors.append(f'candidate has no function {func} on pin {pin}')
    fork_shown, fork_conf = predicted(fork, live, images, 'pinctrl_mtk_v2')
    live_shown = [line.rstrip() for line in (live_dir / 'pins.txt').read_text().splitlines()[1:]]
    live_conf = [line.rstrip() for line in pinconf.splitlines() if line.startswith('pin ')]
    bad = [(a, b) for a, b in zip(fork_shown, live_shown) if a != b]
    bad_conf = [(a, b) for a, b in zip(fork_conf, live_conf) if a != b]
    if bad or len(fork_shown) != len(live_shown):
        errors.append(f'fork harness does not reproduce live pins: {len(bad)} lines, first {bad[:1]}')
    if bad_conf or len(fork_conf) != len(live_conf):
        errors.append(f'fork harness does not reproduce live pinconf-pins: {len(bad_conf)} lines, first {bad_conf[:1]}')
    ack_shown, ack_conf = predicted(ack, live, images, GPIO_LABEL)
    for pin in sorted(images):
        load(fork, [])
        load(ack, [])
        for field, value in images[pin].items():
            fork.ask(f'poke {pin} {field} {value}')
            ack.ask(f'poke {pin} {"DRV_ADV" if field == "DRV_EH" else field} {value}')
        a, b = fork.ask(f'peek {pin}'), ack.ask(f'peek {pin}')
        b['DRV_EH'] = b.pop('DRV_ADV')
        if pin in FORK_ONLY_EH:
            a.pop('DRV_EH')
            b.pop('DRV_EH')
        if a != b:
            errors.append(f'pin {pin}: the live register image decodes differently: {a} {b}')
    notes.append(f'fork harness reproduces {len(live_shown)} live pins lines and {len(live_conf)} pinconf-pins lines; '
                 f'{len(images)} live pin images decode the same through the candidate')
    if out:
        out.mkdir(parents=True, exist_ok=True)
        (out / 'pins.txt').write_text(f'registered pins: {len(ack_shown)}\n' + '\n'.join(ack_shown) + '\n')
        (out / 'pinconf-pins.txt').write_text('Pin config settings per pin\nFormat: pin (name): configs\n' +
                                              '\n'.join(ack_conf) + '\n')
        notes.append('expected ACK debugfs lines written to ' + str(out))
    if args.compare:
        more, extra = compare_device(args.compare, ack_shown, ack_conf)
        errors += more
        notes += extra
    return errors, notes


def masked(line):
    match = re.match(r'(pin \d+ \(\w+\) \S+ \d{3}: \d\d\d)\d(.*)$', line)
    return match.group(1) + '_' + match.group(2) if match else line


def compare_device(directory, shown, conf):
    errors = []
    device = [line.rstrip() for line in (directory / 'pins.txt').read_text().splitlines()[1:]]
    device_conf = [line.rstrip() for line in (directory / 'pinconf-pins.txt').read_text().splitlines()
                   if line.startswith('pin ')]
    for kind, want, got in (('pins', [masked(line) for line in shown], [masked(line) for line in device]),
                            ('pinconf-pins', conf, device_conf)):
        if len(want) != len(got):
            errors.append(f'device {kind} has {len(got)} lines, expected {len(want)}')
        for a, b in zip(want, got):
            if a != b:
                errors.append(f'device {kind}: expected {a!r}, got {b!r}')
    return errors, [f'device dump in {directory} compared with the ACK prediction of the --live dump (DI masked)']


MUTATIONS = (
    ('ies-bit', 'pinctrl-mt6789.c', r'PIN_FIELD_BASE\(0, 1, 6, 0x0070, 0x10, 20, 1\)', 'PIN_FIELD_BASE(0, 1, 6, 0x0070, 0x10, 21, 1)'),
    ('mode-base', 'pinctrl-mt6789.c', r'0x0300, 0x10, 0, 4\)', '0x0310, 0x10, 0, 4)'),
    ('drv-pin-dropped', 'pinctrl-mt6789.c', r'\tPIN_FIELD_BASE\(0, 0, 6, 0x0020, 0x10, 0, 3\),\n', ''),
    ('smt-unsorted', 'pinctrl-mt6789.c', r'(\tPINS_FIELD_BASE\(0, 7, [^\n]*\n)(\tPIN_FIELD_BASE\(8, 9, [^\n]*\n)', r'\2\1'),
    ('function-name', 'pinctrl-mtk-mt6789.h', r'"SCL0"', '"SDA0"'),
    ('eint-number', 'pinctrl-mtk-mt6789.h', r'MTK_EINT_FUNCTION\(0, 137\)', 'MTK_EINT_FUNCTION(0, 138)'),
    ('virtual-pin-real', 'pinctrl-mtk-mt6789.h', r'(199, "GPIO199",\n\t\tMTK_EINT_FUNCTION\(0, 137\),\n\t\tDRV_FIXED,\n\t\tMTK_FUNCTION\(0, )NULL', r'\1"GPIO199"'),
    ('pull-type', 'pinctrl-mt6789.c', r'mt6789_pull_type\[\] = \{\n\tMTK_PULL_PU_PD_TYPE', 'mt6789_pull_type[] = {\n\tMTK_PULL_PUPD_R1R0_TYPE'),
    ('base-name', 'pinctrl-mt6789.c', r'"iocfg_lm"', '"iocfg_rm"'),
    ('drive-rev1', 'pinctrl-mt6789.c', r'mtk_pinconf_drive_set_raw', 'mtk_pinconf_drive_set_rev1'),
    ('debounce-entry', 'pinctrl-mt6789.c', r'\b20000\b', '16000'),
    ('ap-num', 'pinctrl-mt6789.c', r'\.ap_num\s*=\s*160', '.ap_num = 159'),
    ('db-cnt', 'pinctrl-mt6789.c', r'\.db_cnt\s*=\s*32', '.db_cnt = 13'),
    ('eh-pin-dropped', 'pinctrl-mt6789.c', r'\tPIN_FIELD_BASE\(131, 131, 4, 0x0080, 0x10, 15, 3\),\n', ''),
)
DT_MUTATIONS = (
    ('dt-no-eint-reg-name', 'mindone/dts/mt6789-pinctrl.dtsi', r', "eint"', ''),
    ('dt-no-pio-interrupt', 'mindone/dts/mt6789-pinctrl.dtsi', r'\n\t\tinterrupts = <GIC_SPI 244 IRQ_TYPE_LEVEL_HIGH 0>;', ''),
    ('dt-ts-int-direction', 'mindone/dts/mindone-overlay.dtso', r'(ts_int_active[^}]*?)\t\t\tinput-schmitt-enable;\n', r'\1'),
    ('dt-ts-scl-eh', 'mindone/dts/mindone-overlay.dtso', r'(ts_scl_active[^}]*?mediatek,drive-strength-adv = <0x0)7', r'\g<1>6'),
)
COPY_MUTATIONS = (
    ('copy-member', 'drivers/pinctrl/mediatek/pinctrl-mtk-common-v2.h', r'\tbool\t\t\t\ties_present;', '\tbool\t\t\t\ties_present2;'),
)


def mutations(sources, work, bundle, driver, new_dt, fork_dt, fork, fork_actions, gpios, eints, fork_eint, boots):
    errors, survived = [], []
    macros = pinfunc_macros(sources.current(PINFUNC))
    for name, file, pattern, replacement in MUTATIONS:
        mutated = dict(driver)
        text, count = re.subn(pattern, replacement, mutated[file], count=1)
        if not count:
            errors.append(f'mutation {name} does not apply')
            continue
        mutated[file] = text
        found, _, engine = table_errors(sources, work, bundle, mutated, new_dt=new_dt, fork_dt=fork_dt)
        if engine:
            engine[0].close()
        if not found:
            survived.append(name)
    for name, path, pattern, replacement in COPY_MUTATIONS:
        text, count = re.subn(pattern, replacement, sources.current(COPY_ROOT + path), count=1)
        if not count:
            errors.append(f'mutation {name} does not apply')
        elif not check_copies(sources, {path: text}):
            survived.append(name)
    ids = param_ids(sources.ack('include/linux/pinctrl/pinconf-generic.h'))
    params = dt_params(sources, sources.ack('drivers/pinctrl/mediatek/pinctrl-paris.c'))
    ack = Engine(sources, 'ack', work, driver)
    for name, path, pattern, replacement in DT_MUTATIONS:
        texts = []
        applied = 0
        for current in NEW_DT:
            text = sources.current(current)
            if current == path:
                text, applied = re.subn(pattern, replacement, text, count=1, flags=re.S)
            texts.append(text)
        if not applied:
            errors.append(f'mutation {name} does not apply')
            continue
        mutated_dt = pinctrl_dt(texts, macros)
        found, _ = compare_eint(sources, fork_dt, mutated_dt, ack.ask('tables'))
        if not found:
            mutated_actions = state_actions(mutated_dt, params, ids)
            for pin in mutated_actions:
                fork_pin, ack_pin = action_pair(pin, fork_actions, mutated_actions, gpios, eints, fork_eint)
                start = tuple(pin_words(fork, pin, boots[pin]))
                differences, _ = explore(fork, ack, pin, start, fork_pin, ack_pin, limit=400)
                if differences:
                    found = True
                    break
        if not found:
            survived.append(name)
    ack.close()
    if survived:
        errors.append('mutations not detected: ' + ', '.join(survived))
    total = len(MUTATIONS) + len(DT_MUTATIONS) + len(COPY_MUTATIONS)
    return errors, [f'{total - len(survived)} of {total} mutations detected']


if __name__ == '__main__':
    sys.exit(main())
