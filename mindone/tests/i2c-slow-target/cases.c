/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
typedef int (*xfer_fn)(struct i2c_adapter *adap, struct i2c_msg msgs[], int num);

struct comp {
	const char *name;
	const struct mtk_i2c_compatible *comp;
};

static const struct comp comps[] = {
	{ "mt6983", &mt6983_compat },
	{ "mt2712", &mt2712_compat },
	{ "mt8173", &mt8173_compat },
	{ "mt6768", &mt6768_compat },
	{ "mt6739", &mt6739_compat },
};

static const unsigned int parents[] = { 26000000, 68250000, 124800000, 136500000, 156000000 };
static const unsigned int dt_speeds[] = { 50000, 100000, 200000, 400000, 1000000, 3400000 };

#define COMPS (sizeof(comps) / sizeof(comps[0]))
#define PARENTS (sizeof(parents) / sizeof(parents[0]))
#define DT_SPEEDS (sizeof(dt_speeds) / sizeof(dt_speeds[0]))
#define BUS 4
#define TARGET 0x69
#define DT_HZ 400000

static struct dev devs[MODELS];

static void attach(struct dev *d, const struct mtk_i2c_compatible *comp, unsigned int dt_hz, bool hold)
{
	int slot = (int)(d - devs);

	memset(d, 0, sizeof(*d));
	models[slot] = &d->m;
	d->i2c.dev_comp = comp;
	d->i2c.speed_hz = dt_hz;
	d->i2c.clk_src_div = 1;
	d->i2c.ctrl_data_hold = hold;
	d->i2c.base = d->m.regs;
	d->i2c.pdmabase = d->m.dma;
	d->i2c.adap.nr = BUS;
	d->i2c.adap.timeout = 2 * HZ;
	d->i2c.adap.algo_data = &d->i2c;
}

static void probe(struct dev *d, unsigned int parent, bool original)
{
	if (original)
		assert(orig_mtk_i2c_set_speed(&d->i2c, parent) == 0);
	else
		assert(mtk_i2c_set_speed(&d->i2c, parent) == 0);
	d->m.clock_on++;
	mtk_i2c_init_hw(&d->i2c);
	d->m.clock_on--;
}

static struct mtk_i2c_speed live(struct mtk_i2c *i2c)
{
	struct mtk_i2c_speed speed;

	memset(&speed, 0, sizeof(speed));
	mtk_i2c_save_speed(i2c, &speed);
	return speed;
}

static bool same_speed(const struct mtk_i2c_speed *a, const struct mtk_i2c_speed *b)
{
	return a->speed_hz == b->speed_hz && a->timing_reg == b->timing_reg &&
	       a->high_speed_reg == b->high_speed_reg && a->ltiming_reg == b->ltiming_reg &&
	       !memcmp(&a->ac_timing, &b->ac_timing, sizeof(a->ac_timing));
}

static bool same_regs(const struct timing_regs *a, const struct timing_regs *b)
{
	return !memcmp(a, b, sizeof(*a));
}

static bool same_log(const struct model *a, const struct model *b)
{
	return a->writes == b->writes && !memcmp(a->log, b->log, sizeof(a->log[0]) * (size_t)a->writes);
}

static bool same_xfer(const struct xfer *a, const struct xfer *b)
{
	return a->addr == b->addr && a->flags == b->flags && a->len == b->len && a->num == b->num &&
	       a->left_num == b->left_num && a->op == b->op && a->speed_hz == b->speed_hz &&
	       same_regs(&a->regs, &b->regs);
}

static bool same_xfers(const struct dev *a, const struct dev *b)
{
	int i;

	if (a->xfers != b->xfers)
		return false;
	for (i = 0; i < a->xfers; i++)
		if (!same_xfer(&a->xfer[i], &b->xfer[i]))
			return false;
	return true;
}

static void set_param(const char *value)
{
	assert(mtk_i2c_slow_set(value, NULL) == 0);
}

static unsigned int bus_hz(struct mtk_i2c *i2c, unsigned int parent, const struct mtk_i2c_speed *speed)
{
	unsigned int clk_src = parent / ((speed->ac_timing.inter_clk_div & 0xff) + 1);
	unsigned int sample, high, low;

	if (!i2c->dev_comp->timing_adjust)
		clk_src = parent;
	if (i2c->dev_comp->ltiming_adjust) {
		sample = (speed->ac_timing.htiming >> 8) & 0x7;
		high = speed->ac_timing.htiming & 0x3f;
		low = speed->ac_timing.ltiming & 0x3f;
		return clk_src / ((high + 1 + low + 1) * (sample + 1));
	}
	sample = (speed->timing_reg >> 8) & 0x7;
	high = speed->timing_reg & 0x3f;
	return clk_src / (2 * (high + 1) * (sample + 1));
}

enum { MSG_BUFS = 3, MSG_LEN = 32 };
static u8 bufs[MSG_BUFS][MSG_LEN];

struct step {
	const char *name;
	int num;
	struct { u16 addr; u16 flags; u16 len; } msg[3];
	int fail_at;
	int fail_ret;
	int clock_fail;
	bool target;
};

