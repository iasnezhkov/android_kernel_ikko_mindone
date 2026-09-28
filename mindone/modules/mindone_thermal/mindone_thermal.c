// SPDX-License-Identifier: GPL-2.0

#include <linux/cpu_cooling.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>



static int little_trip = 85000;
module_param(little_trip, int, 0644);
MODULE_PARM_DESC(little_trip, "passive trip for the little cluster, millidegrees C");
static int big_trip = 80000;
module_param(big_trip, int, 0644);
MODULE_PARM_DESC(big_trip, "passive trip for the big cluster, millidegrees C");
static int hyst = 5000;
module_param(hyst, int, 0644);
MODULE_PARM_DESC(hyst, "trip hysteresis, millidegrees C");
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
	struct thermal_cooling_device *cdev;
	char cdev_type[16];
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
	struct mindone_cluster *c = thermal_zone_device_priv(tz);
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

static int mindone_set_trip_temp(struct thermal_zone_device *tz,
				 const struct thermal_trip *trip, int temp)
{
	struct mindone_cluster *c = thermal_zone_device_priv(tz);

	c->trips[0].temperature = temp;
	*c->trip_param = temp;
	return 0;
}

static bool mindone_should_bind(struct thermal_zone_device *tz,
				const struct thermal_trip *trip,
				struct thermal_cooling_device *cdev,
				struct cooling_spec *spec)
{
	struct mindone_cluster *c = thermal_zone_device_priv(tz);

	if (!c || !cdev || strcmp(cdev->type, c->cdev_type))
		return false;
	spec->upper = THERMAL_NO_LIMIT;
	spec->lower = 0;
	spec->weight = THERMAL_WEIGHT_DEFAULT;
	return true;
}

static struct thermal_zone_device_ops mindone_tz_ops = {
	.get_temp = mindone_get_temp,
	.should_bind = mindone_should_bind,
	.set_trip_temp = mindone_set_trip_temp,
};

