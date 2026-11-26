# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from baseline_source import require, strip_comments


CHECK = 'gpu-cooling'
MODULES = 'mindone/modules/'
PROVIDER = MODULES + 'mtk_gpufreq_mt6789/gpufreq_mt6789.c'
GPUPPM = MODULES + 'mtk_gpufreq_mt6789/gpuppm.c'
WRAPPER = MODULES + 'mtk_gpufreq_wrapper/gpufreq_v2.c'
INCLUDE = MODULES + 'include/drivers/gpu/mediatek/gpufreq/v2_legacy/include/'
HEADERS = ('gpufreq_v2_legacy.h', 'gpuppm_legacy.h', 'gpufreq_common_legacy.h', 'gpufreq_history_common.h')
BUILD = MODULES + 'BUILD.bazel'
LOAD = 'mindone/vendor_dlkm.modules.load'
GPU_DTS = 'mindone/dts/mt6789-gpu.dtsi'
BOARD_DTS = 'mindone/dts/mindone.dts'
OVERLAY_DTS = 'mindone/dts/mindone-overlay.dtso'
GPUPPM_SHA256 = '4b8f62563cc7c04e2f2592d43a89ddf390c4f4b5f7cd607810610f0a8accf8e6'
PINS = (
    (PROVIDER, '__gpufreq_get_cur_idx_gpu', 'ae9981a14f3cf5d843ed7875294ce4b88044195f2a0e5a45b3d9a1349a9fbe96'),
    (PROVIDER, '__gpufreq_get_opp_num_gpu', '1d4ec12984e208fc33ac286e949351cc298d629a32ab5505d0c67fa6ebc36133'),
    (PROVIDER, '__gpufreq_get_fgpu_by_idx', 'ca6fec9d82e56cfc849f0de8c295a928c1d5f4cf02187b40648efbcaf26e140b'),
    (PROVIDER, '__gpufreq_get_idx_by_fgpu', '4738a3fcb614742b5a573dac6a11dc87ebadd14ffe43c310cdcaa454d0c34b5c'),
    (PROVIDER, '__gpufreq_init_opp_table', 'da23bfb1008c2e83e4b508a10a94104887ad0ca85c0e54e6ce2effda460bd6c2'),
    (PROVIDER, '__gpufreq_generic_commit_gpu', '97ae27e782533399cd86befb8f05135f6324ff7025cde52f8ceb06e0cb888f81'),
    (PROVIDER, '__gpufreq_power_control', '2874c19110319042f31f7729bd03f33969c901d79d0a71a07db1d973c08239b4'),
    (WRAPPER, 'gpufreq_validate_target', '054368aba6e65aa015793860afdb4ba1ea7eccefbc714d1ef478e3d910a32faf'),
    (WRAPPER, 'gpufreq_set_limit', 'd8ba6877dca0490af48af8059f013bd0e7c9e43830a6def8e2321521a1747e95'),
    (WRAPPER, 'gpufreq_commit', '311bb1d18feeb7c669a3f66579ef6cb28ee0a276723ffbe1fb99c9c4a887c7b7'),
    (WRAPPER, 'gpufreq_register_gpufreq_fp', 'd7815d7d5fa98afce7fb9c1c798b7945ef84d4fa662792d5f0439bd8757704c6'),
    (WRAPPER, 'gpufreq_register_gpuppm_fp', '06416d2643ad5608fc292acefd1691deb2ba2112c1a6c6358be697980b1b95db'),
)
BASELINE_PINS = (
    ('__gpufreq_pdrv_probe', '26da865548a8fd25bc25178bc021c6e599d0145db39abc198a96d506030b64f1'),
    ('__gpufreq_pdrv_remove', '68aa320a126793b3dfbc2f780aa8d76cdcce4cabf5bed51554434c5561793e95'),
)
FIX = (
    ('__gpufreq_pdrv_probe', '\tif (!g_gpueb_support)\n\t\t__gpufreq_init_cooling(pdev);\n\n', ''),
    ('__gpufreq_pdrv_remove', '\t__gpufreq_term_cooling();\n\n', ''),
)
PROVIDER_CONFIG = {'GPUFREQ_SELF_CTRL_MTCMOS': '1'}
PROVIDER_STATICS = (
    'static void __iomem *g_apmixed_base;',
    'static struct gpufreq_pmic_info *g_pmic;',
    'static struct gpufreq_clk_info *g_clk;',
    'static struct gpufreq_mtcmos_info *g_mtcmos __maybe_unused;',
    'static struct gpufreq_status g_gpu;',
    'static unsigned int g_gpueb_support;',
    'static unsigned int g_aging_enable;',
    'static unsigned int g_aging_load;',
    'static unsigned int g_max_freq_off_load;',
    'static enum gpufreq_dvfs_state g_dvfs_state;',
    'static struct mt_gpufreq_power_table_info *g_power_table;',
)
COOLING_STATICS = (
    'static struct thermal_cooling_device *g_cooling_dev;',
    'static unsigned long g_cooling_state;',
)
PROVIDER_FUNCTIONS = ('__gpufreq_get_cur_idx_gpu', '__gpufreq_get_opp_num_gpu', '__gpufreq_get_fgpu_by_idx',
                      '__gpufreq_get_idx_by_fgpu', '__gpufreq_init_opp_table', '__gpufreq_cooling_get_max_state',
                      '__gpufreq_cooling_get_cur_state', '__gpufreq_cooling_set_cur_state')
