# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from baseline_source import require, strip_comments


CHECK = 'mali-pm'
MODULES = 'mindone/modules/'
CALLBACK = MODULES + 'mali_kbase_r54/platform/mt6789/mali_kbase_config_mt6789.c'
COMMON = MODULES + 'mali_kbase_r54/platform/mtk_platform_common/mtk_platform_common.c'
DVFS = MODULES + 'mali_kbase_r54/platform/mtk_platform_common/mtk_platform_dvfs.c'
PM_BACKEND = MODULES + 'mali_kbase_r54/backend/gpu/mali_kbase_pm_backend.c'
PM_DRIVER = MODULES + 'mali_kbase_r54/backend/gpu/mali_kbase_pm_driver.c'
PROVIDER = MODULES + 'mtk_gpufreq_mt6789/gpufreq_mt6789.c'
WRAPPER = MODULES + 'mtk_gpufreq_wrapper/gpufreq_v2.c'
GPUFREQ_INCLUDE = 'include/drivers/gpu/mediatek/gpufreq/v2_legacy/include'
CONFIG = MODULES + GPUFREQ_INCLUDE + '/gpufreq_mt6789.h'
GPUFREQ_API = MODULES + GPUFREQ_INCLUDE + '/gpufreq_v2_legacy.h'
GPUFREQ_COMMON = MODULES + GPUFREQ_INCLUDE + '/gpufreq_common_legacy.h'
GED_API = MODULES + 'include/drivers/gpu/mediatek/ged/include/ged_dvfs.h'
GED_NOTIFY = MODULES + 'ged/ged_notify_sw_vsync.c'
SOURCES = (CALLBACK, COMMON, DVFS, PM_BACKEND, PM_DRIVER, PROVIDER, WRAPPER,
           CONFIG, GPUFREQ_API, GPUFREQ_COMMON, GED_API, GED_NOTIFY)
