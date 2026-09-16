// SPDX-License-Identifier: GPL-2.0
/*
 * mindone_thermal - CPU thermal throttling for the MT6789 on 6.1.
 *
 * The device tree leaves the LVTS zones without cooling maps or #cooling-cells, so the
 * thermal core cannot throttle at all; the vendor left that to a userspace daemon this
 * kernel does not have. One zone per cluster (max of its LVTS zones) with a passive trip,
 * bound to cpufreq cooling. Trips are live-tunable parameters. Why: F3573 in the fact log.
 */
#include <linux/cpu_cooling.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#include <linux/version.h>

/* MINDONE: since 6.4 the thermal zone struct is opaque and private data comes from an
 * accessor; on 6.1 the fields are still reachable directly, so keep the old path. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define MINDONE_TZ_PRIV(tz)	thermal_zone_device_priv(tz)
#else
#define MINDONE_TZ_PRIV(tz)	((tz)->devdata)
#endif

/* MINDONE: since 6.8 the set-trip op receives a pointer to the trip itself instead of its
 * index. We have one zone with one trip, so there is nothing to validate on the new
 * kernel: the core only calls the op for an existing trip. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define MINDONE_SET_TRIP_ARGS	struct thermal_zone_device *tz, const struct thermal_trip *trip, int temp
#define MINDONE_TRIP_BAD	(0)
#else
#define MINDONE_SET_TRIP_ARGS	struct thermal_zone_device *tz, int trip, int temp
#define MINDONE_TRIP_BAD	(trip != 0)
#endif


static int little_trip = 85000;
module_param(little_trip, int, 0644);
MODULE_PARM_DESC(little_trip, "passive trip for the little cluster, millidegrees C");
static int big_trip = 80000;
module_param(big_trip, int, 0644);
MODULE_PARM_DESC(big_trip, "passive trip for the big cluster, millidegrees C");
static int hyst = 5000;
module_param(hyst, int, 0644);
MODULE_PARM_DESC(hyst, "trip hysteresis, millidegrees C");
/* MINDONE (12.09 night, 6.18 review C4): thermal governor for the CPU cluster zones. Default keeps
 * step_wise; "power_allocator" (IPA, compiled in) distributes a power budget over the cpufreq
 * cooler through a PID loop instead of stepping states - the cpufreq cooler exposes the EM-based
 * power ops IPA needs. The GPU zone stays step_wise: its cooler has no power ops. Opt-in until a
 * sustained-load rig confirms the tuning (sustainable_power_mw=0 lets IPA estimate it). */
static char *governor = "step_wise";
module_param(governor, charp, 0444);
MODULE_PARM_DESC(governor, "thermal governor for the CPU zones: step_wise (default) or power_allocator");
static int sustainable_power_mw;
module_param(sustainable_power_mw, int, 0444);
MODULE_PARM_DESC(sustainable_power_mw, "power_allocator sustainable power per CPU zone, mW (0 = estimate)");

#define NSENS 4

struct mindone_cluster {
	const char *name;
	const char *sensors[NSENS];
	int first_cpu;
	int *trip_param;
	struct thermal_zone_device *sources[NSENS];
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;	/* < 6.11 only: our own cpufreq cooler */
	char cdev_type[16];			/* >= 6.11: type of the kernel's cooler to bind */
	struct cpufreq_policy *policy;
	struct thermal_trip trips[1];
};

static struct mindone_cluster clusters[] = {
	{
		.name = "mindone_cpu_little",
		.sensors = { "cpu_little1", "cpu_little2", "cpu_little3", "cpu_little4" },
		.first_cpu = 0,
		.trip_param = &little_trip,
	},
	{
		.name = "mindone_cpu_big",
		.sensors = { "cpu_big1", "cpu_big2", "cpu_big3", "cpu_big4" },
		.first_cpu = 6,
		.trip_param = &big_trip,
	},
};

