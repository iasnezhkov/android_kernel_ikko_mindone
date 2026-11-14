/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_GPIO_H
#define MINDONE_COMPAT_GPIO_H

#include <linux/gpio/driver.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_GPIO_CHIP_SET(fn, void_fn) (fn)
#else
#define MINDONE_GPIO_CHIP_SET(fn, void_fn) (void_fn)
#endif

#endif
