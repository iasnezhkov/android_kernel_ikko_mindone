/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_SMI_DEBUG_H
#define __MTK_SMI_DEBUG_H

#include <linux/notifier.h>	/* struct notifier_block must be a complete, file-scope type here (F3563) */

#if IS_ENABLED(CONFIG_MTK_SMI)

int mtk_smi_dbg_register_notifier(struct notifier_block *nb);
int mtk_smi_dbg_unregister_notifier(struct notifier_block *nb);
#else

static inline int mtk_smi_dbg_register_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int mtk_smi_dbg_unregister_notifier(struct notifier_block *nb)
{
	return 0;
}

#endif /* CONFIG_MTK_SMI */

#endif /* __MTK_SMI_DEBUG_H */
