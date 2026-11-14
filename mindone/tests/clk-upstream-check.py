# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
HARNESS = HERE / 'clk-upstream'
GOLDEN = HARNESS / 'fork.golden'
MODULES = 'mindone/modules'
FORK_CORE = MODULES + '/clk_mediatek'
ACK_CORE = MODULES + '/clk_mtk_ack'
SCPSYS = MODULES + '/mtk_pm_domains_ack/mt6789-pm-domains.h'
ACK_BASE = 'mindone/ack-base'
BUILD = MODULES + '/BUILD.bazel'
ACK_FILES = ['clk-gate.c', 'clk-gate.h', 'clk-mtk.c', 'clk-mtk.h', 'clk-mux.c', 'clk-mux.h', 'clk-pll.c',
             'clk-pll.h', 'reset.c', 'reset.h']
ACK_COMPILED = ['clk-mtk.c', 'clk-gate.c', 'clk-mux.c', 'clk-pll.c']
FORK_COMPILED = ['clk-mtk.c', 'clk-gate.c', 'clk-mux.c', 'clk-pll.c']
DATA = ['clk_mt6789', 'clk_mt6789_audiosys', 'clk_mt6789_bus', 'clk_mt6789_cam', 'clk_mt6789_i2c',
        'clk_mt6789_img', 'clk_mt6789_impen', 'clk_mt6789_mfgcfg', 'clk_mt6789_mmsys', 'clk_mt6789_vcodec']
EXTERNAL = {'clk26m', 'clk13m', 'clk32k', 'ulposc', 'mipi_tx0_pll'}
CFLAGS = ['-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
          '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
          '-Wno-pointer-sign', '-Wno-int-conversion', '-Wno-incompatible-pointer-types-discards-qualifiers',
          '-Wno-gnu-variable-sized-type-not-at-end', '-Wno-constant-conversion', '-Wno-unknown-warning-option',
          '-Wno-tautological-constant-out-of-range-compare', '-fsanitize=address,undefined',
          '-fno-sanitize-recover=all', '-g']
EN_MASK_PLLS = ('mainpll', 'univpll', 'mmpll', 'npupll')
MT6789_PATCHES = [
    ('clk-mux.c', 'mux update on every enable', 'if (mux->reparent && mux->data->upd_shift >= 0) {',
     'if (mux->data->upd_shift >= 0) {'),
    ('clk-pll.c', 'FHCTL rate hook',
     'int mtk_pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate) { '
     'struct mtk_clk_pll *pll = to_mtk_clk_pll(hw); u32 pcw = 0; u32 postdiv; '
     'mtk_pll_calc_values(pll, &pcw, &postdiv, rate, parent_rate); mtk_pll_set_rate_regs(pll, pcw, postdiv);',
     'bool (*mtk_fh_set_rate)(const char *name, unsigned long dds, int postdiv); '
     'EXPORT_SYMBOL_GPL(mtk_fh_set_rate); '
     'int mtk_pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate) { '
     'struct mtk_clk_pll *pll = to_mtk_clk_pll(hw); u32 pcw = 0; u32 postdiv; '
     'mtk_pll_calc_values(pll, &pcw, &postdiv, rate, parent_rate); '
     'if (!mtk_fh_set_rate || !mtk_fh_set_rate(pll->data->name, pcw, postdiv)) '
     'mtk_pll_set_rate_regs(pll, pcw, postdiv);'),
    ('clk-pll.h', 'FHCTL rate hook declaration', 'extern const struct clk_ops mtk_pll_fenc_clr_set_ops;',
     'extern const struct clk_ops mtk_pll_fenc_clr_set_ops; '
     'extern bool (*mtk_fh_set_rate)(const char *name, unsigned long dds, int postdiv);'),
]


def split_trace(line):
    head, _, rest = line.partition(' |')
    trace, _, final = rest.partition(' =>')
    return head, trace.split(), final.split()


