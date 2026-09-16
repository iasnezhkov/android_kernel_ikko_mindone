/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-thermal.h — thermal pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_THERMAL_H__
#define __MINDONE_COMPAT_THERMAL_H__

#include <linux/version.h>
#include <linux/thermal.h>

/*
 * thermal_zone_device_ops::set_trip_temp() gets the trip by pointer since 6.7 (index before),
 * and the OF trip table accessor of_thermal_get_trip_points() is gone. Callbacks declare
 * their parameters with MINDONE_TZ_SET_TRIP_TEMP_PARAMS and read the type through
 * MINDONE_TZ_TRIP_TYPE().
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_TZ_SET_TRIP_TEMP_PARAMS	struct thermal_zone_device *tz, const struct thermal_trip *trip, int temp
#define MINDONE_TZ_TRIP_TYPE(_tz, _trip)	((_trip)->type)
#else
const struct thermal_trip *of_thermal_get_trip_points(struct thermal_zone_device *tz);
static inline enum thermal_trip_type mindone_tz_trip_type(struct thermal_zone_device *tz, int trip)
{
	const struct thermal_trip *trips = of_thermal_get_trip_points(tz);

	return trips ? trips[trip].type : THERMAL_TRIP_ACTIVE;
}
#define MINDONE_TZ_SET_TRIP_TEMP_PARAMS	struct thermal_zone_device *tz, int trip, int temp
#define MINDONE_TZ_TRIP_TYPE(_tz, _trip)	mindone_tz_trip_type((_tz), (_trip))
#endif

#endif /* __MINDONE_COMPAT_THERMAL_H__ */
