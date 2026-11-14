/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __iomem
#define unlikely(x) (x)
#define __maybe_unused __attribute__((unused))
#define BIT(n) (1U << (n))
#define USEC_PER_SEC 1000000L
#define GPUFREQ_TRACE_START(...)
#define GPUFREQ_TRACE_END(...)
#define GPUFREQ_LOGD(...) ((void)0)
#define GPUFREQ_LOGI(...) ((void)0)
#define GPUFREQ_LOGE(...) (log_errors++)
#define GPUFREQ_SUCCESS 0
#define GPUFREQ_EINVAL (-EINVAL)
#define GPUFREQ_SELF_CTRL_MTCMOS 1
#define DVFS_POWEROFF 0x1

typedef uint32_t u32;
enum gpufreq_power_state { GPU_PWR_OFF = 0, GPU_PWR_ON = 1 };
enum gpufreq_clk_src { CLOCK_SUB = 0, CLOCK_MAIN = 1 };
struct clk { int enable_count; int fail_enable; };
struct regulator { int use_count; int fail_enable; int fail_disable; };
struct gpufreq_clk_info { struct clk *clk_mux, *clk_ref_mux, *clk_main_parent, *clk_sub_parent, *subsys_bg3d; };
struct gpufreq_pmic_info { struct regulator *reg_vgpu, *reg_vsram_gpu; };
struct gpufreq_status { int power_count, buck_count, mtcmos_count, cg_count; };

static int log_errors, faults, aborts;
static jmp_buf abort_jump;
static struct gpufreq_status g_gpu;
static int g_dvfs_state;
static bool g_bus_idle_timeout;
static int gpufreq_lock;
static struct clk mux, ref_mux, main_parent, sub_parent, bg3d;
static struct clk *mux_parent = &sub_parent;
static int fail_set_parent;
static struct gpufreq_clk_info clk_info = { &mux, &ref_mux, &main_parent, &sub_parent, &bg3d };
static struct gpufreq_clk_info *g_clk = &clk_info;
static struct regulator vgpu, vsram;
static struct gpufreq_pmic_info pmic_info = { &vgpu, &vsram };
static struct gpufreq_pmic_info *g_pmic = &pmic_info;

static u32 spm[0x400 / 4];
static u32 infra[0x800 / 4];
static u32 topck[0x60 / 4];
static char *g_sleep = (char *)spm;
static char *g_infracfg_ao_base = (char *)infra;
static char *g_topckgen_base = (char *)topck;
static u32 prot_en[3];
static u32 stuck_ack_bit_group = 3, stuck_ack_mask;
static int stuck_domain = -1, stuck_on_pwr, stuck_off_pwr, stuck_sram_on, stuck_sram_off;
static int stuck_once;

