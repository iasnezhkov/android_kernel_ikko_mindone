# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
HARNESS = HERE / 'scpsys-mt6789'
sys.path.insert(0, str(HARNESS))
import mt6789_dt

FORK = 'mindone/modules/mtk_scpsys'
COPY = 'mindone/modules/mtk_pm_domains_ack'
DTS = 'mindone/dts'
BINDING = 'mindone/dts/include/dt-bindings/power/mt6789-power.h'
SCPSYS_H = 'mindone/modules/mtk_dvfsrc/scpsys.h'
ACK_C = 'drivers/pmdomain/mediatek/mtk-pm-domains.c'
ACK_H = 'drivers/pmdomain/mediatek/mtk-pm-domains.h'
ACK_INFRACFG = 'include/linux/soc/mediatek/infracfg.h'
ALWAYS_ON_PROVIDERS = {'topckgen_clk', 'infracfg_ao_clk'}
BOOT_ON = {'DISP'}
LP_CLOCK = 'mdpsys_config_clk:CLK_MDP_SMI0'
MODEM_OFF = 'OFF md'
SUPPLY_ACK = ['if (MTK_SCPD_CAPS(pd, MTK_SCPD_DOMAIN_SUPPLY)) {',
              'pd->supply = devm_of_regulator_get_optional(scpsys->dev, node, "domain");',
              'if (IS_ERR(pd->supply))', 'return dev_err_cast_probe(scpsys->dev, pd->supply,',
              '"%pOF: failed to get power supply.\\n",', 'node);', '}']
SETTLE_ACK = ['ret = readx_poll_timeout(scpsys_domain_is_on, pd, tmp, tmp, MTK_POLL_DELAY_US,', 'MTK_POLL_TIMEOUT);',
              'if (ret < 0)', 'return ret;', 'if (pd->data->rtff_type == SCPSYS_RTFF_TYPE_PCIE_PHY)']
SETTLE_COPY = SETTLE_ACK[:4] + ['udelay(100);'] + SETTLE_ACK[4:]
SUPPLY_COPY = ['if (MTK_SCPD_CAPS(pd, MTK_SCPD_DOMAIN_SUPPLY)) {',
               'dev_err(scpsys->dev, "%pOF: domain supplies are not supported\\n", node);',
               'return ERR_PTR(-EOPNOTSUPP);', '}']
AUDIO_BODY = ('\n\t\t\t\treg = <MT6789_POWER_DOMAIN_AUDIO>;\n'
              '\t\t\t\tclocks = <&topckgen_clk CLK_TOP_AUD_INTBUS_SEL &infracfg_ao_clk CLK_IFRAO_AUDIO_26M_BCLK '
              '&infracfg_ao_clk CLK_IFRAO_AUDIO>;\n'
              '\t\t\t\tclock-names = "audio", "audio-0", "audio-1";\n'
              '\t\t\t\tmediatek,infracfg = <&infracfg_ao_clk>;\n'
              '\t\t\t\t#power-domain-cells = <0x00>;\n')
FORK_NODE_CLOCKS = [
    ('mfg', 'topckgen_clk:CLK_TOP_MFG_REF_SEL'),
    ('isp', 'topckgen_clk:CLK_TOP_IMG1_SEL'),
    ('ipe', 'topckgen_clk:CLK_TOP_IPE_SEL'),
    ('vdec', 'topckgen_clk:CLK_TOP_VDEC_SEL'),
    ('venc', 'topckgen_clk:CLK_TOP_VENC_SEL'),
    ('mdp', 'topckgen_clk:CLK_TOP_MDP_SEL'),
    ('disp', 'topckgen_clk:CLK_TOP_DISP_SEL'),
    ('audio', 'topckgen_clk:CLK_TOP_AUD_INTBUS_SEL'),
    ('cam', 'topckgen_clk:CLK_TOP_CAM_SEL'),
    ('isp-0', 'imgsys1_clk:CLK_IMGSYS1_GALS'),
    ('isp-1', 'imgsys1_clk:CLK_IMGSYS1_LARB9'),
    ('ipe-0', 'ipesys_clk:CLK_IPE_SMI_SUBCOM'),
    ('ipe-1', 'ipesys_clk:CLK_IPE_GALS'),
    ('ipe-2', 'ipesys_clk:CLK_IPE_LARB19'),
    ('ipe-3', 'ipesys_clk:CLK_IPE_LARB20'),
    ('vdec-0', 'vdec_gcon_base_clk:CLK_VDE2_VDEC_CKEN'),
    ('vdec-1', 'vdec_gcon_base_clk:CLK_VDE2_LARB1_CKEN'),
    ('venc-0', 'venc_gcon_clk:CLK_VEN1_CKE1_VENC'),
    ('disp_lp-0', 'mdpsys_config_clk:CLK_MDP_SMI0'),
    ('disp-0', 'dispsys_config_clk:CLK_MM_SMI_INFRA'),
    ('disp-1', 'dispsys_config_clk:CLK_MM_SMI_COMMON'),
    ('disp-2', 'dispsys_config_clk:CLK_MM_SMI_GALS'),
    ('disp-3', 'dispsys_config_clk:CLK_MM_SMI_IOMMU'),
    ('audio-0', 'infracfg_ao_clk:CLK_IFRAO_AUDIO_26M_BCLK'),
    ('audio-1', 'infracfg_ao_clk:CLK_IFRAO_AUDIO'),
    ('cam-0', 'camsys_main_clk:CLK_CAM_M_LARB13'),
    ('cam-1', 'camsys_main_clk:CLK_CAM_M_LARB14'),
    ('cam_rawa-0', 'camsys_rawa_clk:CLK_CAM_RA_LARBX'),
    ('cam_rawb-0', 'camsys_rawb_clk:CLK_CAM_RB_LARBX'),
]
GENPD_BASELINE = {
    'md': ([], ['d0cf000.mddriver']),
    'conn': ([], ['18002000.consys']),
    'mfg0': (['mfg1'], []),
    'mfg1': (['mfg2', 'mfg3'], []),
    'mfg2': ([], []),
    'mfg3': ([], []),
    'isp': ([], ['1502f000.img0_smi_2x1_sub_comm', '1502e000.smi_larb9', '15020000.imgsys_config',
                 'disable_unused:disable-unused-clk-imgsys1']),
    'ipe': ([], ['1b00e000.ipe_smi_2x1_sub_comm', '1b00f000.smi_larb20', '1b001000.aie', '1b003000.rsc',
                 'disable_unused:disable-unused-clk-ipesys']),
    'vdec': ([], ['1602e000.smi_larb4', '16080000.vdec_fmt', 'disable_unused:disable-unused-clk-vdecsys']),
    'venc': ([], ['17010000.smi_larb7', '17030000.jpgenc', 'disable_unused:disable-unused-clk-vencsys']),
    'disp': (['isp', 'ipe', 'vdec', 'venc', 'cam'],
             ['1401b000.disp_smi_2x1_sub_comm0', '1401c000.disp_smi_2x1_sub_comm1', '14002000.smi_disp_comm',
              '1401e000.img1_smi_2x1_sub_comm', '14003000.smi_larb0', '14004000.smi_larb1', '1f002000.smi_larb2',
              '14016000.iommu', '14000000.dispsys_config', 'disable_unused:disable-unused-clk-mdpsys',
              'disable_unused:disable-unused-clk-mmsys']),
    'audio': ([], ['11210000.mt6789-afe-pcm', 'disable_unused:disable-unused-clk-afe']),
    'cam': (['cam_rawa', 'cam_rawb'],
            ['1a00c000.cam_smi_3x1_sub_comm1', '1a00d000.cam_smi_4x1_sub_comm0', 'soc:smi_pd_cam_main',
             '1a001000.smi_larb13', '1a002000.smi_larb14', '1a000000.camisp_legacy', '1a004000.seninf_top',
             '1a004000.seninf_n3d_top', 'disable_unused:disable-unused-clk-camsys_main']),
    'cam_rawa': ([], ['soc:smi_pd_cam_rawa', '1a00f000.smi_larb16', '1a030000.cam1_legacy',
                      'disable_unused:disable-unused-clk-camsys_rawa']),
    'cam_rawb': ([], ['soc:smi_pd_cam_rawb', '1a010000.smi_larb17', '1a050000.cam2_legacy',
                      'disable_unused:disable-unused-clk-camsys_rawb']),
}
CFLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
          '-Wno-sign-compare', '-Wno-missing-field-initializers', '-Wno-unused-variable',
          '-Wno-unused-but-set-variable', '-Wno-gnu-statement-expression-from-macro-expansion',
          '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g']
