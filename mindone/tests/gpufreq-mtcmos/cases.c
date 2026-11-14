/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static int cases_run;

static u32 con(int d) { return spm[(0x308 + 4 * d) / 4]; }

static bool domain_on(int d)
{
	return (con(d) & (PWR_ON | PWR_ON_2ND | PWR_RST_B)) == (PWR_ON | PWR_ON_2ND | PWR_RST_B) &&
		!(con(d) & (SRAM_PDN | PWR_ISO | PWR_CLK_DIS));
}

static bool domain_off(int d)
{
	return !(con(d) & (PWR_ON | PWR_ON_2ND | PWR_RST_B)) &&
		(con(d) & (SRAM_PDN | PWR_ISO | PWR_CLK_DIS)) == (SRAM_PDN | PWR_ISO | PWR_CLK_DIS);
}

static void consistent(const char *name)
{
	int d;

	assert(g_gpu.buck_count == 0 || g_gpu.buck_count == 1);
	assert(g_gpu.mtcmos_count == 0 || g_gpu.mtcmos_count == 1);
	assert(g_gpu.cg_count == 0 || g_gpu.cg_count == 1);
	assert(vgpu.use_count == g_gpu.buck_count && vsram.use_count == g_gpu.buck_count);
	assert(mux.enable_count == g_gpu.cg_count && bg3d.enable_count == g_gpu.cg_count);
	assert(ref_mux.enable_count == g_gpu.mtcmos_count);
	for (d = 0; d < 4; d++) {
		if (g_gpu.mtcmos_count && !domain_on(d)) {
			fprintf(stderr, "%s: domain %d not on (0x%x)\n", name, d, con(d));
			abort();
		}
		if (!g_gpu.mtcmos_count && !domain_off(d)) {
			fprintf(stderr, "%s: domain %d not off (0x%x)\n", name, d, con(d));
			abort();
		}
	}
	if (g_gpu.mtcmos_count)
		assert(!prot_en[0] && !prot_en[1] && !prot_en[2]);
	assert(!g_gpu.mtcmos_count || g_gpu.buck_count);
	assert(!g_gpu.cg_count || g_gpu.mtcmos_count);
}

static void clear_faults(void)
{
	stuck_domain = -1;
	stuck_on_pwr = stuck_off_pwr = stuck_sram_on = stuck_sram_off = 0;
	stuck_ack_bit_group = 3;
	stuck_ack_mask = 0;
	fail_set_parent = 0;
	mux.fail_enable = ref_mux.fail_enable = bg3d.fail_enable = 0;
	vgpu.fail_enable = vgpu.fail_disable = vsram.fail_enable = vsram.fail_disable = 0;
}

static void boot_off(void)
{
	int d;

	memset(&g_gpu, 0, sizeof(g_gpu));
	g_dvfs_state = DVFS_POWEROFF;
	g_bus_idle_timeout = false;
	memset(prot_en, 0, sizeof(prot_en));
	prot_en[1] = MFG1_PROT_STEP1_0_MASK;
	prot_en[2] = MFG1_PROT_STEP1_1_MASK | MFG1_PROT_STEP2_1_MASK;
	prot_en[0] = MFG1_PROT_STEP2_0_MASK;
	for (d = 0; d < 4; d++)
		spm[(0x308 + 4 * d) / 4] = SRAM_PDN | PWR_ISO | PWR_CLK_DIS;
	memset(&mux, 0, sizeof(mux));
	memset(&ref_mux, 0, sizeof(ref_mux));
	memset(&bg3d, 0, sizeof(bg3d));
	memset(&vgpu, 0, sizeof(vgpu));
	memset(&vsram, 0, sizeof(vsram));
	mux_parent = &sub_parent;
	clear_faults();
	faults = aborts = log_errors = 0;
}

static int call(enum gpufreq_power_state p)
{
	return __gpufreq_power_control(p);
}