#define PWR_STATUS                      (g_sleep + 0x16C)
#define PWR_STATUS_2ND                  (g_sleep + 0x170)
#define MFG0_PWR_CON                    (g_sleep + 0x308)
#define MFG1_PWR_CON                    (g_sleep + 0x30C)
#define MFG2_PWR_CON                    (g_sleep + 0x310)
#define MFG3_PWR_CON                    (g_sleep + 0x314)
#define PWR_RST_B                       BIT(0)
#define PWR_ISO                         BIT(1)
#define PWR_ON                          BIT(2)
#define PWR_ON_2ND                      BIT(3)
#define PWR_CLK_DIS                     BIT(4)
#define SRAM_PDN                        BIT(8)
#define SRAM_PDN_ACK                    BIT(12)
#define MFG0_PWR_STA_MASK               BIT(2)
#define MFG1_PWR_STA_MASK               BIT(3)
#define MFG2_PWR_STA_MASK               BIT(4)
#define MFG3_PWR_STA_MASK               BIT(5)
#define INFRA_TOPAXI_PROTECTEN_SET      (g_infracfg_ao_base + 0x2A0)
#define INFRA_TOPAXI_PROTECTSTA1        (g_infracfg_ao_base + 0x228)
#define INFRA_TOPAXI_PROTECTEN_CLR      (g_infracfg_ao_base + 0x2A4)
#define INFRA_TOPAXI_PROTECTEN_1_SET    (g_infracfg_ao_base + 0x2A8)
#define INFRA_TOPAXI_PROTECTSTA1_1      (g_infracfg_ao_base + 0x258)
#define INFRA_TOPAXI_PROTECTEN_1_CLR    (g_infracfg_ao_base + 0x2AC)
#define INFRA_TOPAXI_PROTECTEN_2_SET    (g_infracfg_ao_base + 0x714)
#define INFRA_TOPAXI_PROTECTSTA1_2      (g_infracfg_ao_base + 0x724)
#define INFRA_TOPAXI_PROTECTEN_2_CLR    (g_infracfg_ao_base + 0x718)
#define MFG1_PROT_STEP1_0_MASK          (BIT(21))
#define MFG1_PROT_STEP1_0_ACK_MASK      (BIT(21))
#define MFG1_PROT_STEP1_1_MASK          (BIT(5) | BIT(6))
#define MFG1_PROT_STEP1_1_ACK_MASK      (BIT(5) | BIT(6))
#define MFG1_PROT_STEP2_0_MASK          (BIT(21) | BIT(22))
#define MFG1_PROT_STEP2_0_ACK_MASK      (BIT(21) | BIT(22))
#define MFG1_PROT_STEP2_1_MASK          (BIT(7))
#define MFG1_PROT_STEP2_1_ACK_MASK      (BIT(7))

static int domain_of(char *a)
{
	if (a >= MFG0_PWR_CON && a <= MFG3_PWR_CON && !((a - MFG0_PWR_CON) % 4))
		return (int)((a - MFG0_PWR_CON) / 4);
	return -1;
}

static int prot_group(char *a)
{
	if (a == INFRA_TOPAXI_PROTECTEN_SET || a == INFRA_TOPAXI_PROTECTEN_CLR || a == INFRA_TOPAXI_PROTECTSTA1)
		return 0;
	if (a == INFRA_TOPAXI_PROTECTEN_1_SET || a == INFRA_TOPAXI_PROTECTEN_1_CLR || a == INFRA_TOPAXI_PROTECTSTA1_1)
		return 1;
	if (a == INFRA_TOPAXI_PROTECTEN_2_SET || a == INFRA_TOPAXI_PROTECTEN_2_CLR || a == INFRA_TOPAXI_PROTECTSTA1_2)
		return 2;
	return -1;
}

static bool stuck(int *flag)
{
	if (!*flag)
		return false;
	if (stuck_once && --stuck_once == 0)
		*flag = 0;
	return true;
}

static u32 readl(const void *addr)
{
	char *a = (char *)addr;
	int d, g;
	u32 v = 0;

	if (a == PWR_STATUS || a == PWR_STATUS_2ND) {
		for (d = 0; d < 4; d++) {
			u32 con = spm[(0x308 + 4 * d) / 4];
			bool on = con & (a == PWR_STATUS ? PWR_ON : PWR_ON_2ND);

			if (d == stuck_domain && on && stuck(&stuck_on_pwr))
				on = false;
			if (d == stuck_domain && !on && stuck(&stuck_off_pwr))
				on = true;
			if (on)
				v |= BIT(2 + d);
		}
		return v;
	}
	d = domain_of(a);
	if (d >= 0) {
		v = spm[(0x308 + 4 * d) / 4] & ~SRAM_PDN_ACK;
		bool ack = v & SRAM_PDN;

		if (d == stuck_domain && ack && stuck(&stuck_sram_off))
			ack = false;
		if (d == stuck_domain && !ack && stuck(&stuck_sram_on))
			ack = true;
		return v | (ack ? SRAM_PDN_ACK : 0);
	}
	g = prot_group(a);
	if (g >= 0) {
		v = prot_en[g];
		if ((u32)g == stuck_ack_bit_group && (v & stuck_ack_mask) && stuck_ack_mask)
			v &= ~stuck_ack_mask;
		return v;
	}
	if (a >= (char *)topck && a < (char *)topck + sizeof(topck))
		return mux_parent == &main_parent ? 0x40000 : 0;
	return 0;
}