MUTATIONS = [
    (COPY + '/mt6789-pm-domains.h', 'DISP PWR_CON at the AUDIO offset', '.ctl_offs = 0x354,', '.ctl_offs = 0x358,'),
    (COPY + '/mt6789-pm-domains.h', 'ISP status bit 14', '.sta_mask = BIT(13),', '.sta_mask = BIT(14),'),
    (COPY + '/mt6789-pm-domains.h', 'CAM SRAM PDN bit 9',
     '.ctl_offs = 0x35c,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(8),',
     '.ctl_offs = 0x35c,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(9),'),
    (COPY + '/mt6789-pm-domains.h', 'VDEC SRAM ack bit 13',
     '.ctl_offs = 0x340,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(8),\n'
     '\t\t.sram_pdn_ack_bits = BIT(12),',
     '.ctl_offs = 0x340,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(8),\n'
     '\t\t.sram_pdn_ack_bits = BIT(13),'),
    (COPY + '/mt6789-pm-domains.h', 'MD waits for PWR_STATUS_2ND',
     '.ctl_offs = 0x300,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x16c,',
     '.ctl_offs = 0x300,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,'),
    (COPY + '/mt6789-pm-domains.h', 'DISP bus protection steps swapped',
     '\t\t\tBUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_MM_DISP,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_STA1),\n'
     '\t\t\tBUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_DISP,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_STA1),\n',
     '\t\t\tBUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_DISP,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_STA1),\n'
     '\t\t\tBUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_MM_DISP,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_STA1),\n'),
    (COPY + '/mt6789-pm-domains.h', 'AUDIO bus mask bit 3', '#define MT6789_TOP_AXI_PROT_EN_2_AUDIO\t\t\t(BIT(4))',
     '#define MT6789_TOP_AXI_PROT_EN_2_AUDIO\t\t\t(BIT(3))'),
    (COPY + '/mt6789-pm-domains.h', 'MFG1 first step on the _2 registers',
     'BUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_1_MFG1,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_1_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_1_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_1_STA1),',
     'BUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_1_MFG1,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_2_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_2_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_2_STA1),'),
    (COPY + '/mt6789-pm-domains.h', 'VENC second bus step dropped',
     '\t\t\tBUS_PROT_WR_IGN(INFRA, MT6789_TOP_AXI_PROT_EN_MM_VENC_2ND,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_SET,\n'
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_CLR,\n\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_STA1),\n', ''),
    (COPY + '/mt6789-pm-domains.h', 'CONN powered at probe',
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_1_STA1),\n\t\t},\n\t\t.caps = MTK_SCPD_KEEP_DEFAULT_OFF,\n\t},\n'
     '\t[MT6789_POWER_DOMAIN_MFG0]',
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_1_STA1),\n\t\t},\n\t},\n\t[MT6789_POWER_DOMAIN_MFG0]'),
    (COPY + '/mt6789-pm-domains.h', 'MFG0 powered at probe',
     '.ctl_offs = 0x308,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(8),\n'
     '\t\t.sram_pdn_ack_bits = BIT(12),\n\t\t.caps = MTK_SCPD_KEEP_DEFAULT_OFF,\n',
     '.ctl_offs = 0x308,\n\t\t.pwr_sta_offs = 0x16c,\n\t\t.pwr_sta2nd_offs = 0x170,\n\t\t.sram_pdn_bits = BIT(8),\n'
     '\t\t.sram_pdn_ack_bits = BIT(12),\n'),
    (COPY + '/mt6789-pm-domains.h', 'ISP left off at probe',
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_2_STA1),\n\t\t},\n\t},\n\t[MT6789_POWER_DOMAIN_IPE]',
     '\t\t\t\t\tMT8192_TOP_AXI_PROT_EN_MM_2_STA1),\n\t\t},\n\t\t.caps = MTK_SCPD_KEEP_DEFAULT_OFF,\n\t},\n'
     '\t[MT6789_POWER_DOMAIN_IPE]'),
    (COPY + '/mt6789-pm-domains.h', 'MD without the external buck isolation',
     'MTK_SCPD_MODEM_PWRSEQ | MTK_SCPD_EXT_BUCK_ISO | MTK_SCPD_KEEP_DEFAULT_OFF',
     'MTK_SCPD_MODEM_PWRSEQ | MTK_SCPD_KEEP_DEFAULT_OFF'),
    (COPY + '/mt6789-pm-domains.h', 'MD with the generic power sequence',
     'MTK_SCPD_MODEM_PWRSEQ | MTK_SCPD_EXT_BUCK_ISO | MTK_SCPD_KEEP_DEFAULT_OFF',
     'MTK_SCPD_EXT_BUCK_ISO | MTK_SCPD_KEEP_DEFAULT_OFF'),
    (COPY + '/mt6789-pm-domains.h', 'MD isolation register 0x39c', '.ext_buck_iso_offs = 0x398,',
     '.ext_buck_iso_offs = 0x39c,'),
    (COPY + '/mt6789-pm-domains.h', 'CAM_RAWB PWR_CON at the CAM_RAWA offset', '.ctl_offs = 0x364,',
     '.ctl_offs = 0x360,'),
    (DTS + '/mt6789.dtsi', 'ISP loses its LARB9 subsys clock',
     'clocks = <&topckgen_clk CLK_TOP_IMG1_SEL &imgsys1_clk CLK_IMGSYS1_GALS &imgsys1_clk CLK_IMGSYS1_LARB9>;\n'
     '\t\t\t\t\tclock-names = "isp", "isp-0", "isp-1";',
     'clocks = <&topckgen_clk CLK_TOP_IMG1_SEL &imgsys1_clk CLK_IMGSYS1_GALS>;\n'
     '\t\t\t\t\tclock-names = "isp", "isp-0";'),
    (DTS + '/mt6789.dtsi', 'IPE subsys clocks in another order',
     '&ipesys_clk CLK_IPE_SMI_SUBCOM &ipesys_clk CLK_IPE_GALS', '&ipesys_clk CLK_IPE_GALS &ipesys_clk CLK_IPE_SMI_SUBCOM'),
    (DTS + '/mt6789.dtsi', 'DISP without the MDP SMI clock',
     ' &mdpsys_config_clk CLK_MDP_SMI0>;\n\t\t\t\tclock-names = "disp", "mdp", "disp-0", "disp-1", "disp-2", "disp-3", '
     '"disp_lp-0";',
     '>;\n\t\t\t\tclock-names = "disp", "mdp", "disp-0", "disp-1", "disp-2", "disp-3";'),
    (DTS + '/mt6789.dtsi', 'VENC basic clock from VDEC', 'clocks = <&topckgen_clk CLK_TOP_VENC_SEL',
     'clocks = <&topckgen_clk CLK_TOP_VDEC_SEL'),
    (DTS + '/mt6789.dtsi', 'VENC node claims the VDEC id',
     'power-domain@MT6789_POWER_DOMAIN_VENC {\n\t\t\t\t\treg = <MT6789_POWER_DOMAIN_VENC>;',
     'power-domain@MT6789_POWER_DOMAIN_VENC {\n\t\t\t\t\treg = <MT6789_POWER_DOMAIN_VDEC>;'),
    (DTS + '/mt6789.dtsi', 'AUDIO nested under DISP',
     '\t\t\t\t};\n\t\t\t};\n\n\t\t\tpower-domain@MT6789_POWER_DOMAIN_AUDIO {' + AUDIO_BODY + '\t\t\t};\n',
     '\t\t\t\t};\n\n\t\t\tpower-domain@MT6789_POWER_DOMAIN_AUDIO {' + AUDIO_BODY + '\t\t\t};\n\t\t\t};\n'),
    (DTS + '/mt6789.dtsi', 'CAM_RAWB domain dropped',
     '\t\t\t\t\tpower-domain@MT6789_POWER_DOMAIN_CAM_RAWB {\n\t\t\t\t\t\treg = <MT6789_POWER_DOMAIN_CAM_RAWB>;\n'
     '\t\t\t\t\t\tclocks = <&camsys_rawb_clk CLK_CAM_RB_LARBX>;\n\t\t\t\t\t\tclock-names = "cam_rawb-0";\n'
     '\t\t\t\t\t\t#power-domain-cells = <0x00>;\n\t\t\t\t\t};\n', ''),
    (DTS + '/mt6789-media.dtsi', 'JPEG encoder attached to VDEC', '<&spm MT6789_POWER_DOMAIN_VENC>',
     '<&spm MT6789_POWER_DOMAIN_VDEC>'),
    (COPY + '/mtk-pm-domains.c', 'generic code changed: PCIe RTFF wait', 'udelay(5);', 'udelay(1);'),
    (COPY + '/mtk-pm-domains.c', 'power-on without the 100 us settle', '\tudelay(100);\n\n', ''),
    (COPY + '/mtk-pm-domains.c', 'power-on settle of 10 us', 'udelay(100);', 'udelay(10);'),
    (COPY + '/mtk-pm-domains.c', 'settle after the isolation release',
     '\tudelay(100);\n\n\tif (pd->data->rtff_type == SCPSYS_RTFF_TYPE_PCIE_PHY)\n'
     '\t\tregmap_set_bits(scpsys->base, pd->data->ctl_offs, PWR_RTFF_CLK_DIS);\n\n'
     '\tregmap_clear_bits(scpsys->base, pd->data->ctl_offs, PWR_CLK_DIS_BIT);\n'
     '\tregmap_clear_bits(scpsys->base, pd->data->ctl_offs, PWR_ISO_BIT);\n',
     '\tif (pd->data->rtff_type == SCPSYS_RTFF_TYPE_PCIE_PHY)\n'
     '\t\tregmap_set_bits(scpsys->base, pd->data->ctl_offs, PWR_RTFF_CLK_DIS);\n\n'
     '\tregmap_clear_bits(scpsys->base, pd->data->ctl_offs, PWR_CLK_DIS_BIT);\n'
     '\tregmap_clear_bits(scpsys->base, pd->data->ctl_offs, PWR_ISO_BIT);\n\tudelay(100);\n'),
    (COPY + '/mt6789-pm-domains.h', 'MFG0 asks for a domain supply',
     '\t\t.sram_pdn_ack_bits = BIT(12),\n\t\t.caps = MTK_SCPD_KEEP_DEFAULT_OFF,\n\t},\n\t[MT6789_POWER_DOMAIN_MFG1]',
     '\t\t.sram_pdn_ack_bits = BIT(12),\n\t\t.caps = MTK_SCPD_KEEP_DEFAULT_OFF | MTK_SCPD_DOMAIN_SUPPLY,\n\t},\n'
     '\t[MT6789_POWER_DOMAIN_MFG1]'),
]


