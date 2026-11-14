// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
void test_provider_config(unsigned int segment, unsigned int gpueb, unsigned int bringup);
int test_probe(struct platform_device *pdev);
void test_remove(struct platform_device *pdev);
void test_power(int on);
int test_cur_oppidx(void);
int test_commits(void);
int test_opp_num(void);
int test_signed_opp_num(void);
unsigned int test_working_freq(int idx);
unsigned int test_signed_freq(int idx);
int test_segment_upbound(void);
int test_cooling_live(void);
void test_wrapper_eb(int eb, int error);
void test_wrapper_drop_gpuppm(int drop);

enum { SEGMENT_MT6789 = 1, SEGMENT_MT6789T = 2 };

static char events[256][32];
static int events_count;
static struct device_node gpufreq_node = { "gpufreq" };
static struct platform_device pdev = { { &gpufreq_node } };
static struct thermal_cooling_device cdev;
static int cdev_live, register_calls, unregister_calls;
static long register_error;

void event(const char *name)
{
	if (!strcmp(name, "log") || !strcmp(name, "commit"))
		return;
	assert(events_count < 256);
	snprintf(events[events_count++], sizeof(events[0]), "%s", name);
}

static int position(const char *name)
{
	for (int i = 0; i < events_count; i++)
		if (!strcmp(events[i], name))
			return i;
	return -1;
}

struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node *np, const char *type,
	void *devdata, const struct thermal_cooling_device_ops *ops)
{
	unsigned long state = ULONG_MAX;

	event("cooling_register");
	register_calls++;
	if (register_error)
		return ERR_PTR(register_error);
	assert(!cdev_live);
	assert(ops && ops->get_max_state && ops->get_cur_state && ops->set_cur_state);
	cdev = (struct thermal_cooling_device){ type, devdata, np, ops, 0 };
	if (ops->get_max_state(&cdev, &cdev.max_state))
		return ERR_PTR(-EINVAL);
	if (ops->get_cur_state(&cdev, &state))
		return ERR_PTR(-EINVAL);
	assert(state == 0);
	cdev_live = 1;
	return &cdev;
}

void thermal_cooling_device_unregister(struct thermal_cooling_device *device)
{
	assert(cdev_live && device == &cdev);
	event("cooling_unregister");
	unregister_calls++;
	cdev_live = 0;
}

static int set_state(unsigned long state)
{
	assert(cdev_live);
	return cdev.ops->set_cur_state(&cdev, state);
}

static unsigned long get_state(void)
{
	unsigned long state = ULONG_MAX;

	assert(cdev_live);
	assert(cdev.ops->get_cur_state(&cdev, &state) == 0);
	return state;
}

static int row(void)
{
	return gpuppm_get_limit_table(TARGET_GPU)[LIMIT_THERMAL_AP].ceiling;
}

static struct gpufreq_debug_limit_info limits(void)
{
	return gpuppm_get_debug_limit_info(TARGET_GPU);
}

static void boot(unsigned int segment)
{
	test_provider_config(segment, 0, 0);
	assert(test_probe(&pdev) == 0);
	assert(register_calls == 1 && cdev_live && test_cooling_live());
}

static void ged(int oppidx)
{
	assert(gpufreq_commit(TARGET_DEFAULT, oppidx) == 0);
}