static void writel(u32 val, void *addr)
{
	char *a = (char *)addr;
	int d = domain_of(a), g = prot_group(a);

	if (d >= 0) {
		spm[(0x308 + 4 * d) / 4] = val & ~SRAM_PDN_ACK;
		return;
	}
	if (g >= 0) {
		if (a == INFRA_TOPAXI_PROTECTEN_SET || a == INFRA_TOPAXI_PROTECTEN_1_SET || a == INFRA_TOPAXI_PROTECTEN_2_SET)
			prot_en[g] |= val;
		else if (a == INFRA_TOPAXI_PROTECTEN_CLR || a == INFRA_TOPAXI_PROTECTEN_1_CLR || a == INFRA_TOPAXI_PROTECTEN_2_CLR)
			prot_en[g] &= ~val;
	}
}

static void udelay(unsigned long us) { (void)us; }

#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read, args...) \
	({ int __n = 0; (void)(sleep_us); (void)(timeout_us); (void)(sleep_before_read); \
	   for (;;) { (val) = op(args); if (cond) break; if (++__n > 50) break; } \
	   (cond) ? 0 : -ETIMEDOUT; })
#define readl_poll_timeout(addr, val, cond, delay_us, timeout_us) \
	read_poll_timeout(readl, val, cond, delay_us, timeout_us, false, addr)

static int clk_prepare_enable(struct clk *c)
{
	if (c->fail_enable) {
		c->fail_enable--;
		return -EIO;
	}
	c->enable_count++;
	return 0;
}

static void clk_disable_unprepare(struct clk *c)
{
	assert(c->enable_count > 0);
	c->enable_count--;
}

static int clk_set_parent(struct clk *c, struct clk *p)
{
	assert(c == &mux);
	if (fail_set_parent) {
		fail_set_parent--;
		return -EIO;
	}
	mux_parent = p;
	return 0;
}

static int regulator_enable(struct regulator *r)
{
	if (r->fail_enable) {
		r->fail_enable--;
		return -EIO;
	}
	r->use_count++;
	return 0;
}

static int regulator_disable(struct regulator *r)
{
	assert(r->use_count > 0);
	if (r->fail_disable) {
		r->fail_disable--;
		return -EIO;
	}
	r->use_count--;
	return 0;
}

static void mutex_lock(int *m) { assert(!*m); *m = 1; }
static void mutex_unlock(int *m) { assert(*m); *m = 0; }
static void __gpufreq_footprint_power_count(int n) { (void)n; }
static void __gpufreq_footprint_power_step(unsigned int n) { (void)n; }
static void __gpufreq_check_bus_idle(void) { }
static void __gpufreq_set_timestamp(void) { }
static void __gpufreq_external_cg_control(void) { }
static void __gpufreq_fault(const char *fmt, ...) { (void)fmt; faults++; }
static void __gpufreq_abort(const char *fmt, ...) { (void)fmt; aborts++; longjmp(abort_jump, 1); }
static int __gpufreq_switch_clksrc(enum gpufreq_clk_src clksrc);
static int __gpufreq_clock_control(enum gpufreq_power_state power);
static int __gpufreq_mtcmos_control(enum gpufreq_power_state power);
static int __gpufreq_buck_control(enum gpufreq_power_state power);
static int __gpufreq_mfg0_control(enum gpufreq_power_state power);
static int __gpufreq_mfg1_control(enum gpufreq_power_state power);
static int __gpufreq_mfg2_control(enum gpufreq_power_state power);
static int __gpufreq_mfg3_control(enum gpufreq_power_state power);
