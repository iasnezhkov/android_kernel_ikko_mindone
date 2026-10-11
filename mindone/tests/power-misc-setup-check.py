import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
function = runpy.run_path(str(HERE/'gauge-algo-check.py'))['function']
activation_case = runpy.run_path(str(HERE/'gauge-enable-check.py'))['activation_case']
FUNCTIONS = runpy.run_path(str(HERE/'gauge-irq-setup-check.py'))['FUNCTIONS']


def replace(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Ambiguous fixture adaptation: '+before[:100])
    return text.replace(before, after, 1)


def build(tree):
    base = tree/'mindone/modules/mt6358_battery'
    sources = {name: (base/name).read_text() for name, _ in FUNCTIONS}
    header = (base/'mtk_gauge.h').read_text()
    enums = '\n'.join(re.search(r'enum '+name+r'\s*\{.*?\};', header, re.S).group(0)
                      for name in ('gauge_irq', 'gauge_hw_version'))
    stubs = (HERE/'gauge-irq-setup/stubs.h').read_text()
    stubs = replace(stubs, '#define smp_load_acquire(pointer) (*(pointer))', '')
    stubs = replace(stubs, 'struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };',
                    'struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); struct notifier_block *next; int priority; };')
    stubs = replace(stubs, 'struct power_supply { struct device dev; };',
                    'struct power_supply { struct device dev; const struct power_supply_desc *desc; };')
    stubs = replace(stubs, 'struct mtk_battery {', 'struct shutdown_controller { struct mutex lock; int kthread_fgtimer, wait_que; bool timeout, overheat; struct notifier_block psy_nb; };\nstruct mtk_battery {\n    struct shutdown_controller sdc;\n    int cur_bat_temp;')
    stubs = replace(stubs, 'static void mtk_power_misc_init(struct mtk_battery *gm) { misc_calls++; }', '')
    insert = (HERE/'power-misc-setup/stubs.h').read_text()
    stubs = replace(stubs, 'static struct task_struct *kthread_run(', insert+'\nstatic struct task_struct *kthread_run(')
    stubs = replace(stubs, '    if (entry == gauge_coulomb_thread) {',
                    '    if (entry == power_misc_routine_thread) return misc_kthread(entry, data, name);\n    if (entry == gauge_coulomb_thread) {')
    notifier = (tree/'kernel/notifier.c').read_text()
    core = (tree/'drivers/power/supply/power_supply_core.c').read_text()
    text = enums+'\n'+stubs+'\n'
    for name in ('notifier_chain_register', '__blocking_notifier_chain_register', 'blocking_notifier_chain_register'):
        text += function(notifier, name)
    text += function(core, 'power_supply_reg_notifier')
    for name in ('wake_up_power_misc', 'wake_up_overheat', 'mtk_power_misc_psy_event', 'mtk_power_misc_init'):
        text += function(sources['mtk_battery.c'], name)
    for filename, name in FUNCTIONS:
        text += function(sources[filename], name)
    cases = (HERE/'gauge-irq-setup/cases.c').read_text()
    text += replace(cases, 'int main(void)', 'static int prior_regression_main(void)')
    text += 'static int activate_805(struct mtk_battery *gm) { int cmd=805; int ret; switch(cmd) {\n'
    text += activation_case(sources['mtk_battery_daemon.c'])
    text += 'default: abort(); } return 0; }\n'
    text += (HERE/'power-misc-setup/cases.c').read_text()
    return text


def execute(source, name, directory, barrier=False):
    path = directory/name
    path.with_suffix('.c').write_text(source)
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
               '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
               '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined', '-DCONFIG_PM=1']
    if barrier:
        command.append('-DEXPECT_EARLY_BARRIER=1')
    subprocess.run([*command, str(path.with_suffix('.c')), '-o', str(path)], check=True, timeout=30)
    return subprocess.run([str(path)], capture_output=True, text=True, timeout=10)


def main():
    parser = argparse.ArgumentParser(description='Exact power_misc startup, required readiness805, ACK notifier errno and diagnostic probe lifetime fault controls. No ADB, VM, worker/remove or hardware acceptance.')
    parser.add_argument('kernel_tree', type=Path)
    args = parser.parse_args()
    source = build(args.kernel_tree)
    barrier_anchor = '\tif (READ_ONCE(gm->kernel_setup_error))'
    barrier_after = '\tif (!smp_load_acquire(&gm->is_probe_done))\n\t\treturn -EAGAIN;\n'+barrier_anchor
    variants = [('actual', source, barrier_after in source)]
    if barrier_after not in source:
        variants.append(('checked-early-barrier', replace(source, barrier_anchor, barrier_after), True))
    mutations = (
        ('lose-worker-error', 'if (IS_ERR(misc_task))', 'if (IS_ERR(misc_task) && false)'),
        ('lose-notifier-error', 'ret = power_supply_reg_notifier(&gm->sdc.psy_nb);\n\tif (ret)', 'ret = power_supply_reg_notifier(&gm->sdc.psy_nb);\n\tif (ret && false)'),
        ('overwrite-first-service-error', 'cmpxchg(&gm->kernel_service_error, 0, ret);', 'WRITE_ONCE(gm->kernel_service_error, ret);'),
        ('ignore-required-service-error', 'ret = READ_ONCE(gm->kernel_service_error);', 'ret = 0;')
    )
    misc = function(source, 'mtk_power_misc_init')
    for name, before, after in mutations:
        if name == 'overwrite-first-service-error':
            if misc.count(before) != 2:
                raise ValueError('Expected two monitor error-latch sites')
            mutated = replace(source, misc, misc.replace(before, after))
        else:
            mutated = replace(source, before, after)
        variants.append((name, mutated, barrier_after in source))
    if barrier_after in source:
        checkpoint = '\tif (!smp_load_acquire(&gm->is_probe_done))\n\t\treturn -EAGAIN;\n'
        variants.append(('bypass-probe-completion', replace(source, checkpoint, ''), True))
        misplaced = replace(source, checkpoint, '')
        misplaced = replace(misplaced, '\tif (!gm->cs.init || gm->disableGM30)', checkpoint+'\tif (!gm->cs.init || gm->disableGM30)')
        variants.append(('acquire-after-service-error-read', misplaced, True))
        variants.append(('omit-probe-completion-publication', replace(source, 'smp_store_release(&gm->is_probe_done, true);', '(void)gm->is_probe_done;'), True))
    with tempfile.TemporaryDirectory(prefix='power-misc-setup-') as tmp:
        directory = Path(tmp)
        for name, text, barrier in variants:
            result = execute(text, name, directory, barrier)
            if name in ('actual', 'checked-early-barrier'):
                if result.returncode:
                    raise RuntimeError(name+':\n'+result.stdout+result.stderr)
                print(name+'\n'+result.stdout, end='')
            elif result.returncode == 0 or not re.search(r'Assertion failed:|runtime error:|ERROR: AddressSanitizer', result.stderr):
                raise RuntimeError('Mutation did not fail at runtime: '+name+'\n'+result.stdout+result.stderr)
            else:
                print('PASS rejected '+name)


if __name__ == '__main__':
    main()