def pll_div_enable_order(fork, ack):
    head, ftrace, ffinal = split_trace(fork)
    if not re.match(r'^(%s) (prepare|unprepare) (zero|ones|mix1)$' % '|'.join(EN_MASK_PLLS), head):
        return False
    _, atrace, afinal = split_trace(ack)

    def shape(trace):
        return [t if t.startswith('D') else t[0] + t[1:].split('=')[0] for t in trace]

    return ffinal == afinal and sorted(shape(ftrace)) == sorted(shape(atrace)) and \
        [t for t in ftrace if t.startswith('D')] == [t for t in atrace if t.startswith('D')]


ALLOWED = [
    ('PLL div-enable order', pll_div_enable_order,
     'fork sets en_mask before BIT(pll_en_bit) in prepare and clears it after in unprepare; ACK sets it after '
     'and clears it before; same register (en_reg == reg), same accesses and delays, same final state'),
]
MUTATIONS = [
    ('clk_mt6789_bus', 'infra gate set_ofs', '\t.set_ofs = 0x80,', '\t.set_ofs = 0x84,'),
    ('clk_mt6789_bus', 'infra gate parent', '"ifrao_ufs", "ufs_ck"', '"ifrao_ufs", "axi_ck"'),
    ('clk_mt6789_bus', 'infra gate shift', '"ifrao_ap_dma", "apdma_ck", 31', '"ifrao_ap_dma", "apdma_ck", 30'),
    ('clk_mt6789_bus', 'infra ID swap', 'GATE_IFRAO0(CLK_IFRAO_PMIC_TMR, "ifrao_pmic_tmr"',
     'GATE_IFRAO0(CLK_IFRAO_PMIC_AP, "ifrao_pmic_tmr"'),
    ('clk_mt6789_bus', 'gates lose CLK_OPS_PARENT_ENABLE',
     '&mtk_clk_gate_ops_setclr, CLK_OPS_PARENT_ENABLE)', '&mtk_clk_gate_ops_setclr, 0)', 'all'),
    ('clk_mt6789_vcodec', 'venc gates not inverted', 'MT6789_PWR_GATE_OPS(ven1_setclr_inv_ops, mtk_clk_gate_ops_setclr_inv,',
     'MT6789_PWR_GATE_OPS(ven1_setclr_inv_ops, mtk_clk_gate_ops_setclr,'),
    ('clk_mt6789_vcodec', 'vdec mini_mdp with set/clr', 'MT6789_PWR_GATE_OPS(vde2_no_setclr_inv_ops, mtk_clk_gate_ops_no_setclr_inv,',
     'MT6789_PWR_GATE_OPS(vde2_no_setclr_inv_ops, mtk_clk_gate_ops_setclr_inv,'),
    ('clk_mt6789_audiosys', 'afe with set/clr ops', 'MT6789_PWR_GATE_OPS(afe_no_setclr_ops, mtk_clk_gate_ops_no_setclr,',
     'MT6789_PWR_GATE_OPS(afe_no_setclr_ops, mtk_clk_gate_ops_setclr,'),
    ('clk_mt6789_mmsys', 'mmsys guard without DISP', 'MT6789_PWR_GATE_OPS(mm_setclr_ops, mtk_clk_gate_ops_setclr,\n'
     '\t\t    MT6789_PWR_DISP);', 'MT6789_PWR_GATE_OPS(mm_setclr_ops, mtk_clk_gate_ops_setclr,\n\t\t    0);'),
    ('clk_mt6789_img', 'ipe guard without DISP', '\t\t    MT6789_PWR_DISP | MT6789_PWR_IPE);',
     '\t\t    MT6789_PWR_IPE);'),
    ('clk_mt6789_cam', 'rawa guard without CAM', '\t\t    MT6789_PWR_DISP | MT6789_PWR_CAM | MT6789_PWR_CAM_RAWA);',
     '\t\t    MT6789_PWR_DISP | MT6789_PWR_CAM_RAWA);'),
    ('clk_mt6789_mfgcfg', 'mfg guard without MFG0', 'MT6789_PWR_MFG0 | MT6789_PWR_MFG1 | MT6789_PWR_MFG3',
     'MT6789_PWR_MFG1 | MT6789_PWR_MFG3'),
    ('clk_mt6789_audiosys', 'afe guard also on DISP', '\t\t    MT6789_PWR_AUDIO);', '\t\t    MT6789_PWR_AUDIO | MT6789_PWR_DISP);'),
    ('clk_mt6789/clk-mt6789-pwr.c', 'guard ORs the status words',
     'readl(spm + MT6789_SPM_PWR_STATUS) & readl', 'readl(spm + MT6789_SPM_PWR_STATUS) | readl'),
    ('clk_mt6789/clk-mt6789-pwr.c', 'guard ignores ISO', '#define MT6789_PWR_CON_ISO\t\tBIT(1)',
     '#define MT6789_PWR_CON_ISO\t\t0'),
    ('clk_mt6789/clk-mt6789-pwr.c', 'guard reads with SPM unmapped', '\tif (!spm)\n\t\treturn false;',
     '\tif (!spm)\n\t\treturn true;'),
    ('clk_mt6789/clk-mt6789-pwr.c', 'guard CAM PWR_CON offset', '{ "CAM", BIT(23), 0x35c }', '{ "CAM", BIT(23), 0x358 }'),
    ('clk_mt6789/clk-mt6789-pwr.c', 'guard wrong SPM compatible', '"mediatek,mt6789-scpsys"', '"mediatek,mt6789-spm"'),
    ('clk_mt6789/clk-mt6789.c', 'factor divider', '"mainpll_d4_d16", "mainpll", 1, 64', '"mainpll_d4_d16", "mainpll", 1, 32'),
    ('clk_mt6789/clk-mt6789.c', 'factor parent', '"univpll_192m_d2", "univpll", 1, 26', '"univpll_192m_d2", "univpll", 1, 13'),
    ('clk_mt6789/clk-mt6789.c', 'mux parent order', '\t"mainpll_d4_d4",\n\t"mainpll_d7_d2",',
     '\t"mainpll_d7_d2",\n\t"mainpll_d4_d4",'),
    ('clk_mt6789/clk-mt6789.c', 'mux width', 'CLK_CFG(1), CLK_CFG_SET(1), CLK_CFG_CLR(1), 0, 4, 7,',
     'CLK_CFG(1), CLK_CFG_SET(1), CLK_CFG_CLR(1), 0, 3, 7,'),
    ('clk_mt6789/clk-mt6789.c', 'mux gate bit', 'CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 8, 1, 15,',
     'CLK_CFG(6), CLK_CFG_SET(6), CLK_CFG_CLR(6), 8, 1, 14,'),
    ('clk_mt6789/clk-mt6789.c', 'mux update bit', 'CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 16, 1, CLK_CFG_UPDATE2, 0)',
     'CLK_CFG(15), CLK_CFG_SET(15), CLK_CFG_CLR(15), 16, 1, CLK_CFG_UPDATE1, 0)'),
    ('clk_mt6789/clk-mt6789.c', 'mux becomes gated',
     'MUX_CLR_SET_UPD(CLK_TOP_SSPM_SEL, "sspm_sel", sspm_parents,\n\t\tCLK_CFG(8), CLK_CFG_SET(8), CLK_CFG_CLR(8), 24, 3, '
     'CLK_CFG_UPDATE1, 4)', 'MUX_GATE_CLR_SET_UPD(CLK_TOP_SSPM_SEL, "sspm_sel", sspm_parents,\n\t\tCLK_CFG(8), '
     'CLK_CFG_SET(8), CLK_CFG_CLR(8), 24, 3, 31, CLK_CFG_UPDATE1, 4)'),
    ('clk_mt6789/clk-mt6789.c', 'composite divider shift', 'CLK_AUDDIV_0, 9, CLK_AUDDIV_4, 8, 8),',
     'CLK_AUDDIV_0, 9, CLK_AUDDIV_4, 8, 16),'),
    ('clk_mt6789/clk-mt6789.c', 'composite gate bit', 'CLK_AUDDIV_0, 5, CLK_AUDDIV_3, 8, 8),',
     'CLK_AUDDIV_0, 6, CLK_AUDDIV_3, 8, 8),'),
    ('clk_mt6789/clk-mt6789.c', 'audio mck mux shift', '\t    CLK_AUDDIV_0, 25, 1),', '\t    CLK_AUDDIV_0, 26, 1),'),
    ('clk_mt6789/clk-mt6789.c', 'PLL pcw bits', '\t    0x44, 0xc, 5, 0x334, 0, 32),', '\t    0x44, 0xc, 5, 0x334, 0, 31),'),
    ('clk_mt6789/clk-mt6789.c', 'PLL tuner bit', '\t    0x44, 0xc, 5, 0x334, 0, 32),', '\t    0x44, 0xc, 4, 0x334, 0, 32),'),
    ('clk_mt6789/clk-mt6789.c', 'PLL not critical', '0x34c, HAVE_RST_BAR | PLL_AO, BIT(23)',
     '0x34c, HAVE_RST_BAR, BIT(23)'),
    ('clk_mt6789/clk-mt6789.c', 'PLL en_mask', 'PLL(CLK_APMIXED_MMPLL, "mmpll", 0x360, 0x360, 0xff000000, 0,',
     'PLL(CLK_APMIXED_MMPLL, "mmpll", 0x360, 0x360, 0x0, 0,'),
    ('clk_mt6789/clk-mt6789.c', 'PLL rst_bar', '0x314, HAVE_RST_BAR, BIT(23), 0x30c, 24,', '0x314, HAVE_RST_BAR, BIT(22), 0x30c, 24,'),
    ('clk_mt6789/clk-mt6789.c', 'PLL postdiv shift', '0x354, 24,\n', '0x354, 23,\n'),
    ('clk_mt6789/clk-mt6789.c', 'PLL usb enable bit', 'PLL(CLK_APMIXED_USBPLL, "usbpll", 0x3c4, 0x3cc, 0x0, 2,',
     'PLL(CLK_APMIXED_USBPLL, "usbpll", 0x3c4, 0x3cc, 0x0, 0,'),
    ('clk_mt6789/clk-mt6789.c', 'PLL fmin', '#define MT6789_PLL_FMIN\t\t(1500UL * MHZ)', '#define MT6789_PLL_FMIN\t\t(1000UL * MHZ)'),
    ('clk_mt6789/clk-mt6789.c', 'PLL integer bits', '#define MT6789_INTEGER_BITS\t8', '#define MT6789_INTEGER_BITS\t7'),
    ('clk_mtk_ack/clk-mux.c', 'mux update only after a gated reparent',
     'BIT(mux->data->gate_shift));\n\n\tif (mux->data->upd_shift >= 0) {',
     'BIT(mux->data->gate_shift));\n\n\tif (mux->reparent && mux->data->upd_shift >= 0) {'),
    ('clk_mtk_ack/clk-pll.c', 'PLL set_rate bypasses the FHCTL hook',
     'if (!mtk_fh_set_rate || !mtk_fh_set_rate(pll->data->name, pcw, postdiv))', 'if (true)'),
    ('clk_mtk_ack/clk-pll.c', 'FHCTL hook gets the postdiv shift',
     '!mtk_fh_set_rate(pll->data->name, pcw, postdiv)', '!mtk_fh_set_rate(pll->data->name, pcw, ffs(postdiv) - 1)'),
]


