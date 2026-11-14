/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_EXTCON_MTK_USB_H
#define MINDONE_EXTCON_MTK_USB_H

#include <linux/notifier.h>

#define MTK_USB_EXTCON_AUDIO_DETACH	0
#define MTK_USB_EXTCON_AUDIO_ATTACH	1

int mtk_usb_extcon_register_audio_notifier(struct notifier_block *nb);
int mtk_usb_extcon_unregister_audio_notifier(struct notifier_block *nb);

#endif
