# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import posixpath
import re
import subprocess
from urllib.parse import unquote, urlsplit


SCOPE = ('mindone', 'arch/arm64/boot/dts/mediatek', 'arch/arm64/configs',
         'docs', 'README.md', 'CONTRIBUTING.md', 'build.sh')
DOC = re.compile(r'docs/[A-Za-z0-9_-]+\.md')
INLINE = re.compile(r'!?\[[^\]\n]*\]\(\s*(?:<(?P<angle>[^>\n]+)>|(?P<plain>[^\s)]+))')
REFERENCE = re.compile(r'^[ \t]{0,3}\[[^\]\n]+\]:\s*(?:<(?P<angle>[^>\n]+)>|(?P<plain>[^\s]+))', re.M)
URL = re.compile(r'https?://[^\s<>"\)]+')


def local_target(relative, target):
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc:
        return None
    path = unquote(parsed.path)
    if path.startswith('/'):
        return path.lstrip('/')
    path = posixpath.normpath(posixpath.join(posixpath.dirname(relative), path))
    if path == '..' or path.startswith('../'):
        return None
    return path


def missing_references(root, relative, text):
    targets = []
    if relative.endswith('.md'):
        for pattern in (INLINE, REFERENCE):
            for match in pattern.finditer(text):
                group = 'angle' if match.group('angle') is not None else 'plain'
                targets.append((*match.span(group), local_target(relative, match.group(group))))
    urls = [match.span() for match in URL.finditer(text)]
    missing = set()
    for match in DOC.finditer(text):
        if any(start <= match.start() and match.end() <= end for start, end in urls):
            continue
        target = match.group()
        for start, end, resolved in targets:
            if start <= match.start() and match.end() <= end:
                target = resolved
                break
        if target is not None and not (root / target).is_file():
            missing.add((text.count('\n', 0, match.start()) + 1, target))
    return sorted(missing)


def main():
    parser = argparse.ArgumentParser(description='Check local documentation references while preserving external repository links')
    parser.add_argument('--root', type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.root.resolve()
    result = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--', *SCOPE],
                            capture_output=True, check=True, timeout=30)
    failures = 0
    checked = 0
    for entry in result.stdout.split(b'\0'):
        if not entry:
            continue
        relative = entry.decode()
        path = root / relative
        if not path.is_file():
            continue
        raw = path.read_bytes()
        if b'\0' in raw:
            continue
        text = raw.decode('utf-8', errors='replace')
        checked += 1
        for line, target in missing_references(root, relative, text):
            print(f'{relative}:{line}: missing local documentation: {target}')
            failures += 1
    print(f'Checked {checked} tracked text files; {failures} missing local documentation references')
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