def fail(message):
    raise SystemExit('clk-upstream: ' + message)


def scpsys(tree):
    source = (tree / SCPSYS).read_text()
    table = source[source.index('scpsys_domain_data_mt6789[]'):source.index('mt6789_scpsys_data = {')]
    rows, status, status_2nd = [], set(), set()
    for match in re.finditer(r'\[MT6789_POWER_DOMAIN_(\w+)\]\s*=\s*\{(.*?)\n\t\},', table, re.S):
        body = match.group(2)
        sta = re.search(r'\.sta_mask\s*=\s*BIT\((\d+)\)', body)
        ctl = re.search(r'\.ctl_offs\s*=\s*(0x[0-9a-fA-F]+)', body)
        regs = re.search(r'\.pwr_sta_offs\s*=\s*(0x[0-9a-fA-F]+),\s*\.pwr_sta2nd_offs\s*=\s*(0x[0-9a-fA-F]+)', body)
        if not sta or not ctl or not regs:
            fail('domain %s has no sta_mask, ctl_offs or status offsets in %s' % (match.group(1), SCPSYS))
        rows.append((match.group(1), 1 << int(sta.group(1)), int(ctl.group(1), 16)))
        status.add(int(regs.group(1), 16))
        if 'MTK_SCPD_MODEM_PWRSEQ' not in body:
            status_2nd.add(int(regs.group(2), 16))
    if len(rows) < 10 or len(status) != 1 or len(status_2nd) != 1:
        fail('cannot read one PWR_STATUS pair and the domain table of ' + SCPSYS)
    lines = ['#define ORACLE_SPM_COMPATIBLE "mediatek,mt6789-scpsys"',
             '#define ORACLE_PWR_STATUS 0x%x' % status.pop(),
             '#define ORACLE_PWR_STATUS_2ND 0x%x' % status_2nd.pop(),
             'struct oracle_domain {', '\tconst char *name;', '\tu32 sta_mask;', '\tu32 con_ofs;', '};',
             'static const struct oracle_domain oracle_domains[] = {']
    lines += ['\t{ "%s", 0x%x, 0x%x },' % row for row in rows] + ['};']
    return '\n'.join(lines) + '\n'