LIFECYCLE_FUNCTIONS = ('__gpufreq_init_cooling', '__gpufreq_term_cooling', '__gpufreq_pdrv_probe', '__gpufreq_pdrv_remove')
WRAPPER_STATICS = (
    'static unsigned int g_dual_buck;',
    'static unsigned int g_gpueb_support;',
    'static struct gpufreq_shared_status *g_shared_status;',
    'static struct gpufreq_platform_fp *gpufreq_fp;',
    'static struct gpuppm_platform_fp *gpuppm_fp;',
    'static struct gpufreq_ipi_data g_recv_msg;',
    'static unsigned long g_ipi_irq_flags;',
    'static raw_spinlock_t gpufreq_ipi_lock;',
)
WRAPPER_FUNCTIONS = ('gpufreq_validate_target', 'gpufreq_set_limit', 'gpufreq_commit',
                     'gpufreq_register_gpufreq_fp', 'gpufreq_register_gpuppm_fp',
                     'gpufreq_unregister_gpufreq_fp', 'gpufreq_unregister_gpuppm_fp')
CASES = ('registration-mt6789', 'registration-mt6789t', 'table-shape', 'every-state', 'immediate-throttle', 'recovery',
         'out-of-range', 'composition', 'boost-floor', 'powered-off', 'limit-failure', 'probe-eb', 'probe-bringup',
         'register-failure', 'reset-failure', 'rebind-reset', 'remove-order')