def fail(message):
    raise SystemExit('scpsys-mt6789: ' + message)


def ack_source(tree, path):
    for candidate in (tree / path, tree / 'common' / path):
        if candidate.exists():
            return candidate.read_text()
    base = (tree / 'mindone/ack-base').read_text().split()
    for repo in (tree, tree / 'ack-6.18'):
        if (repo / '.git').exists() or (repo / 'HEAD').exists():
            result = subprocess.run(['git', '-C', str(repo), 'show', '%s:%s' % (base[1], path)], capture_output=True,
                                    text=True, env=dict(os.environ, GIT_NO_LAZY_FETCH='1'))
            if result.returncode == 0:
                return result.stdout
    fail('no ACK source for %s: not in %s, %s/common or a repository holding %s' % (path, tree, tree, base[1]))


def code_lines(text):
    out = []
    i = 0
    n = len(text)
    buf = []
    while i < n:
        if text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if text[i] in '"\'':
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == '\\' else 1
            buf.append(text[i:j + 1])
            i = j + 1
            continue
        buf.append(text[i])
        i += 1
    return [line.strip() for line in ''.join(buf).split('\n') if line.strip()]


def fidelity(tree, copy_dir):
    upstream_h = code_lines(ack_source(tree, ACK_H))
    if upstream_h != code_lines((copy_dir / 'mtk-pm-domains.h').read_text()):
        fail('mtk-pm-domains.h differs from %s of %s beyond comments' % (ACK_H, 'ack-base'))
    upstream = code_lines(ack_source(tree, ACK_C))
    expected = []
    included = False
    skipping = False
    for line in upstream:
        if re.fullmatch(r'#include "mt\d+-pm-domains\.h"', line):
            if not included:
                expected.append('#include "mt6789-pm-domains.h"')
                included = True
            continue
        if line == 'static const struct of_device_id scpsys_of_match[] = {':
            expected += [line, '{', '.compatible = "mediatek,mt6789-power-controller",',
                         '.data = &mt6789_scpsys_data,', '},', '{ }', '};']
            skipping = True
            continue
        if skipping:
            skipping = line != '};'
            continue
        expected.append(line)
    expected += ['MODULE_DESCRIPTION("MediaTek SCPSYS power domains");', 'MODULE_LICENSE("GPL");']
    settles = [index for index in range(len(expected)) if expected[index:index + len(SETTLE_ACK)] == SETTLE_ACK]
    if len(settles) != 1:
        fail('%s has %d power-on PWR_ACK polls followed by the RTFF step, expected 1' % (ACK_C, len(settles)))
    expected[settles[0]:settles[0] + len(SETTLE_ACK)] = SETTLE_COPY
    for index in range(len(expected)):
        if expected[index:index + len(SUPPLY_ACK)] == SUPPLY_ACK:
            expected[index:index + len(SUPPLY_ACK)] = SUPPLY_COPY
            break
    else:
        fail('%s has no domain supply lookup to replace' % ACK_C)
    actual = code_lines((copy_dir / 'mtk-pm-domains.c').read_text())
    if actual != expected:
        for index, (a, b) in enumerate(zip(actual, expected)):
            if a != b:
                fail('mtk-pm-domains.c line %d differs from %s plus the MT6789 table: %r != %r' % (index, ACK_C, a, b))
        fail('mtk-pm-domains.c length differs from %s plus the MT6789 table' % ACK_C)