def data_sources(tree, texts=None):
    out = []
    for module in DATA:
        for source in sorted((tree / MODULES / module).glob('*.c')):
            out.append(str(source.relative_to(tree)))
    return out


class Side:
    def __init__(self, tree, work, name, core, compiled, extra_includes):
        self.tree, self.work, self.name = tree, work / name, name
        self.work.mkdir()
        self.include = self.work / 'include'
        self.core = core
        self.paths = [core + '/' + f for f in compiled] + data_sources(tree)
        self.texts = {p: (tree / p).read_text() for p in self.paths}
        headers = [h.read_text() for d in [core] + list(extra_includes) for h in sorted((tree / d).glob('*.h'))]
        for text in list(self.texts.values()) + headers:
            for header in re.findall(r'^#include <([^>]+)>', text, re.M):
                if header.startswith(('include/', 'dt-bindings/')):
                    continue
                path = self.include / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        self.flags = CFLAGS + ['-include', str(HARNESS / 'stubs.h'), '-I', str(self.include), '-I', str(work),
                               '-I', str(tree / core), '-I', str(tree / 'mindone/dts/include'),
                               '-I', str(tree / MODULES / 'include')]
        for extra in extra_includes:
            self.flags += ['-I', str(tree / extra)]
        self.harness = self.compile(HARNESS / 'harness.c', 'harness.o')
        self.cache = {}

    def compile(self, source, name):
        target = self.work / name
        result = subprocess.run(['cc'] + self.flags + ['-c', str(source), '-o', str(target)],
                                capture_output=True, text=True)
        if result.returncode:
            return None, result.stderr
        return target, ''

    def build(self, texts, tag):
        objects = [self.harness[0]]
        for index, path in enumerate(sorted(texts)):
            key = (path, texts[path])
            if key not in self.cache:
                directory = self.work / ('%s-%d-%d' % (tag, index, len(self.cache)))
                directory.mkdir()
                source = directory / Path(path).name
                source.write_text(texts[path])
                self.cache[key] = self.compile(source, directory.name + '.o')
            obj, err = self.cache[key]
            if obj is None:
                errors = [l for l in err.split('\n') if 'error' in l]
                return None, '%s: %s' % (path, (errors or [err.strip() or 'compile error'])[0])
            objects.append(obj)
        target = self.work / tag
        result = subprocess.run(['cc', '-fsanitize=address,undefined'] + [str(o) for o in objects] + ['-o', str(target)],
                                capture_output=True, text=True)
        if result.returncode:
            return None, 'link: ' + result.stderr.strip().split('\n')[0]
        return target, ''

    def dump(self, texts, tag):
        binary, err = self.build(texts, tag)
        if binary is None:
            return None, err
        environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
        out = []
        for mode in ('full', 'nospm'):
            result = subprocess.run([str(binary), mode], capture_output=True, text=True, env=environment, timeout=600)
            if result.returncode:
                return None, '%s exit %d: %s' % (mode, result.returncode, result.stderr.strip()[-300:])
            out.append(result.stdout if mode == 'full' else
                       ''.join('nospm: ' + line for line in result.stdout.splitlines(True)))
        return out[0] + out[1], ''


