/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef ETA_TEST_ERRNO_H
#define ETA_TEST_ERRNO_H
#if __has_include_next(<linux/errno.h>)
#include_next <linux/errno.h>
#endif
#include <errno.h>
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
#ifndef ENODATA
#define ENODATA 61
#endif
#endif