static const struct step script[] = {
	{ "write 9 to target", 1, { { TARGET, 0, 9 } }, 0, 0, 0, true },
	{ "write 1 read 14 target", 2, { { TARGET, 0, 1 }, { TARGET, I2C_M_RD, 14 } }, 0, 0, 0, true },
	{ "read 1 target", 1, { { TARGET, I2C_M_RD, 1 } }, 0, 0, 0, true },
	{ "sensor write 2 read 2", 2, { { 0x10, 0, 2 }, { 0x10, I2C_M_RD, 2 } }, 0, 0, 0, false },
	{ "eeprom write 3", 1, { { 0x50, 0, 3 } }, 0, 0, 0, false },
	{ "sensor write, target read", 2, { { 0x10, 0, 2 }, { TARGET, I2C_M_RD, 1 } }, 0, 0, 0, true },
	{ "target write, af write", 2, { { TARGET, 0, 1 }, { 0x18, 0, 2 } }, 0, 0, 0, true },
	{ "target continuous write", 3, { { TARGET, 0, 2 }, { TARGET, 0, 2 }, { TARGET, 0, 2 } }, 0, 0, 0, true },
	{ "target read nack", 2, { { TARGET, 0, 1 }, { TARGET, I2C_M_RD, 14 } }, 1, -ENXIO, 0, true },
	{ "target write timeout", 1, { { TARGET, 0, 9 } }, 1, -ETIMEDOUT, 0, true },
	{ "sensor clock failure", 2, { { 0x10, 0, 2 }, { 0x10, I2C_M_RD, 2 } }, 0, 0, 1, false },
	{ "target clock failure", 1, { { TARGET, 0, 9 } }, 0, 0, 1, true },
	{ "ten-bit 0x69", 1, { { TARGET, I2C_M_TEN, 2 } }, 0, 0, 0, false },
	{ "target read 20", 1, { { TARGET, I2C_M_RD, 20 } }, 0, 0, 0, true },
	{ "sensor read nack", 1, { { 0x10, I2C_M_RD, 2 } }, 1, -ENXIO, 0, false },
};

#define STEPS (sizeof(script) / sizeof(script[0]))

static int run_step(struct dev *d, xfer_fn fn, const struct step *s)
{
	struct i2c_msg msgs[3];
	int i;

	for (i = 0; i < s->num; i++) {
		msgs[i].addr = s->msg[i].addr;
		msgs[i].flags = s->msg[i].flags;
		msgs[i].len = s->msg[i].len;
		msgs[i].buf = bufs[i];
	}
	d->clock_fail = s->clock_fail;
	d->fail_at = s->fail_at ? d->xfers + s->fail_at : 0;
	d->fail_ret = s->fail_ret;
	return fn(&d->i2c.adap, msgs, s->num);
}

static int expected_ret(const struct step *s)
{
	if (s->clock_fail)
		return -EIO;
	if (s->fail_at)
		return s->fail_ret;
	return s->num;
}

static void case_equivalence(void)
{
	size_t c, p, s;
	int hold, combos = 0;

	for (c = 0; c < COMPS; c++)
		for (p = 0; p < PARENTS; p++)
			for (s = 0; s < DT_SPEEDS; s++)
				for (hold = 0; hold < 2; hold++) {
					struct mtk_i2c_speed a, b;

					attach(&devs[0], comps[c].comp, dt_speeds[s], hold);
					attach(&devs[1], comps[c].comp, dt_speeds[s], hold);
					probe(&devs[0], parents[p], true);
					probe(&devs[1], parents[p], false);
					a = live(&devs[0].i2c);
					b = live(&devs[1].i2c);
					assert(same_speed(&a, &b));
					assert(same_log(&devs[0].m, &devs[1].m));
					assert(devs[1].i2c.parent_clk == parents[p]);
					assert(devs[1].i2c.dt_speed.speed_hz == dt_speeds[s]);
					if (!hold || dt_speeds[s] <= I2C_MAX_FAST_MODE_PLUS_FREQ)
						assert(same_speed(&b, &devs[1].i2c.dt_speed));
					combos++;
				}
	printf("PASS equivalence: %d probes (5 compatibles x 5 parent clocks x 6 DT speeds x data-hold) program the "
	       "same registers and fields as the original mtk_i2c_set_speed\n", combos);
}

static void case_transfer_off(void)
{
	static const char *const params[] = { "off", "3:0x69:100000", "4:0x30:100000", "4:0x69:400000",
					      "3:0x69:100000,4:0x30:1000000,5:0x10:100000",
					      "4:0x69:400000,3:0x10:1000000,4:0x31:50000" };
	size_t c, p, s;
	int runs = 0;

	for (c = 0; c < 3; c++)
		for (p = 0; p < sizeof(params) / sizeof(params[0]); p++) {
			int info = dev_info_count, warn = dev_warn_count;

			set_param(params[p]);
			attach(&devs[0], comps[c].comp, DT_HZ, false);
			attach(&devs[1], comps[c].comp, DT_HZ, false);
			probe(&devs[0], 124800000, true);
			probe(&devs[1], 124800000, false);
			for (s = 0; s < STEPS; s++) {
				int a = run_step(&devs[0], orig_mtk_i2c_transfer, &script[s]);
				int b = run_step(&devs[1], mtk_i2c_transfer, &script[s]);

				assert(a == b && b == expected_ret(&script[s]));
				assert(same_log(&devs[0].m, &devs[1].m));
				assert(same_xfers(&devs[0], &devs[1]));
				assert(devs[1].m.clock_on == 0);
			}
			assert(dev_info_count == info && dev_warn_count == warn);
			assert(devs[1].i2c.slow_hz == 0);
			runs++;
		}
	set_param("off");
	printf("PASS transfer-off: %d runs of %zu transfers (off, other adapter, other address, Hz equal to DT, three "
	       "targets none of which matches) write the same registers and start the same transactions as the original "
	       "mtk_i2c_transfer\n", runs, STEPS);
}