def evaluate(expression):
    expression = re.sub(r'GENMASK\((\d+),\s*(\d+)\)', lambda m: '(((1 << (%s + 1)) - 1) & ~((1 << %s) - 1))' %
                        (m.group(1), m.group(2)), expression)
    expression = re.sub(r'BIT\((\d+)\)', r'(1 << \1)', expression)
    if not re.fullmatch(r'[\s()|&~<>+\-x0-9a-fA-F]+', expression):
        fail('cannot evaluate %r' % expression)
    return eval(expression)


def fork_oracle(tree):
    source = (tree / FORK / 'mtk-scpsys-mt6789.c').read_text()
    header = (tree / SCPSYS_H).read_text().replace('\\\n', ' ')
    masks = {m.group(1): evaluate(m.group(2)) for m in
             re.finditer(r'^#define (MT6789_TOP_AXI_PROT_EN_\w+)\s+(\(.*\))\s*$', header, re.M)}
    index = {m.group(1): int(m.group(2)) for m in
             re.finditer(r'#define MT6789_POWER_DOMAIN_(\w+)\s+(\d+)', (tree / BINDING).read_text())}
    count = index.pop('NR')
    table = source[source.index('scp_domain_data_mt6789[]'):source.index('scp_subdomain_mt6789[]')]
    domains = [None] * count
    for match in re.finditer(r'\[MT6789_POWER_DOMAIN_(\w+)\]\s*=\s*\{(.*?)\n\t\},', table, re.S):
        body = match.group(2)

        def field(name, default=None):
            found = re.search(r'\.%s = (.+),\n' % name, body + '\n')
            return evaluate(found.group(1)) if found else default
        caps = re.search(r'\.caps = ([\w |]+),', body)
        caps = caps.group(1) if caps else ''
        bp = [[int(b.group(i), 16) for i in (1, 2, 4)] + [masks[b.group(5)]] for b in re.finditer(
            r'BUS_PROT_IGN\(IFR_TYPE,\s*(0x\w+),\s*(0x\w+),\s*(0x\w+),\s*(0x\w+),\s*(MT6789_\w+)\)', body)]
        if re.search(r'[^_]BUS_PROT\(', body):
            fail('unexpected bus protection form in ' + match.group(1))
        prefix = re.search(r'\.subsys_clk_prefix = "(\w+)"', body)
        lp = re.search(r'\.subsys_lp_clk_prefix = "(\w+)"', body)
        basic = re.search(r'\.basic_clk_name = \{([^}]*)\}', body)
        domains[index[match.group(1)]] = {
            'id': match.group(1), 'name': re.search(r'\.name = "(\w+)"', body).group(1),
            'ctl': field('ctl_offs'), 'sta': field('sta_mask'), 'sram': field('sram_pdn_bits', 0),
            'ack': field('sram_pdn_ack_bits', 0), 'ext_ofs': field('extb_iso_offs', 0),
            'ext_mask': field('extb_iso_bits', 0), 'modem': 'MTK_SCPD_MD_OPS' in caps,
            'default_off': 'MTK_SCPD_BYPASS_INIT_ON' in caps, 'bp': bp, 'parent': -1,
            'prefix': prefix.group(1) if prefix else None, 'lp': lp.group(1) if lp else None,
            'basic': re.findall(r'"([\w-]+)"', basic.group(1)) if basic else []}
    if None in domains:
        fail('the fork table misses a domain of the binding')
    for parent, child in re.findall(r'\{MT6789_POWER_DOMAIN_(\w+),\s*MT6789_POWER_DOMAIN_(\w+)\}',
                                    source[source.index('scp_subdomain_mt6789[]'):]):
        domains[index[child]]['parent'] = index[parent]
    regs = re.search(r'\.pwr_sta_offs = (0x\w+),\s*\.pwr_sta2nd_offs = (0x\w+)', source)
    return domains, int(regs.group(1), 16), int(regs.group(2), 16), index