static int mindone_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);
	int i, t, max = INT_MIN, ret = -ENODEV;

	for (i = 0; i < NSENS; i++) {
		if (!c->sources[i])
			continue;
		if (thermal_zone_get_temp(c->sources[i], &t))
			continue;
		if (t > max)
			max = t;
		ret = 0;
	}
	if (!ret)
		*temp = max;
	return ret;
}

/* 6.1 still insists on the trip callbacks even when the trips array is passed. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
static int mindone_get_trip_type(struct thermal_zone_device *tz, int trip,
				 enum thermal_trip_type *type)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*type = c->trips[0].type;
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
static int mindone_get_trip_temp(struct thermal_zone_device *tz, int trip, int *temp)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*temp = c->trips[0].temperature;
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
static int mindone_get_trip_hyst(struct thermal_zone_device *tz, int trip, int *h)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*h = c->trips[0].hysteresis;
	return 0;
}
#endif

static int mindone_set_trip_temp(MINDONE_SET_TRIP_ARGS)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	c->trips[0].temperature = temp;
	*c->trip_param = temp;
	return 0;
}

/*
 * MINDONE (12.09 night): since 6.11 a driver binds a cooling device to a trip through the
 * .should_bind() zone callback - the core calls it for every (zone, trip, cdev) pair when
 * the zone or the cdev is registered. The earlier "ret = 0 on >= 6.8" left BOTH the CPU and
 * the GPU zones unbound, so the passive trips never throttled (rig: mindone_cpu_big at 56-60 C
 * with the trip lowered to 50 C kept cpufreq-cpu6 at state 0/15 and 2.2 GHz for 90 s). The
 * cdev is therefore registered BEFORE the zone, so it is known when the core asks.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static bool mindone_should_bind(struct thermal_zone_device *tz,
				const struct thermal_trip *trip,
				struct thermal_cooling_device *cdev,
				struct cooling_spec *spec)
{
	struct mindone_cluster *c = MINDONE_TZ_PRIV(tz);

	/* Bind the cpufreq cooler the kernel already registered for this policy from DT
	 * (#cooling-cells -> "cpufreq-cpuN"); registering a second one made two identical
	 * coolers per cluster (F4264). */
	if (!c || !cdev || strcmp(cdev->type, c->cdev_type))
		return false;
	spec->upper = THERMAL_NO_LIMIT;
	spec->lower = 0;
	spec->weight = THERMAL_WEIGHT_DEFAULT;
	return true;
}
#endif

static struct thermal_zone_device_ops mindone_tz_ops = {
	.get_temp = mindone_get_temp,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.should_bind = mindone_should_bind,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
	.get_trip_type = mindone_get_trip_type,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
	.get_trip_temp = mindone_get_trip_temp,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	.get_trip_hyst = mindone_get_trip_hyst,
#endif
	.set_trip_temp = mindone_set_trip_temp,
};

static void mindone_cluster_teardown(struct mindone_cluster *c)
{
	if (c->tz && c->cdev)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		thermal_zone_unbind_cooling_device(c->tz, 0, c->cdev);
#else
	/* MINDONE: since 6.8 binding a cooling device to a trip is done by the device tree
	 * (cooling-maps); there is no driver API for it. The zone registers and reports
	 * temperature; automatic cooling on 6.12 needs the binding described in the DT
	 * (separate task). */
#endif
	if (!IS_ERR_OR_NULL(c->tz))
		thermal_zone_device_unregister(c->tz);
	c->tz = NULL;
	if (!IS_ERR_OR_NULL(c->cdev))
		cpufreq_cooling_unregister(c->cdev);
	c->cdev = NULL;
	if (c->policy)
		cpufreq_cpu_put(c->policy);
	c->policy = NULL;
}

