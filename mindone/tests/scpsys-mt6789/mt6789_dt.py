# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import re
from pathlib import Path


class Node:
    def __init__(self, name, parent):
        self.name = name
        self.parent = parent
        self.labels = []
        self.props = {}
        self.children = []

    def child(self, name):
        for node in self.children:
            if node.name == name:
                return node
        node = Node(name, self)
        self.children.append(node)
        return node

    def walk(self):
        yield self
        for node in self.children:
            yield from node.walk()

    def path(self):
        return '/' if not self.parent else (self.parent.path().rstrip('/') + '/' + self.name)


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def tokens(text):
    pattern = re.compile(r'"(?:\\.|[^"\\])*"|<[^>]*>|\[[^\]]*\]|[{};=]|[^\s{};="<\[]+')
    return pattern.findall(text)


def parse_value(raw):
    values = []
    for item in raw:
        if item == ',':
            continue
        if item.startswith('"'):
            values.append(('str', item[1:-1]))
        elif item.startswith('<'):
            values.append(('cells', item[1:-1].split()))
        elif item.startswith('&'):
            values.append(('ref', item[1:]))
        else:
            values.append(('raw', item))
    return values


class Tree:
    def __init__(self):
        self.root = Node('/', None)
        self.labels = {}

    def load(self, path, seen=None):
        seen = seen if seen is not None else set()
        path = Path(path)
        if path in seen:
            return
        seen.add(path)
        text = strip_comments(path.read_text())
        pending = []
        for line in text.split('\n'):
            include = re.match(r'\s*#include\s+"([^"]+)"', line)
            if include:
                if pending:
                    self.parse(tokens('\n'.join(pending)))
                    pending = []
                self.load(path.parent / include.group(1), seen)
                continue
            if re.match(r'\s*#', line):
                continue
            pending.append(line)
        if pending:
            self.parse(tokens('\n'.join(pending)))

    def parse(self, toks):
        stack = []
        i = 0
        while i < len(toks):
            tok = toks[i]
            if tok in ('/dts-v1/', '/plugin/') and i + 1 < len(toks) and toks[i + 1] == ';':
                i += 2
                continue
            if tok == '}':
                stack.pop()
                i += 2 if i + 1 < len(toks) and toks[i + 1] == ';' else 1
                continue
            labels = []
            while tok.endswith(':') and not tok.startswith('&'):
                labels.append(tok[:-1])
                i += 1
                tok = toks[i]
            if i + 1 < len(toks) and toks[i + 1] == '{':
                if tok.startswith('&'):
                    node = self.labels[tok[1:]]
                elif tok == '/' and not stack:
                    node = self.root
                else:
                    node = stack[-1].child(tok)
                for label in labels:
                    node.labels.append(label)
                    self.labels[label] = node
                stack.append(node)
                i += 2
                continue
            if tok == '/delete-node/':
                victim = toks[i + 1]
                parent = stack[-1]
                parent.children = [n for n in parent.children if n.name != victim]
                i += 3
                continue
            if tok == '/delete-property/':
                stack[-1].props.pop(toks[i + 1], None)
                i += 3
                continue
            name = tok
            if toks[i + 1] == ';':
                stack[-1].props[name] = []
                i += 2
                continue
            if toks[i + 1] != '=':
                raise ValueError('unexpected token %r after %r' % (toks[i + 1], name))
            j = i + 2
            raw = []
            while toks[j] != ';':
                raw.append(toks[j])
                j += 1
            stack[-1].props[name] = parse_value(raw)
            i = j + 1
        if stack:
            raise ValueError('unbalanced braces')


def load(dts_dir):
    tree = Tree()
    tree.load(Path(dts_dir) / 'mindone.dts')
    return tree


def macros(*headers):
    values = {}
    for header in headers:
        for match in re.finditer(r'^#define[ \t]+(\w+)[ \t]+(\S+)', Path(header).read_text(), re.M):
            values[match.group(1)] = match.group(2)
    return values


def cells(node, prop):
    out = []
    for kind, value in node.props.get(prop, []):
        if kind == 'cells':
            out.extend(value)
    return out


def strings(node, prop):
    return [value for kind, value in node.props.get(prop, []) if kind == 'str']


def phandle_groups(node, prop):
    groups = []
    for item in cells(node, prop):
        if item.startswith('&'):
            groups.append([item[1:]])
        elif groups:
            groups[-1].append(item)
        else:
            raise ValueError('%s of %s does not start with a phandle' % (prop, node.path()))
    return groups


def device_name(node):
    reg = cells(node, 'reg')
    if len(reg) >= 2 and node.parent is not None:
        return '%x.%s' % ((int(reg[0], 0) << 32) | int(reg[1], 0), node.name.split('@')[0])
    if '@' in node.name:
        base, unit = node.name.split('@', 1)
        return '%x.%s' % (int(unit, 16), base)
    return '%s:%s' % (node.parent.name.split('@')[0], node.name)