def compare(golden, dump):
    def keyed(text):
        rows = {}
        for line in text.splitlines():
            key = line.split(' |')[0]
            if key in rows:
                fail('duplicate dump line key: ' + key)
            rows[key] = line
        return rows

    old, new = keyed(golden), keyed(dump)
    unexpected, used = [], {label: 0 for label, _, _ in ALLOWED}
    for key in sorted(set(old) | set(new)):
        a, b = old.get(key), new.get(key)
        if a == b:
            continue
        for label, rule, _ in ALLOWED:
            if a and b and rule(a, b):
                used[label] += 1
                break
        else:
            unexpected.append((key, a, b))
    return unexpected, used


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            out.append(' ')
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            out.append(' ')
            i = j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def tokens(text):
    return re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\w+|[^\s\w]', strip_comments(text))


def patched_tokens(name, upstream):
    out = tokens(upstream)
    for target, label, old, new in MT6789_PATCHES:
        if target != name:
            continue
        old_t, new_t = tokens(old), tokens(new)
        hits = [i for i in range(len(out) - len(old_t) + 1) if out[i:i + len(old_t)] == old_t]
        if len(hits) != 1:
            fail('MT6789 patch "%s" matches %d times in the ACK %s' % (label, len(hits), name))
        out[hits[0]:hits[0] + len(old_t)] = new_t
    return out


