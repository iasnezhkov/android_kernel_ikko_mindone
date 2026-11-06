/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_BLK_CRYPTO_H
#define MINDONE_COMPAT_BLK_CRYPTO_H

#include <linux/blk-crypto.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_BLK_CRYPTO_KEY_BYTES(key) ((key)->bytes)
#define MINDONE_BLK_CRYPTO_KEY_TYPE_RAW BLK_CRYPTO_KEY_TYPE_RAW
#else
#define MINDONE_BLK_CRYPTO_KEY_BYTES(key) ((key)->raw)
#define MINDONE_BLK_CRYPTO_KEY_TYPE_RAW BLK_CRYPTO_KEY_TYPE_STANDARD
#endif

#endif
