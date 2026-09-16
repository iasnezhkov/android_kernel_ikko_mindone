/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_DISP_NOTIFY_H__
#define __MTK_DISP_NOTIFY_H__

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/notifier.h>

/* A hardware display blank change occurred */
#define MTK_DISP_EARLY_EVENT_BLANK	0x00
#define MTK_DISP_EVENT_BLANK		0x01
/* MINDONE (F3811/K8): the panel is REALLY lit after the deferred display-on that follows the LK
 * takeover (MINDONE-LK-PANELON, mtk_dsi.c). Separate from MTK_DISP_EVENT_BLANK, which fires
 * synchronously at CRTC enable ~2 s BEFORE that display-on (early return, F3581). Subscribers
 * (leds_mtk) re-apply state the panel init wiped, e.g. the AMOLED brightness register (0x51). */
#define MTK_DISP_EVENT_LK_PANEL_ON	0x02

enum {
	/* disp power on */
	MTK_DISP_BLANK_UNBLANK,
	/* disp power off */
	MTK_DISP_BLANK_POWERDOWN,
};

int mtk_disp_notifier_register(const char *source, struct notifier_block *nb);
int mtk_disp_notifier_unregister(struct notifier_block *nb);
int mtk_disp_notifier_call_chain(unsigned long val, void *v);
int mtk_disp_sub_notifier_register(const char *source, struct notifier_block *nb);
int mtk_disp_sub_notifier_unregister(struct notifier_block *nb);
int mtk_disp_sub_notifier_call_chain(unsigned long val, void *v);

#endif
