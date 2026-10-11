import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'mindone/modules/eta6965_charger/eta6965_charger.c'
BASELINE = '76285b19ca789e39e1c3631f600db13ce07b4af6'
FIXTURE = Path(__file__).with_name('eta-current')
NAMES = (
    'eta6965_read_byte_unlocked', 'eta6965_read_byte', 'eta6965_update_bits_unlocked',
    'eta6965_update_bits', 'eta6965_update_bits_checked', 'eta6965_table_lookup_code', 'eta_op_set_ichg',
    'eta_op_get_ichg', 'eta_op_set_iinlim', 'eta6965_get_iinlim_uA',
    'eta_op_get_iinlim', 'eta_op_run_aicl', 'eta_op_set_cv', 'eta6965_get_cv_uV',
)


def function(source, name):
    match = re.search(r'^static (?:int|u32) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('Missing exact function: ' + name)
    at, depth = match.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


def extract(source):
    constants = '\n'.join(re.findall(r'^#define\s+(?:ETA6965_REG_|REG|ICHG_|IINLIM_|CV_)\S+[^\n]*', source, re.M))
    structure = re.search(r'struct eta6965_device \{.*?\n\};', source, re.S).group()
    table = re.search(r'static const u32 iinlim_table_uA\[\] = \{.*?\n\};', source, re.S).group()
    names = tuple(name for name in NAMES if name != 'eta6965_update_bits_checked' or
                  'eta6965_update_bits_checked(' in function(source, 'eta_op_run_aicl'))
    return '\n'.join((constants, structure, table, *(function(source, name) for name in names)))


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Mutation boundary is not unique: ' + old)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description='Execute exact ETA current register contracts and negative controls')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    candidate = SOURCE.read_text()
    result = subprocess.run(['git', 'show', BASELINE + ':' + str(SOURCE.relative_to(ROOT))],
                            cwd=ROOT, env=dict(os.environ, GIT_NO_LAZY_FETCH='1'),
                            text=True, capture_output=True, timeout=30, check=True)
    baseline = result.stdout
    unchanged = ('eta_op_set_cv', 'eta6965_get_cv_uV', 'eta6965_hw_init',
                 'eta_op_enable_safety_timer', 'eta_op_is_safety_timer_enabled')
    for name in unchanged:
        assert function(candidate, name) == function(baseline, name), name
    assert function(candidate, 'eta6965_psy_get_property') == function(baseline, 'eta6965_psy_get_property')
    assert 'eta6965_get_iinlim_uA(eta, &uA)' in function(candidate, 'eta6965_psy_get_property')
    assert 'case POWER_SUPPLY_PROP_CURRENT_MAX:' in function(candidate, 'eta6965_psy_get_property')
    aicl = function(candidate, 'eta_op_run_aicl')
    encoding_aicl = replace_once(aicl, '\tint ret, restore_ret;', '\tint ret;')
    encoding_aicl = replace_once(encoding_aicl,
        '\trestore_ret = eta6965_update_bits_checked(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);\n\tif (ret)\n\t\treturn ret;\n\tif (restore_ret)\n\t\treturn restore_ret;',
        '\teta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);')
    encoding_candidate = replace_once(candidate, aicl, encoding_aicl)
    assert encoding_candidate == replace_once(replace_once(baseline, '#define ICHG_MAX_CODE\t54', '#define ICHG_MAX_CODE\t50'),
                                             '2100000, 2200000, 2300000,', '2100000, 2200000, 2300000, 2400000,')
    decoder = replace_once(baseline, '#define ICHG_MAX_CODE\t54', '#define ICHG_MAX_CODE\t50')
    old_getter = function(decoder, 'eta6965_get_iinlim_uA')
    new_getter = old_getter.replace('\t\tif (code > IINLIM_MAX_CODE)\n\t\t\tcode = IINLIM_MAX_CODE;\n', '')
    new_getter = replace_once(new_getter, '*uA = iinlim_table_uA[code];', '*uA = 100000 + code * 100000;')
    decoder = replace_once(decoder, old_getter, new_getter)
    mutations = {
        'charge-clamp54': replace_once(candidate, '#define ICHG_MAX_CODE\t50', '#define ICHG_MAX_CODE\t54'),
        'input-gap': replace_once(candidate, '2300000, 2400000,', '2300000,'),
        'input-max30': replace_once(candidate, '#define IINLIM_MAX_CODE\t(ARRAY_SIZE(iinlim_table_uA) - 1)', '#define IINLIM_MAX_CODE\t30'),
        'charge-upper-bit': replace_once(candidate, '#define  REG02_ICHG_MASK\t0x3f', '#define  REG02_ICHG_MASK\t0x7f'),
        'input-upper-bit': replace_once(candidate, '#define  REG00_IINLIM_MASK\t0x1f', '#define  REG00_IINLIM_MASK\t0x3f'),
        'charge-round-up': replace_once(candidate, 'u32 code = uA / ICHG_STEP_UA;', 'u32 code = (uA + ICHG_STEP_UA - 1) / ICHG_STEP_UA;'),
        'input-round-down-exact': replace_once(candidate, 'if (tbl[i] > target)', 'if (tbl[i] >= target)'),
        'aicl-mislabel': replace_once(candidate, '*uA = iinlim_table_uA[good_code];', '*uA = iinlim_table_uA[good_code] + 100000;'),
    }
    getter = function(candidate, 'eta_op_get_ichg')
    mutations['error-overwrites-output'] = replace_once(candidate, getter, replace_once(getter, '\treturn ret;', '\tif (ret) *uA = 0;\n\treturn ret;'))
    setter = function(candidate, 'eta_op_set_ichg')
    mutations['setter-ignores-error'] = replace_once(candidate, setter,
        replace_once(setter, '\treturn eta6965_update_bits(eta, ETA6965_REG_02, REG02_ICHG_MASK, (u8)code);',
                     '\teta6965_update_bits(eta, ETA6965_REG_02, REG02_ICHG_MASK, (u8)code);\n\treturn 0;'))
    report = {'baseline_commit': BASELINE, 'candidate_source_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
              'baseline_source_sha256': hashlib.sha256(baseline.encode()).hexdigest(),
              'unchanged_source_functions': list(unchanged) + ['eta6965_psy_get_property'],
              'controls': {}, 'mutations': {}}
    with tempfile.TemporaryDirectory(prefix='eta-current-') as temporary:
        folder = Path(temporary)
        for name, source in [('candidate', candidate), ('baseline', baseline), ('decoder-only', decoder), *mutations.items()]:
            case = folder / name
            case.mkdir()
            (case / 'eta_source.h').write_text(extract(source))
            for file in ('stubs.h', 'cases.c'):
                shutil.copy2(FIXTURE / file, case / file)
            binary = case / 'fixture'
            build = subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-parameter', '-pthread', '-fsanitize=address,undefined',
                                    '-fno-omit-frame-pointer', '-I', str(case), str(case / 'cases.c'), '-o', str(binary)],
                                   text=True, capture_output=True, timeout=30)
            (case / 'build.stdout').write_text(build.stdout)
            (case / 'build.stderr').write_text(build.stderr)
            assert build.returncode == 0, build.stderr
            mode = 'roundtrip' if name == 'decoder-only' else 'contract'
            run = subprocess.run([str(binary), mode], text=True, capture_output=True, timeout=30)
            (case / 'run.stdout').write_text(run.stdout)
            (case / 'run.stderr').write_text(run.stderr)
            row = {'actual_returncode': run.returncode, 'stdout': run.stdout.strip(), 'stderr': run.stderr.strip()}
            if name == 'candidate':
                assert run.returncode == 0 and json.loads(run.stdout)['datasheet_discrepancies'] == 0, row
            elif name == 'baseline':
                assert run.returncode == 17 and 'contract REG00 decode:' in run.stderr, row
                audit = subprocess.run([str(binary), 'audit'], text=True, capture_output=True, timeout=30)
                assert audit.returncode == 0, audit.stderr
                row['audit'] = json.loads(audit.stdout)
            elif name == 'decoder-only':
                assert run.returncode == 17 and 'contract PD old-IIN restore:' in run.stderr, row
            else:
                assert run.returncode != 0, row
                assert 'contract ' in run.stderr or 'Assertion failed:' in run.stderr, row
                report['mutations'][name] = row
            if name in ('candidate', 'baseline', 'decoder-only'):
                report['controls'][name] = row
        report['physical_delta'] = 'Requests>=2400000 gain100000uA effective REG00 versus the old missing entry, with saturation3200000 instead of3100000. No source/thermal/current request policy changes. ICHG>=3000000 remains hardware-clamped3000000.'
        report['limits'] = 'Host bus/DPM models are source-bound controls, not hardware acknowledgement or energy evidence. AICL error/cleanup/caller/PM contracts are independently exercised by eta-aicl-check.py; neither suite proves physical fault acceptance.'
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            for path in folder.iterdir():
                shutil.copytree(path, args.output / path.name, dirs_exist_ok=True)
            (args.output / 'proof.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
