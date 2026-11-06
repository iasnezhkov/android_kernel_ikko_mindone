/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MINDONE_ALARMTIMER_COMPAT_H
#define _MINDONE_ALARMTIMER_COMPAT_H

#include <linux/alarmtimer.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define MINDONE_ALARM_CALLBACK_RET void
#define MINDONE_ALARM_NORESTART
#else
#define MINDONE_ALARM_CALLBACK_RET enum alarmtimer_restart
#define MINDONE_ALARM_NORESTART ALARMTIMER_NORESTART
#endif

#endif
