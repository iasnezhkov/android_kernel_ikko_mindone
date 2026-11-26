# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = 'btcvsd'
SRC = 'mindone/modules/mtk_btcvsd/mtk_btcvsd.c'
HARNESS = Path(__file__).resolve().parent / CHECK
FLAGS = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-but-set-variable',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def fail(msg):
    raise SystemExit(f'{CHECK}: {msg}')


def extract_unit(src):
    defs_start = src.index('#define BT_CVSD_TX_NREADY')
    defs_end = src.index('struct mtk_btcvsd_snd_hw_info {')
    structs_end = src.index('static const unsigned int btsco_packet_valid_mask')
    clean_start = src.index('static int btcvsd_tx_clean_buffer(struct mtk_btcvsd_snd *bt)')
    clean_end = src.index('\n}\n', clean_start) + 3
    structs = src[defs_end:structs_end]
    structs = structs.replace('struct regmap *infra;\n', '')
    structs = structs.replace('wait_queue_head_t tx_wait;\n', '')
    structs = structs.replace('wait_queue_head_t rx_wait;\n', '')
    structs = structs.replace('struct snd_pcm_substream *substream;\n', '')
    return '\n'.join([
        '#include "stubs.h"',
        src[defs_start:defs_end],
        structs,
        src[clean_start:clean_end],
        '#include "cases.c"',
    ]) + '\n'


def build_and_run(work, name, src):
    cpath = work / (name + '.c')
    cpath.write_text(extract_unit(src))
    binary = work / name
    res = subprocess.run(FLAGS + ['-I', str(HARNESS), str(cpath), '-o', str(binary)],
                         capture_output=True, text=True, timeout=120)
    if res.returncode:
        return res
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description='Verify ACK-based mtk_btcvsd IRQ flags, 0xdeadfeed guard and MT6789 deltas')
    parser.add_argument('tree', type=Path)
    tree = parser.parse_args().tree.resolve()
    src = (tree / SRC).read_text()

    if 'IRQF_TRIGGER_LOW' in src:
        fail('IRQF_TRIGGER_LOW conflicts with DT IRQ_TYPE_LEVEL_HIGH (#1531)')
    if 'IRQF_TRIGGER_NONE, "BTCVSD_ISR_Handle"' not in src:
        fail('devm_request_irq must use IRQF_TRIGGER_NONE')
    if 'disable_write_silence' not in src:
        fail('missing DT disable_write_silence support')
    if 'BTCVSD Bypass Switch' not in src:
        fail('missing BTCVSD Bypass Switch kcontrol')
    if 'sched_clock()' in src:
        fail('mtk_btcvsd must use ktime_get_ns() instead of sched_clock()')

    with tempfile.TemporaryDirectory(prefix=CHECK + '-') as tmp:
        work = Path(tmp)
        res = build_and_run(work, 'candidate', src)
        if res.returncode:
            fail(f'candidate failed:\n{res.stdout}{res.stderr}')
        print(res.stdout.rstrip())

        mutants = (
            ('drop-deadfeed-check',
             '\tconnsys_addr_tx = *bt->bt_reg_pkt_w;\n\tif (connsys_addr_tx == 0xdeadfeed) {\n\t\tdev_warn(bt->dev, "%s(), connsys_addr_tx == 0xdeadfeed\\n",\n\t\t\t __func__);\n\t\tspin_unlock_irqrestore(&bt->tx_lock, flags);',
             '\tconnsys_addr_tx = *bt->bt_reg_pkt_w;\n\tif (0 && connsys_addr_tx == 0xdeadfeed) {\n\t\tdev_warn(bt->dev, "%s(), connsys_addr_tx == 0xdeadfeed\\n",\n\t\t\t __func__);\n\t\tspin_unlock_irqrestore(&bt->tx_lock, flags);'),
            ('drop-bypass-check',
             '\tenum BT_SCO_BAND band = bt->band;\n\n\tif (bt->bypass_bt_access)\n\t\treturn -EIO;',
             '\tenum BT_SCO_BAND band = bt->band;\n\n\tif (0 && bt->bypass_bt_access)\n\t\treturn -EIO;'),
            ('drop-write-tx-flag',
             '\tbt->write_tx = 1;\n\tspin_unlock_irqrestore(&bt->tx_lock, flags);',
             '\t(void)bt;\n\tspin_unlock_irqrestore(&bt->tx_lock, flags);'),
            ('clear-tx-underflow',
             '(BT_CVSD_TX_NREADY | BT_CVSD_RX_READY | BT_CVSD_RX_OVERFLOW |\\',
             '(BT_CVSD_TX_NREADY | BT_CVSD_RX_READY | BT_CVSD_TX_UNDERFLOW | BT_CVSD_RX_OVERFLOW |\\'),
        )
        for name, before, after in mutants:
            if src.count(before) != 1:
                fail(f'mutant {name} count={src.count(before)}')
            mres = build_and_run(work, 'mutant-' + name, src.replace(before, after, 1))
            if mres.returncode == 0:
                fail(f'mutant {name} survived')
            print(f'PASS mutant {name}')
    print(f'{CHECK}: PASS ({len(mutants)} mutants caught)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
