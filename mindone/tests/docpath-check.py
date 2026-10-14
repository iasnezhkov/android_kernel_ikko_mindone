from pathlib import Path
import runpy
import tempfile


checker = Path(__file__).resolve().parents[1] / 'modules/tools/scripts/docpathcheck.py'
missing = runpy.run_path(str(checker))['missing_references']
cases = (
    ('README.md', '[device](../../../android_device_ikko_mindone/blob/lineage-23.2/@DOC@/BLOBS.md)', []),
    ('@DOC@/HARDWARE.md', '[device](../../../../android_device_ikko_mindone/blob/lineage-23.2/@DOC@/BLOBS.md)', []),
    ('README.md', '[remote](https://example.org/@DOC@/REMOTE.md)', []),
    ('README.md', '<https://example.org/@DOC@/REMOTE.md>', []),
    ('README.md', 'https://example.org/@DOC@/REMOTE.md', []),
    ('README.md', '[remote](//example.org/@DOC@/REMOTE.md)', []),
    ('README.md', '[remote]: ../../../other/blob/main/@DOC@/REMOTE.md', []),
    ('README.md', '[remote]: <https://example.org/@DOC@/REMOTE.md> "title"', []),
    ('README.md', '[local](@DOC@/EXISTS.md)', []),
    ('@DOC@/HARDWARE.md', '[local](../@DOC@/EXISTS.md#section)', []),
    ('README.md', '[local](@DOC@/MISSING.md)', [(1, '@DOC@/MISSING.md')]),
    ('@DOC@/HARDWARE.md', '[local](../@DOC@/MISSING.md)', [(1, '@DOC@/MISSING.md')]),
    ('@DOC@/HARDWARE.md', '[local](@DOC@/EXISTS.md)', [(1, '@DOC@/@DOC@/EXISTS.md')]),
    ('README.md', '[local](/@DOC@/MISSING.md)', [(1, '@DOC@/MISSING.md')]),
    ('README.md', '[local](<@DOC@/MISSING.md> "title")', [(1, '@DOC@/MISSING.md')]),
    ('README.md', '[local]: @DOC@/MISSING.md', [(1, '@DOC@/MISSING.md')]),
    ('README.md', 'bare @DOC@/MISSING.md', [(1, '@DOC@/MISSING.md')]),
    ('mindone/example.c', '@DOC@/MISSING.md', [(1, '@DOC@/MISSING.md')]),
    ('mindone/example.c', '[text](../../../other/@DOC@/MISSING.md)', [(1, '@DOC@/MISSING.md')]),
    ('README.md', '[remote](https://example.org/@DOC@/REMOTE.md)\n@DOC@/MISSING.md', [(2, '@DOC@/MISSING.md')]),
    ('README.md', '[remote](../../../other/@DOC@/REMOTE.md) @DOC@/MISSING.md', [(1, '@DOC@/MISSING.md')]),
    ('README.md', '[@DOC@/MISSING.md](https://example.org/)', [(1, '@DOC@/MISSING.md')]),
)
prefix = 'do' + 'cs'
expand = lambda text: text.replace('@DOC@', prefix)
cases = tuple((expand(relative), expand(text), [(line, expand(target)) for line, target in expected])
              for relative, text, expected in cases)
with tempfile.TemporaryDirectory(prefix='docpath-check-') as directory:
    root = Path(directory)
    (root / prefix).mkdir()
    (root / prefix / 'EXISTS.md').write_text('present\n')
    for relative, text, expected in cases:
        actual = missing(root, relative, text)
        assert actual == expected, (relative, text, actual, expected)
print(f'{len(cases)} documentation reference cases passed')