def check_ack_copies(tree, ack_repo):
    core = tree / ACK_CORE
    names = sorted(p.name for p in core.iterdir())
    if names != sorted(ACK_FILES):
        fail('%s holds %s, expected %s' % (ACK_CORE, names, sorted(ACK_FILES)))
    build = (tree / BUILD).read_text()
    target = re.search(r'name = "clk_mtk_ack",(.*?)\n\)', build, re.S)
    if not target:
        fail('no clk_mtk_ack target in ' + BUILD)
    srcs = sorted(re.findall(r'"clk_mtk_ack/([\w.-]+\.c)"', target.group(1)))
    if srcs != sorted(f for f in ACK_FILES if f.endswith('.c')):
        fail('clk_mtk_ack builds %s' % srcs)
    for name in ACK_FILES:
        text = (core / name).read_text()
        for line in strip_comments(text).split('\n'):
            pass
        comments = [c for c in re.findall(r'//[^\n]*|/\*.*?\*/', re.sub(r'"(?:\\.|[^"\\])*"', '""', text), re.S)
                    if not re.search(r'SPDX-License-Identifier|Copyright', c)]
        if comments:
            fail('%s/%s carries a comment: %s' % (ACK_CORE, name, comments[0][:60]))
    if not ack_repo:
        return 'ACK identity not checked (no --ack-repo)'
    tag, commit = (tree / ACK_BASE).read_text().split()[:2]
    for name in ACK_FILES:
        result = subprocess.run(['git', '-C', str(ack_repo), 'show', '%s:drivers/clk/mediatek/%s' % (commit, name)],
                                capture_output=True, text=True, env=dict(os.environ, GIT_NO_LAZY_FETCH='1'))
        if result.returncode:
            fail('cannot read %s from %s at %s: %s' % (name, ack_repo, commit, result.stderr.strip()))
        upstream = result.stdout
        local = (core / name).read_text()
        if patched_tokens(name, upstream) != tokens(local):
            fail('%s/%s differs from %s drivers/clk/mediatek/%s beyond comments and the MT6789 patches' %
                 (ACK_CORE, name, tag, name))
        spdx = [l for l in upstream.split('\n') if 'SPDX-License-Identifier' in l][:1]
        if spdx != [l for l in local.split('\n') if 'SPDX-License-Identifier' in l][:1]:
            fail('%s/%s changed the SPDX line' % (ACK_CORE, name))
    return 'ACK identity: %d files token-identical to %s (%s) plus %d MT6789 patches: %s' % (
        len(ACK_FILES), tag, commit[:12], len(MT6789_PATCHES), ', '.join(label for _, label, _, _ in MT6789_PATCHES))