static void reference(struct dev *d, const struct mtk_i2c_compatible *comp, unsigned int parent, unsigned int hz,
		      bool hold, struct timing_regs *regs, struct mtk_i2c_speed *speed)
{
	attach(d, comp, hz, hold);
	probe(d, parent, true);
	*regs = timing_regs(&d->i2c);
	*speed = live(&d->i2c);
}

static void check_script(struct dev *d, const struct timing_regs *r_slow, const struct timing_regs *r_dt,
			 unsigned int hz)
{
	size_t s;

	for (s = 0; s < STEPS; s++) {
		int before = d->xfers, writes = d->m.writes, i, ret;
		bool slow = script[s].target;
		struct timing_regs after;
		struct mtk_i2c_speed now;

		ret = run_step(d, mtk_i2c_transfer, &script[s]);
		assert(ret == expected_ret(&script[s]));
		for (i = before; i < d->xfers; i++) {
			struct xfer *x = &d->xfer[i];

			if (slow)
				assert(x->speed_hz == hz && same_regs(&x->regs, r_slow));
			else
				assert(x->speed_hz == DT_HZ && same_regs(&x->regs, r_dt));
		}
		if (!slow && !script[s].fail_at)
			assert(d->m.writes == writes);
		if (script[s].clock_fail)
			assert(d->m.writes == writes && d->xfers == before);
		after = timing_regs(&d->i2c);
		assert(same_regs(&after, r_dt));
		now = live(&d->i2c);
		assert(same_speed(&now, &d->i2c.dt_speed));
		assert(d->m.clock_on == 0);
	}
}

static void case_slow(void)
{
	size_t c, p;
	int combos = 0, unreachable = 0;

	for (c = 0; c < COMPS; c++)
		for (p = 0; p < PARENTS; p++) {
			struct timing_regs r100, r400;
			struct mtk_i2c_speed s100, s400;
			struct dev *d = &devs[1];
			int info, writes;
			unsigned int f;

			attach(&devs[0], comps[c].comp, 100000, false);
			if (mtk_i2c_calc_timing(&devs[0].i2c, parents[p])) {
				attach(d, comps[c].comp, DT_HZ, false);
				probe(d, parents[p], false);
				set_param("4:0x69:100000");
				writes = d->m.writes;
				assert(run_step(d, mtk_i2c_transfer, &script[1]) == -EINVAL);
				assert(d->m.writes == writes && d->xfers == 0);
				assert(run_step(d, mtk_i2c_transfer, &script[3]) == 2);
				set_param("off");
				unreachable++;
				continue;
			}
			reference(&devs[2], comps[c].comp, parents[p], 100000, false, &r100, &s100);
			reference(&devs[3], comps[c].comp, parents[p], DT_HZ, false, &r400, &s400);
			attach(d, comps[c].comp, DT_HZ, false);
			probe(d, parents[p], false);
			assert(same_speed(&d->i2c.dt_speed, &s400));
			set_param("4:0x69:100000");
			info = dev_info_count;
			verbose = !strcmp(comps[c].name, "mt6983");
			if (verbose)
				printf("  mt6983, parent clock %u Hz, DT %u Hz:\n", parents[p], DT_HZ);
			check_script(d, &r100, &r400, 100000);
			verbose = 0;
			assert(dev_info_count == info + 1);
			assert(d->i2c.slow_hz == 100000);
			assert(same_speed(&d->i2c.slow_speed, &s100));
			f = bus_hz(&d->i2c, parents[p], &d->i2c.slow_speed);
			assert(f <= 100000 && f >= 50000);
			if (!strcmp(comps[c].name, "mt6983"))
				printf("    100 kHz set: TIMING 0x%04x LTIMING 0x%04x HS 0x%04x EXT_CONF 0x%04x CLOCK_DIV 0x%04x "
				       "SDA_TIMING 0x%04x, SCL %u Hz by the driver's formula; DT set: TIMING 0x%04x LTIMING 0x%04x "
				       "EXT_CONF 0x%04x CLOCK_DIV 0x%04x, SCL %u Hz\n",
				       r100.timing, r100.ltiming, r100.hs, r100.ext, r100.clock_div, r100.sda, f,
				       r400.timing, r400.ltiming, r400.ext, r400.clock_div,
				       bus_hz(&d->i2c, parents[p], &d->i2c.dt_speed));
			d->m.writes = 0;
			d->m.clock_on++;
			mtk_i2c_use_speed(&d->i2c, &d->i2c.slow_speed);
			d->m.clock_on--;
			assert(same_log(&d->m, &devs[2].m));
			d->m.writes = 0;
			d->m.clock_on++;
			mtk_i2c_use_speed(&d->i2c, &d->i2c.dt_speed);
			d->m.clock_on--;
			assert(same_log(&d->m, &devs[3].m));
			set_param("off");
			combos++;
		}
	printf("PASS slow-100k: %d compatible/parent-clock pairs with DT 400 kHz and slow_target=4:0x69:100000: every "
	       "transaction that addresses 0x69 runs on the registers a 100 kHz DT probe programs, every other one on the "
	       "400 kHz registers, the bus returns to 400 kHz after each transfer and after NACK, timeout and clock "
	       "failures, and the set is computed once; %d pairs cannot divide down to 100 kHz and fail the target "
	       "transfer with -EINVAL before touching the controller\n", combos, unreachable);
}