static void powered(void)
{
	boot_off();
	assert(call(GPU_PWR_ON) == 1);
	consistent("powered");
	assert(g_gpu.buck_count == 1 && g_gpu.mtcmos_count == 1 && g_gpu.cg_count == 1);
	assert(mux_parent == &main_parent);
}

static void cycle_ok(const char *name)
{
	clear_faults();
	assert(call(GPU_PWR_ON) == 1);
	consistent(name);
	assert(g_gpu.buck_count == 1 && g_gpu.mtcmos_count == 1 && g_gpu.cg_count == 1);
	assert(!(g_dvfs_state & DVFS_POWEROFF));
	assert(call(GPU_PWR_OFF) == 0);
	consistent(name);
	assert(!g_gpu.buck_count && !g_gpu.mtcmos_count && !g_gpu.cg_count);
}

typedef void (*inject_fn)(void);

static void off_case(const char *name, inject_fn inject)
{
	int ret;

	powered();
	inject();
	if (setjmp(abort_jump)) {
		fprintf(stderr, "%s: unexpected abort\n", name);
		abort();
	}
	ret = call(GPU_PWR_OFF);
	assert(ret < 0);
	assert(!aborts && faults >= 1);
	assert(g_gpu.power_count == 0);
	consistent(name);
	assert(g_gpu.buck_count == 1);
	assert(g_dvfs_state & DVFS_POWEROFF);
	cycle_ok(name);
	cases_run++;
}

static void on_case(const char *name, inject_fn inject)
{
	int ret;

	boot_off();
	inject();
	if (setjmp(abort_jump)) {
		fprintf(stderr, "%s: unexpected abort\n", name);
		abort();
	}
	ret = call(GPU_PWR_ON);
	assert(ret < 0);
	assert(!aborts);
	assert(g_gpu.power_count == 0);
	consistent(name);
	assert(!g_gpu.buck_count && !g_gpu.mtcmos_count && !g_gpu.cg_count);
	assert(g_dvfs_state & DVFS_POWEROFF);
	cycle_ok(name);
	cases_run++;
}

static void fatal_case(const char *name, enum gpufreq_power_state p, inject_fn inject)
{
	if (p == GPU_PWR_OFF)
		powered();
	else
		boot_off();
	inject();
	if (!setjmp(abort_jump)) {
		call(p);
		fprintf(stderr, "%s: expected abort\n", name);
		abort();
	}
	assert(aborts == 1);
	gpufreq_lock = 0;
	cases_run++;
}

static void e0(void) { stuck_domain = 0; stuck_sram_off = 1; }
static void e1(void) { stuck_domain = 0; stuck_off_pwr = 1; }
static void e4(void) { stuck_ack_bit_group = 1; stuck_ack_mask = MFG1_PROT_STEP1_0_ACK_MASK; }
static void e5(void) { stuck_ack_bit_group = 2; stuck_ack_mask = MFG1_PROT_STEP1_1_ACK_MASK; }
static void e6(void) { stuck_ack_bit_group = 0; stuck_ack_mask = MFG1_PROT_STEP2_0_ACK_MASK; }
static void e7(void) { stuck_ack_bit_group = 2; stuck_ack_mask = MFG1_PROT_STEP2_1_ACK_MASK; }
static void e8(void) { stuck_domain = 1; stuck_sram_off = 1; }
static void e9(void) { stuck_domain = 1; stuck_off_pwr = 1; }
static void f0(void) { stuck_domain = 2; stuck_sram_off = 1; }
static void f1(void) { stuck_domain = 2; stuck_off_pwr = 1; }
static void f4(void) { stuck_domain = 3; stuck_sram_off = 1; }
static void f5(void) { stuck_domain = 3; stuck_off_pwr = 1; }
static void vsram_off(void) { vsram.fail_disable = 1; }
static void vgpu_off(void) { vgpu.fail_disable = 1; }
static void sub_off(void) { fail_set_parent = 1; }

