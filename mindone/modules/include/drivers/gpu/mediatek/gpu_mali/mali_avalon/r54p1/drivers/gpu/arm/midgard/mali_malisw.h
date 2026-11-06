/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALISW_H_
#define _MALISW_H_

#include <linux/version.h>

#if (KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE)

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif

#define CSTD_UNUSED(x) ((void)(x))

#define CSTD_NOP(...) ((void)#__VA_ARGS__)

#define CSTD_STR1(x) #x

#define CSTD_STR2(x) CSTD_STR1(x)

#ifndef fallthrough
#define fallthrough __fallthrough
#endif

#ifndef __fallthrough
#define __fallthrough __attribute__((fallthrough))
#endif

#endif
