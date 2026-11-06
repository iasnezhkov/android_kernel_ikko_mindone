/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_HRTIMER_H
#define MINDONE_COMPAT_HRTIMER_H

#include <linux/hrtimer.h>
#include <linux/version.h>

static inline void mindone_hrtimer_setup(struct hrtimer *timer,
					 enum hrtimer_restart (*function)(struct hrtimer *),
					 clockid_t clock_id, enum hrtimer_mode mode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
	hrtimer_setup(timer, function, clock_id, mode);
#else
	hrtimer_init(timer, clock_id, mode);
	timer->function = function;
#endif
}

#endif