static void case_rate_change(void)
{
	struct timing_regs r100, r50, r400;
	struct mtk_i2c_speed s100, s50, s400;
	struct dev *d = &devs[1];
	const struct step *wrrd = &script[1], *sensor = &script[3];
	int info, writes;

	reference(&devs[2], &mt6983_compat, 124800000, 100000, false, &r100, &s100);
	reference(&devs[3], &mt6983_compat, 124800000, DT_HZ, false, &r400, &s400);
	attach(&devs[0], &mt6983_compat, 50000, false);
	probe(&devs[0], 124800000, true);
	r50 = timing_regs(&devs[0].i2c);
	s50 = live(&devs[0].i2c);
	attach(d, &mt6983_compat, DT_HZ, false);
	probe(d, 124800000, false);
	info = dev_info_count;
	set_param("4:0x69:100000");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r100));
	set_param("4:0x69:50000");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r50) && d->xfer[d->xfers - 1].speed_hz == 50000);
	assert(same_speed(&d->i2c.slow_speed, &s50));
	set_param("4:0x69:100000\n");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r100));
	assert(run_step(d, mtk_i2c_transfer, sensor) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400));
	assert(dev_info_count == info + 2);
	set_param("5:0x69:100000");
	writes = d->m.writes;
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400) && d->m.writes == writes);
	set_param("off\n");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400) && d->m.writes == writes);
	set_param("4:0x69:100000");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r100));
	assert(dev_info_count == info + 2);
	set_param("");
	assert(run_step(d, mtk_i2c_transfer, wrrd) == 2);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400) && d->xfer[d->xfers - 1].speed_hz == DT_HZ);
	printf("PASS rate-change: 100 -> 50 -> 100 kHz computes each set once and takes 100 kHz back from the cache, "
	       "another adapter, off and an empty value leave 0x69 on the DT registers without reprogramming the "
	       "controller\n");
}

static void case_from_hs(void)
{
	static const struct { const struct mtk_i2c_compatible *comp; bool hold; } cases[] = {
		{ &mt2712_compat, false }, { &mt6983_compat, false }, { &mt6983_compat, true }, { &mt6768_compat, true },
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct timing_regs r100, rhs;
		struct mtk_i2c_speed s100, shs;
		struct dev *d = &devs[1];
		int ret;

		reference(&devs[2], cases[i].comp, 124800000, 100000, cases[i].hold, &r100, &s100);
		reference(&devs[3], cases[i].comp, 124800000, 3400000, cases[i].hold, &rhs, &shs);
		attach(d, cases[i].comp, 3400000, cases[i].hold);
		probe(d, 124800000, false);
		set_param("4:0x69:100000");
		ret = run_step(d, mtk_i2c_transfer, &script[1]);
		assert(ret == 2);
		assert(d->xfer[d->xfers - 1].speed_hz == 100000 && same_regs(&d->xfer[d->xfers - 1].regs, &r100));
		assert(same_speed(&d->i2c.slow_speed, &s100));
		{
			struct timing_regs after = timing_regs(&d->i2c);

			assert(same_regs(&after, &rhs));
		}
		set_param("off");
	}
	printf("PASS slow-from-hs: with a 3.4 MHz DT bus the 100 kHz set equals a fresh 100 kHz probe (no stale "
	       "high-speed fields) and the high-speed registers, including the data-hold bits, come back\n");
}

static void case_unsupported(void)
{
	struct dev *d = &devs[1];
	struct mtk_i2c_speed now;
	int writes, xfers, warn, ret;

	attach(&devs[0], &mt8173_compat, 10000, false);
	devs[0].i2c.speed_hz = 10000;
	assert(mtk_i2c_calc_timing(&devs[0].i2c, 26000000) == -EINVAL);
	attach(d, &mt8173_compat, DT_HZ, false);
	probe(d, 26000000, false);
	set_param("4:0x69:10000");
	writes = d->m.writes;
	xfers = d->xfers;
	warn = dev_warn_count;
	ret = run_step(d, mtk_i2c_transfer, &script[1]);
	assert(ret == -EINVAL);
	assert(d->m.writes == writes && d->xfers == xfers && dev_warn_count == warn + 1);
	assert(d->i2c.slow_hz == 0);
	now = live(&d->i2c);
	assert(same_speed(&now, &d->i2c.dt_speed));
	assert(run_step(d, mtk_i2c_transfer, &script[3]) == 2);
	assert(d->xfer[d->xfers - 1].speed_hz == DT_HZ);
	set_param("4:0x69:100000");
	assert(run_step(d, mtk_i2c_transfer, &script[1]) == 2);
	assert(d->xfer[d->xfers - 1].speed_hz == 100000);

	attach(d, &mt6983_compat, DT_HZ, false);
	d->i2c.ch_offset_i2c = I2C_OFFSET_SCP;
	probe(d, 124800000, false);
	writes = d->m.writes;
	xfers = d->xfers;
	assert(run_step(d, mtk_i2c_transfer, &script[1]) == -EOPNOTSUPP);
	assert(d->m.writes == writes && d->xfers == xfers);
	assert(run_step(d, mtk_i2c_transfer, &script[3]) == 2);
	set_param("off");
	printf("PASS unsupported: a rate the controller cannot divide to fails the target transfer with -EINVAL and an "
	       "SCP-shared channel with -EOPNOTSUPP, both before touching the controller; the DT set stays live\n");
}