static void mindone_cluster_teardown(struct mindone_cluster *c)
{
	if (c->tz && c->cdev)
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
	tzp.governor_name = governor;

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
	c->trips[0].flags = THERMAL_TRIP_FLAG_RW;

	snprintf(c->cdev_type, sizeof(c->cdev_type), "cpufreq-cpu%d", c->first_cpu);
	c->cdev = NULL;

	c->tz = thermal_zone_device_register_with_trips(c->name, c->trips, 1,
							c, &mindone_tz_ops, &tzp,
							250, 1000);
	if (IS_ERR(c->tz)) {
		ret = PTR_ERR(c->tz);
		pr_err("mindone_thermal: zone %s: %d\n", c->name, ret);
		c->tz = NULL;
		goto err;
	}

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
	struct mindone_gpu *g = thermal_zone_device_priv(tz);
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

static int mindone_gpu_set_trip_temp(struct thermal_zone_device *tz,
				 const struct thermal_trip *trip, int temp)
{
	struct mindone_gpu *g = thermal_zone_device_priv(tz);

	g->trips[0].temperature = temp;
	gpu_trip = temp;
	return 0;
}

static bool mindone_gpu_should_bind(struct thermal_zone_device *tz,
				    const struct thermal_trip *trip,
				    struct thermal_cooling_device *cdev,
				    struct cooling_spec *spec)
{
	struct mindone_gpu *g = thermal_zone_device_priv(tz);

	if (!g || cdev != g->cdev)
		return false;
	spec->upper = THERMAL_NO_LIMIT;
	spec->lower = 0;
	spec->weight = THERMAL_WEIGHT_DEFAULT;
	return true;
}

static struct thermal_zone_device_ops mindone_gpu_tz_ops = {
	.get_temp = mindone_gpu_get_temp,
	.should_bind = mindone_gpu_should_bind,
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
	g->trips[0].flags = THERMAL_TRIP_FLAG_RW;

	g->cdev = thermal_cooling_device_register("mindone-gpufreq", g, &mindone_gpu_cdev_ops);
	if (IS_ERR(g->cdev)) {
		ret = PTR_ERR(g->cdev);
		g->cdev = NULL;
		goto err;
	}
	g->tz = thermal_zone_device_register_with_trips("mindone_gpu", g->trips, 1, g,
							&mindone_gpu_tz_ops, &tzp, 250, 1000);
	if (IS_ERR(g->tz)) {
		ret = PTR_ERR(g->tz);
		g->tz = NULL;
		goto err;
	}
	ret = 0;
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

static int skin_trip = 46000;
module_param(skin_trip, int, 0644);
MODULE_PARM_DESC(skin_trip, "passive trip for the skin (ap_ntc) proxy zone, millidegrees C");
static int skin_hyst = 1000;
module_param(skin_hyst, int, 0644);
MODULE_PARM_DESC(skin_hyst, "skin trip hysteresis, millidegrees C");

static const char * const mindone_skin_cdev_types[] = {
	"cpufreq-cpu0", "cpufreq-cpu6", "mindone-gpufreq",
};

struct mindone_skin {
	struct thermal_zone_device *source;
	struct thermal_zone_device *tz;
	struct thermal_trip trips[1];
};

static struct mindone_skin skin;

static int mindone_skin_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct mindone_skin *s = thermal_zone_device_priv(tz);

	if (!s->source)
		return -ENODEV;
	return thermal_zone_get_temp(s->source, temp);
}

static int mindone_skin_set_trip_temp(struct thermal_zone_device *tz,
				      const struct thermal_trip *trip, int temp)
{
	struct mindone_skin *s = thermal_zone_device_priv(tz);

	s->trips[0].temperature = temp;
	skin_trip = temp;
	return 0;
}

static bool mindone_skin_should_bind(struct thermal_zone_device *tz,
				     const struct thermal_trip *trip,
				     struct thermal_cooling_device *cdev,
				     struct cooling_spec *spec)
{
	int i;

	if (!cdev)
		return false;
	for (i = 0; i < ARRAY_SIZE(mindone_skin_cdev_types); i++)
		if (!strcmp(cdev->type, mindone_skin_cdev_types[i]))
			break;
	if (i == ARRAY_SIZE(mindone_skin_cdev_types))
		return false;
	spec->upper = THERMAL_NO_LIMIT;
	spec->lower = 0;
	spec->weight = THERMAL_WEIGHT_DEFAULT;
	return true;
}

static struct thermal_zone_device_ops mindone_skin_tz_ops = {
	.get_temp = mindone_skin_get_temp,
	.should_bind = mindone_skin_should_bind,
	.set_trip_temp = mindone_skin_set_trip_temp,
};

static void mindone_skin_teardown(struct mindone_skin *s)
{
	if (!IS_ERR_OR_NULL(s->tz))
		thermal_zone_device_unregister(s->tz);
	s->tz = NULL;
	s->source = NULL;
}

static int mindone_skin_setup(struct mindone_skin *s)
{
	struct thermal_zone_params tzp = {
		.no_hwmon = true,
		.governor_name = "step_wise",
	};
	int ret;

	s->source = thermal_zone_get_zone_by_name("ap_ntc");
	if (IS_ERR(s->source)) {
		ret = PTR_ERR(s->source);
		s->source = NULL;
		return ret;
	}

	s->trips[0].temperature = skin_trip;
	s->trips[0].hysteresis = skin_hyst;
	s->trips[0].type = THERMAL_TRIP_PASSIVE;
	s->trips[0].flags = THERMAL_TRIP_FLAG_RW;

	s->tz = thermal_zone_device_register_with_trips("mindone_skin", s->trips, 1,
							s, &mindone_skin_tz_ops, &tzp,
							250, 1000);
	if (IS_ERR(s->tz)) {
		ret = PTR_ERR(s->tz);
		s->tz = NULL;
		goto err;
	}

	ret = thermal_zone_device_enable(s->tz);
	if (ret)
		goto err;

	pr_info("mindone_thermal: mindone_skin: ap_ntc proxy, passive trip %d mC, cpufreq-cpu0/cpu6+mindone-gpufreq\n",
		s->trips[0].temperature);
	return 0;
err:
	pr_err("mindone_thermal: mindone_skin: %d\n", ret);
	mindone_skin_teardown(s);
	return ret;
}

#define MINDONE_SETUP_MAX_TRIES 20
static struct delayed_work mindone_setup_work;
static int mindone_setup_tries;

static void mindone_setup_work_fn(struct work_struct *w)
{
	int i, ret, pending = 0;

	for (i = 0; i < ARRAY_SIZE(clusters); i++) {
		if (clusters[i].tz)
			continue;
		ret = mindone_cluster_setup(&clusters[i]);
		if (ret == -EPROBE_DEFER || ret == -ENODEV)
			pending++;
		else if (ret)
			pr_warn("mindone_thermal: %s setup failed (%d), disabled\n",
				clusters[i].name, ret);
	}
	if (!gpu.tz) {
		ret = mindone_gpu_setup(&gpu);
		if (ret == -EPROBE_DEFER || ret == -ENODEV)
			pending++;
		else if (ret)
			pr_warn("mindone_thermal: GPU zone not registered (%d)\n", ret);
	}
	if (!skin.tz) {
		ret = mindone_skin_setup(&skin);
		if (ret == -EPROBE_DEFER || ret == -ENODEV)
			pending++;
		else if (ret)
			pr_warn("mindone_thermal: skin zone not registered (%d)\n", ret);
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
	mindone_skin_teardown(&skin);
	mindone_gpu_teardown(&gpu);
	for (i = ARRAY_SIZE(clusters) - 1; i >= 0; i--)
		mindone_cluster_teardown(&clusters[i]);
}

module_init(mindone_thermal_init);
module_exit(mindone_thermal_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iKKO MindOne: CPU/GPU/skin thermal zones sharing cpufreq and gpufreq cooling");