PINS = (
    (PROVIDER, 'function', '__gpufreq_bringup', '9305d0c9e7e7456a8b79053bd2d6ed629e5af0e657f1de525daf6d37fa63cf02'),
    (PROVIDER, 'function', '__gpufreq_power_ctrl_enable', '7e88bc0844406fb5a411a5d48b702f5430c2ebf0b61dc64c5143266669d7acfd'),
    (PROVIDER, 'function', '__gpufreq_set_timestamp', 'c9d862e69f5147c4ae2d62272f4c390d42e2e42b2b2c1be4f8e2b76508841dc2'),
    (PROVIDER, 'function', '__gpufreq_check_bus_idle', '26abaab1760f1b4e490f074d1fea312a05d33868890da8ee8155c986cea4a47f'),
    (PROVIDER, 'function', '__gpufreq_power_control', 'ac6bd36b3ea63c598f6c73fc73b3759793ec5391113657ce48edb57f5476975e'),
    (WRAPPER, 'function', 'gpufreq_power_ctrl_enable', '370d3a803c5bc99381145aad782fca18293d351b7d1b928fee19ea72bb390994'),
    (WRAPPER, 'function', 'gpufreq_power_control', '15d82a3bf6ca3715c14b4d181f52486eb2d1cd9c255b61727fea951bd18d8c1a'),
    (COMMON, 'function', 'mtk_common_pm_is_mfg_active', 'a2624972c423b9947a8985b75f8b98d2aecc64ece3da486b16d11b40eb468efd'),
    (COMMON, 'function', 'mtk_common_pm_mfg_active', '18c914ac2b0281f6bf11acff7381cf5a127e067f900298d5bda4ad9c2d5405fd'),
    (COMMON, 'function', 'mtk_common_pm_mfg_idle', 'e7950a1e2a9096071e9ea9ad1407f4533941bd4fbd6c92b2f6b79724f5288259'),
    (COMMON, 'function', 'mtk_common_gpufreq_commit', '9ebe94341d4d25a9ec190db394091caa16117f0a44f5b2c4df8edeffa58cb7ee'),
    (COMMON, 'function', 'mtk_common_ged_dvfs_get_last_commit_idx', 'f68fcb7f2f8611ae45f45d6a054c7e3a2a1373ebba27bec4540aa969dadb6c92'),
    (CALLBACK, 'function', 'gpu_dvfs_status_footprint', '90fcc795622ee894530a27835eeade9a1e2397e90a0560a755775b86e9f6cf7b'),
    (CALLBACK, 'function', 'pm_callback_power_on_nolock', '76d0cebd60ca7bb6ee3c116bf4ac7e7e4f7cc8f657fd948a8e0c8d640975e1fe'),
    (CALLBACK, 'function', 'pm_callback_power_off_nolock', 'c075070a07dd6ba361fb83a15718000d62c4789498a2d5d29e86b92dbcc064cb'),
    (CALLBACK, 'function', 'pm_callback_power_on', '9a5bf2f3005e293618f55c9b91e6228e3ef2e22d9949abf668da7bbf9d786a9c'),
    (CALLBACK, 'function', 'pm_callback_power_off', 'ef5d2150ddc8580454ba3ae916d4e106dbe222571227d0abfce85ef81c2f0e1f'),
    (GPUFREQ_API, 'enum', 'gpufreq_return', 'ebb88babc06c55d39e09e57c9e0319628da8f9731399a3cd2b9d585fec33fb9f'),
    (GPUFREQ_API, 'enum', 'gpufreq_dvfs_state', 'f698c255e617f947299172af2c01b896de6a6a9f3ce9e1cb021b76e7eb4b078d'),
    (GPUFREQ_API, 'enum', 'gpufreq_target', 'eaa5fb0829faac8d66f02f6f85b843ec96b3ba51134e72b5ebbb7171b413cce9'),
    (GPUFREQ_API, 'enum', 'gpufreq_power_state', '249baff69d7396e05f8f332a88f57ac3ad7a920abed9bd526ca560f989f1cc4d'),
    (GED_API, 'enum', 'ged_gpu_power_state', '8b5be7a7353eda0f28c27d6a0262140b7681f0747fc7437c1446d9a757c450b6'),
    (CALLBACK, 'enum', 'gpu_dvfs_status_step', 'd851b2d58af77b3c35ae8fa6006c13fa441f50686bf4c05a280dad61cb556359'),
    (GPUFREQ_COMMON, 'function', '__gpufreq_abort', '5954185f9d70fc2c47518bdea372a14d91f0b60c46bb2428f44043ff9f17f6ac'),
)
BUILD = MODULES + 'BUILD.bazel'
DTS = 'mindone/dts/mt6789-gpu.dtsi'
MALI_DEFINES = {'CONFIG_MTK_GPUFREQ_V2': '1', 'CONFIG_MALI_MIDGARD_DVFS': '1',
                'CONFIG_MALI_MTK_DVFS_POLICY': '1', 'CONFIG_MTK_GPU_SWPM_SUPPORT': '1', 'MALI_USE_CSF': '0'}
PROVIDER_CONFIG = {'GPUFREQ_BRINGUP': '0', 'GPUFREQ_POWER_CTRL_ENABLE': '1', 'GPUFREQ_SELF_CTRL_MTCMOS': '1'}
ENUMS = ((GPUFREQ_API, ('gpufreq_return', 'gpufreq_dvfs_state', 'gpufreq_target', 'gpufreq_power_state')),
         (GED_API, ('ged_gpu_power_state',)),
         (CALLBACK, ('gpu_dvfs_status_step',)))
FUNCTIONS = ((PROVIDER, ('__gpufreq_bringup', '__gpufreq_power_ctrl_enable', '__gpufreq_set_timestamp',
                         '__gpufreq_check_bus_idle', '__gpufreq_power_control')),
             (WRAPPER, ('gpufreq_power_ctrl_enable', 'gpufreq_power_control')),
             (COMMON, ('mtk_common_pm_is_mfg_active', 'mtk_common_pm_mfg_active', 'mtk_common_pm_mfg_idle',
                       'mtk_common_gpufreq_commit', 'mtk_common_ged_dvfs_get_last_commit_idx')),
             (CALLBACK, ('gpu_dvfs_status_footprint', 'pm_callback_power_on_nolock',
                         'pm_callback_power_off_nolock', 'pm_callback_power_on', 'pm_callback_power_off')))