static void case_parse(void)
{
	static const struct { const char *in, *out; } good[] = {
		{ "4:0x69:100000", "4:0x69:100000\n" },
		{ "4:0x69:100000\n", "4:0x69:100000\n" },
		{ "4:105:100000", "4:0x69:100000\n" },
		{ "4:0X69:100000", "4:0x69:100000\n" },
		{ "4:0x69:0100000", "4:0x69:100000\n" },
		{ "4:0151:100000", "4:0x69:100000\n" },
		{ "0:0x08:10000", "0:0x08:10000\n" },
		{ "2147483647:0x77:1000000", "2147483647:0x77:1000000\n" },
		{ "4:0x69:100000,4:0x10:1000000", "4:0x69:100000,4:0x10:1000000\n" },
		{ "4:0x69:100000,4:16:1000000\n", "4:0x69:100000,4:0x10:1000000\n" },
		{ "4:0x69:100000,5:0x69:100000,4:0x10:1000000", "4:0x69:100000,5:0x69:100000,4:0x10:1000000\n" },
		{ "2147483647:0x77:1000000,2147483646:0x76:999999,2147483645:0x75:10000",
		  "2147483647:0x77:1000000,2147483646:0x76:999999,2147483645:0x75:10000\n" },
		{ "off", "off\n" },
		{ "off\n", "off\n" },
		{ "", "off\n" },
		{ "\n", "off\n" },
	};
	static const char *const bad[] = {
		"4", "4:0x69", "4:0x69:", "4:0x69:100000:", "4:0x69:100000:5", "4::100000", ":0x69:100000", "::", ":",
		"4:0x69:100000\n\n", " 4:0x69:100000", "4:0x69:100000 ", "4: 0x69:100000", "+4:0x69:100000",
		"-1:0x69:100000", "4:0x69:-100000", "4:0x07:100000", "4:0x78:100000", "4:0x69:9999", "4:0x69:1000001",
		"4:0x69:100k", "4:0x69:0x186a0", "a:0x69:100000", "4:0x6g:100000", "4:69h:100000",
		"2147483648:0x69:100000", "4294967296:0x69:100000", "4:0x69:4294967296", "4:0x69:100000x", "OFF", "of",
		"offf", "0000000000000000000004:0x69:100000", "4\n:0x69:100000", "4:0x69\n:100000", "4:0x:100000",
		"4:08:100000", "4:0x69:100000\r", "4;0x69;100000", "4:0x69:100000\t",
		"4:0x69:100000,", ",4:0x69:100000", "4:0x69:100000,,4:0x10:1000000", ",", "4:0x69:100000,off",
		"4:0x69:100000,4:0x69:50000", "4:0x69:100000,4:105:50000", "4:0x69:100000,4:0x10:1000000,4:0x69:100000",
		"4:0x69:100000,4:0x10:1000000,4:0x50:100000,4:0x18:100000", "4:0x69:100000,4:0x10",
		"4:0x69:100000,4:0x10:1000001", "4:0x69:100000,4:0x78:1000000", "4:0x69:100000 ,4:0x10:1000000",
		"4:0x69:100000,0000000000000000000004:0x10:1000000",
		"2147483647:0x77:1000000,2147483646:0x76:1000000,2147483645:0x75:1000000,",
	};
	char out[PAGE_SIZE];
	size_t i;

	for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		char again[PAGE_SIZE];

		set_param("3:0x10:20000");
		assert(mtk_i2c_slow_set(good[i].in, NULL) == 0);
		assert(mtk_i2c_slow_get(out, NULL) == (int)strlen(good[i].out));
		assert(!strcmp(out, good[i].out));
		assert(mtk_i2c_slow_set(out, NULL) == 0);
		assert(mtk_i2c_slow_get(again, NULL) > 0 && !strcmp(again, out));
	}
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		set_param("3:0x10:20000");
		if (mtk_i2c_slow_set(bad[i], NULL) != -EINVAL) {
			fprintf(stderr, "accepted: \"%s\"\n", bad[i]);
			assert(mtk_i2c_slow_set(bad[i], NULL) == -EINVAL);
		}
		assert(mtk_i2c_slow_get(out, NULL) > 0 && !strcmp(out, "3:0x10:20000\n"));
	}
	assert(!mtk_i2c_slow_lock);
	set_param("off");
	printf("PASS parse: %zu accepted values round-trip through the getter, %zu malformed or out-of-range values return "
	       "-EINVAL and keep the previous target\n", sizeof(good) / sizeof(good[0]), sizeof(bad) / sizeof(bad[0]));
}

static void expect_target(const char *want)
{
	char out[PAGE_SIZE];

	assert(mtk_i2c_slow_get(out, NULL) == (int)strlen(want));
	assert(!strcmp(out, want));
}

