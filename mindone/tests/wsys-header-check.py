# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


IDENTIFIER = re.compile(r'\b[A-Za-z_]\w*\b')
TOKEN = re.compile(r'[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|[^\s]')
OLD_NAME = 'wsys_cmd_' + 'handler_fw.h'
NEW_NAME = 'mindone_wsys_cmd.h'


def clean(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)


def words(source):
    return set(IDENTIFIER.findall(source))


def declarations(source):
    lines = clean(source).splitlines(keepends=True)
    units = []
    conditions = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        if not line:
            index += 1
            continue
        if line.startswith('#'):
            if re.match(r'#\s*if(?:ndef|def)?\b', line):
                conditions.append(line)
            elif line.startswith('#endif'):
                conditions.pop()
            elif line.startswith(('#else', '#elif')):
                raise ValueError('Unsupported top-level alternative: ' + line)
            elif line.startswith('#define'):
                match = re.match(r'#define\s+(\w+)(.*)', line)
                name, body = match.groups()
                if len(conditions) != 1 or not conditions[0].startswith('#ifndef '):
                    guard = False
                else:
                    guard = conditions[0].split()[-1] == name
                if not guard:
                    if body.startswith('('):
                        raise ValueError('Unexpected function macro: ' + name)
                    units.append({'kind': 'macro', 'name': name, 'names': {name},
                                  'text': line, 'conditions': tuple(conditions[1:])})
            elif not line.startswith('#include'):
                raise ValueError('Unsupported directive: ' + line)
            index += 1
            continue
        text = ''
        depth = 0
        while index < len(lines):
            text += lines[index]
            depth += lines[index].count('{') - lines[index].count('}')
            index += 1
            if depth == 0 and text.rstrip().endswith(';'):
                break
        if text.lstrip().startswith('typedef'):
            kind = 'typedef'
            name = re.search(r'(\w+)\s*(?:\[[^]]*\])?\s*;', text)[1]
            names = {name}
        else:
            kind, name = re.match(r'\s*(struct|union|enum)\s+(\w+)', text).groups()
            names = {name}
            if kind == 'enum':
                body = text.split('{', 1)[1].rsplit('}', 1)[0]
                body = re.sub(r'^\s*#.*$', '', body, flags=re.M)
                names.update(re.match(r'\s*(\w+)', item)[1]
                             for item in body.split(',') if item.strip())
            else:
                names.update(re.findall(r'\b(?:struct|union)\s+(\w+)\s*\{', text))
        units.append({'kind': kind, 'name': name, 'names': names,
                      'text': text.strip(), 'conditions': tuple(conditions[1:])})
    if conditions:
        raise ValueError('Unclosed conditional')
    return units


def include_tree(root, driver):
    build = (root / 'mindone/modules/BUILD.bazel').read_text()
    targets = {}
    for block in build.split('\n)\n'):
        match = re.search(r'\bname\s*=\s*"([^"]+)"', block)
        if match:
            targets[match[1]] = block
    pending_targets = ['wlan_drv_gen4m_6789']
    seen_targets = set()
    search = []
    while pending_targets:
        name = pending_targets.pop()
        if name in seen_targets or name not in targets:
            continue
        seen_targets.add(name)
        block = targets[name]
        for include_block in re.findall(r'\b(?:includes|linux_includes)\s*=\s*\[([^]]*)\]', block):
            search.extend(root / 'mindone/modules' / value
                          for value in re.findall(r'"([^"]+)"', include_block))
        for deps in re.findall(r'\bdeps\s*=\s*\[([^]]*)\]', block):
            pending_targets.extend(re.findall(r'":([^"]+)"', deps))
    search += [root / 'include', root / 'arch/arm64/include',
               root / 'include/uapi', root / 'arch/arm64/include/uapi',
               root / 'include/generated', root / 'arch/arm64/include/generated',
               root / 'include/generated/uapi', root / 'arch/arm64/include/generated/uapi']
    excluded = {driver / 'include' / OLD_NAME, driver / 'include' / NEW_NAME}
    pending = sorted(p for p in driver.rglob('*') if p.suffix in ('.c', '.h'))
    visited = set()
    references = set()
    locations = {}
    unresolved = set()
    while pending:
        path = pending.pop()
        if path in visited or path in excluded:
            continue
        visited.add(path)
        source = clean(path.read_text(errors='replace'))
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', source)
        for name in words(code):
            references.add(name)
            locations.setdefault(name, path.relative_to(root).as_posix())
        for delimiter, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]', source, re.M):
            if name in (OLD_NAME, NEW_NAME):
                continue
            candidates = ([path.parent / name] if delimiter == '"' else [])
            candidates += [directory / name for directory in search]
            found = next((p.resolve() for p in candidates if p.is_file()), None)
            if found is None:
                unresolved.add((path.relative_to(root).as_posix(), name))
            elif found not in visited:
                pending.append(found)
    return references, locations, visited, sorted(unresolved)