REUSE = ('\tif (power == GPU_PWR_ON && g_gpu.power_count == 1 && g_bus_idle_timeout) {\n'
         '\t\tg_bus_idle_timeout = false;\n'
         '\t} else if (power == GPU_PWR_ON && g_gpu.power_count == 1) {\n')
NO_REUSE = '\tif (power == GPU_PWR_ON && g_gpu.power_count == 1) {\n'
ABORT = ('\t\tif (g_bus_idle_timeout) {\n'
         '\t\t\tGPUFREQ_LOGE("bus not idle after timeout, power-off aborted, GPU stays on");\n')
ABORT_RESTORE = ('\t\tif (g_bus_idle_timeout) {\n'
                 '\t\t\tg_gpu.power_count++;\n'
                 '\t\t\t__gpufreq_footprint_power_count(g_gpu.power_count);\n'
                 '\t\t\tGPUFREQ_LOGE("bus not idle after timeout, power-off aborted, GPU stays on");\n')
FAILED_OFF = ('    if (gpufreq_power_control(GPU_PWR_OFF) < 0) {\n'
              '        KBASE_PLATFORM_LOGE("Power Off Failed");\n')
ROLLBACK = ('    if (gpufreq_power_control(GPU_PWR_OFF) < 0) {\n'
            '        mtk_common_pm_mfg_active();\n'
            '#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)\n'
            '        ged_dvfs_gpu_clock_switch_notify(GED_POWER_ON);\n'
            '#endif\n'
            '        KBASE_PLATFORM_LOGE("Power Off Failed");\n')
COUNT_RESTORE = {PROVIDER: ((REUSE, NO_REUSE), (ABORT, ABORT_RESTORE))}
CALLBACK_ROLLBACK = {CALLBACK: ((FAILED_OFF, ROLLBACK),)}
MODELS = (('actual', 0, {}),
          ('count-restore-provider', 1, COUNT_RESTORE),
          ('callback-rollback', 2, CALLBACK_ROLLBACK),
          ('callback-rollback-count-restore', 3, {**COUNT_RESTORE, **CALLBACK_ROLLBACK}))