static void case_setter(void)
{
	static const struct { int nr; u16 addr; unsigned int hz; } bad[] = {
		{ -1, 0x69, 100000 }, { 4, 0x07, 100000 }, { 4, 0x78, 100000 }, { 4, 0x69, 9999 },
		{ 4, 0x69, 1000001 }, { -1, 0x69, 0 }, { 4, 0x00, 0 }, { 4, 0x80, 0 },
	};
	size_t i;

	set_param("off");
	assert(mtk_i2c_set_slow_target(4, 0x69, 100000) == 0);
	expect_target("4:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x69, 50000) == 0);
	expect_target("4:0x69:50000\n");
	assert(mtk_i2c_set_slow_target(4, 0x10, 1000000) == 0);
	assert(mtk_i2c_set_slow_target(3, 0x69, 100000) == 0);
	expect_target("4:0x69:50000,4:0x10:1000000,3:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x50, 100000) == -EBUSY);
	assert(mtk_i2c_set_slow_target(5, 0x30, 400000) == -EBUSY);
	expect_target("4:0x69:50000,4:0x10:1000000,3:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x10, 400000) == 0);
	expect_target("4:0x69:50000,4:0x10:400000,3:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x50, 0) == 0);
	assert(mtk_i2c_set_slow_target(5, 0x69, 0) == 0);
	expect_target("4:0x69:50000,4:0x10:400000,3:0x69:100000\n");
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		assert(mtk_i2c_set_slow_target(bad[i].nr, bad[i].addr, bad[i].hz) == -EINVAL);
		expect_target("4:0x69:50000,4:0x10:400000,3:0x69:100000\n");
	}
	assert(mtk_i2c_set_slow_target(4, 0x10, 0) == 0);
	expect_target("4:0x69:50000,3:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x50, 100000) == 0);
	expect_target("4:0x69:50000,4:0x50:100000,3:0x69:100000\n");
	assert(mtk_i2c_set_slow_target(4, 0x69, 0) == 0);
	assert(mtk_i2c_set_slow_target(3, 0x69, 0) == 0);
	assert(mtk_i2c_set_slow_target(4, 0x50, 0) == 0);
	expect_target("off\n");
	assert(mtk_i2c_set_slow_target(4, 0x69, 0) == 0);
	expect_target("off\n");
	set_param("4:0x69:400000");
	assert(mtk_i2c_set_slow_target(4, 0x69, 100000) == 0);
	expect_target("4:0x69:100000\n");
	set_param("2:0x50:100000,2:0x51:100000,2:0x52:100000");
	assert(mtk_i2c_set_slow_target(4, 0x69, 100000) == -EBUSY);
	expect_target("2:0x50:100000,2:0x51:100000,2:0x52:100000\n");
	set_param("2:0x50:100000");
	assert(mtk_i2c_set_slow_target(4, 0x69, 100000) == 0);
	expect_target("2:0x50:100000,4:0x69:100000\n");
	assert(!mtk_i2c_slow_lock);
	set_param("off");
	printf("PASS setter: the exported setter stores, updates and clears its own target next to up to two others, "
	       "returns -EBUSY only while three other targets are set, leaves a different target alone on clear and "
	       "rejects %zu out-of-range values\n", sizeof(bad) / sizeof(bad[0]));
}

#define SENSOR 0x10
#define FAST_HZ 1000000
#define SLOW_HZ 100000

struct fm_plus {
	unsigned int clk_src;
	unsigned int hz;
	unsigned int low_ns;
	unsigned int high_ns;
	unsigned int su_sta_ns;
	unsigned int hd_dat_ns;
	unsigned int su_dat_ns;
};

static struct fm_plus fm_plus_timing(unsigned int parent, const struct mtk_i2c_speed *speed)
{
	const struct mtk_i2c_ac_timing *t = &speed->ac_timing;
	struct fm_plus f;
	unsigned int sample, high, low;
	double sample_ns, clk_ns;

	f.clk_src = parent / ((t->inter_clk_div & 0xff) + 1);
	sample = (t->htiming >> 8) & 0x7;
	high = t->htiming & 0x3f;
	low = t->ltiming & 0x3f;
	clk_ns = 1e9 / f.clk_src;
	sample_ns = clk_ns * (sample + 1);
	f.hz = f.clk_src / ((high + 1 + low + 1) * (sample + 1));
	f.high_ns = (unsigned int)(sample_ns * (high + 1));
	f.low_ns = (unsigned int)(sample_ns * (low + 1));
	f.su_sta_ns = (unsigned int)(clk_ns * ((t->ext >> 8) & 0xff));
	f.hd_dat_ns = (unsigned int)(sample_ns * (t->sda_timing & 0xfff));
	f.su_dat_ns = f.low_ns - f.hd_dat_ns;
	return f;
}

static void check_fm_plus(unsigned int parent, const struct mtk_i2c_speed *speed, bool show)
{
	struct fm_plus f = fm_plus_timing(parent, speed);

	assert(speed->speed_hz == FAST_HZ && speed->high_speed_reg == 0 && speed->ac_timing.hs == 0);
	assert(speed->ac_timing.sda_timing & (1 << 12));
	assert(f.hz <= 1000000 && f.hz >= 900000);
	assert(f.low_ns >= 500 && f.high_ns >= 260 && f.su_sta_ns >= 260);
	assert(f.hd_dat_ns <= 450 && f.su_dat_ns >= 50);
	if (show)
		printf("    parent %u Hz: 1 MHz set htiming 0x%04x ltiming 0x%04x hs 0x%04x ext 0x%04x sda 0x%04x "
		       "clock_div 0x%04x: SCL %u Hz, tLOW %u ns, tHIGH %u ns, tSU;STA %u ns, tHD;DAT %u ns, "
		       "tSU;DAT %u ns\n", parent, speed->ac_timing.htiming, speed->ac_timing.ltiming,
		       speed->ac_timing.hs, speed->ac_timing.ext, speed->ac_timing.sda_timing,
		       speed->ac_timing.inter_clk_div, f.hz, f.low_ns, f.high_ns, f.su_sta_ns, f.hd_dat_ns,
		       f.su_dat_ns);
}

struct fast_step {
	const char *name;
	int num;
	struct { u16 addr; u16 flags; u16 len; } msg[3];
	int fail_at;
	int fail_ret;
	int clock_fail;
	unsigned int hz;
};

static const struct fast_step fast_script[] = {
	{ "sensor write 3", 1, { { SENSOR, 0, 3 } }, 0, 0, 0, FAST_HZ },
	{ "sensor write 2 read 1", 2, { { SENSOR, 0, 2 }, { SENSOR, I2C_M_RD, 1 } }, 0, 0, 0, FAST_HZ },
	{ "sensor burst 1026", 1, { { SENSOR, 0, 1026 } }, 0, 0, 0, FAST_HZ },
	{ "sensor continuous write", 3, { { SENSOR, 0, 3 }, { SENSOR, 0, 3 }, { SENSOR, 0, 3 } }, 0, 0, 0, FAST_HZ },
	{ "eeprom write 2 read 255", 2, { { 0x50, 0, 2 }, { 0x50, I2C_M_RD, 255 } }, 0, 0, 0, DT_HZ },
	{ "af write 2", 1, { { 0x0c, 0, 2 } }, 0, 0, 0, DT_HZ },
	{ "ois write 1 read 14", 2, { { TARGET, 0, 1 }, { TARGET, I2C_M_RD, 14 } }, 0, 0, 0, SLOW_HZ },
	{ "sensor write, eeprom read", 2, { { SENSOR, 0, 2 }, { 0x50, I2C_M_RD, 1 } }, 0, 0, 0, DT_HZ },
	{ "eeprom write, sensor read", 2, { { 0x50, 0, 2 }, { SENSOR, I2C_M_RD, 1 } }, 0, 0, 0, DT_HZ },
	{ "sensor write, ois read", 2, { { SENSOR, 0, 2 }, { TARGET, I2C_M_RD, 1 } }, 0, 0, 0, SLOW_HZ },
	{ "ois write, sensor write, af write", 3, { { TARGET, 0, 1 }, { SENSOR, 0, 2 }, { 0x0c, 0, 2 } }, 0, 0, 0,
	  SLOW_HZ },
	{ "sensor read nack", 2, { { SENSOR, 0, 2 }, { SENSOR, I2C_M_RD, 1 } }, 1, -ENXIO, 0, FAST_HZ },
	{ "sensor write timeout", 1, { { SENSOR, 0, 3 } }, 1, -ETIMEDOUT, 0, FAST_HZ },
	{ "sensor clock failure", 1, { { SENSOR, 0, 3 } }, 0, 0, 1, FAST_HZ },
	{ "ten-bit 0x10", 1, { { SENSOR, I2C_M_TEN, 2 } }, 0, 0, 0, DT_HZ },
	{ "sensor write 3 again", 1, { { SENSOR, 0, 3 } }, 0, 0, 0, FAST_HZ },
	{ "ois read 1 again", 1, { { TARGET, I2C_M_RD, 1 } }, 0, 0, 0, SLOW_HZ },
};

#define FAST_STEPS (sizeof(fast_script) / sizeof(fast_script[0]))

static u8 big[3][1026];

static int run_fast_step(struct dev *d, const struct fast_step *s)
{
	struct i2c_msg msgs[3];
	int i;

	for (i = 0; i < s->num; i++) {
		msgs[i].addr = s->msg[i].addr;
		msgs[i].flags = s->msg[i].flags;
		msgs[i].len = s->msg[i].len;
		msgs[i].buf = big[i];
	}
	d->clock_fail = s->clock_fail;
	d->fail_at = s->fail_at ? d->xfers + s->fail_at : 0;
	d->fail_ret = s->fail_ret;
	return mtk_i2c_transfer(&d->i2c.adap, msgs, s->num);
}

static void check_fast_script(struct dev *d, const struct timing_regs *r_fast, const struct timing_regs *r_slow,
			      const struct timing_regs *r_dt)
{
	size_t s;

	for (s = 0; s < FAST_STEPS; s++) {
		const struct fast_step *st = &fast_script[s];
		int before = d->xfers, writes = d->m.writes, i, ret;
		const struct timing_regs *want = st->hz == FAST_HZ ? r_fast : st->hz == SLOW_HZ ? r_slow : r_dt;
		struct timing_regs after;
		struct mtk_i2c_speed now;

		ret = run_fast_step(d, st);
		if (st->clock_fail)
			assert(ret == -EIO && d->m.writes == writes && d->xfers == before);
		else if (st->fail_at)
			assert(ret == st->fail_ret);
		else
			assert(ret == st->num);
		for (i = before; i < d->xfers; i++)
			assert(d->xfer[i].speed_hz == st->hz && same_regs(&d->xfer[i].regs, want));
		if (st->hz == DT_HZ && !st->fail_at)
			assert(d->m.writes == writes);
		after = timing_regs(&d->i2c);
		assert(same_regs(&after, r_dt));
		now = live(&d->i2c);
		assert(same_speed(&now, &d->i2c.dt_speed));
		assert(d->m.clock_on == 0);
	}
}

static void case_fast(void)
{
	size_t c, p;
	int combos = 0, unreachable = 0;

	for (c = 0; c < COMPS; c++)
		for (p = 0; p < PARENTS; p++) {
			struct timing_regs r1m, r100, r400;
			struct mtk_i2c_speed s1m, s100, s400;
			struct dev *d = &devs[1];
			int info;

			attach(&devs[0], comps[c].comp, SLOW_HZ, false);
			if (mtk_i2c_calc_timing(&devs[0].i2c, parents[p])) {
				unreachable++;
				continue;
			}
			reference(&devs[2], comps[c].comp, parents[p], FAST_HZ, false, &r1m, &s1m);
			reference(&devs[3], comps[c].comp, parents[p], SLOW_HZ, false, &r100, &s100);
			reference(&devs[0], comps[c].comp, parents[p], DT_HZ, false, &r400, &s400);
			attach(d, comps[c].comp, DT_HZ, false);
			probe(d, parents[p], false);
			assert(same_speed(&d->i2c.dt_speed, &s400));
			set_param("4:0x69:100000,4:0x10:1000000");
			info = dev_info_count;
			check_fast_script(d, &r1m, &r100, &r400);
			assert(dev_info_count == info + 2);
			assert(same_speed(&d->i2c.slow_sets[0], &s100) || same_speed(&d->i2c.slow_sets[1], &s100));
			assert(same_speed(&d->i2c.slow_sets[0], &s1m) || same_speed(&d->i2c.slow_sets[1], &s1m));
			if (comps[c].comp == &mt6983_compat)
				check_fm_plus(parents[p], &s1m, true);
			d->m.writes = 0;
			d->m.clock_on++;
			mtk_i2c_use_speed(&d->i2c, &s1m);
			d->m.clock_on--;
			assert(same_log(&d->m, &devs[2].m));
			set_param("off");
			combos++;
		}
	printf("PASS fast-1m: %d compatible/parent-clock pairs with DT 400 kHz and slow_target=4:0x69:100000,"
	       "4:0x10:1000000: every transfer whose messages all address 0x10 runs on the registers a 1 MHz DT probe "
	       "programs, one with an EEPROM, AF or 10-bit message stays at 400 kHz, one with an OIS message runs at "
	       "100 kHz, the bus returns to 400 kHz after every transfer and after NACK, timeout and clock failure, and "
	       "each set is computed once; the mt6983 1 MHz sets meet the Fm+ limits of the driver's own timing rules "
	       "(tLOW >= 500 ns, tHIGH >= 260 ns, tSU;STA >= 260 ns, tHD;DAT <= 450 ns, tSU;DAT >= 50 ns, SCL 0.9-1 MHz); "
	       "%d pairs without a 100 kHz set skipped\n", combos, unreachable);
}

static void case_targets(void)
{
	struct timing_regs r1m, r100, r50, r400;
	struct mtk_i2c_speed s1m, s100, s50, s400;
	struct dev *d = &devs[1];
	static const struct step sensor = { "sensor", 1, { { SENSOR, 0, 3 } }, 0, 0, 0, false };
	static const struct step eeprom = { "eeprom", 1, { { 0x50, 0, 3 } }, 0, 0, 0, false };
	static const struct step ois = { "ois", 1, { { TARGET, 0, 3 } }, 0, 0, 0, false };
	static const struct step af = { "af", 1, { { 0x0c, 0, 3 } }, 0, 0, 0, false };
	int info, round;

	reference(&devs[2], &mt6983_compat, 124800000, FAST_HZ, false, &r1m, &s1m);
	reference(&devs[3], &mt6983_compat, 124800000, SLOW_HZ, false, &r100, &s100);
	reference(&devs[0], &mt6983_compat, 124800000, 50000, false, &r50, &s50);
	attach(&devs[0], &mt6983_compat, DT_HZ, false);
	probe(&devs[0], 124800000, true);
	r400 = timing_regs(&devs[0].i2c);
	s400 = live(&devs[0].i2c);
	attach(d, &mt6983_compat, DT_HZ, false);
	probe(d, 124800000, false);
	info = dev_info_count;
	set_param("4:0x69:100000,4:0x10:1000000,4:0x50:50000");
	for (round = 0; round < 20; round++) {
		assert(run_step(d, mtk_i2c_transfer, &sensor) == 1);
		assert(same_regs(&d->xfer[d->xfers - 1].regs, &r1m));
		assert(run_step(d, mtk_i2c_transfer, &ois) == 1);
		assert(same_regs(&d->xfer[d->xfers - 1].regs, &r100));
		assert(run_step(d, mtk_i2c_transfer, &eeprom) == 1);
		assert(same_regs(&d->xfer[d->xfers - 1].regs, &r50));
		assert(run_step(d, mtk_i2c_transfer, &af) == 1);
		assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400));
		d->xfers = 0;
	}
	assert(dev_info_count == info + 3);
	set_param("4:0x10:1000000");
	assert(run_step(d, mtk_i2c_transfer, &eeprom) == 1);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400));
	assert(run_step(d, mtk_i2c_transfer, &sensor) == 1);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r1m));
	assert(dev_info_count == info + 3);
	set_param("3:0x10:1000000");
	assert(run_step(d, mtk_i2c_transfer, &sensor) == 1);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400));
	set_param("4:0x10:400000");
	assert(run_step(d, mtk_i2c_transfer, &sensor) == 1);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400));
	set_param("off");
	assert(run_step(d, mtk_i2c_transfer, &sensor) == 1);
	assert(same_regs(&d->xfer[d->xfers - 1].regs, &r400) && same_speed(&d->i2c.dt_speed, &s400));
	assert(dev_info_count == info + 3);
	printf("PASS targets: three targets at 1 MHz, 100 kHz and 50 kHz next to the 400 kHz bus alternate for 80 "
	       "transfers on their own registers and each set is computed once; a target on another adapter or at the "
	       "DT rate leaves 0x10 on the DT registers\n");
}

int main(int argc, char **argv)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "equivalence", case_equivalence }, { "transfer-off", case_transfer_off }, { "slow-100k", case_slow },
		{ "rate-change", case_rate_change }, { "slow-from-hs", case_from_hs }, { "unsupported", case_unsupported },
		{ "parse", case_parse }, { "setter", case_setter }, { "fast-1m", case_fast }, { "targets", case_targets },
	};
	size_t i;
	int ran = 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		if (argc < 2 || !strcmp(argv[1], cases[i].name)) {
			cases[i].fn();
			ran++;
		}
	if (!ran) {
		fprintf(stderr, "unknown case %s\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	return 0;
}