SET_LIMIT = '\tret = gpufreq_set_limit(TARGET_DEFAULT, LIMIT_THERMAL_AP, (int)ceiling, GPUPPM_KEEP_IDX);\n'
RESET_LIMIT = '\tret = gpufreq_set_limit(TARGET_DEFAULT, LIMIT_THERMAL_AP, 0, GPUPPM_KEEP_IDX);\n'
PROBE_CALL = '\tif (!g_gpueb_support)\n\t\t__gpufreq_init_cooling(pdev);\n\n'
REMOVE_CALL = '\t__gpufreq_term_cooling();\n\n'
CONTROLS = (
    ('state-plus-one', (('ceiling = __gpufreq_get_fgpu_by_idx((int)state);', 'ceiling = __gpufreq_get_fgpu_by_idx((int)state + 1);'),),
     'every-state', 'row() == (s ? (int)s : GPUPPM_DEFAULT_IDX)'),
    ('state-zero-frequency', (('\tif (state)\n\t\tceiling =', '\tif (1)\n\t\tceiling ='),),
     'every-state', 'row() == (s ? (int)s : GPUPPM_DEFAULT_IDX)'),
    ('wrong-limiter', ((SET_LIMIT, SET_LIMIT.replace('LIMIT_THERMAL_AP', 'LIMIT_DEBUG')),),
     'every-state', 'row() == (s ? (int)s : GPUPPM_DEFAULT_IDX)'),
    ('max-state-off-by-one', (('*state = (unsigned long)(opp_num - 1);', '*state = (unsigned long)(opp_num);'),),
     'registration-mt6789', 'cdev.max_state == (unsigned long)(test_opp_num() - 1)'),
    ('no-bound-check', (('if (opp_num < 1 || state >= (unsigned long)opp_num)', 'if (opp_num < 1)'),),
     'out-of-range', 'set_state(cdev.max_state + 1) == -EINVAL'),
    ('record-on-failure', (('\t\treturn ret;\n\t}\n\n\tWRITE_ONCE(g_cooling_state, state);',
                            '\t\tWRITE_ONCE(g_cooling_state, state);\n\t\treturn ret;\n\t}\n\n\tWRITE_ONCE(g_cooling_state, state);'),),
     'limit-failure', 'get_state() == 3 && row() == 3'),
    ('swallow-error', (('\t\treturn ret;\n\t}\n\n\tWRITE_ONCE(g_cooling_state, state);', '\t\treturn 0;\n\t}\n\n\tWRITE_ONCE(g_cooling_state, state);'),),
     'limit-failure', 'set_state(7) == -ENOENT'),
    ('no-registration-reset', ((RESET_LIMIT, '\tret = GPUFREQ_SUCCESS;\n'),),
     'rebind-reset', 'get_state() == 0 && row() == GPUPPM_DEFAULT_IDX'),
    ('register-in-eb-mode', ((PROBE_CALL, '\t__gpufreq_init_cooling(pdev);\n\n'),),
     'probe-eb', 'register_calls == 0'),
    ('register-before-gpuppm', ((PROBE_CALL, ''), ('\tret = gpuppm_init(TARGET_GPU, g_gpueb_support);\n',
                                                   PROBE_CALL + '\tret = gpuppm_init(TARGET_GPU, g_gpueb_support);\n')),
     'registration-mt6789', 'register_calls == 1'),
    ('unregister-after-free', ((REMOVE_CALL, ''), ('\tkfree(g_power_table);\n\tg_power_table = NULL;\n\tif (g_apmixed_base) {',
                                                   '\tkfree(g_power_table);\n\tg_power_table = NULL;\n' + REMOVE_CALL + '\tif (g_apmixed_base) {')),
     'remove-order', 'position("cooling_unregister") < position("free_working_table")'),
    ('missing-unregister', ((REMOVE_CALL, ''),), 'remove-order', 'unregister_calls == 1'),
)


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Change must match exactly once: ' + before)
    return source.replace(before, after, 1)


def closing(source, opening):
    depth, index = 0, opening
    while True:
        char = source[index]
        if char in '"\'':
            index += 1
            while source[index] != char:
                index += 2 if source[index] == '\\' else 1
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if not depth:
                return index + 1
        index += 1


def function(source, name):
    source = strip_comments(source)
    pattern = r'^(?:static )?(?:inline )?(?:unsigned int|int|void|bool) ' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError(f'Expected one definition of {name}, found {len(matches)}')
    start = matches[0].start()
    return source[start:closing(source, source.index('{', start))] + '\n'


def block(source, pattern, label):
    matches = re.findall(pattern, strip_comments(source), re.M | re.S)
    if len(matches) != 1:
        raise ValueError(f'Expected one {label}, found {len(matches)}')
    return matches[0]


def lines(source, wanted, label):
    for line in wanted:
        if len(re.findall('^' + re.escape(line) + '$', source, re.M)) != 1:
            raise ValueError(f'{label} no longer declares: {line}')
    return '\n'.join(wanted) + '\n'


def header(tree, name):
    return re.sub(r'^#include [<"].*$', '', (tree / (INCLUDE + name)).read_text(), flags=re.M)


def target(build, name):
    body = block(build, r'^ddk_module\(\n    name = "' + re.escape(name) + r'",\n(.*?)^\)', 'Kleaf target ' + name)
    found = {}
    for key in ('srcs', 'deps'):
        match = re.search(r'^    ' + key + r' = \[(.*?)^    \],', body, re.M | re.S)
        found[key] = re.findall(r'"([^"]+)"', match.group(1)) if match else []
    return found


def zone(dts, name):
    return block(dts, r'^\t\t(?:\w+: )?' + re.escape(name) + r' \{\n(.*?)^\t\t\};', 'thermal zone ' + name)