CONTROLS = (
    ('contract-count-restore-provider', 0, COUNT_RESTORE,
     'g_gpu.power_count == 0 && g_bus_idle_timeout && hw_on()'),
    ('contract-callback-rollback', 0, CALLBACK_ROLLBACK,
     '!mfg_powered && ged_state == GED_POWER_OFF && off_calls == 1'),
    ('verbatim-6.12-rollback', 2, {CALLBACK: ((FAILED_OFF, ROLLBACK.replace('(GED_POWER_ON)', '(1)')),)},
     'mfg_powered && ged_state == GED_POWER_ON && off_calls == 1'),
    ('drop-provider-reuse', 0, {PROVIDER: ((REUSE, NO_REUSE),)},
     'g_gpu.power_count == 1 && hw_on() && !g_bus_idle_timeout && on_calls == 1'),
    ('keep-timeout-on-reuse', 0, {PROVIDER: (('\t\tg_bus_idle_timeout = false;\n\t} else if', '\t} else if'),)},
     'g_gpu.power_count == 1 && hw_on() && !g_bus_idle_timeout && on_calls == 1'),
    ('restore-provider-count', 0, {PROVIDER: ((ABORT, ABORT_RESTORE),)},
     'g_gpu.power_count == 0 && g_bus_idle_timeout && hw_on()'),
    ('skip-bus-check', 0, {PROVIDER: (('\t\t__gpufreq_check_bus_idle();\n\t\tif (g_bus_idle_timeout) {',
                                       '\t\tif (g_bus_idle_timeout) {'),)},
     'provider_off,bus_check,clock_off'),
    ('drop-commit-admission', 0, {COMMON: (('opp_idx >= 0 && mtk_common_pm_is_mfg_active()', 'opp_idx >= 0'),)},
     'mtk_common_gpufreq_commit(last_commit) == -1'),
    ('drop-off-serialization', 0, {CALLBACK: (('    mutex_lock(&g_mfg_lock);\n    mtk_notify_gpu_power_change(0);',
                                               '    mtk_notify_gpu_power_change(0);'),)},
     'g_mfg_lock == 1'),
    ('drop-mfg-idle', 0, {CALLBACK: (('    mtk_common_pm_mfg_idle();\n', ''),)},
     '!mfg_powered && ged_state == GED_POWER_OFF && g_curFreqID == last_commit'),
    ('ignore-active-on', 0, {CALLBACK: (('    if (mtk_common_pm_is_mfg_active())\n        return 0;',
                                         '    if (false)\n        return 0;'),)},
     'pm_callback_power_on(&kbdev) == 0'),
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


def enum(source, name):
    matches = re.findall(r'^enum ' + re.escape(name) + r' \{.*?^\};', strip_comments(source), re.M | re.S)
    if len(matches) != 1:
        raise ValueError(f'Expected one definition of enum {name}, found {len(matches)}')
    return matches[0] + '\n'


def block(text, pattern, label):
    match = re.search(pattern, text, re.M | re.S)
    if not match:
        raise ValueError('Missing ' + label)
    return match.group(1)


def target(build, name):
    body = block(build, r'^ddk_module\(\n    name = "' + re.escape(name) + r'",\n(.*?)^\)', 'Kleaf target ' + name)
    lists = {}
    for key in ('srcs', 'includes', 'local_defines'):
        found = re.search(r'^    ' + key + r' = \[(.*?)^    \],', body, re.M | re.S)
        lists[key] = re.findall(r'"([^"]+)"', found.group(1)) if found else []
    return lists


def bindings(tree):
    build = (tree / BUILD).read_text()
    mali = target(build, 'mali_kbase_mt6789')
    for path in (CALLBACK, COMMON, DVFS, PM_BACKEND, PM_DRIVER):
        if path[len(MODULES):] not in mali['srcs']:
            raise ValueError('mali_kbase_mt6789 no longer builds ' + path)
    defines = dict(item.split('=', 1) if '=' in item else (item, '1') for item in mali['local_defines'])
    for name, value in MALI_DEFINES.items():
        if defines.get(name) != value:
            raise ValueError(f'mali_kbase_mt6789 changed {name}: {defines.get(name)}, reviewed {value}')
    provider = target(build, 'mtk_gpufreq_mt6789')
    if PROVIDER[len(MODULES):] not in provider['srcs'] or GPUFREQ_INCLUDE not in provider['includes']:
        raise ValueError('mtk_gpufreq_mt6789 no longer builds the reviewed provider and configuration')
    if WRAPPER[len(MODULES):] not in target(build, 'mtk_gpufreq_wrapper')['srcs']:
        raise ValueError('mtk_gpufreq_wrapper no longer builds ' + WRAPPER)
    dts = strip_comments((tree / DTS).read_text())
    wrapper_node = block(dts, r'^\tgpufreq_wrapper: gpufreq_wrapper \{(.*?)^\t\};', 'gpufreq_wrapper DT node')
    if not re.search(r'^\t\tgpueb-support = <0x00>;$', wrapper_node, re.M):
        raise ValueError('gpufreq_wrapper is no longer in AP power-control mode (gpueb-support)')
    mali_node = block(dts, r'^\tmali: mali@13000000 \{(.*?)^\t\};', 'Mali DT node')
    if 'sleep-mode-enable' in mali_node:
        raise ValueError('Mali DT node enables the runtime idle callbacks this check does not model')
    config = strip_comments((tree / CONFIG).read_text())
    lines = []
    for name, value in PROVIDER_CONFIG.items():
        found = re.search(r'^#define ' + name + r'\s+\((\d+)\)\s*$', config, re.M)
        if not found or found.group(1) != value:
            raise ValueError(f'AP provider configuration changed: {name}')
        lines.append(found.group(0).strip())
    abort = function((tree / GPUFREQ_COMMON).read_text(), '__gpufreq_abort')
    if 'BUG_ON(1);' not in abort:
        raise ValueError('__gpufreq_abort no longer stops on hardware control failures')
    return defines, lines


def sources(tree):
    texts = {}
    for path in SOURCES:
        texts[path] = (tree / path).read_text()
    for path, kind, name, pin in PINS:
        extracted = function(texts[path], name) if kind == 'function' else enum(texts[path], name)
        require(f'Reviewed {kind} {name} in {path}', extracted, pin)
    return texts


def assemble(texts, defines, config_lines, model, edits):
    texts = dict(texts)
    for path, changes in edits.items():
        for before, after in changes:
            texts[path] = replace_once(texts[path], before, after)
    functions = {path: ''.join(function(texts[path], name) for name in names) for path, names in FUNCTIONS}
    mali = set(re.findall(r'\bCONFIG_[A-Z0-9_]+', functions[CALLBACK] + functions[COMMON]))
    if mali - set(MALI_DEFINES):
        raise ValueError('Extracted Mali code reads an unbound configuration: ' + ', '.join(sorted(mali - set(MALI_DEFINES))))
    if re.search(r'\bCONFIG_[A-Z0-9_]+', functions[PROVIDER] + functions[WRAPPER]):
        raise ValueError('Extracted gpufreq code reads a configuration this check does not bind')
    prologue = ['#include <errno.h>', '#define BIT(n) (1U << (n))']
    prologue += [f'#define {name} {defines[name]}' for name in sorted(mali)]
    prologue += config_lines + [f'#define MODEL {model}']
    enums = [enum(texts[path], name) for path, names in ENUMS for name in names]
    fixtures = Path(__file__).resolve().parent / CHECK
    return ('\n'.join(prologue) + '\n' + ''.join(enums) + (fixtures / 'stubs.h').read_text()
            + ''.join(functions.values()) + (fixtures / 'cases.c').read_text())


def compile_source(directory, name, source):
    path = directory / (name + '.c')
    path.write_text(source)
    binary = directory / name
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(path), '-o', str(binary)], check=True, timeout=60)
    return binary