def blocks(domains):
    out = {}
    for number, domain in enumerate(domains):
        for prefix in (domain['prefix'], domain['lp']):
            if not prefix:
                continue
            for name, clock in FORK_NODE_CLOCKS:
                provider = clock.split(':')[0]
                if name.startswith(prefix + '-') and provider not in ALWAYS_ON_PROVIDERS:
                    if out.setdefault(provider, number) != number:
                        fail('provider %s feeds two domains' % provider)
    return out


def oracle_header(domains, sta, sta2):
    lines = ['struct oracle_bp {', '\tu32 set, clr, sta, mask;', '};', 'struct oracle_domain {',
             '\tconst char *id;', '\tconst char *name;', '\tu32 ctl;', '\tu32 sta;', '\tint parent;', '\tbool modem;',
             '\tu32 ext_ofs;', '\tu32 ext_mask;', '\tu32 sram;', '\tu32 ack;', '\tint nbp;',
             '\tstruct oracle_bp bp[8];', '\tbool boot_on;', '};',
             'struct oracle_block {', '\tconst char *label;', '\tint domain;', '};',
             '#define ORACLE_DOMAINS %d' % len(domains), '#define ORACLE_PWR_STATUS 0x%x' % sta,
             '#define ORACLE_PWR_STATUS_2ND 0x%x' % sta2, 'static const struct oracle_domain oracle_domains[] = {']
    for d in domains:
        bp = ', '.join('{ 0x%x, 0x%x, 0x%x, 0x%x }' % tuple(b) for b in d['bp'])
        lines.append('\t{ "%s", "%s", 0x%x, 0x%x, %d, %s, 0x%x, 0x%x, 0x%x, 0x%x, %d, { %s }, %s },' % (
            d['id'], d['name'], d['ctl'], d['sta'], d['parent'], 'true' if d['modem'] else 'false', d['ext_ofs'],
            d['ext_mask'], d['sram'], d['ack'], len(d['bp']), bp, 'true' if d['id'] in BOOT_ON else 'false'))
    lines += ['};', 'static const struct oracle_block oracle_blocks[] = {']
    lines += ['\t{ "%s", %d },' % item for item in sorted(blocks(domains).items())]
    lines += ['};']
    return '\n'.join(lines) + '\n'


class TreeC:
    def __init__(self):
        self.decls = []
        self.body = []
        self.count = 0

    def node(self, parent, name, label=None, disabled=False):
        number = self.count
        self.count += 1
        self.body.append('\tn[%d] = harness_node(%s, "%s", %s);' % (
            number, 'NULL' if parent is None else 'n[%d]' % parent, name, '"%s"' % label if label else 'NULL'))
        if disabled:
            self.body.append('\tn[%d]->disabled = true;' % number)
        return number

    def strings(self, number, prop, values):
        name = 'tree_s%d' % len(self.decls)
        self.decls.append('static const char *const %s[] = { %s };' % (name, ', '.join('"%s"' % v for v in values)))
        self.body.append('\tharness_prop_strings(n[%d], "%s", %d, %s);' % (number, prop, len(values), name))

    def u32(self, number, prop, values):
        name = 'tree_c%d' % len(self.decls)
        self.decls.append('static const u32 %s[] = { %s };' % (name, ', '.join('0x%x' % v for v in values)))
        self.body.append('\tharness_prop_u32(n[%d], "%s", %d, %s);' % (number, prop, len(values), name))

    def render(self, probe):
        head = ['#include "stubs.h"',
                'struct device_node *harness_node(struct device_node *parent, const char *name, const char *label);',
                'void harness_prop_strings(struct device_node *np, const char *name, int count, '
                'const char *const *strings);',
                'void harness_prop_u32(struct device_node *np, const char *name, int count, const u32 *cells);',
                'static struct device_node *n[%d];' % max(1, self.count)]
        tail = ['struct device_node *harness_probe_node(void)', '{', '\treturn n[%d];' % probe, '}']
        return '\n'.join(head + self.decls + ['void harness_build_tree(void)', '{'] + self.body + ['}'] + tail) + '\n'


