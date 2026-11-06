/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JD_DEBUGFS_H
#define _KBASE_JD_DEBUGFS_H

#include <linux/debugfs.h>

#define MALI_JD_DEBUGFS_VERSION 3

struct kbase_context;

void kbasep_jd_debugfs_ctx_init(struct kbase_context *kctx);

#endif
