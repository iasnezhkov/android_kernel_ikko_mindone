/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_PINCONF_H
#define MINDONE_COMPAT_PINCONF_H

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_PIN_CONFIG_LEVEL PIN_CONFIG_LEVEL
#else
#define MINDONE_PIN_CONFIG_LEVEL PIN_CONFIG_OUTPUT
#endif

#endif
