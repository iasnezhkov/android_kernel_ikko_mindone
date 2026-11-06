/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MINDONE_COMPAT_TIMER_H
#define MINDONE_COMPAT_TIMER_H

#include <linux/timer.h>

#ifndef timer_container_of
#define timer_container_of(var, callback_timer, timer_fieldname) \
	from_timer(var, callback_timer, timer_fieldname)
#endif

#endif