static int mindone_cluster_setup(struct mindone_cluster *c)
{
	struct thermal_zone_params tzp = {
		.no_hwmon = true,
		.sustainable_power = sustainable_power_mw,
	};
	int i, found = 0, ret;

	if (strcmp(governor, "power_allocator") && strcmp(governor, "step_wise")) {
		pr_warn("mindone_thermal: unknown governor '%s', using step_wise\n", governor);
		governor = "step_wise";
	}
	tzp.governor_name = governor;	/* const char * in struct thermal_zone_params */

	for (i = 0; i < NSENS; i++) {
		struct thermal_zone_device *s =
			thermal_zone_get_zone_by_name(c->sensors[i]);

		if (IS_ERR(s)) {
			pr_warn("mindone_thermal: zone %s not found (%ld)\n",
				c->sensors[i], PTR_ERR(s));
			c->sources[i] = NULL;
			continue;
		}
		c->sources[i] = s;
		found++;
	}
	if (!found)
		return -ENODEV;

	c->policy = cpufreq_cpu_get(c->first_cpu);
	if (!c->policy) {
		pr_warn("mindone_thermal: no cpufreq policy for cpu%d\n", c->first_cpu);
		return -EPROBE_DEFER;
	}

	c->trips[0].temperature = *c->trip_param;
	c->trips[0].hysteresis = hyst;
	c->trips[0].type = THERMAL_TRIP_PASSIVE;

	snprintf(c->cdev_type, sizeof(c->cdev_type), "cpufreq-cpu%d", c->first_cpu);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	c->cdev = cpufreq_cooling_register(c->policy);
	if (IS_ERR(c->cdev)) {
		ret = PTR_ERR(c->cdev);
		pr_err("mindone_thermal: cooling device for cpu%d: %d\n", c->first_cpu, ret);
		c->cdev = NULL;
		goto err;
	}
#else
	/* >= 6.11: the cpufreq driver already registered "cpufreq-cpuN" from DT #cooling-cells;
	 * .should_bind() binds our trip to it at zone registration - no duplicate cooler. */
	c->cdev = NULL;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	/* MINDONE: since 6.8 the trip mask is gone from the signature: eight args, not nine */
	c->tz = thermal_zone_device_register_with_trips(c->name, c->trips, 1,
							c, &mindone_tz_ops, &tzp,
							250, 1000);
#else
	c->tz = thermal_zone_device_register_with_trips(c->name, c->trips, 1, 0,
							c, &mindone_tz_ops, &tzp,
							250, 1000);
#endif
	if (IS_ERR(c->tz)) {
		ret = PTR_ERR(c->tz);
		pr_err("mindone_thermal: zone %s: %d\n", c->name, ret);
		c->tz = NULL;
		goto err;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	ret = thermal_zone_bind_cooling_device(c->tz, 0, c->cdev, THERMAL_NO_LIMIT,
					       THERMAL_NO_LIMIT, THERMAL_WEIGHT_DEFAULT);
	if (ret) {
		pr_err("mindone_thermal: bind %s: %d\n", c->name, ret);
		goto err;
	}
#endif	/* >= 6.11: bound by the core via .should_bind() above */

	ret = thermal_zone_device_enable(c->tz);
	if (ret) {
		pr_err("mindone_thermal: enable %s: %d\n", c->name, ret);
		goto err;
	}

	pr_info("mindone_thermal: %s: %d sensors, cpufreq cooling %s, passive trip %d mC, governor %s\n",
		c->name, found, c->cdev_type, c->trips[0].temperature, tzp.governor_name);
	return 0;
err:
	mindone_cluster_teardown(c);
	return ret;
}

/*
 * GPU: the LVTS gpu1/gpu2 zones have no cooling map either, and the GPU DVFS (gpufreq/GED) only
 * throttles through its "limiter" API, which the vendor drove from userspace. A cooling device
 * with GPU_STATES steps maps state s to an OPP ceiling index (0 = 1.1 GHz ... 44 = 390 MHz) via
 * gpufreq_set_limit(TARGET_GPU, LIMIT_THERMAL_AP, ceiling_khz, keep-floor); state 0 resets the limit.
 */
#include <gpufreq_v2.h>

static int gpu_trip = 85000;
module_param(gpu_trip, int, 0644);
MODULE_PARM_DESC(gpu_trip, "passive trip for the GPU, millidegrees C");

#define GPU_STATES 8

struct mindone_gpu {
	struct thermal_zone_device *sources[2];
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;
	struct thermal_trip trips[1];
	int opp_num;
	unsigned long state;
};

static struct mindone_gpu gpu;

static int mindone_gpu_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct mindone_gpu *g = MINDONE_TZ_PRIV(tz);
	int i, t, max = INT_MIN, ret = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(g->sources); i++) {
		if (!g->sources[i] || thermal_zone_get_temp(g->sources[i], &t))
			continue;
		if (t > max)
			max = t;
		ret = 0;
	}
	if (!ret)
		*temp = max;
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
static int mindone_gpu_get_trip_type(struct thermal_zone_device *tz, int trip,
				     enum thermal_trip_type *type)
{
	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*type = THERMAL_TRIP_PASSIVE;
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
static int mindone_gpu_get_trip_temp(struct thermal_zone_device *tz, int trip, int *temp)
{
	struct mindone_gpu *g = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*temp = g->trips[0].temperature;
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
static int mindone_gpu_get_trip_hyst(struct thermal_zone_device *tz, int trip, int *h)
{
	struct mindone_gpu *g = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	*h = g->trips[0].hysteresis;
	return 0;
}
#endif

static int mindone_gpu_set_trip_temp(MINDONE_SET_TRIP_ARGS)
{
	struct mindone_gpu *g = MINDONE_TZ_PRIV(tz);

	if (MINDONE_TRIP_BAD)
		return -EINVAL;
	g->trips[0].temperature = temp;
	gpu_trip = temp;
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static bool mindone_gpu_should_bind(struct thermal_zone_device *tz,
				    const struct thermal_trip *trip,
				    struct thermal_cooling_device *cdev,
				    struct cooling_spec *spec)
{
	struct mindone_gpu *g = MINDONE_TZ_PRIV(tz);

	if (!g || cdev != g->cdev)
		return false;
	spec->upper = THERMAL_NO_LIMIT;
	spec->lower = 0;
	spec->weight = THERMAL_WEIGHT_DEFAULT;
	return true;
}
#endif

static struct thermal_zone_device_ops mindone_gpu_tz_ops = {
	.get_temp = mindone_gpu_get_temp,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.should_bind = mindone_gpu_should_bind,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
	.get_trip_type = mindone_gpu_get_trip_type,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* MINDONE: since 6.8 these ops are gone from the interface; the core reads the trips
 * from the thermal_trip array we pass at zone registration. */
	.get_trip_temp = mindone_gpu_get_trip_temp,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	.get_trip_hyst = mindone_gpu_get_trip_hyst,
#endif
	.set_trip_temp = mindone_gpu_set_trip_temp,
};

static int mindone_gpu_cdev_get_max(struct thermal_cooling_device *cdev, unsigned long *st)
{
	*st = GPU_STATES;
	return 0;
}

static int mindone_gpu_cdev_get_cur(struct thermal_cooling_device *cdev, unsigned long *st)
{
	struct mindone_gpu *g = cdev->devdata;

	*st = g->state;
	return 0;
}

static int mindone_gpu_cdev_set_cur(struct thermal_cooling_device *cdev, unsigned long st)
{
	struct mindone_gpu *g = cdev->devdata;
	int ceiling, ret;

	if (st > GPU_STATES)
		return -EINVAL;
	if (st == g->state)
		return 0;
	/*
	 * state s -> ceiling OPP index s * (opp_num - 1) / GPU_STATES; LIMIT_THERMAL_AP takes the
	 * ceiling as a FREQUENCY in kHz (gpuppm.c __gpuppm_convert_limit_to_idx), <= 0 resets.
	 */
	ceiling = st ? gpufreq_get_freq_by_idx(TARGET_GPU, (int)(st * (g->opp_num - 1) / GPU_STATES)) : 0;
	ret = gpufreq_set_limit(TARGET_GPU, LIMIT_THERMAL_AP, ceiling, GPUPPM_KEEP_IDX);
	if (ret) {
		pr_err("mindone_thermal: gpufreq_set_limit(ceiling %d): %d\n", ceiling, ret);
		return ret;
	}
	g->state = st;
	return 0;
}

static const struct thermal_cooling_device_ops mindone_gpu_cdev_ops = {
	.get_max_state = mindone_gpu_cdev_get_max,
	.get_cur_state = mindone_gpu_cdev_get_cur,
	.set_cur_state = mindone_gpu_cdev_set_cur,
};

static void mindone_gpu_teardown(struct mindone_gpu *g)
{
	if (g->tz && g->cdev)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		thermal_zone_unbind_cooling_device(g->tz, 0, g->cdev);
#else
	/* MINDONE: since 6.8 binding a cooling device to a trip is done by the device tree
	 * (cooling-maps); there is no driver API for it. The zone registers and reports
	 * temperature; automatic cooling on 6.12 needs the binding described in the DT
	 * (separate task). */
#endif
	if (!IS_ERR_OR_NULL(g->tz))
		thermal_zone_device_unregister(g->tz);
	g->tz = NULL;
	if (!IS_ERR_OR_NULL(g->cdev)) {
		if (g->state)
			gpufreq_set_limit(TARGET_GPU, LIMIT_THERMAL_AP, 0, GPUPPM_KEEP_IDX);
		thermal_cooling_device_unregister(g->cdev);
	}
	g->cdev = NULL;
}

static int mindone_gpu_setup(struct mindone_gpu *g)
{
	static const char * const names[2] = { "gpu1", "gpu2" };
	struct thermal_zone_params tzp = {
		.governor_name = "step_wise",
		.no_hwmon = true,
	};
	int i, found = 0, ret;

	for (i = 0; i < 2; i++) {
		struct thermal_zone_device *s = thermal_zone_get_zone_by_name(names[i]);

		if (IS_ERR(s)) {
			pr_warn("mindone_thermal: zone %s not found (%ld)\n", names[i], PTR_ERR(s));
			g->sources[i] = NULL;
			continue;
		}
		g->sources[i] = s;
		found++;
	}
	if (!found)
		return -ENODEV;

	g->opp_num = gpufreq_get_opp_num(TARGET_GPU);
	if (g->opp_num <= 1) {
		pr_warn("mindone_thermal: gpufreq opp num %d, no GPU cooling\n", g->opp_num);
		return -ENODEV;
	}

	g->trips[0].temperature = gpu_trip;
	g->trips[0].hysteresis = hyst;
	g->trips[0].type = THERMAL_TRIP_PASSIVE;

	/* cdev first: on >= 6.11 the core binds through .should_bind() at zone registration. */
	g->cdev = thermal_cooling_device_register("mindone-gpufreq", g, &mindone_gpu_cdev_ops);
	if (IS_ERR(g->cdev)) {
		ret = PTR_ERR(g->cdev);
		g->cdev = NULL;
		goto err;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	g->tz = thermal_zone_device_register_with_trips("mindone_gpu", g->trips, 1, g,
							&mindone_gpu_tz_ops, &tzp, 250, 1000);
#else
	g->tz = thermal_zone_device_register_with_trips("mindone_gpu", g->trips, 1, 0, g,
							&mindone_gpu_tz_ops, &tzp, 250, 1000);
#endif
	if (IS_ERR(g->tz)) {
		ret = PTR_ERR(g->tz);
		g->tz = NULL;
		goto err;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	ret = thermal_zone_bind_cooling_device(g->tz, 0, g->cdev, THERMAL_NO_LIMIT,
					       THERMAL_NO_LIMIT, THERMAL_WEIGHT_DEFAULT);
#else
	/* MINDONE: since 6.8 binding a cooling device to a trip is done by the device tree
	 * (cooling-maps); there is no driver API for it. The zone registers and reports
	 * temperature; automatic cooling on 6.12 needs the binding described in the DT
	 * (separate task). */
	ret = 0;
#endif
	if (ret)
		goto err;
	ret = thermal_zone_device_enable(g->tz);
	if (ret)
		goto err;
	pr_info("mindone_thermal: mindone_gpu: %d sensors, %d OPPs, %d cooling states, passive trip %d mC\n",
		found, g->opp_num, GPU_STATES, g->trips[0].temperature);
	return 0;
err:
	pr_err("mindone_thermal: mindone_gpu: %d\n", ret);
	mindone_gpu_teardown(g);
	return ret;
}

/*
 * MINDONE (F: B27 panic 12.09): module_init MUST NOT fail. Android init treats any
 * modules.load insmod failure as fatal ("Failed to load kernel modules" -> panic,
 * exitcode 0x7f00). The CPU coolers depend on cpufreq policies and the LVTS sensor
 * zones, neither guaranteed up when this module loads (B27 died with "no cpufreq
 * policy for cpu0" -> -EPROBE_DEFER(517) -> insmod fail -> init panic). So init always
 * returns 0 and does all setup from a retrying delayed work: register whatever is
 * ready, retry the rest (cpufreq policy / sensor zones / gpufreq) until they appear or
 * a cap is hit. An already-set-up cluster/GPU (tz != NULL) is skipped -> no
 * double-register.
 */
#define MINDONE_SETUP_MAX_TRIES 20
static struct delayed_work mindone_setup_work;
static int mindone_setup_tries;

static void mindone_setup_work_fn(struct work_struct *w)
{
	int i, ret, pending = 0;

	for (i = 0; i < ARRAY_SIZE(clusters); i++) {
		if (clusters[i].tz)		/* already registered */
			continue;
		ret = mindone_cluster_setup(&clusters[i]);
		if (ret == -EPROBE_DEFER || ret == -ENODEV)
			pending++;		/* cpufreq/sensors not ready yet */
		else if (ret)
			pr_warn("mindone_thermal: %s setup failed (%d), disabled\n",
				clusters[i].name, ret);
	}
	if (!gpu.tz) {
		ret = mindone_gpu_setup(&gpu);
		if (ret == -EPROBE_DEFER || ret == -ENODEV)
			pending++;		/* gpufreq/gpu sensors not ready yet */
		else if (ret)
			pr_warn("mindone_thermal: GPU zone not registered (%d)\n", ret);
	}
	if (pending && ++mindone_setup_tries < MINDONE_SETUP_MAX_TRIES)
		schedule_delayed_work(&mindone_setup_work, msecs_to_jiffies(2000));
	else if (pending)
		pr_warn("mindone_thermal: gave up after %d tries; some zones disabled\n",
			mindone_setup_tries);
}

static int __init mindone_thermal_init(void)
{
	INIT_DELAYED_WORK(&mindone_setup_work, mindone_setup_work_fn);
	schedule_delayed_work(&mindone_setup_work, 0);
	return 0;
}

static void __exit mindone_thermal_exit(void)
{
	int i;

	cancel_delayed_work_sync(&mindone_setup_work);
	mindone_gpu_teardown(&gpu);
	for (i = ARRAY_SIZE(clusters) - 1; i >= 0; i--)
		mindone_cluster_teardown(&clusters[i]);
}

module_init(mindone_thermal_init);
module_exit(mindone_thermal_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iKKO MindOne: CPU cluster thermal zones with cpufreq cooling");
