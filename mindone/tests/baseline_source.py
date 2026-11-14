# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import hashlib


def strip_comments(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            end = text.find('\n', i)
            i = n if end < 0 else end
        elif c == '/' and text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('Unterminated block comment')
            out.append('\n' * text.count('\n', i, end) or ' ')
            i = end + 2
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def require(label, text, expected):
    actual = digest(text)
    if actual != expected:
        raise ValueError(f'{label} differs from its baseline: sha256 {actual}, pinned {expected}')