def pll_rate(fin, pcw, postdiv, pcwbits, ibits):
    fbits = pcwbits - ibits if pcwbits > ibits else 0
    vco = fin * pcw
    carry = 1 if fbits and vco & ((1 << fbits) - 1) else 0
    vco = (vco >> fbits) + carry
    return (vco + postdiv - 1) // postdiv


def live_check(dump, path):
    live = {}
    for line in Path(path).read_text().splitlines():
        name, rate, enable, prepare, parent, flags = line.split('\t')
        live[name] = (int(rate), parent, flags)
    clocks, plls = {}, {}
    for line in dump.splitlines():
        m = re.match(r'^(\S+) clk provider=(\S+) flags=(\S*) parents=(\S*) (\w+)(.*)$', line)
        if m:
            clocks[m.group(1)] = {'flags': m.group(3), 'parents': m.group(4).split(',') if m.group(4) else [],
                                  'kind': m.group(5), 'rest': m.group(6)}
        m = re.match(r'^(\S+) round 3800000000 -> (\d+)$', line)
        if m:
            plls[m.group(1)] = True
    problems = []
    ours = set(clocks)
    if ours != set(live) - EXTERNAL:
        problems.append('names only in the dump: %s; only live: %s' % (sorted(ours - set(live)),
                                                                        sorted(set(live) - EXTERNAL - ours)))
    rates = 0
    for name in sorted(ours & set(live)):
        c, (rate, parent, flags) = clocks[name], live[name]
        if c['flags'] != flags:
            problems.append('%s flags %s, live %s' % (name, c['flags'], flags))
        if parent not in c['parents']:
            problems.append('%s live parent %s not among %s' % (name, parent, c['parents']))
            continue
        prate = live[parent][0] if parent in live else None
        if prate is None:
            continue
        rest = c['rest']
        if c['kind'] == 'factor':
            mult, div = map(int, re.findall(r'mult=(\d+) div=(\d+)', rest)[0])
            ok = rate == prate * mult // div
        elif 'div(' in rest:
            ok = any(-(-prate // d) == rate for d in range(1, 257))
        elif name in plls:
            pcwbits = 32 if name in ('apll1', 'apll2') else 22
            ok = False
            for postdiv in (1, 2, 4, 8, 16):
                fbits = pcwbits - 8
                guess = (rate * postdiv << fbits) // prate
                if any(pll_rate(prate, p, postdiv, pcwbits, 8) == rate for p in range(guess - 2, guess + 3)):
                    ok = True
                    break
        else:
            ok = rate == prate
        rates += 1
        if not ok:
            problems.append('%s live rate %d does not follow from parent %s at %d' % (name, rate, parent, prate))
    return problems, rates


def main():
    parser = argparse.ArgumentParser(description='Compile the clk-mtk core (the fork in modules/clk_mediatek and/or '
                                     'the ACK copy in modules/clk_mtk_ack) with every MT6789 clock data driver on '
                                     'the host, probe each provider node and run every clock operation against a '
                                     'register model with an SPM power model. The fork output is pinned in '
                                     'clk-upstream/fork.golden; the ACK output must match it except for the listed '
                                     'intentional differences, and every mutation in MUTATIONS must be caught.')
    parser.add_argument('tree', type=Path)
    parser.add_argument('--ack-repo', type=Path)
    parser.add_argument('--live', type=Path)
    parser.add_argument('--write-golden', action='store_true')
    parser.add_argument('--no-mutations', action='store_true')
    args = parser.parse_args()
    tree = args.tree.resolve()
    fork = (tree / FORK_CORE / 'clk-gate.c').exists()
    ack = (tree / ACK_CORE).exists()
    if ack:
        default_repo = tree / 'ack-6.18'
        print('clk-upstream: ' + check_ack_copies(tree, args.ack_repo or (default_repo if default_repo.exists() else None)))
    golden = GOLDEN.read_text() if GOLDEN.exists() else None
    with tempfile.TemporaryDirectory(prefix='clk-upstream-') as name:
        work = Path(name)
        (work / 'oracle.h').write_text(scpsys(tree))
        if fork:
            side = Side(tree, work, 'fork', FORK_CORE, FORK_COMPILED, [])
            dump, err = side.dump(side.texts, 'fork')
            if dump is None:
                fail('fork build or run failed: ' + err)
            if args.write_golden:
                GOLDEN.write_text(dump)
                golden = dump
            elif golden != dump:
                fail('the fork in this tree no longer matches clk-upstream/fork.golden')
            print('clk-upstream: fork core and data reproduce fork.golden (%d lines)' % len(dump.splitlines()))
        if golden is None:
            fail('no fork.golden and no fork in the tree')
        if not ack or fork:
            print('clk-upstream: PASS (the data drivers still run on %s)' % FORK_CORE)
            return
        side = Side(tree, work, 'ack', ACK_CORE, ACK_COMPILED, [MODULES + '/clk_mt6789'])
        dump, err = side.dump(side.texts, 'ack')
        if dump is None:
            fail('ACK build or run failed: ' + err)
        unexpected, used = compare(golden, dump)
        for key, a, b in unexpected[:20]:
            print('clk-upstream: DIFF %s\n  fork: %s\n  ack:  %s' % (key, (a or '-')[:300], (b or '-')[:300]))
        if unexpected:
            fail('FAIL: %d unexpected differences between fork.golden and the ACK build' % len(unexpected))
        for label, pattern, reason in ALLOWED:
            if not used[label]:
                fail('allowed difference "%s" no longer occurs; drop it' % label)
            print('clk-upstream: intentional difference "%s" on %d lines: %s' % (label, used[label], reason))
        summary = re.search(r'summary clocks=(\d+) errors=(\d+)', dump)
        print('clk-upstream: ACK core with the new data drivers matches the fork on %s clocks' % summary.group(1))
        if args.live:
            problems, rates = live_check(dump, args.live)
            for problem in problems:
                print('clk-upstream: LIVE ' + problem)
            if problems:
                fail('FAIL: %d mismatches against %s' % (len(problems), args.live))
            print('clk-upstream: live tree matches names, parents and flags; %d rates follow from the data' % rates)
        if args.no_mutations:
            print('clk-upstream: PASS (mutations skipped)')
            return
        survivors = []
        for module, label, old, new, *every in MUTATIONS:
            path = MODULES + '/' + (module if '/' in module else '%s/%s.c' % (module, module))
            texts = dict(side.texts)
            if texts[path].count(old) != 1 and not (every and texts[path].count(old) > 1):
                fail('mutation "%s" does not apply once to %s' % (label, path))
            texts[path] = texts[path].replace(old, new)
            mutant, err = side.dump(texts, 'mutant%d' % len(survivors + [label]))
            if mutant is None:
                verdict = 'build/run: ' + err[:120]
            else:
                diffs, _ = compare(golden, mutant)
                verdict = '%d differing lines, first %s' % (len(diffs), diffs[0][0][:80]) if diffs else None
            print('clk-upstream: mutation %-36s %s' % (label, 'caught (%s)' % verdict if verdict else 'SURVIVED'))
            if not verdict:
                survivors.append(label)
        if survivors:
            fail('FAIL: %d mutation(s) survived: %s' % (len(survivors), ', '.join(survivors)))
    print('clk-upstream: PASS (%d mutations caught)' % len(MUTATIONS))


if __name__ == '__main__':
    main()