def common_nodes(c):
    root = c.node(None, '/')
    infra = c.node(root, 'syscon@10001000', 'infracfg_ao_clk')
    c.strings(infra, 'compatible', ['mediatek,mt6789-infracfg_ao', 'syscon', 'simple-mfd'])
    return root


def fork_tree():
    c = TreeC()
    root = common_nodes(c)
    spm = c.node(root, 'power-controller@10006000', 'scpsys')
    c.strings(spm, 'compatible', ['mediatek,mt6789-scpsys', 'syscon'])
    c.u32(spm, 'reg', [0, 0x10006000, 0, 0x1000])
    c.strings(spm, 'clocks', [clock for _, clock in FORK_NODE_CLOCKS])
    c.strings(spm, 'clock-names', [name for name, _ in FORK_NODE_CLOCKS])
    c.strings(spm, 'infracfg', ['infracfg_ao_clk'])
    afe = c.node(root, 'mt6789-afe-pcm@11210000', 'afe')
    c.strings(afe, 'mediatek,infracfg', ['infracfg_ao_clk'])
    return c.render(spm)


def dt_values(node, prop, values):
    out = []
    for item in mt6789_dt.cells(node, prop):
        if item.startswith('&'):
            fail('unexpected phandle in %s of %s' % (prop, node.path()))
        out.append(int(values.get(item, item), 0))
    return out


def ack_tree(tree, dts_dir):
    dt = mt6789_dt.load(dts_dir)
    values = mt6789_dt.macros(tree / BINDING)
    controllers = [node for node in dt.root.walk()
                   if 'mediatek,mt6789-power-controller' in mt6789_dt.strings(node, 'compatible')]
    if len(controllers) != 1:
        fail('expected one mediatek,mt6789-power-controller node, found %d' % len(controllers))
    controller = controllers[0]
    syscon = controller.parent
    c = TreeC()
    root = common_nodes(c)
    mapping = {}

    def emit(node, parent):
        label = node.labels[0] if node.labels else None
        number = c.node(parent, node.name, label, mt6789_dt.strings(node, 'status') == ['disabled'])
        mapping[id(node)] = number
        for prop in node.props:
            if prop in ('compatible', 'clock-names'):
                c.strings(number, prop, mt6789_dt.strings(node, prop))
            elif prop == 'reg':
                c.u32(number, prop, dt_values(node, prop, values))
            elif prop == 'clocks':
                c.strings(number, prop, ['%s:%s' % (g[0], g[1]) for g in mt6789_dt.phandle_groups(node, prop)])
            elif prop in ('mediatek,infracfg', 'mediatek,smi', 'mediatek,infracfg-nao', 'access-controllers'):
                c.strings(number, prop, [g[0] for g in mt6789_dt.phandle_groups(node, prop)])
            elif prop not in ('#address-cells', '#size-cells', '#power-domain-cells', 'status'):
                fail('unexpected property %s in %s' % (prop, node.path()))
        for child in node.children:
            emit(child, number)
    emit(syscon, root)
    afe = c.node(root, 'mt6789-afe-pcm@11210000', 'afe')
    c.strings(afe, 'mediatek,infracfg', ['infracfg_ao_clk'])
    return c.render(mapping[id(controller)]), dt, controller


def dt_checks(tree, dt, controller, domains):
    values = mt6789_dt.macros(tree / BINDING)
    names = {index: d['name'] for index, d in enumerate(domains)}
    clocks = []
    for node in controller.walk():
        if node is controller:
            continue
        groups = mt6789_dt.phandle_groups(node, 'clocks')
        clock_names = mt6789_dt.strings(node, 'clock-names')
        if len(groups) != len(clock_names):
            fail('%s has %d clocks and %d clock-names' % (node.path(), len(groups), len(clock_names)))
        number = dt_values(node, 'reg', values)[0]
        basic = [n for n in clock_names if '-' not in n]
        if clock_names[:len(basic)] != basic:
            fail('%s lists a subsys clock before a basic clock' % node.path())
        domain = domains[number]
        table = dict(FORK_NODE_CLOCKS)
        expected = [(n, table[n]) for n in domain['basic']]
        for prefix in (domain['prefix'], domain['lp']):
            if prefix:
                expected += [(n, c) for n, c in FORK_NODE_CLOCKS if n.startswith(prefix + '-')]
        actual = [(n, '%s:%s' % (g[0], g[1])) for n, g in zip(clock_names, groups)]
        if actual != expected:
            fail('%s clocks %s differ from the fork domain %s: %s' % (node.path(), actual, domain['id'], expected))
        clocks += actual
    if sorted(clocks) != sorted(FORK_NODE_CLOCKS):
        fail('domain clocks differ from the fork node: missing %s, extra %s' % (
            sorted(set(FORK_NODE_CLOCKS) - set(clocks)), sorted(set(clocks) - set(FORK_NODE_CLOCKS))))
    spm_labels = set(controller.labels)
    attached = {}
    for node in dt.root.walk():
        for group in mt6789_dt.phandle_groups(node, 'power-domains') if 'power-domains' in node.props else []:
            target = dt.labels.get(group[0])
            if target is not controller:
                if target is not None and 'mediatek,mt6789-scpsys' in mt6789_dt.strings(target, 'compatible'):
                    fail('%s still takes its power domain from the scpsys syscon' % node.path())
                continue
            attached.setdefault(names[int(values.get(group[1], group[1]), 0)], []).append(
                mt6789_dt.device_name(node))
    for name, (_, devices) in GENPD_BASELINE.items():
        if sorted(attached.get(name, [])) != sorted(devices):
            fail('DT consumers of %s differ from the live baseline: %s vs %s' % (
                name, sorted(attached.get(name, [])), sorted(devices)))
    return spm_labels