def selected(units, references):
    indices = {i for i, unit in enumerate(units) if unit['names'] & references}
    while True:
        needed = words('\n'.join(units[i]['text'] for i in indices))
        expanded = indices | {i for i, unit in enumerate(units) if unit['names'] & needed}
        if expanded == indices:
            return indices
        indices = expanded


def macro(source, name):
    lines = clean(source).splitlines()
    for index, line in enumerate(lines):
        if re.match(r'^#\s*define\s+' + re.escape(name) + r'\b', line):
            result = [line]
            while result[-1].endswith('\\'):
                index += 1
                result.append(lines[index])
            return '\n'.join(result)
    raise ValueError('Missing support macro ' + name)


def support(root, driver):
    preamble = '\n'.join(
        'typedef ' + spelling + ' ' + name + ';'
        for spelling, name in [('unsigned char', 'u8'), ('unsigned short', 'u16'),
                               ('unsigned int', 'u32'), ('unsigned long long', 'u64'),
                               ('u8', 'uint8_t'), ('u16', 'uint16_t'), ('u32', 'uint32_t'),
                               ('u64', 'uint64_t'), ('signed char', 'int8_t'),
                               ('short', 'int16_t'), ('int', 'int32_t'),
                               ('long long', 'int64_t'), ('_Bool', 'bool')]) + '\n'
    preamble += '#define BIT(n) (1UL << (n))\n'
    large = clean((driver / 'os/linux/include/gl_typedef.h').read_text())
    preamble += re.search(r'union LARGE_INTEGER\s*\{.*?\n\};', large, re.S)[0] + '\n'
    for name in ('__struct_group_tag', '__struct_group'):
        preamble += macro((root / 'include/uapi/linux/stddef.h').read_text(), name) + '\n'
    preamble += macro((root / 'include/linux/stddef.h').read_text(), 'struct_group') + '\n'
    mac = (driver / 'include/nic/mac.h').read_text()
    for name in ('MAC_ADDR_LEN', 'ELEM_MAX_LEN_SSID', 'IPV4_ADDR_LEN', 'IPV6_ADDR_LEN'):
        preamble += macro(mac, name) + '\n'
    preamble += macro((driver / 'include/config.h').read_text(), 'CFG_SCAN_OOB_MAX_NUM') + '\n'
    return preamble


