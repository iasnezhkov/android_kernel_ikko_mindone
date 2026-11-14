# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CHECK = 'mtk-jpeg'
DRIVER = 'mindone/modules/mtk_jpeg'
CORE = 'mtk_jpeg_core.c'
ENC = 'mtk_jpeg_enc_hw.c'
SOURCES = (CORE, ENC, 'mtk_jpeg_core.h', 'mtk_jpeg_enc_hw.h')
BUILD = 'mindone/modules/BUILD.bazel'
DTSI = 'mindone/dts/mt6789-media.dtsi'
LOAD_LISTS = ('mindone/vendor_dlkm.modules.load', 'mindone/modules/modules.load')
HARNESS = Path(__file__).resolve().parent / CHECK
CASES = ['abi-querycap', 'abi-controls', 'abi-dst-offset', 'quality-table', 'pm-one-job', 'pm-two-jobs',
         'pm-timeout', 'pm-get-fails', 'release-in-flight', 'system-suspend', 'remove']
FUNCTIONS = ('ctrl_to_ctx', 'mtk_jpeg_fh_to_ctx', 'mtk_jpeg_querycap', 'vidioc_jpeg_enc_s_ctrl',
             'mtk_jpeg_enc_ctrls_setup', 'mtk_jpeg_update_bw_request', 'mtk_jpeg_end_bw_request',
             'mtk_jpeg_dvfs_begin', 'mtk_jpeg_dvfs_end', 'mtk_jpeg_qbuf', 'mtk_jpeg_enc_device_run',
             'mtk_jpeg_clk_on', 'mtk_jpeg_clk_off', 'mtk_jpeg_enc_done', 'mtk_jpeg_enc_irq', 'mtk_jpeg_release',
             'mtk_jpeg_job_timeout_work', 'mtk_jpeg_clk_release', 'mtk_jpeg_remove', 'mtk_jpeg_pm_suspend',
             'mtk_jpeg_pm_resume', 'mtk_jpeg_suspend', 'mtk_jpeg_resume')
DEFINITIONS = (r'^static struct mtk_jpeg_fmt mtk_jpeg_enc_formats\[\] = \{\n.*?^\};\n',
               r'^static const struct v4l2_ctrl_ops mtk_jpeg_enc_ctrl_ops = \{\n.*?^\};\n',
               r'^static int jpgenc_probe_time;\n')