def check_dt(gpu_dts, board_dts, overlay_dts):
    node = block(gpu_dts, r'^\tgpufreq: gpufreq \{\n(.*?)^\t\};', 'gpufreq DT node')
    if '\t\tcompatible = "mediatek,gpufreq";' not in node or '\t\t#cooling-cells = <0x02>;' not in node:
        raise ValueError('gpufreq DT node is not a two-cell cooling device of the provider')
    for name, trip in (('gpu2', 'gpu_throttle'), ('gpu1', 'gpu1_passive')):
        maps = zone(board_dts, name)
        if not re.search(r'trip = <&' + trip + r'>;\n\t+cooling-device = <&gpufreq 0xffffffff 0xffffffff>;', maps):
            raise ValueError(f'thermal zone {name} does not map {trip} to the gpufreq cooling device')
    if '<&gpufreq 0xffffffff 0xffffffff>' not in zone(board_dts, 'ap_ntc'):
        raise ValueError('ap_ntc passive map lost the gpufreq cooling device')
    for text in (board_dts, overlay_dts, gpu_dts):
        if re.search(r'cooling-device = [^;]*&mali\b', strip_comments(text), re.S):
            raise ValueError('a cooling map still names &mali, which registers no cooling device')


def bindings(tree, provider):
    build = (tree / BUILD).read_text()
    gpufreq = target(build, 'mtk_gpufreq_mt6789')
    for path in (PROVIDER, GPUPPM):
        if path[len(MODULES):] not in gpufreq['srcs']:
            raise ValueError('mtk_gpufreq_mt6789 no longer builds ' + path)
    if ':mtk_gpufreq_wrapper' not in gpufreq['deps'] or WRAPPER[len(MODULES):] not in target(build, 'mtk_gpufreq_wrapper')['srcs']:
        raise ValueError('mtk_gpufreq_wrapper no longer provides gpufreq_set_limit to the provider')
    load = (tree / LOAD).read_text().split()
    if load.index('mtk_gpufreq_wrapper.ko') > load.index('mtk_gpufreq_mt6789.ko'):
        raise ValueError('the provider loads before the wrapper')
    if '{ .compatible = "mediatek,gpufreq" },' not in provider:
        raise ValueError('the provider no longer binds mediatek,gpufreq')
    if not re.search(r'^#include <linux/thermal.h>$', provider, re.M) or 'CONFIG_THERMAL)\n#include <linux/thermal.h>' in provider:
        raise ValueError('linux/thermal.h is not an unconditional provider include')
    config = (tree / (INCLUDE + 'gpufreq_mt6789.h')).read_text()
    for name, value in PROVIDER_CONFIG.items():
        if not re.search(r'^#define ' + name + r'\s+\(' + value + r'\)\s*$', config, re.M):
            raise ValueError('reviewed provider configuration changed: ' + name)
    gpu_dts, board_dts, overlay_dts = ((tree / path).read_text() for path in (GPU_DTS, BOARD_DTS, OVERLAY_DTS))
    check_dt(gpu_dts, board_dts, overlay_dts)
    try:
        check_dt(gpu_dts, board_dts.replace('<&gpufreq 0xffffffff', '<&mali 0xffffffff'), overlay_dts)
    except ValueError as error:
        print(f'PASS rejected DT with the r54 &mali maps: {error}')
    else:
        raise RuntimeError('DT control accepted the &mali cooling maps')


def baseline(provider):
    result = provider
    for name, before, after in FIX:
        result = result.replace(function(result, name), replace_once(function(result, name), before, after))
    return result


def sources(tree):
    texts = {path: (tree / path).read_text() for path in (PROVIDER, GPUPPM, WRAPPER)}
    require('Reviewed gpuppm.c', texts[GPUPPM], GPUPPM_SHA256)
    for path, name, pin in PINS:
        require(f'Reviewed {name} in {path}', function(texts[path], name), pin)
    old = baseline(texts[PROVIDER])
    for name, pin in BASELINE_PINS:
        require(f'{name} without the cooling call', function(old, name), pin)
    return texts, old