def invoke(clang, path, *flags):
    result = subprocess.run([clang, '--target=aarch64-linux-gnu', '-ffreestanding',
                             '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                             '-fsyntax-only', *flags, str(path)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr or result.stdout)
    return result.stdout


def descendants(node):
    yield node
    for child in node.get('inner', []):
        yield from descendants(child)


def inventory(ast, names):
    result = {}
    for node in descendants(ast):
        name = node.get('name')
        if name not in names:
            continue
        kind = node['kind']
        if kind == 'RecordDecl' and node.get('completeDefinition'):
            result[name] = node
        elif kind in ('TypedefDecl', 'EnumDecl'):
            result[name] = node
    return result


def expressions(nodes, macros):
    result = {}
    bitfields = []
    shape = {}
    for name, node in sorted(nodes.items()):
        kind = node['kind']
        spelling = (node['tagUsed'] + ' ' if kind == 'RecordDecl'
                    else 'enum ' if kind == 'EnumDecl' else '') + name
        result['sizeof ' + spelling] = 'sizeof(' + spelling + ')'
        result['alignof ' + spelling] = '_Alignof(' + spelling + ')'
        if kind == 'RecordDecl':
            fields = []
            for field in node.get('inner', []):
                member = field.get('name')
                if field['kind'] not in ('FieldDecl', 'IndirectFieldDecl') or not member:
                    continue
                fields.append((member, field.get('type', {}).get('qualType'),
                               bool(field.get('isBitfield'))))
                if field.get('isBitfield'):
                    bitfields.append((spelling, member))
                else:
                    result['offsetof ' + spelling + '.' + member] = (
                        '__builtin_offsetof(' + spelling + ', ' + member + ')')
            shape[name] = fields
        elif kind == 'EnumDecl':
            for child in node.get('inner', []):
                if child['kind'] == 'EnumConstantDecl':
                    result['enum ' + child['name']] = child['name']
        else:
            shape[name] = node['type']['qualType']
    for name in sorted(macros):
        result['macro ' + name] = name
    return result, bitfields, shape


def constant_values(ast):
    result = {}
    for node in descendants(ast):
        if node['kind'] == 'EnumConstantDecl' and node.get('name', '').startswith('wsys_metric_'):
            values = [child['value'] for child in descendants(node)
                      if child['kind'] == 'ConstantExpr' and 'value' in child]
            if len(values) != 1:
                raise ValueError('Ambiguous metric: ' + node['name'])
            result[node['name']] = int(values[0])
    return result


def bit_layouts(dump, bitfields):
    result = {}
    for spelling, member in bitfields:
        blocks = re.findall(r'\*\*\* Dumping AST Record Layout\n(.*?)(?=\n\n|\Z)', dump, re.S)
        block = next((b for b in blocks if re.search(r'\| ' + re.escape(spelling) + r'\s*\n', b)), None)
        if block is None:
            raise ValueError('Missing record layout: ' + spelling)
        match = re.search(r'^\s*(\d+):(\d+)-(\d+)\s*\|\s+[^\n]*\b' + re.escape(member) + r'\s*$', block, re.M)
        if match is None:
            raise ValueError('Missing bit field layout: ' + spelling + '.' + member)
        byte, first, last = map(int, match.groups())
        result['bitfield ' + spelling + '.' + member] = (byte * 8 + first, last - first + 1)
    return result


def check_profile(clang, directory, preamble, old, new, new_units, features, profile):
    prefix = preamble + ''.join('#define ' + name + ' ' + str(value) + '\n'
                                for name, value in zip(features, profile))
    names = set().union(*(unit['names'] for unit in new_units))
    rows = []
    baseline = None
    baseline_shape = None
    for label, source in [('old', old), ('new', new)]:
        path = directory / (label + '.c')
        body = prefix + re.sub(r'^\s*#\s*include[^\n]*', '', clean(source), flags=re.M) + '\n'
        path.write_text(body)
        ast = json.loads(invoke(clang, path, '-Xclang', '-ast-dump=json'))
        nodes = inventory(ast, names)
        active = set()
        for unit in new_units:
            if unit['kind'] == 'macro':
                guard = '\n'.join(unit['conditions'])
                if not guard or all(profile[features.index(name)] for name in words(guard) if name in features):
                    active.add(unit['name'])
        metrics, bitfields, shape = expressions(nodes, active)
        if baseline_shape is not None and shape != baseline_shape:
            raise ValueError('Field or typedef mismatch in profile ' + str(profile))
        if baseline_shape is None:
            baseline_shape = shape
        keys = list(metrics)
        constants = 'enum {\n' + ',\n'.join(
            'wsys_metric_' + str(i) + ' = (' + value + ')'
            for i, value in enumerate(metrics.values())) + '\n};\n'
        if baseline is not None:
            if keys != list(baseline):
                raise ValueError('Metric inventory mismatch')
            constants += '\n'.join(
                '_Static_assert(wsys_metric_' + str(i) + ' == ' + str(baseline[key]) + ', "' + key + '");'
                for i, key in enumerate(keys)) + '\n'
        path.write_text(body + constants)
        values = constant_values(json.loads(invoke(clang, path, '-Xclang', '-ast-dump=json')))
        row = {key: values['wsys_metric_' + str(i)] for i, key in enumerate(keys)}
        dump = invoke(clang, path, '-Xclang', '-fdump-record-layouts-complete')
        row.update(bit_layouts(dump, bitfields))
        if baseline is None:
            baseline = {key: row[key] for key in keys}
        rows.append(row)
    if rows[0] != rows[1]:
        mismatches = {key: (rows[0].get(key), rows[1].get(key))
                      for key in rows[0].keys() | rows[1].keys()
                      if rows[0].get(key) != rows[1].get(key)}
        raise ValueError('ABI mismatch: ' + repr(mismatches))
    return rows[0]


def main():
    parser = argparse.ArgumentParser(description='Compare the WLAN firmware ABI kept in mindone_wsys_cmd.h with the '
                                     'MediaTek header it replaced, read from --reference, using host Clang')
    parser.add_argument('--reference', default='2a3bafed896c198a8c4adaecb31c9811285f388c',
                        help='Last commit that still has wsys_cmd_handler_fw.h')
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    driver = root / 'mindone/modules/wlan_drv_gen4m_6789'
    relative = (driver / 'include' / OLD_NAME).relative_to(root).as_posix()
    old = subprocess.run(['git', '-C', str(root), 'show', args.reference + ':' + relative],
                         env={**os.environ, 'GIT_NO_LAZY_FETCH': '1'},
                         capture_output=True, text=True, check=True, timeout=60).stdout
    new = (driver / 'include' / NEW_NAME).read_text()
    old_units, new_units = declarations(old), declarations(new)
    old_names = set().union(*(unit['names'] for unit in old_units))
    new_names = set().union(*(unit['names'] for unit in new_units))
    references, locations, visited, unresolved = include_tree(root, driver)
    missing = sorted((old_names & references) - new_names)
    print('Missing referenced identifiers: ' + json.dumps(missing), flush=True)
    for name in missing:
        print(name + ': ' + locations[name], flush=True)
    if missing:
        raise ValueError('Referenced definitions were removed')
    wanted = selected(old_units, references)
    expected = {old_units[i]['name'] for i in wanted}
    actual = {unit['name'] for unit in new_units}
    if actual != expected:
        raise ValueError('Nonminimal definition set: extra=' + repr(sorted(actual - expected))
                         + ' missing=' + repr(sorted(expected - actual)))
    old_by_name = {unit['name']: unit for unit in old_units}
    for unit in new_units:
        prior = old_by_name[unit['name']]
        if (TOKEN.findall(unit['text']), unit['conditions']) != (TOKEN.findall(prior['text']), prior['conditions']):
            raise ValueError('Declaration tokens or conditions changed: ' + unit['name'])
    features = sorted(set(re.findall(r'\bCFG_SUPPORT_\w+', clean(old))))
    profiles = {tuple([0] * len(features)), tuple([1] * len(features))}
    for index in range(len(features)):
        for default in (0, 1):
            profile = [default] * len(features)
            profile[index] = 1 - default
            profiles.add(tuple(profile))
    if 'CFG_SUPPORT_802_11AX' in features and 'CFG_SUPPORT_802_11BE' in features:
        for ax, be in itertools.product((0, 1), repeat=2):
            profile = [1] * len(features)
            profile[features.index('CFG_SUPPORT_802_11AX')] = ax
            profile[features.index('CFG_SUPPORT_802_11BE')] = be
            profiles.add(tuple(profile))
    metrics = 0
    with tempfile.TemporaryDirectory(prefix='mindone-wsys-') as temporary:
        for profile in sorted(profiles):
            row = check_profile(args.clang, Path(temporary), support(root, driver),
                                old, new, new_units, features, profile)
            metrics += len(row)
            if args.verbose:
                print(json.dumps({'features': dict(zip(features, profile)), 'metrics': row}, sort_keys=True))
    external = len([path for path in visited if not path.is_relative_to(driver)])
    print('Definitions kept: ' + str(len(new_units)) + '/' + str(len(old_units))
          + ' top-level; identifiers: ' + str(len(new_names)) + '/' + str(len(old_names)))
    print('Reference scan: ' + str(len(visited)) + ' files (' + str(external)
          + ' included external headers); unresolved literal includes: ' + str(len(unresolved)))
    if args.verbose:
        for path, name in unresolved:
            print('Unresolved include: ' + path + ': ' + name)
    print('PASS wsys-header-check: ' + str(len(profiles)) + ' aarch64 feature profiles, '
          + str(metrics) + ' ABI comparisons; retained declaration tokens identical; missing references=0')


if __name__ == '__main__':
    main()
