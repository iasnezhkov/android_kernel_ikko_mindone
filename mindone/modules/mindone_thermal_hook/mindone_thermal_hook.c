// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/thermal.h>
#include <linux/tracepoint.h>
#include <trace/hooks/thermal.h>

static void mindone_thermal_trip_flag_hook(void *data, struct thermal_trip *trip)
{
	trip->flags = THERMAL_TRIP_FLAG_RW;
}

static int __init mindone_thermal_hook_init(void)
{
	int ret;

	ret = register_trace_android_vh_update_thermal_trip_flag(
			mindone_thermal_trip_flag_hook, NULL);
	if (ret)
		pr_err("mindone_thermal_hook: register_trace_android_vh_update_thermal_trip_flag failed: %d\n",
		       ret);
	return ret;
}

static void __exit mindone_thermal_hook_exit(void)
{
	unregister_trace_android_vh_update_thermal_trip_flag(
			mindone_thermal_trip_flag_hook, NULL);
	tracepoint_synchronize_unregister();
}

module_init(mindone_thermal_hook_init);
module_exit(mindone_thermal_hook_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iKKO MindOne: restore RW hysteresis on DT thermal trips");