def provider_unit(tree, fixtures, common, provider):
    result = common + header(tree, 'gpufreq_mt6789.h') + lines(provider, PROVIDER_STATICS, PROVIDER)
    result += (fixtures / 'provider-stubs.h').read_text() + lines(provider, COOLING_STATICS, PROVIDER)
    result += ''.join(function(provider, name) for name in PROVIDER_FUNCTIONS)
    result += block(provider, r'^(static const struct thermal_cooling_device_ops g_cooling_ops = \{.*?^\};)', 'cooling ops') + '\n'
    result += ''.join(function(provider, name) for name in LIFECYCLE_FUNCTIONS)
    return result + (fixtures / 'provider-hooks.c').read_text()


def units(tree, texts):
    fixtures = Path(__file__).resolve().parent / CHECK
    common = (fixtures / 'stubs.h').read_text() + ''.join(header(tree, name) for name in HEADERS)
    wrapper = common + header(tree, 'gpufreq_ipi.h') + (fixtures / 'wrapper-stubs.h').read_text()
    wrapper += lines(texts[WRAPPER], WRAPPER_STATICS, WRAPPER)
    wrapper += ''.join(function(texts[WRAPPER], name) for name in WRAPPER_FUNCTIONS)
    wrapper += (fixtures / 'wrapper-hooks.c').read_text()
    gpuppm = common + re.sub(r'^#include [<"].*$', '', texts[GPUPPM], flags=re.M)
    return fixtures, common, {'wrapper': wrapper, 'gpuppm': gpuppm,
                              'externals': common + (fixtures / 'externals.c').read_text(),
                              'cases': common + (fixtures / 'cases.c').read_text()}


FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
         '-Wno-unused-variable', '-Wno-unused-const-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def compile_object(directory, name, source):
    path = directory / (name + '.c')
    path.write_text(source)
    subprocess.run(FLAGS + ['-c', str(path), '-o', str(directory / (name + '.o'))], check=True, timeout=120)
    return directory / (name + '.o')


def link(directory, name, objects):
    binary = directory / name
    subprocess.run(FLAGS + [str(item) for item in objects] + ['-o', str(binary)], check=True, timeout=120)
    return binary


def run(binary, case, assertion=None):
    result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=60)
    if assertion is None:
        if result.returncode:
            raise RuntimeError(f'{binary.name}/{case} exited {result.returncode}: {result.stdout}{result.stderr}')
        print(result.stdout.strip())
        return
    if not result.returncode or assertion not in result.stderr or 'Assertion' not in result.stderr:
        raise RuntimeError(f'Control did not reach its expected assertion: {binary.name}/{case}: {result.stderr}')
    print(f'PASS rejected {binary.name}/{case}: {assertion}')


def main():
    parser = argparse.ArgumentParser(description='Exercise the gpufreq thermal cooling device of the MT6789 provider '
                                                 'through the actual wrapper and gpuppm limiter code')
    parser.add_argument('kernel_tree', type=Path)
    tree = parser.parse_args().kernel_tree.resolve()
    texts, old = sources(tree)
    bindings(tree, texts[PROVIDER])
    with tempfile.TemporaryDirectory(prefix='gpu-cooling-') as temporary:
        directory = Path(temporary)
        fixtures, common, shared = units(tree, texts)
        objects = [compile_object(directory, name, source) for name, source in shared.items()]
        candidate = link(directory, 'candidate', objects + [compile_object(directory, 'provider', provider_unit(tree, fixtures, common, texts[PROVIDER]))])
        for case in CASES:
            run(candidate, case)
        original = link(directory, 'baseline', objects + [compile_object(directory, 'provider-baseline', provider_unit(tree, fixtures, common, old))])
        run(original, 'registration-mt6789', 'register_calls == 1')
        for name, edits, case, assertion in CONTROLS:
            mutated = texts[PROVIDER]
            for before, after in edits:
                mutated = replace_once(mutated, before, after)
            mutant = link(directory, name, objects + [compile_object(directory, 'provider-' + name, provider_unit(tree, fixtures, common, mutated))])
            run(mutant, case, assertion)
    print(f'PASS {len(PINS)} reviewed gpufreq functions and gpuppm.c equal their pins; DT, Kleaf and load-order bindings hold; '
          f'{len(CASES)} cases on the actual provider, wrapper and gpuppm code; the baseline probe registers no cooling device; '
          f'{len(CONTROLS)} mutations rejected at their assertions; kernel build and device throttling pending')


if __name__ == '__main__':
    main()
