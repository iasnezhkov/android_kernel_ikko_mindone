/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_ASOC_H
#define MINDONE_COMPAT_ASOC_H

#include <linux/version.h>
#include <sound/soc.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_SND_SOC_COMPRESS_NUM_PARAM(name)
#define MINDONE_SND_SOC_NEW_COMPRESS(rtd, num) snd_soc_new_compress(rtd)
#else
#define MINDONE_SND_SOC_COMPRESS_NUM_PARAM(name) , int name
#define MINDONE_SND_SOC_NEW_COMPRESS(rtd, num) snd_soc_new_compress(rtd, num)
#endif

#endif
