/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static struct clk_core cam_sel = { .name = "cam_sel" };
static struct clk_core camtm_sel = { .name = "camtm_sel" };
static struct clk_core cg_larbx = { .name = "cam_ra_larbx" };
static struct clk_core cg_cam = { .name = "cam_ra_cam" };
static struct clk_core cg_camtg = { .name = "cam_ra_camtg" };
static struct clk_core cg_root = { .name = "cam_ra_root" };
static struct clk_core *const cores[] = { &cam_sel, &camtm_sel, &cg_larbx, &cg_cam, &cg_camtg, &cg_root };
#define NCORES ((int)(sizeof(cores) / sizeof(cores[0])))
#define NGATES 4

static void reset_core(struct clk_core *core, struct clk_core *parent, bool in_domain, bool hw_on)
{
	core->parent = parent;
	core->in_domain = in_domain;
	core->hw_on = hw_on;
	core->prepare_count = 0;
	core->enable_count = 0;
	core->hw.core = core;
	core->own.core = core;
	core->own.handle = false;
}

static void setup(const char *name, bool in_domain)
{
	case_name = name;
	reset_core(&cam_sel, NULL, false, true);
	reset_core(&camtm_sel, NULL, false, true);
	reset_core(&cg_larbx, &cam_sel, in_domain, true);
	reset_core(&cg_cam, &cam_sel, in_domain, true);
	reset_core(&cg_camtg, &camtm_sel, in_domain, false);
	reset_core(&cg_root, NULL, in_domain, true);
	memset(slots, 0, sizeof(slots));
	slots[0].core = &cg_larbx;
	slots[1].core = &cg_cam;
	slots[2].core = &cg_camtg;
	slots[3].core = &cg_root;
	nslots = NGATES;
	count_result = NGATES;
	pdev_model.name = "disable-unused-clk-model";
	pdev_model.dev.of_node = &node;
	pdev_model.dev.power.disable_depth = 1;
	pdev_model.dev.power.usage_count = 0;
	pdev_model.dev.power.runtime_status = RPM_SUSPENDED;
	resume_error = 0;
	handles_live = handles_made = puts_made = 0;
	enable_calls = fail_at_call = failures_injected = 0;
	last_index = failed_index = -1;
	gets_made = rpm_enables = 0;
}

static void settled(int ret)
{
	int i;

	expect(ret == 0);
	expect(pdev_model.dev.power.usage_count == 0);
	expect(handles_live == 0);
	for (i = 0; i < NCORES; i++)
		expect(cores[i]->enable_count == 0 && cores[i]->prepare_count == 0);
	if (rpm_enables) {
		expect(rpm_enables == 1 && pdev_model.dev.power.disable_depth == 0);
		expect(pdev_model.dev.power.runtime_status == RPM_SUSPENDED);
	} else {
		expect(pdev_model.dev.power.disable_depth == 1 && !gets_made);
	}
}

static void gated(int first, int last)
{
	int i;

	for (i = first; i < last; i++)
		if (!slots[i].get_error)
			expect(!slots[i].core->hw_on);
}

static int probe(void)
{
	return disable_unused_probe(&pdev_model);
}

static void case_empty(void)
{
	setup("empty", true);
	count_result = 0;
	settled(probe());
	expect(!rpm_enables && !handles_made);
	printf("PASS empty: no clocks, runtime PM untouched\n");
}

static void case_absent(void)
{
	setup("absent", true);
	count_result = -ENOENT;
	settled(probe());
	expect(rpm_enables == 1 && gets_made == 1 && !handles_made);
	printf("PASS absent: no clocks property, one get and one put\n");
}

static void case_pass(void)
{
	setup("pass", true);
	settled(probe());
	expect(handles_made == NGATES && puts_made == NGATES);
	gated(0, NGATES);
	expect(!cam_sel.hw_on && !camtm_sel.hw_on);
	printf("PASS pass: %d gates off, %d handles put, domain released\n", NGATES, puts_made);
}

static void case_active(void)
{
	setup("already-active", true);
	pdev_model.dev.power.runtime_status = RPM_ACTIVE;
	settled(probe());
	expect(handles_made == NGATES);
	gated(0, NGATES);
	printf("PASS already-active: resume returns 1, the reference is still dropped\n");
}

static void case_no_domain(void)
{
	setup("no-domain", false);
	settled(probe());
	expect(handles_made == NGATES);
	gated(0, NGATES);
	printf("PASS no-domain: a node without power-domains balances the same way\n");
}

static void case_get_errors(void)
{
	setup("get-errors", true);
	slots[1].get_error = -EPROBE_DEFER;
	slots[2].get_error = -ENOENT;
	settled(probe());
	expect(handles_made == 2);
	gated(0, NGATES);
	expect(cg_cam.hw_on);
	printf("PASS get-errors: -EPROBE_DEFER and -ENOENT slots skipped, the others gated\n");
}

static void case_resume_fail(void)
{
	static const int errors[] = { -EIO, -EBUSY, -ETIMEDOUT, -EAGAIN };
	unsigned int e;

	for (e = 0; e < sizeof(errors) / sizeof(errors[0]); e++) {
		setup("resume-fail", true);
		resume_error = errors[e];
		settled(probe());
		expect(gets_made == 1 && !handles_made && !enable_calls);
		expect(cg_larbx.hw_on && cg_cam.hw_on && cg_root.hw_on);
	}
	printf("PASS resume-fail: %u resume errors, no clock touched, no reference left\n", e);
}

static void case_fail_sweep(void)
{
	int m, total;

	setup("fail-sweep", true);
	settled(probe());
	total = enable_calls;
	expect(total > NGATES);
	for (m = 1; m <= total; m++) {
		setup("fail-sweep", true);
		fail_at_call = m;
		settled(probe());
		expect(failures_injected == 1 && failed_index >= 0);
		gated(0, failed_index);
	}
	printf("PASS fail-sweep: a failure at each of %d clk_prepare_enable calls leaves no reference\n", total);
}

static const struct {
	const char *name;
	void (*run)(void);
} cases[] = {
	{ "empty", case_empty },
	{ "absent", case_absent },
	{ "pass", case_pass },
	{ "already-active", case_active },
	{ "no-domain", case_no_domain },
	{ "get-errors", case_get_errors },
	{ "resume-fail", case_resume_fail },
	{ "fail-sweep", case_fail_sweep },
};

int main(int argc, char **argv)
{
	unsigned int i;
	int ran = 0;

	verbose = getenv("CLK_DISABLE_UNUSED_VERBOSE") != NULL;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (argc > 1 && strcmp(argv[1], cases[i].name))
			continue;
		cases[i].run();
		ran++;
	}
	if (!ran) {
		fprintf(stderr, "unknown case %s\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	return 0;
}
