/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MINDONE: carried over from device/mainline/mt6789-power-dtbindings.h
 * (harvested from mt6789-next, commit 2ed59e432779). Upstream this lives at
 * include/dt-bindings/power/mediatek,mt6789-power.h -- renamed to match OUR
 * tree's convention (no "mediatek," in the filename, see the neighboring
 * mt8195-power.h etc. in this same directory).
 */
#ifndef _DT_BINDINGS_POWER_MT6789_POWER_H
#define _DT_BINDINGS_POWER_MT6789_POWER_H

#define MT6789_POWER_DOMAIN_MD		0
#define MT6789_POWER_DOMAIN_CONN	1
#define MT6789_POWER_DOMAIN_MFG0	2
#define MT6789_POWER_DOMAIN_MFG1	3
#define MT6789_POWER_DOMAIN_MFG2	4
#define MT6789_POWER_DOMAIN_MFG3	5
#define MT6789_POWER_DOMAIN_ISP	6
#define MT6789_POWER_DOMAIN_IPE	7
#define MT6789_POWER_DOMAIN_VDEC	8
#define MT6789_POWER_DOMAIN_VENC	9
#define MT6789_POWER_DOMAIN_DISP	10
#define MT6789_POWER_DOMAIN_AUDIO	11
#define MT6789_POWER_DOMAIN_CAM	12
#define MT6789_POWER_DOMAIN_CAM_RAWA	13
#define MT6789_POWER_DOMAIN_CAM_RAWB	14

#endif /* _DT_BINDINGS_POWER_MT6789_POWER_H */
