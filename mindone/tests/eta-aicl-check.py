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
FIXTURE = Path(__file__).with_name('eta-aicl')
BASELINE = '029526af83665dee2e9c5a6361b5af031ff60bad'
SOURCE = ROOT / 'mindone/modules/eta6965_charger/eta6965_charger.c'
BASIC = ROOT / 'mindone/modules/mtk_charger_framework/mtk_basic_charger.c'
NAMES = ('eta6965_read_byte_unlocked', 'eta6965_read_byte',
         'eta6965_update_bits_unlocked', 'eta6965_update_bits',
         'eta6965_update_bits_checked', 'eta6965_table_lookup_code',
         'eta_op_set_iinlim', 'eta_op_run_aicl', 'eta6965_suspend', 'eta6965_resume')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def function(source, name):
    match = re.search(r'^static (?:int|u32) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('Missing actual function ' + name)
    at, depth = match.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Nonunique source boundary: ' + old)
    return source.replace(old, new, 1)


def extract(source):
    constants = '\n'.join(re.findall(r'^#define\s+(?:ETA6965_REG_|REG|ICHG_|IINLIM_|CV_)\S+[^\n]*', source, re.M))
    structure = re.search(r'struct eta6965_device \{.*?\n\};', source, re.S).group()
    table = re.search(r'static const u32 iinlim_table_uA\[\] = \{.*?\n\};', source, re.S).group()
    return '\n'.join((constants, structure, table, *(function(source, name) for name in NAMES)))


def read_baseline(path):
    result = subprocess.run(['git', 'show', BASELINE + ':' + str(path.relative_to(ROOT))], cwd=ROOT,
                            env=dict(os.environ, GIT_NO_LAZY_FETCH='1'),
                            capture_output=True, check=True, timeout=30)
    return result.stdout.decode()


def basic_slice(source):
    start = source.index('\tif (support_fast_charging(info))')
    end = source.index('\t\tif (is_typec_adapter(info))', start)
    invoke = source[start:end] + '\t}\n'
    start = source.index('\tif (is_basic == true && pdata->input_current_limit_by_aicl != -1')
    end = source.index('\tinfo->setting.input_current_limit_dvchg1', start)
    limits = source[start:end]
    body = re.sub(r'/\*.*?\*/', '', invoke + limits, flags=re.S)
    return 'static void basic_slice(struct mtk_charger *info, struct charger_data *pdata)\n{\n\tbool is_basic = false;\n' + body + '}\n'


def run():
    parser = argparse.ArgumentParser(description='Execute exact ETA AICL error, cleanup, caller and PM controls')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    original = read_baseline(SOURCE)
    basic = BASIC.read_text()
    assert basic == read_baseline(BASIC)
    assert sha(original.encode()) == 'a4d686f9dd8d400997d748382c09bf5a3fb5d958138e80fcd9378bca7357700d'
    errno_source = (ROOT / 'include/uapi/asm-generic/errno.h').read_text()
    for name, value in (('ENODATA', 61), ('ETIMEDOUT', 110), ('EREMOTEIO', 121)):
        assert re.search(r'^#define\s+' + name + r'\s+' + str(value) + r'\b', errno_source, re.M), name
    old_function = function(original, 'eta_op_run_aicl')
    expected_function = once(old_function, '\tint ret;', '\tint ret, restore_ret;')
    tail = '\teta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);\n\t*uA = iinlim_table_uA[good_code];\n\treturn 0;'
    new_tail = '\trestore_ret = eta6965_update_bits_checked(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);\n\tif (ret)\n\t\treturn ret;\n\tif (restore_ret)\n\t\treturn restore_ret;\n\t*uA = iinlim_table_uA[good_code];\n\treturn 0;'
    expected_function = once(expected_function, tail, new_tail)
    candidate = SOURCE.read_text()
    assert candidate == once(original, old_function, expected_function)
    mutants = {
        'ignore-acquisition-error': once(candidate, '\tif (ret)\n\t\treturn ret;\n\tif (restore_ret)', '\tif (restore_ret)'),
        'ignore-final-error': once(candidate, '\tif (restore_ret)\n\t\treturn restore_ret;\n\t*uA = iinlim_table_uA[good_code];', '\t(void)restore_ret;\n\t*uA = iinlim_table_uA[good_code];'),
        'unchecked-final-write': once(candidate, '\trestore_ret = eta6965_update_bits_checked(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);', '\trestore_ret = eta6965_update_bits(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);'),
        'publish-before-error': once(candidate, new_tail, new_tail.replace('\tif (ret)', '\t*uA = iinlim_table_uA[good_code];\n\tif (ret)', 1)),
        'cleanup-overwrites-first-error': once(candidate, '\tif (ret)\n\t\treturn ret;\n\tif (restore_ret)\n\t\treturn restore_ret;', '\tif (restore_ret)\n\t\treturn restore_ret;\n\tif (ret)\n\t\treturn ret;'),
        'skip-failed-acquisition-cleanup': once(candidate, '\trestore_ret = eta6965_update_bits_checked(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);', '\tif (ret)\n\t\treturn ret;\n\trestore_ret = eta6965_update_bits_checked(eta, ETA6965_REG_00, REG00_IINLIM_MASK, good_code);'),
    }
    proof = {'baseline_commit': BASELINE, 'baseline_eta_sha256': sha(original.encode()),
             'basic_sha256': sha(basic.encode()), 'candidate_sha256': sha(candidate.encode()), 'results': {},
             'basic_slice_scope': 'Exact fast-support/AICL invocation/dynamic-MIVR and cached-limit application slices; surrounding eligibility, other caps and do_algorithm are source-reviewed, not fully emulated.'}
    expected_failure = {
        'ignore-acquisition-error': 'every_access_error',
        'ignore-final-error': 'every_access_error',
        'unchecked-final-write': 'cleanup_failures',
        'publish-before-error': 'every_access_error',
        'cleanup-overwrites-first-error': 'cleanup_failures',
        'skip-failed-acquisition-cleanup': 'cleanup_failures',
    }
    traces = {}
    with tempfile.TemporaryDirectory(prefix='eta-aicl-') as temporary:
        output = Path(temporary)
        for name, source in [('baseline', original), ('candidate', candidate), *mutants.items()]:
            folder = output / name
            folder.mkdir()
            (folder / 'eta_source.h').write_text(extract(source))
            (folder / 'basic_slice.h').write_text(basic_slice(basic))
            for file in ('cases.c', 'stubs.h'):
                shutil.copy2(FIXTURE / file, folder / file)
            binary = folder / 'fixture'
            build = subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-pointer-sign',
                                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                    '-I', str(folder), str(folder / 'cases.c'), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=30)
            (folder / 'build.stdout').write_text(build.stdout)
            (folder / 'build.stderr').write_text(build.stderr)
            assert build.returncode == 0, name + ': ' + build.stderr
            mode = 'baseline' if name == 'baseline' else 'contract'
            result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=20)
            (folder / 'run.stdout').write_text(result.stdout)
            (folder / 'run.stderr').write_text(result.stderr)
            if name in ('baseline', 'candidate'):
                assert result.returncode == 0, name + ': ' + result.stderr
                traces[name] = next(line[6:].split() for line in result.stdout.splitlines() if line.startswith('TRACE '))
            else:
                assert result.returncode != 0 and 'Assertion failed' in result.stderr and \
                    'function ' + expected_failure[name] + ',' in result.stderr, name + ': ' + result.stderr
            proof['results'][name] = {'actual_returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
            if name == 'baseline':
                reject = subprocess.run([str(binary), 'contract'], capture_output=True, text=True, timeout=20)
                assert reject.returncode != 0 and 'Assertion failed' in reject.stderr and \
                    'function every_access_error,' in reject.stderr, reject.stderr
                (folder / 'fix-contract.stderr').write_text(reject.stderr)
                proof['baseline_rejected_fix_contract'] = reject.returncode
        assert traces['candidate'] == traces['baseline'] + ['R00:00']
        proof['healthy_command_order'] = 'Identical maximal healthy bus sequence plus exactly one final REG00 readback under the existing per-access mutex; fixed sleeps/probe count and normal commands unchanged.'
        proof['limits'] = ['Controlled bus/write/PM/caller observations are not hardware acknowledgement.',
                           'No absolute elapsed-time bound: I2C and mutex scheduling excluded from 32x50ms requested sleeps.',
                           'Basic re-applies its normal input policy even after errno; this fixes a false positive cached AICL result, not fail-closed charging.',
                           'PM is tested by direct callbacks; charger-thread wakeup source prevents claiming ordinary suspend overlaps an active AICL call.',
                           'Individual access locks do not serialize the whole AICL operation or prevent another writer after verified readback; no new long-held mutex.',
                           'Initial negative write and failed cleanup can leave hardware changed or unknown; no physical rollback guarantee.',
                           'Native exact-module/full-set/device gates remain separate.']
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            for folder in output.iterdir():
                shutil.copytree(folder, args.output / folder.name, dirs_exist_ok=True)
            (args.output / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    run()