MUTANTS = (
    ('enc-done-keeps-reference', CORE,
     (('\tv4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);\n\tpm_runtime_put(jpeg->dev);\n\treturn IRQ_HANDLED;',
       '\tv4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);\n\treturn IRQ_HANDLED;'),),
     'pm-two-jobs', 'm_dev.usage == 0'),
    ('release-drops-reference', CORE,
     (('\t\tmtk_jpeg_end_bw_request(ctx);\n\t}\n\tv4l2_ctrl_handler_free',
       '\t\tmtk_jpeg_end_bw_request(ctx);\n\t\tpm_runtime_put(ctx->jpeg->dev);\n\t}\n\tv4l2_ctrl_handler_free'),),
     'pm-one-job', 'runtime-PM usage count underflow'),
    ('get-sync-keeps-reference', CORE,
     (('ret = pm_runtime_resume_and_get(jpeg->dev);', 'ret = pm_runtime_get_sync(jpeg->dev);'),),
     'pm-get-fails', 'm_dev.usage == 0'),
    ('votes-dropped-before-wait', CORE,
     (('\tmutex_lock(&jpeg->lock);\n\tv4l2_m2m_ctx_release(ctx->fh.m2m_ctx);\n\tif (ctx->state == MTK_JPEG_RUNNING) {\n'
       '\t\tmtk_jpeg_dvfs_end(ctx);\n\t\tmtk_jpeg_end_bw_request(ctx);\n\t}\n',
       '\tif (ctx->state == MTK_JPEG_RUNNING) {\n\t\tmtk_jpeg_dvfs_end(ctx);\n\t\tmtk_jpeg_end_bw_request(ctx);\n\t}\n'
       '\tmutex_lock(&jpeg->lock);\n\tv4l2_m2m_ctx_release(ctx->fh.m2m_ctx);\n'),),
     'release-in-flight', 'm.volt_lowered_running == 0'),
    ('timeout-keeps-reference', CORE,
     (('\tpm_runtime_put(jpeg->dev);\n\n\tv4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);',
       '\n\tv4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);'),),
     'pm-timeout', 'm_dev.usage == 0 && !m_dev.active'),
    ('suspend-under-running-job', CORE,
     (('\tv4l2_m2m_suspend(jpeg->m2m_dev);\n', '\t(void)jpeg;\n'),),
     'system-suspend', 'm.clock_off_running == 0'),
    ('capture-offset-dropped', CORE,
     (('ctx->dst_offset = buf->m.planes[0].data_offset;', 'ctx->dst_offset = 0;'),),
     'abi-dst-offset', 'dst_addr0 == (u32)(dma + 0x2a40)'),
    ('exif-at-fixed-64k', ENC,
     (('\tdma_addr_offset = 0;\n', '\tdma_addr_offset = ctx->enable_exif ? MTK_JPEG_MAX_EXIF_SIZE : 0;\n'),),
     'abi-dst-offset', 'm.regs[JPEG_ENC_OFFSET_ADDR / 4] == 0'),
    ('no-encoder-count', CORE,
     (('\tcap->reserved[0] = jpgenc_probe_time;\n', ''),),
     'abi-querycap', 'cap.reserved[0] == 1'),
    ('quality-floor-48', CORE,
     (('V4L2_CID_JPEG_COMPRESSION_QUALITY, 0,', 'V4L2_CID_JPEG_COMPRESSION_QUALITY, 48,'),),
     'abi-controls', 'quality->minimum == 0'),
    ('quality-rounds-up', ENC,
     (('if (ctx->enc_quality >= mtk_jpeg_enc_quality[i].quality_param) {',
       'if (ctx->enc_quality <= mtk_jpeg_enc_quality[i].quality_param) {'),),
     'quality-table', 'quality_reg == table[i].reg'),
    ('remove-releases-vdev-twice', CORE,
     (('\tvideo_unregister_device(jpeg->vdev);\n\tv4l2_m2m_release',
       '\tvideo_unregister_device(jpeg->vdev);\n\tvideo_device_release(jpeg->vdev);\n\tv4l2_m2m_release'),),
     'remove', 'video device released twice'),
)
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
         '-Wno-missing-field-initializers', '-Wno-unused-function', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all']


def skip(source, index):
    if source.startswith('//', index):
        return source.index('\n', index)
    if source.startswith('/*', index):
        return source.index('*/', index + 2) + 2
    if source[index] in '"\'':
        quote, index = source[index], index + 1
        while source[index] != quote:
            index += 2 if source[index] == '\\' else 1
        return index + 1
    return None


def closing(source, index):
    depth = 0
    while True:
        after = skip(source, index)
        if after is not None:
            index = after
            continue
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if not depth:
                return index + 1
        index += 1


def function(source, name):
    pattern = r'^(?:static )?\w[^;{}=()]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {name} has {len(found)} definitions in {CORE}')
    start = found[0].start()
    return start, source[start:closing(source, source.index('{', start))] + '\n'


def definition(source, pattern):
    found = list(re.finditer(pattern, source, re.M | re.S))
    if len(found) != 1:
        raise SystemExit(f'{CHECK}: {pattern!r} matches {len(found)} times in {CORE}')
    return found[0].start(), found[0].group(0)


def replace_once(text, before, after, label):
    if text.count(before) != 1:
        raise SystemExit(f'{CHECK}: {label}: expected one {before!r}')
    return text.replace(before, after)


