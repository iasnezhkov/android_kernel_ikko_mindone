/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_MMC_H
#define MINDONE_COMPAT_MMC_H

#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/version.h>

static inline bool mindone_mmc_can_gpio_cd(struct mmc_host *host)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return mmc_host_can_gpio_cd(host);
#else
	return mmc_can_gpio_cd(host);
#endif
}

#endif
