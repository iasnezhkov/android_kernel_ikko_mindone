/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-sound.h — ASoC pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_SOUND_H__
#define __MINDONE_COMPAT_SOUND_H__

#include <linux/version.h>
#include <sound/soc.h>

/* asoc_rtd_to_cpu()/asoc_rtd_to_codec() were renamed snd_soc_rtd_to_*() in 6.7. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_RTD_TO_CPU(rtd, n)	snd_soc_rtd_to_cpu((rtd), (n))
#define MINDONE_RTD_TO_CODEC(rtd, n)	snd_soc_rtd_to_codec((rtd), (n))
#else
#define MINDONE_RTD_TO_CPU(rtd, n)	asoc_rtd_to_cpu((rtd), (n))
#define MINDONE_RTD_TO_CODEC(rtd, n)	asoc_rtd_to_codec((rtd), (n))
#endif

/* snd_soc_dai_driver::compress_new moved into snd_soc_dai_ops in 6.7; use both forms. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_DAI_COMPRESS_NEW(fn)
#define MINDONE_DAI_OPS_COMPRESS_NEW(fn)	.compress_new = (fn),
#else
#define MINDONE_DAI_COMPRESS_NEW(fn)		.compress_new = (fn),
#define MINDONE_DAI_OPS_COMPRESS_NEW(fn)
#endif

/*
 * snd_soc_component_driver::copy_user() became copy() in 6.5 and takes an iov_iter
 * instead of a user pointer. Copy helpers keep one signature via mindone_snd_buf_t;
 * both copy macros are non-zero on failure, like copy_{from,to}_user().
 */
#include <linux/uaccess.h>
#include <linux/uio.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
typedef struct iov_iter *mindone_snd_buf_t;
#define MINDONE_SND_COPY_OP(fn)			.copy = (fn)
#define MINDONE_SND_COPY_FROM_USER(dst, buf, n)	(copy_from_iter((dst), (n), (buf)) != (n))
#define MINDONE_SND_COPY_TO_USER(buf, src, n)	(copy_to_iter((src), (n), (buf)) != (n))
#else
typedef void __user *mindone_snd_buf_t;
#define MINDONE_SND_COPY_OP(fn)			.copy_user = (fn)
#define MINDONE_SND_COPY_FROM_USER(dst, buf, n)	copy_from_user((dst), (buf), (n))
#define MINDONE_SND_COPY_TO_USER(buf, src, n)	copy_to_user((buf), (src), (n))
#endif

/*
 * Ring-buffer helpers copy the user buffer in consecutive chunks. An iov_iter
 * advances itself, so "buf + offset" is just the iterator on 6.5+; only valid for
 * strictly sequential chunk copies (all callers here).
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
#define MINDONE_SND_BUF_AT(buf, off)		(buf)
#else
#define MINDONE_SND_BUF_AT(buf, off)		((buf) + (off))
#endif

/* asoc_substream_to_rtd() was renamed snd_soc_substream_to_rtd() in 6.7. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_SUBSTREAM_TO_RTD(_s)	snd_soc_substream_to_rtd(_s)
#else
#define MINDONE_SUBSTREAM_TO_RTD(_s)	asoc_substream_to_rtd(_s)
#endif

#endif /* __MINDONE_COMPAT_SOUND_H__ */