static void registration(unsigned int segment, int upbound)
{
	boot(segment);
	assert(test_segment_upbound() == upbound);
	assert(test_opp_num() == test_signed_opp_num() - upbound);
	assert(!strcmp(cdev.type, "gpufreq") && cdev.np == &gpufreq_node && cdev.devdata == NULL);
	assert(cdev.max_state == (unsigned long)(test_opp_num() - 1));
	assert(get_state() == 0 && row() == GPUPPM_DEFAULT_IDX);
	assert(position("gpuppm_fp") >= 0 && position("gpuppm_fp") < position("cooling_register"));
	assert(limits().ceiling == 0 && limits().c_limiter == LIMIT_SEGMENT);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	const char *name = argv[1];

	if (!strcmp(name, "registration-mt6789")) {
		registration(SEGMENT_MT6789, 7);
		assert(cdev.max_state == 37);
	} else if (!strcmp(name, "registration-mt6789t")) {
		registration(SEGMENT_MT6789T, 0);
		assert(cdev.max_state == 44);
	} else if (!strcmp(name, "table-shape")) {
		boot(SEGMENT_MT6789);
		for (int i = 0; i < test_opp_num(); i++) {
			assert(test_working_freq(i) == test_signed_freq(i + test_segment_upbound()));
			assert(i == 0 || test_working_freq(i) < test_working_freq(i - 1));
			assert(__gpufreq_get_idx_by_fgpu(test_working_freq(i)) == i);
			assert(__gpufreq_get_fgpu_by_idx(i) == test_working_freq(i));
		}
		assert(__gpufreq_get_fgpu_by_idx(test_opp_num()) == 0);
	} else if (!strcmp(name, "every-state")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		unsigned long max = cdev.max_state;
		for (unsigned long s = 0; s <= max; s++) {
			assert(set_state(s) == 0);
			assert(get_state() == s);
			assert(row() == (s ? (int)s : GPUPPM_DEFAULT_IDX));
			assert(limits().ceiling == (int)s);
			assert(limits().c_limiter == (s ? LIMIT_THERMAL_AP : LIMIT_SEGMENT));
			ged(0);
			assert(test_cur_oppidx() == (int)s);
			assert(test_working_freq(test_cur_oppidx()) == __gpufreq_get_fgpu_by_idx((int)s));
			ged((int)max);
			assert(test_cur_oppidx() == (int)max);
		}
	} else if (!strcmp(name, "immediate-throttle")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		ged(0);
		int commits = test_commits();
		assert(set_state(10) == 0);
		assert(test_commits() == commits + 1 && test_cur_oppidx() == 10);
		assert(set_state(5) == 0);
		assert(test_commits() == commits + 1 && test_cur_oppidx() == 10);
		ged(0);
		assert(test_cur_oppidx() == 5);
	} else if (!strcmp(name, "recovery")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		assert(set_state(20) == 0);
		ged(0);
		assert(test_cur_oppidx() == 20);
		assert(set_state(0) == 0);
		assert(row() == GPUPPM_DEFAULT_IDX && limits().ceiling == 0 && limits().c_limiter == LIMIT_SEGMENT);
		ged(0);
		assert(test_cur_oppidx() == 0);
	} else if (!strcmp(name, "out-of-range")) {
		boot(SEGMENT_MT6789);
		assert(set_state(5) == 0);
		assert(set_state(cdev.max_state + 1) == -EINVAL);
		assert(set_state(ULONG_MAX) == -EINVAL);
		assert(get_state() == 5 && row() == 5);
	} else if (!strcmp(name, "composition")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		assert(gpufreq_set_limit(TARGET_DEFAULT, LIMIT_POWERHAL, 15, GPUPPM_KEEP_IDX) == 0);
		assert(set_state(10) == 0);
		assert(limits().ceiling == 15 && limits().c_limiter == LIMIT_POWERHAL);
		assert(set_state(20) == 0);
		assert(limits().ceiling == 20 && limits().c_limiter == LIMIT_THERMAL_AP);
		ged(0);
		assert(test_cur_oppidx() == 20);
		assert(set_state(0) == 0);
		assert(limits().ceiling == 15 && limits().c_limiter == LIMIT_POWERHAL);
		assert(gpufreq_set_limit(TARGET_DEFAULT, LIMIT_POWERHAL, GPUPPM_DEFAULT_IDX, GPUPPM_KEEP_IDX) == 0);
		assert(limits().ceiling == 0);
	} else if (!strcmp(name, "boost-floor")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		assert(gpufreq_set_limit(TARGET_DEFAULT, LIMIT_POWERHAL, GPUPPM_KEEP_IDX, 5) == 0);
		assert(limits().floor == 5);
		assert(set_state(12) == 0);
		assert(limits().ceiling == 12 && limits().floor == 12);
		ged(0);
		assert(test_cur_oppidx() == 12);
		ged((int)cdev.max_state);
		assert(test_cur_oppidx() == 12);
		assert(set_state(0) == 0);
		assert(limits().ceiling == 0 && limits().floor == 5);
	} else if (!strcmp(name, "powered-off")) {
		boot(SEGMENT_MT6789);
		int commits = test_commits();
		assert(set_state(15) == 0);
		assert(test_commits() == commits && row() == 15 && get_state() == 15);
		test_power(1);
		ged(0);
		assert(test_cur_oppidx() == 15);
	} else if (!strcmp(name, "limit-failure")) {
		boot(SEGMENT_MT6789);
		test_power(1);
		assert(set_state(3) == 0);
		test_wrapper_drop_gpuppm(1);
		assert(set_state(7) == -ENOENT);
		assert(get_state() == 3 && row() == 3);
		test_wrapper_drop_gpuppm(0);
		test_wrapper_eb(1, -EIO);
		assert(set_state(9) == -EINVAL);
		assert(get_state() == 3 && row() == 3);
		test_wrapper_eb(0, 0);
		assert(set_state(7) == 0);
		assert(get_state() == 7 && row() == 7);
	} else if (!strcmp(name, "probe-eb")) {
		test_wrapper_eb(1, 0);
		test_provider_config(SEGMENT_MT6789, 1, 0);
		assert(test_probe(&pdev) == 0);
		assert(register_calls == 0 && !cdev_live && !test_cooling_live());
		test_remove(&pdev);
		assert(unregister_calls == 0);
	} else if (!strcmp(name, "probe-bringup")) {
		test_provider_config(SEGMENT_MT6789, 0, 1);
		assert(test_probe(&pdev) == 0);
		assert(register_calls == 0 && !cdev_live && !test_cooling_live());
	} else if (!strcmp(name, "register-failure")) {
		register_error = -ENOMEM;
		test_provider_config(SEGMENT_MT6789, 0, 0);
		assert(test_probe(&pdev) == 0);
		assert(register_calls == 1 && !cdev_live && !test_cooling_live());
		test_remove(&pdev);
		assert(unregister_calls == 0);
	} else if (!strcmp(name, "reset-failure")) {
		test_wrapper_eb(1, -EIO);
		test_provider_config(SEGMENT_MT6789, 0, 0);
		assert(test_probe(&pdev) == 0);
		assert(register_calls == 0 && !cdev_live && !test_cooling_live());
	} else if (!strcmp(name, "rebind-reset")) {
		boot(SEGMENT_MT6789);
		assert(set_state(9) == 0 && row() == 9);
		test_remove(&pdev);
		assert(!cdev_live && row() == 9);
		assert(test_probe(&pdev) == 0);
		assert(register_calls == 2 && cdev_live && test_cooling_live());
		assert(get_state() == 0 && row() == GPUPPM_DEFAULT_IDX && limits().ceiling == 0);
	} else if (!strcmp(name, "remove-order")) {
		boot(SEGMENT_MT6789);
		test_remove(&pdev);
		assert(unregister_calls == 1 && !cdev_live && !test_cooling_live());
		assert(position("cooling_unregister") >= 0 && position("cooling_unregister") < position("free_working_table"));
	} else {
		fprintf(stderr, "unknown case %s\n", name);
		return 2;
	}
	printf("PASS %s events=%d commits=%d\n", name, events_count, test_commits());
	return 0;
}
