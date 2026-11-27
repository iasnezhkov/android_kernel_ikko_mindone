# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import re
import sys

CHECK = 'vdec-stop-streaming'
VDEC = 'mindone/modules/mtk_vcodec_dec_v2/mtk-vcodec-dec.c'


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def main():
    parser = argparse.ArgumentParser(description='Verify mtk-vcodec-dec does not orphan CAPTURE buffers on res_chg or leak dmabuf/used state on stop_streaming')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    src = (tree / VDEC).read_text()

    m_res = re.search(r'if \(res_chg\)\s*\{([^}]*)\}', src)
    if not m_res:
        fail('res_chg block not found')
    if 'v4l2_m2m_dst_buf_remove' in m_res.group(1):
        fail('res_chg still pops CAPTURE buffers without returning them to vb2')

    if 'else if (free_frame_buffer->status == FB_ST_FREE)' in src:
        fail('get_free_buffer still checks exact == FB_ST_FREE instead of bitmask')
    if '(free_frame_buffer->status & FB_ST_FREE) &&\n\t\t\t\t!(free_frame_buffer->status & FB_ST_DISPLAY)' not in src:
        fail('get_free_buffer missing bitmask check for FB_ST_FREE without FB_ST_DISPLAY')

    m_stop = re.search(r'still owned by driver after reset, forcing it back(.*?)v4l2_m2m_buf_done\(&dstbuf->vb, VB2_BUF_STATE_ERROR\);', src, re.S)
    if not m_stop:
        fail('stop_streaming fallback block not found')
    body = m_stop.group(1)
    for required in (
        'if (dstbuf->used)',
        'fput(dstbuf->frame_buffer.fb_base[j].dmabuf->file)',
        'dstbuf->used = false;',
        'vb2_set_plane_payload(&dst_vb2_v4l2->vb2_buf, j, 0);',
        'dstbuf->queued_in_vb2 = false;',
        'dstbuf->queued_in_v4l2 = false;',
        'dstbuf->ready_to_display = false;',
    ):
        if required not in body:
            fail(f'stop_streaming fallback missing {required!r}')

    print(f'{CHECK}: PASS (res_chg orphan loop removed, get_free_buffer bitmask, stop_streaming fput/payload/state cleanup)')


if __name__ == '__main__':
    sys.exit(main())