def bindings(tree, sources):
    build = (tree / BUILD).read_text()
    block = re.search(r'^ddk_module\(\n    name = "mtk_jpeg",\n(.*?)\n\)\n', build, re.M | re.S)
    if not block:
        raise SystemExit(f'{CHECK}: no mtk_jpeg ddk_module in {BUILD}')
    srcs = re.findall(r'"(mtk_jpeg/[^"]+\.c)"', block.group(1))
    if sorted(srcs) != ['mtk_jpeg/mtk_jpeg_core.c', 'mtk_jpeg/mtk_jpeg_dec_hw.c', 'mtk_jpeg/mtk_jpeg_dec_parse.c',
                        'mtk_jpeg/mtk_jpeg_enc_hw.c'] or 'out = "mtk_jpeg.ko"' not in block.group(1):
        raise SystemExit(f'{CHECK}: the mtk_jpeg target does not build the four sources into mtk_jpeg.ko')
    for dep in ('":mtk_smi"', '":mtk_icc_core"'):
        if dep not in block.group(1):
            raise SystemExit(f'{CHECK}: the mtk_jpeg target lacks the dependency {dep}')
    for relative in LOAD_LISTS:
        if 'mtk_jpeg.ko' not in (tree / relative).read_text().split():
            raise SystemExit(f'{CHECK}: {relative} does not load mtk_jpeg.ko')
    nodes = re.findall(r'^\t\tjpgenc@17030000 \{\n(.*?)^\t\t\};', (tree / DTSI).read_text(), re.M | re.S)
    if len(nodes) != 1:
        raise SystemExit(f'{CHECK}: {DTSI} needs one jpgenc@17030000 node')
    for prop in ('compatible = "mediatek,mtk-jpgenc-32bit";', 'clock-names = "jpgenc";',
                 'mediatek,larb = <&smi_larb7>;', 'operating-points-v2 = <&opp_table_venc>;',
                 'dvfsrc-vcore-supply = <&dvfsrc_vcore>;',
                 'interconnect-names = "path_jpegenc_y_rdma", "path_jpegenc_c_rmda", "path_jpegenc_q_table", '
                 '"path_jpegenc_bsdma";'):
        if prop not in nodes[0]:
            raise SystemExit(f'{CHECK}: jpgenc@17030000 lacks {prop}')
    core = sources[CORE]
    for name in ('path_jpegenc_y_rdma', 'path_jpegenc_c_rmda', 'path_jpegenc_q_table', 'path_jpegenc_bsdma'):
        if f'of_mtk_icc_get(jpeg->dev, "{name}")' not in core:
            raise SystemExit(f'{CHECK}: the driver does not request the interconnect path {name}')
    match = re.search(r'\.compatible = "mediatek,mtk-jpgenc-32bit",\n\t\t\.data = &(\w+),', core)
    variant = match and re.search(r'^static const struct mtk_jpeg_variant ' + match.group(1) + r' = \{\n(.*?)^\};',
                                  core, re.M | re.S)
    if not variant:
        raise SystemExit(f'{CHECK}: "mediatek,mtk-jpgenc-32bit" does not match a variant of {CORE}')
    for field in ('.clks = mtk_jpeg_clocks,', '.irq_handler = mtk_jpeg_enc_irq,', '.hw_reset = mtk_jpeg_enc_reset,',
                  '.dev_name = "mtk-jpeg-enc",', '.ioctl_ops = &mtk_jpeg_enc_ioctl_ops,',
                  '.support_34bit = false,'):
        if field not in variant.group(1):
            raise SystemExit(f'{CHECK}: the -32bit variant lacks {field}')
    if not re.search(r'^static struct clk_bulk_data mtk_jpeg_clocks\[\] = \{\n\t\{ \.id = "jpgenc" \},\n\};', core,
                     re.M):
        raise SystemExit(f'{CHECK}: mtk_jpeg_clocks is not the single "jpgenc" clock')
    ioctls = re.search(r'^static const struct v4l2_ioctl_ops mtk_jpeg_enc_ioctl_ops = \{\n(.*?)^\};', core,
                       re.M | re.S)
    for op in ('.vidioc_querycap                = mtk_jpeg_querycap,', '.vidioc_qbuf                    = mtk_jpeg_qbuf,'):
        if not ioctls or op not in ioctls.group(1):
            raise SystemExit(f'{CHECK}: mtk_jpeg_enc_ioctl_ops lacks {op}')
    if not re.search(r'if \(strcmp\(\(const char \*\)jpeg->variant->dev_name, "mtk-jpeg-enc"\) == 0\)\n'
                     r'\t\tjpgenc_probe_time\+\+;\n\treturn 0;\n', core):
        raise SystemExit(f'{CHECK}: probe does not count the encoders it registers in jpgenc_probe_time')
    ops = re.search(r'^static const struct dev_pm_ops mtk_jpeg_pm_ops = \{\n(.*?)^\};', core, re.M | re.S)
    for entry in ('SET_SYSTEM_SLEEP_PM_OPS(mtk_jpeg_suspend, mtk_jpeg_resume)',
                  'SET_RUNTIME_PM_OPS(mtk_jpeg_pm_suspend, mtk_jpeg_pm_resume, NULL)'):
        if not ops or entry not in ops.group(1):
            raise SystemExit(f'{CHECK}: mtk_jpeg_pm_ops lacks {entry}')


