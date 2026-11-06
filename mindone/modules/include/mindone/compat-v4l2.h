/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_V4L2_H
#define MINDONE_COMPAT_V4L2_H

#include <linux/fs.h>
#include <linux/version.h>
#include <media/v4l2-fh.h>

static inline struct v4l2_fh *mindone_file_fh(struct file *file)
{
	return file->private_data;
}

static inline void mindone_v4l2_fh_add(struct v4l2_fh *fh, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	v4l2_fh_add(fh, file);
#else
	file->private_data = fh;
	v4l2_fh_add(fh);
#endif
}

static inline void mindone_v4l2_fh_del(struct v4l2_fh *fh, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	v4l2_fh_del(fh, file);
#else
	v4l2_fh_del(fh);
	file->private_data = NULL;
#endif
}

#endif
