/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */
#ifndef __MTK_PLATFORM_UTILS_H__
#define __MTK_PLATFORM_UTILS_H__

#if IS_ENABLED(CONFIG_MALI_MTK_PREVENT_PRINTK_TOO_MUCH)
#undef dev_dbg
#define dev_dbg dev_vdbg
#endif

#endif