static void e2(void) { stuck_domain = 0; stuck_on_pwr = 1; }
static void e3(void) { stuck_domain = 0; stuck_sram_on = 1; }
static void ea(void) { stuck_domain = 1; stuck_on_pwr = 1; }
static void eb(void) { stuck_domain = 1; stuck_sram_on = 1; }
static void f2(void) { stuck_domain = 2; stuck_on_pwr = 1; }
static void f3(void) { stuck_domain = 2; stuck_sram_on = 1; }
static void f6(void) { stuck_domain = 3; stuck_on_pwr = 1; }
static void f7(void) { stuck_domain = 3; stuck_sram_on = 1; }
static void refmux_on(void) { ref_mux.fail_enable = 1; }
static void vgpu_on(void) { vgpu.fail_enable = 1; }
static void vsram_on(void) { vsram.fail_enable = 1; }
static void mux_on(void) { mux.fail_enable = 1; }
static void main_on(void) { fail_set_parent = 1; }
static void bg3d_on(void) { bg3d.fail_enable = 1; }

static void e5_then_mfg1_dead(void) { e5(); stuck_domain = 1; stuck_on_pwr = 1; }
static void f2_then_mfg2_dead(void) { stuck_domain = 2; stuck_on_pwr = 1; stuck_sram_off = 1; }
static void vgpu_off_vsram_dead(void) { vgpu.fail_disable = 1; vsram.fail_enable = 1; }

int main(void)
{
	powered();
	assert(call(GPU_PWR_OFF) == 0);
	consistent("basic");
	cycle_ok("basic");
	cases_run++;

	off_case("E0", e0); off_case("E1", e1); off_case("E4", e4); off_case("E5", e5);
	off_case("E6", e6); off_case("E7", e7); off_case("E8", e8); off_case("E9", e9);
	off_case("F0", f0); off_case("F1", f1); off_case("F4", f4); off_case("F5", f5);
	off_case("VSRAM off", vsram_off); off_case("VGPU off", vgpu_off);

	powered();
	sub_off();
	assert(call(GPU_PWR_OFF) == 0);
	consistent("CLOCK_SUB off");
	cycle_ok("CLOCK_SUB off");
	cases_run++;

	on_case("E2", e2); on_case("E3", e3); on_case("EA", ea); on_case("EB", eb);
	on_case("F2", f2); on_case("F3", f3); on_case("F6", f6); on_case("F7", f7);
	on_case("ref_mux", refmux_on); on_case("VGPU on", vgpu_on); on_case("VSRAM on", vsram_on);
	on_case("clk_mux", mux_on); on_case("CLOCK_MAIN", main_on); on_case("bg3d", bg3d_on);

	powered();
	e5();
	assert(call(GPU_PWR_OFF) < 0);
	clear_faults();
	bg3d.fail_enable = 1;
	assert(call(GPU_PWR_ON) < 0);
	consistent("rejected off then failed on");
	assert(!g_gpu.buck_count && !g_gpu.mtcmos_count && !g_gpu.cg_count && g_gpu.power_count == 0);
	cycle_ok("rejected off then failed on");
	cases_run++;

	powered();
	e5();
	assert(call(GPU_PWR_OFF) < 0);
	bg3d.fail_enable = 1;
	assert(call(GPU_PWR_ON) < 0);
	consistent("failed on whose unwind is rejected");
	assert(g_gpu.buck_count == 1 && g_gpu.mtcmos_count == 1 && !g_gpu.cg_count && g_gpu.power_count == 0);
	assert(!aborts);
	cycle_ok("failed on whose unwind is rejected");
	cases_run++;

	fatal_case("E5 then MFG1 cannot power back", GPU_PWR_OFF, e5_then_mfg1_dead);
	fatal_case("F2 then MFG2 cannot power down", GPU_PWR_ON, f2_then_mfg2_dead);
	fatal_case("VGPU off then VSRAM cannot return", GPU_PWR_OFF, vgpu_off_vsram_dead);

	printf("gpufreq-fault: %d cases pass\n", cases_run);
	return 0;
}
