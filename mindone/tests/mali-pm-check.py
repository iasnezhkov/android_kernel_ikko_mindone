import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


BASELINE = '8e7569ad533f128157d01fd24702f2e78e898b95'
CALLBACK = 'mindone/modules/mali_kbase_clean/mali_kbase_config_mt6789.c'
COMMON = 'mindone/modules/mali_kbase_clean/mtk_platform_common.c'
PROVIDER = 'mindone/modules/mtk_gpufreq_mt6789/gpufreq_mt6789.c'
CONFIG = 'mindone/modules/include/drivers/gpu/mediatek/gpufreq/v2/include/gpufreq_mt6789.h'


def uncomment(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)


def function(source, name):
    source = uncomment(source)
    match = re.search(r'^(?:static )?(?:inline )?(?:int|void|bool) ' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError('Missing actual function: ' + name)
    opening = source.index('{', match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Change must match exactly once: ' + before)
    return source.replace(before, after, 1)


def git_show(tree, revision, path):
    return subprocess.run(['git', '-C', str(tree), 'show', revision + ':' + path],
                          check=True, capture_output=True, text=True, timeout=20,
                          env={**os.environ, 'GIT_NO_LAZY_FETCH': '1'}).stdout


def assemble(tree, callback, proposed):
    fixtures = Path(__file__).resolve().parent / 'mali-pm'
    result = (fixtures / 'stubs.h').read_text()
    result += '\n#define PROPOSED ' + str(int(proposed)) + '\n'
    common = (tree / COMMON).read_text()
    provider = (tree / PROVIDER).read_text()
    for name in ('mtk_common_pm_is_mfg_active', 'mtk_common_pm_mfg_active',
                 'mtk_common_pm_mfg_idle', 'mtk_common_gpufreq_commit'):
        result += function(common, name)
    result += function(provider, '__gpufreq_check_bus_idle')
    result += function(provider, '__gpufreq_power_control')
    for name in ('pm_callback_power_on_nolock', 'pm_callback_power_off_nolock',
                 'pm_callback_power_on', 'pm_callback_power_off'):
        result += function(callback, name)
    return result + (fixtures / 'cases.c').read_text()


def compile_source(directory, name, source):
    path = directory / (name + '.c')
    path.write_text(source)
    binary = directory / name
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', str(path), '-o', str(binary)], check=True, timeout=30)
    return binary


def run(binary, assertion=None):
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    if assertion is None:
        if result.returncode:
            raise RuntimeError(f'{binary.name} exited {result.returncode}: {result.stderr}')
        print(result.stdout.strip())
    else:
        if not result.returncode or assertion not in result.stderr or 'assert' not in result.stderr.lower():
            raise RuntimeError(f'Control did not reach its expected assertion: {binary.name}: {result.stderr}')
        print(f'PASS rejected {binary.name}: {assertion}')


def main():
    parser = argparse.ArgumentParser(description='Exercise exact MT6789 Mali power callbacks, bus-idle rejection, refcounts, healthy order and admission rollback')
    parser.add_argument('kernel_tree', type=Path)
    parser.add_argument('--baseline', default=BASELINE)
    args = parser.parse_args()
    tree = args.kernel_tree.resolve()
    source = (tree / CALLBACK).read_text()
    baseline = git_show(tree, args.baseline, CALLBACK)
    before = '''if (gpufreq_power_control(POWER_OFF) < 0) {
		dev_info(kbdev->dev, "GPU PM Callback - Power Off Failed");'''
    after = ('if (gpufreq_power_control(POWER_OFF) < 0) {\n'
             '\t\tmtk_common_pm_mfg_active();\n'
             '#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)\n'
             '\t\tged_dvfs_gpu_clock_switch_notify(1);\n'
             '#endif\n'
             '\t\tdev_info(kbdev->dev, "GPU PM Callback - Power Off Failed");')
    if source != replace_once(baseline, before, after):
        raise ValueError('Production scope exceeds the failed power-off rollback')
    for path in (COMMON, PROVIDER, CONFIG, 'mindone/modules/BUILD.bazel',
                 'mindone/modules/mali_kbase_clean/mali_kbase_pm_driver.c',
                 'mindone/modules/mali_kbase_clean/mali_kbase_pm_backend.c',
                 'mindone/modules/mali_kbase_clean/mtk_gpu_dvfs.c',
                 'mindone/modules/mtk_gpufreq_wrapper/gpufreq_v2.c',
                 'mindone/modules/ged/ged_notify_sw_vsync.c'):
        if (tree / path).read_text() != git_show(tree, args.baseline, path):
            raise ValueError('Actual PM dependency changed: ' + path)
    build = (tree / 'mindone/modules/BUILD.bazel').read_text()
    target = re.search(r'name = "mali_kbase_mt6789",.*?^\)', build, re.M | re.S)
    if not target:
        raise ValueError('Missing actual Mali DDK target')
    for binding in ('"mali_kbase_clean/mali_kbase_config_mt6789.c",',
                    '"CONFIG_MTK_GPUFREQ_V2=1",', '"CONFIG_MALI_MIDGARD_DVFS=1",',
                    '"CONFIG_MALI_MTK_DVFS_POLICY=1",', '"MALI_USE_CSF=0",'):
        if binding not in target.group(0):
            raise ValueError('Actual callback branch binding changed: ' + binding)
    config = uncomment((tree / CONFIG).read_text())
    for name, value in (('GPUFREQ_BRINGUP', 0), ('GPUFREQ_POWER_CTRL_ENABLE', 1),
                        ('GPUFREQ_SELF_CTRL_MTCMOS', 1)):
        if not re.search(r'^#define ' + name + r'\s+\(' + str(value) + r'\)', config, re.M):
            raise ValueError('Actual AP provider configuration changed: ' + name)
    with tempfile.TemporaryDirectory(prefix='mali-pm-') as temporary:
        directory = Path(temporary)
        candidate = assemble(tree, source, True)
        original = assemble(tree, baseline, False)
        run(compile_source(directory, 'candidate', candidate))
        run(compile_source(directory, 'baseline', original))
        run(compile_source(directory, 'baseline-fix-contract', assemble(tree, baseline, True)), 'mfg_powered && ged_on')
        controls = (
            ('omit-powered-rollback', after, after.replace('\t\tmtk_common_pm_mfg_active();\n', ''), 'mfg_powered && ged_on'),
            ('omit-ged-rollback', after, after.replace('\t\tged_dvfs_gpu_clock_switch_notify(1);\n', ''), 'mfg_powered && ged_on'),
            ('ignore-active-on', 'if (mtk_common_pm_is_mfg_active())\n\t\treturn 0;', 'if (false)\n\t\treturn 0;', 'g_gpu.power_count == 1 && on_calls == 2'),
            ('omit-provider-count-restore', '\t\t\tg_gpu.power_count++;\n\t\t\tg_dvfs_state', '\t\t\tg_dvfs_state', 'g_gpu.power_count == 1 && g_gpu.buck_count == 1'),
            ('skip-second-bus-check', '\t\t__gpufreq_check_bus_idle();\n\t\tif (g_bus_idle_timeout)', '\t\tif (g_bus_idle_timeout)', 'strcmp(events'),
            ('drop-commit-admission', 'opp_idx >= 0 && mtk_common_pm_is_mfg_active()', 'opp_idx >= 0', 'mtk_common_gpufreq_commit(last_commit) == -1'),
            ('drop-off-serialization', '\tmutex_lock(&g_mfg_lock);\n#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)', '#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)', 'g_mfg_lock == 1 && mfg_pm_lock == 0'),
        )
        for name, old, new, assertion in controls:
            mutant = compile_source(directory, name, replace_once(candidate, old, new))
            run(mutant, assertion)
    print('PASS 14 exact candidate and baseline control groups each, original failed fix contract, 7 executed mutations; native compile and physical power-state validation pending')


if __name__ == '__main__':
    main()