class Build:
    def __init__(self, tree, work):
        self.tree, self.work = tree, work
        self.fake = work / 'fake'
        self.ackinc = work / 'ackinc'
        (self.ackinc / Path(ACK_INFRACFG).parent.relative_to('include')).mkdir(parents=True, exist_ok=True)
        (self.ackinc / Path(ACK_INFRACFG).relative_to('include')).write_text(ack_source(tree, ACK_INFRACFG))
        self.model = self.compile(HARNESS / 'model.c', 'model.o', [])

    def touch(self, sources):
        for source in sources:
            for name in re.findall(r'^#include <([^>]+)>', Path(source).read_text(), re.M):
                path = self.fake / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()

    def compile(self, source, name, include):
        target = self.work / name
        flags = CFLAGS + ['-include', str(HARNESS / 'stubs.h'), '-I', str(self.work), '-I', str(HARNESS)]
        for directory in include + [self.ackinc, self.tree / DTS / 'include', self.fake]:
            flags += ['-I', str(directory)]
        self.touch([source])
        subprocess.run(['cc'] + flags + ['-c', str(source), '-o', str(target)], check=True)
        return target

    def link(self, objects, name):
        target = self.work / ('bin-' + name)
        subprocess.run(['cc', '-fsanitize=address,undefined'] + [str(o) for o in [self.model] + objects] +
                       ['-o', str(target)], check=True)
        return target


def run(binary):
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
    result = subprocess.run([str(binary)], capture_output=True, text=True, env=environment, timeout=300)
    if result.returncode:
        sys.stderr.write(result.stderr)
        raise RuntimeError('%s exited with %d' % (binary.name, result.returncode))
    return result.stdout


def phases(output):
    out = {}
    order = []
    for line in output.splitlines():
        phase, event = line.split('|', 1)
        if phase not in out:
            order.append(phase)
        out.setdefault(phase, []).append(event)
    return out, order


def segments(events):
    calls, current, state = [], [], []
    for event in events:
        if event.startswith('STATE '):
            state.append(event)
            continue
        current.append(event)
        match = re.fullmatch(r'(ON|OFF) (\w+) (-?\d+)', event)
        if match:
            calls.append(('%s %s' % (match.group(1), match.group(2)), current))
            current = []
    if current:
        calls.append(('tail', current))
    return calls, state


def canonical(events, notes):
    out = []
    for event in events:
        if event.endswith(LP_CLOCK) and event.startswith('CLK'):
            continue
        match = re.fullmatch(r'SPM (0x\w+) (0x\w+)->(0x\w+)', event)
        if match and out and out[-1][0] == 'SPM' and out[-1][1] == match.group(1):
            out[-1] = ('SPM', match.group(1), out[-1][2], match.group(3))
            continue
        out.append(('SPM', match.group(1), match.group(2), match.group(3)) if match else (event,))
    return out


def relaxed(events):
    finals = {}
    others = []
    for item in events:
        if item[0] == 'SPM':
            finals[item[1]] = item[3]
        else:
            others.append(item)
    return finals, others


def lp_coverage(output, domains, label):
    disp = [d for d in domains if d['id'] == 'DISP'][0]
    count = 0
    for line in output.splitlines():
        event = line.split('|', 1)[1]
        if event == 'CLK+ ' + LP_CLOCK:
            count += 1
        elif event == 'CLK- ' + LP_CLOCK:
            count -= 1
        match = re.fullmatch(r'INFRA (SET|CLR) (0x\w+) (0x\w+)', event)
        if match:
            offset, mask = int(match.group(2), 16), int(match.group(3), 16)
            for set_ofs, clr_ofs, _, bp_mask in disp['bp']:
                if offset in (set_ofs, clr_ofs) and mask == bp_mask and count <= 0:
                    fail('%s: DISP bus protection changed while %s was off (%s)' % (label, LP_CLOCK, line))


def compare(fork_out, ack_out, domains):
    notes = {}
    for label, output in (('fork', fork_out), ('ack', ack_out)):
        bad = [line for line in output.splitlines() if '|VIOLATION ' in line or '|POLLTIMEOUT' in line or
               line.startswith(('end|VIOLATIONS', 'end|TIMEOUTS')) and not line.endswith(' 0')]
        if bad:
            fail('%s run: %s' % (label, bad[0]))
        if 'probe|RESULT 0' not in output.splitlines():
            fail('%s probe failed' % label)
        lp_coverage(output, domains, label)
    fork, fork_order = phases(fork_out)
    ack, ack_order = phases(ack_out)
    if fork_order != ack_order:
        fail('phase order differs: %s vs %s' % (fork_order, ack_order))
    lines_fork = sorted(e for e in fork['genpd'] if e.startswith(('GENPD', 'LINK', 'PROVIDER')))
    lines_ack = sorted(e for e in ack['genpd'] if e.startswith(('GENPD', 'LINK', 'PROVIDER')))
    if lines_fork != lines_ack:
        fail('genpd registration differs: %s' % sorted(set(lines_fork) ^ set(lines_ack)))
    links = {}
    for event in ack['genpd']:
        if event.startswith('LINK '):
            parent, child = event.split()[1:]
            links.setdefault(parent, []).append(child)
    registered = {event.split()[1] for event in ack['genpd'] if event.startswith('GENPD ')}
    if registered != set(GENPD_BASELINE):
        fail('genpd names differ from the live baseline: %s' % sorted(registered ^ set(GENPD_BASELINE)))
    for name, (children, _) in GENPD_BASELINE.items():
        if sorted(links.get(name, [])) != sorted(children):
            fail('subdomains of %s differ from the live baseline: %s vs %s' % (name, links.get(name), children))
    deltas = []
    for phase in fork_order:
        if phase in ('genpd', 'end'):
            continue
        calls_fork, state_fork = segments(fork[phase])
        calls_ack, state_ack = segments(ack[phase])
        state_fork = sorted(s for s in state_fork if LP_CLOCK not in s)
        state_ack = sorted(s for s in state_ack if LP_CLOCK not in s)
        if state_fork != state_ack:
            fail('%s: state differs: %s' % (phase, sorted(set(state_fork) ^ set(state_ack))))
        if phase == 'probe':
            continue
        if [name for name, _ in calls_fork] != [name for name, _ in calls_ack]:
            fail('%s: calls differ: %s vs %s' % (phase, [n for n, _ in calls_fork], [n for n, _ in calls_ack]))
        for (name, events_fork), (_, events_ack) in zip(calls_fork, calls_ack):
            a, b = canonical(events_fork, notes), canonical(events_ack, {})
            if a == b:
                continue
            if name == MODEM_OFF and relaxed(a) == relaxed(b):
                deltas.append('%s/%s: fork %s; ack %s' % (phase, name, [x for x in a if x[0] == 'SPM'],
                                                          [x for x in b if x[0] == 'SPM']))
                continue
            first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            fail('%s/%s: sequences differ at step %d: fork %s, ack %s' % (phase, name, first, a[first:first + 3],
                                                                         b[first:first + 3]))
    return notes, deltas


