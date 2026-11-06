/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_FB_H
#define MINDONE_COMPAT_FB_H

#include <linux/fb.h>
#include <linux/limits.h>

#ifdef FB_EVENT_BLANK
#define MINDONE_FB_EVENT_BLANK FB_EVENT_BLANK
#else
#define MINDONE_FB_EVENT_BLANK ULONG_MAX
#endif

#endif