def run(binary, assertion=None):
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    if assertion is None:
        if result.returncode:
            raise RuntimeError(f'{binary.name} exited {result.returncode}: {result.stdout}{result.stderr}')
        print(result.stdout.strip())
        return int(re.search(r'^PASS model\d (\d+) groups$', result.stdout, re.M).group(1))
    if not result.returncode or assertion not in result.stderr or 'assert' not in result.stderr.lower():
        raise RuntimeError(f'Control did not reach its expected assertion: {binary.name}: {result.stderr}')
    print(f'PASS rejected {binary.name}: {assertion}')
    return 0


def main():
    parser = argparse.ArgumentParser(description='Exercise the r54 MT6789 Mali power callbacks with the actual GED, '
                                                 'gpufreq wrapper and provider sources across a rejected power-off')
    parser.add_argument('kernel_tree', type=Path)
    tree = parser.parse_args().kernel_tree.resolve()
    texts = sources(tree)
    defines, config_lines = bindings(tree)
    with tempfile.TemporaryDirectory(prefix='mali-pm-') as temporary:
        directory = Path(temporary)
        groups = {}
        for name, model, edits in MODELS:
            groups[name] = run(compile_source(directory, name, assemble(texts, defines, config_lines, model, edits)))
        for name, model, edits, assertion in CONTROLS:
            run(compile_source(directory, name, assemble(texts, defines, config_lines, model, edits)), assertion)
    print(f'PASS {len(PINS)} reviewed r54 PM functions and enums equal their pins, Kleaf and DT bindings hold; '
          f'actual model {groups["actual"]} groups; count-restore {groups["count-restore-provider"]}, '
          f'callback-rollback {groups["callback-rollback"]} and 6.12 pairing '
          f'{groups["callback-rollback-count-restore"]} groups; {len(CONTROLS)} controls rejected at their '
          f'assertions; native build and physical power-off pending')


if __name__ == '__main__':
    main()