def probe_order(output, domains):
    order = []
    by_ctl = {'0x%03x' % d['ctl']: d['name'] for d in domains}
    for line in output.splitlines():
        match = re.fullmatch(r'probe\|SPM (0x\w+) .*', line)
        if match and by_ctl.get(match.group(1)) and by_ctl[match.group(1)] not in order:
            order.append(by_ctl[match.group(1)])
    return order


def gate(name, function, verdicts):
    try:
        result = function()
    except (SystemExit, RuntimeError, ValueError, KeyError) as error:
        verdicts.append('%s: %s' % (name, str(error).splitlines()[0].replace('scpsys-mt6789: ', '')[:110]))
        return None
    return result


def evaluate_ack(tree, work, build, copy_dir, dts_dir, domains, fork_out, name):
    verdicts = []
    gate('fidelity', lambda: fidelity(tree, copy_dir), verdicts)
    parsed = gate('dt', lambda: ack_tree(tree, dts_dir), verdicts)
    if parsed:
        source, dt, controller = parsed
        gate('dt', lambda: dt_checks(tree, dt, controller, domains), verdicts)
        (work / ('%s-tree.c' % name)).write_text(source)
        objects = [build.compile(work / ('%s-tree.c' % name), '%s-tree.o' % name, []),
                   build.compile(copy_dir / 'mtk-pm-domains.c', '%s-pm.o' % name, [copy_dir])]
        ack_out = gate('run', lambda: run(build.link(objects, name)), verdicts)
        if ack_out:
            result = gate('trace', lambda: compare(fork_out, ack_out, domains), verdicts)
            if result:
                return ack_out, result[0], result[1], verdicts
    return None, None, None, verdicts


def main():
    parser = argparse.ArgumentParser(description='Compile the mtk_scpsys fork and the ACK mtk-pm-domains copy with '
                                     'its MT6789 data against one SPM, infracfg and clock model with ASan/UBSan, '
                                     'probe both from their device trees and power every domain off and on. The '
                                     'register, bus protection and clock sequences of every call must match after '
                                     'the listed normalisations, the genpd tree must match the live baseline of '
                                     '14.11, the copy must equal the ACK source of ack-base apart from comments and '
                                     'the MT6789 table, and every mutation in MUTATIONS must be rejected.')
    parser.add_argument('tree', type=Path)
    parser.add_argument('--no-mutations', action='store_true')
    parser.add_argument('--trace', type=Path)
    args = parser.parse_args()
    tree = args.tree.resolve()
    domains, sta, sta2, _ = fork_oracle(tree)
    with tempfile.TemporaryDirectory(prefix='scpsys-mt6789-') as name:
        work = Path(name)
        (work / 'oracle.h').write_text(oracle_header(domains, sta, sta2))
        (work / 'fork-tree.c').write_text(fork_tree())
        build = Build(tree, work)
        fork_objects = [build.compile(work / 'fork-tree.c', 'fork-tree.o', []),
                        build.compile(tree / FORK / 'mtk-scpsys-common.c', 'fork-common.o', [tree / FORK]),
                        build.compile(tree / FORK / 'mtk-scpsys-mt6789.c', 'fork-mt6789.o', [tree / FORK])]
        fork_out = run(build.link(fork_objects, 'fork'))
        ack_out, notes, deltas, verdicts = evaluate_ack(tree, work, build, tree / COPY, tree / DTS, domains, fork_out,
                                                        'ack')
        if verdicts:
            fail('; '.join(verdicts))
        if args.trace:
            args.trace.mkdir(parents=True, exist_ok=True)
            (args.trace / 'fork.txt').write_text(fork_out)
            (args.trace / 'ack.txt').write_text(ack_out)
        calls = sum(1 for line in ack_out.splitlines() if re.search(r'\|(ON|OFF) \w+ 0$', line))
        print('scpsys-mt6789: %d domains, %d genpd calls compared, genpd tree and DT consumers match the 14.11 '
              'baseline' % (len(domains), calls))
        print('  probe power-on order: fork %s; ack %s' % (probe_order(fork_out, domains), probe_order(ack_out, domains)))
        for note, count in sorted(notes.items()):
            print('  normalised: %s (%d calls)' % (note, count))
        print('  normalised: power-off PWR_CON bit order collapsed to the final value of each consecutive run')
        print('  normalised: %s enabled with DISP (ack) instead of around each transition (fork); both cover '
              'every DISP bus protection change' % LP_CLOCK)
        for delta in deltas:
            print('  modem delta: ' + delta)
        if args.no_mutations:
            print('scpsys-mt6789: PASS (mutations skipped)')
            return
        survivors = []
        for number, (path, label, old, new) in enumerate(MUTATIONS):
            scratch = work / ('mutant%d' % number)
            copy_dir = scratch / 'copy'
            dts_dir = scratch / 'dts'
            shutil.copytree(tree / COPY, copy_dir)
            shutil.copytree(tree / DTS, dts_dir)
            target = (copy_dir if path.startswith(COPY) else dts_dir) / Path(path).name
            text = target.read_text()
            if text.count(old) != 1:
                fail('mutation "%s" does not apply once to %s' % (label, path))
            target.write_text(text.replace(old, new))
            verdicts = evaluate_ack(tree, scratch, build, copy_dir, dts_dir, domains, fork_out, 'mutant%d' % number)[3]
            if not verdicts:
                survivors.append(label)
            print('  mutation %-42s %s' % (label, 'SURVIVED' if not verdicts else 'rejected by ' + ' | '.join(verdicts)))
        if survivors:
            fail('FAIL: mutations survived: ' + ', '.join(survivors))
    print('scpsys-mt6789: PASS (%d mutations rejected)' % len(MUTATIONS))


if __name__ == '__main__':
    main()