def shims(directory, texts):
    for text in texts:
        for header in re.findall(r'^#include <([^>]+)>', text, re.M):
            path = directory / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "stubs.h"\n')


def unit(directory, sources):
    source_dir = directory / 'src'
    source_dir.mkdir(parents=True, exist_ok=True)
    for name in (ENC, 'mtk_jpeg_core.h', 'mtk_jpeg_enc_hw.h'):
        (source_dir / name).write_text(sources[name])
    (source_dir / 'mtk-interconnect-provider.h').write_text('')
    shims(directory / 'sys', [sources[name] for name in (ENC, 'mtk_jpeg_core.h', 'mtk_jpeg_enc_hw.h')])
    core = sources[CORE]
    parts = [definition(core, pattern) for pattern in DEFINITIONS]
    parts += [function(core, name) for name in FUNCTIONS]
    text = ['#include "stubs.h"', '#include "mtk_jpeg_core.h"', '#include "mtk_jpeg_enc_hw.h"',
            f'#include "{ENC}"']
    text += [body for _, body in sorted(parts)]
    text += ['#include "cases.c"']
    path = source_dir / 'unit.c'
    path.write_text('\n'.join(text) + '\n')
    return path, ['-I', str(directory / 'sys'), '-I', str(HARNESS), '-I', str(source_dir)]


def build(path, includes, binary):
    subprocess.run(FLAGS + includes + [str(path), '-o', str(binary)], check=True, timeout=300)
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=300)


def caught(result, assertion):
    return result.returncode and 'Assertion' in result.stderr and assertion in result.stderr


def main():
    parser = argparse.ArgumentParser(description='Compile the JPEG encoder paths of mtk_jpeg against a model of '
                                     'runtime PM, the m2m job queue, vb2, the jpgenc registers, vcore DVFS and the '
                                     'mmqos paths with ASan/UBSan: every encode job must hold exactly one runtime-PM '
                                     'reference until it ends (IRQ, timeout or failed resume), close must wait for a '
                                     'running job before it drops the DVFS and bandwidth votes, system suspend must '
                                     'finish the job before the engine goes off, remove must release the video '
                                     'device once, and the behaviour libJpgEncPipe.so relies on must hold (driver '
                                     'name and encoder count in QUERYCAP, the capture plane data_offset as the '
                                     'bitstream offset, quality 0..100 on the MT6789 BSP table); every mutation must '
                                     'be caught')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    sources = {name: (tree / DRIVER / name).read_text() for name in SOURCES}
    bindings(tree, sources)
    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as temporary:
        directory = Path(temporary)
        path, includes = unit(directory / 'candidate', sources)
        candidate = build(path, includes, directory / 'candidate' / 'unit')
        for case in CASES:
            result = run(candidate, case)
            if result.returncode:
                raise SystemExit(f'{CHECK}: FAIL {case} (exit {result.returncode})\n{result.stdout}{result.stderr}')
            print(result.stdout.rstrip())
        for name, file, edits, case, assertion in MUTANTS:
            mutated = dict(sources)
            for before, after in edits:
                mutated[file] = replace_once(mutated[file], before, after, 'mutant ' + name)
            path, includes = unit(directory / ('mutant-' + name), mutated)
            result = run(build(path, includes, directory / ('mutant-' + name) / 'unit'), case)
            if not caught(result, assertion):
                raise SystemExit(f'{CHECK}: mutant {name} was not caught by {case} at {assertion!r}\n'
                                 f'{result.stdout}{result.stderr}')
            print(f'PASS mutant {name}: {case} fails at {assertion}')
    print(f'{CHECK}: PASS {len(CASES)} cases, {len(MUTANTS)} mutants caught; kernel build and device check pending')


if __name__ == '__main__':
    main()
