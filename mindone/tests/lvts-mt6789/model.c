/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include "stubs.h"

#define LVTS_CTRL_COUNT 4
#define LVTS_REG_SPACE 0x1000
#define LVTS_MAX_ZONES 16

static u32 mmio_regs[LVTS_REG_SPACE / 4];
static u8 *mmio_base_ptr;
static u32 rck_samples[LVTS_CTRL_COUNT][4];
static u32 rck_selected[LVTS_CTRL_COUNT];
static int rck_kicks[LVTS_CTRL_COUNT];
static int calscale_writes[LVTS_CTRL_COUNT];
static int low_power_single_writes[LVTS_CTRL_COUNT];
static int reset_all_writes[LVTS_CTRL_COUNT];
static int dev_cmd_count[LVTS_CTRL_COUNT];
static u32 dev_cmd_log[LVTS_CTRL_COUNT][64];
static int model_udelay_total;
static u8 efuse_buf[52];
static int cell_iter_idx;
static struct clk model_clk;
static struct reset_control model_rst;
static struct resource model_res = { .start = 0x1100b000, .end = 0x1100bfff };
static irq_handler_t registered_irq_fn;
static void *registered_irq_data;
static struct thermal_zone_device zones[LVTS_MAX_ZONES];
static int zone_count;
static void *allocs[256];
static int alloc_count;
static int force_rck_timeout;
static int force_reset_fail;
struct devm_entry {
	int kind;
	void (*action)(void *);
	void *data;
};
static struct devm_entry devm_stack[64];
static int devm_count;
static int teardown_zones_before_stop;
static int teardown_irq_before_stop;
static int teardown_hw_stopped;

static void record_devm(int kind, void (*action)(void *), void *data)
{
	if (devm_count < 64) {
		devm_stack[devm_count].kind = kind;
		devm_stack[devm_count].action = action;
		devm_stack[devm_count].data = data;
		devm_count++;
	}
}

int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data)
{
	(void)dev;
	record_devm(1, action, data);
	return 0;
}

static void run_devm_teardown(void)
{
	int zones_gone = 0, irq_gone = 0;

	teardown_zones_before_stop = 0;
	teardown_irq_before_stop = 0;
	teardown_hw_stopped = 0;
	while (devm_count > 0) {
		struct devm_entry e = devm_stack[--devm_count];

		if (e.kind == 3) {
			irq_gone = 1;
		} else if (e.kind == 2) {
			zones_gone++;
		} else if (e.kind == 1 && e.action) {
			teardown_irq_before_stop = irq_gone;
			teardown_zones_before_stop = zones_gone;
			e.action(e.data);
			teardown_hw_stopped = 1;
		}
	}
}

static void track_alloc(void *p)
{
	if (p && alloc_count < 256)
		allocs[alloc_count++] = p;
}

static void free_all_allocs(void)
{
	int i;

	for (i = 0; i < alloc_count; i++)
		free(allocs[i]);
	alloc_count = 0;
}

void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	void *p = calloc(1, size);

	(void)dev;
	(void)gfp;
	track_alloc(p);
	return p;
}

void *devm_krealloc(struct device *dev, void *ptr, size_t size, int gfp)
{
	void *p = realloc(ptr, size);
	int i;

	(void)dev;
	(void)gfp;
	for (i = 0; i < alloc_count; i++) {
		if (allocs[i] == ptr) {
			allocs[i] = p;
			return p;
		}
	}
	track_alloc(p);
	return p;
}

void devm_kfree(struct device *dev, const void *ptr)
{
	int i;

	(void)dev;
	for (i = 0; i < alloc_count; i++) {
		if (allocs[i] == ptr) {
			free(allocs[i]);
			allocs[i] = NULL;
			return;
		}
	}
}

void kfree(const void *ptr)
{
	free((void *)ptr);
}

void udelay(unsigned long us)
{
	model_udelay_total += (int)us;
}

void usleep_range(unsigned long min, unsigned long max)
{
	(void)max;
	model_udelay_total += (int)min;
}

struct clk *devm_clk_get_enabled(struct device *dev, const char *id)
{
	(void)dev;
	(void)id;
	model_clk.enabled = 1;
	model_clk.enable_calls++;
	return &model_clk;
}

int clk_prepare_enable(struct clk *clk)
{
	clk->enabled = 1;
	clk->enable_calls++;
	return 0;
}

void clk_disable_unprepare(struct clk *clk)
{
	clk->enabled = 0;
	clk->disable_calls++;
}

struct resource *platform_get_mem_or_io(struct platform_device *pdev, unsigned int num)
{
	(void)pdev;
	return num == 0 ? &model_res : NULL;
}

void __iomem *devm_platform_get_and_ioremap_resource(struct platform_device *pdev,
						     unsigned int index,
						     struct resource **res)
{
	(void)pdev;
	(void)index;
	if (res)
		*res = &model_res;
	mmio_base_ptr = (u8 *)mmio_regs;
	return mmio_base_ptr;
}

struct reset_control *devm_reset_control_get_by_index(struct device *dev, int index)
{
	(void)dev;
	return index == 0 ? &model_rst : ERR_PTR(-ENODEV);
}

int reset_control_assert(struct reset_control *rstc)
{
	if (force_reset_fail)
		return -EIO;
	rstc->asserted = 1;
	rstc->assert_calls++;
	memset(mmio_regs, 0, sizeof(mmio_regs));
	return 0;
}

int reset_control_deassert(struct reset_control *rstc)
{
	rstc->asserted = 0;
	rstc->deassert_calls++;
	return 0;
}

int platform_get_irq(struct platform_device *pdev, unsigned int num)
{
	(void)pdev;
	return num == 0 ? 201 : -ENXIO;
}

int devm_request_threaded_irq(struct device *dev, unsigned int irq,
			      irq_handler_t handler, irq_handler_t thread_fn,
			      unsigned long irqflags, const char *devname, void *dev_id)
{
	(void)dev;
	(void)irq;
	(void)handler;
	(void)irqflags;
	(void)devname;
	registered_irq_fn = thread_fn;
	registered_irq_data = dev_id;
	record_devm(3, NULL, NULL);
	return 0;
}

const char *lvts_model_next_cell(int *idx)
{
	static const char *const cells[] = { "e_data1", "e_data2", NULL };

	if (idx == NULL)
		cell_iter_idx = 0;
	else
		cell_iter_idx++;
	return cells[cell_iter_idx];
}

struct nvmem_cell *of_nvmem_cell_get(struct device_node *np, const char *name)
{
	static struct nvmem_cell c0 = { .idx = 0 };
	static struct nvmem_cell c1 = { .idx = 1 };

	(void)np;
	if (strcmp(name, "e_data1") == 0)
		return &c0;
	if (strcmp(name, "e_data2") == 0)
		return &c1;
	return ERR_PTR(-ENOENT);
}

void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
	size_t off = cell->idx == 0 ? 0 : 44;
	size_t n = cell->idx == 0 ? 44 : 8;
	u8 *buf = malloc(n);

	memcpy(buf, efuse_buf + off, n);
	*len = n;
	return buf;
}

void nvmem_cell_put(struct nvmem_cell *cell)
{
	(void)cell;
}

struct thermal_zone_device *devm_thermal_of_zone_register(
	struct device *dev, int sensor_id, void *data,
	const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device *tz;

	(void)dev;
	if (zone_count >= LVTS_MAX_ZONES)
		return ERR_PTR(-ENOMEM);
	tz = &zones[zone_count++];
	memset(tz, 0, sizeof(*tz));
	tz->dt_id = sensor_id;
	tz->priv = data;
	tz->ops = ops;
	snprintf(tz->type, sizeof(tz->type), "lvts_%d", sensor_id);
	record_devm(2, NULL, NULL);
	return tz;
}

void thermal_zone_device_update(struct thermal_zone_device *tz,
				enum thermal_notify_event event)
{
	(void)event;
	tz->updates++;
}

u32 lvts_model_readl(const void __iomem *addr)
{
	size_t off = (const u8 *)addr - mmio_base_ptr;

	if (off >= LVTS_REG_SPACE || (off & 3)) {
		fprintf(stderr, "FAIL: out-of-range readl 0x%zx\n", off);
		abort();
	}
	return mmio_regs[off / 4];
}

void lvts_model_writel(u32 val, void __iomem *addr)
{
	size_t off = (const u8 *)addr - mmio_base_ptr;
	size_t ctrl = off / 0x100;
	size_t reg = off % 0x100;

	if (off >= LVTS_REG_SPACE || (off & 3)) {
		fprintf(stderr, "FAIL: out-of-range writel 0x%zx\n", off);
		abort();
	}
	if (reg == 0x10) {
		mmio_regs[off / 4] &= ~val;
		return;
	}
	mmio_regs[off / 4] = val;
	if (ctrl >= LVTS_CTRL_COUNT)
		return;
	if (reg == 0x48)
		calscale_writes[ctrl]++;
	if (reg == 0x50) {
		if (dev_cmd_count[ctrl] < 64)
			dev_cmd_log[ctrl][dev_cmd_count[ctrl]++] = val;
		mmio_regs[off / 4] = val & ~(BIT(24) | BIT(25));
		if (val == 0xC103FFFF) {
			reset_all_writes[ctrl]++;
		} else if (val == 0xC502FC00) {
			mmio_regs[(ctrl * 0x100 + 0x4C) / 4] = 0x81 + (u32)ctrl;
		} else if ((val & 0xFFFFFF00U) == 0xC1030D00U && (val & 0xFF) < 4) {
			rck_selected[ctrl] = val & 0xFF;
		} else if (val == 0xC1030302) {
			rck_kicks[ctrl]++;
			if (force_rck_timeout && ctrl == 2 && rck_selected[ctrl] == 0) {
				mmio_regs[off / 4] |= BIT(25);
				mmio_regs[(ctrl * 0x100 + 0xB0) / 4] = 2880;
			} else {
				mmio_regs[(ctrl * 0x100 + 0xB0) / 4] = rck_samples[ctrl][rck_selected[ctrl]];
			}
		} else if (val == 0xC10306B8) {
			low_power_single_writes[ctrl]++;
		}
	}
}

static void put_u32(u8 *buf, size_t word, u32 val)
{
	buf[word * 4 + 0] = (u8)(val & 0xff);
	buf[word * 4 + 1] = (u8)((val >> 8) & 0xff);
	buf[word * 4 + 2] = (u8)((val >> 16) & 0xff);
	buf[word * 4 + 3] = (u8)((val >> 24) & 0xff);
}

static struct thermal_zone_device *find_zone(int dt_id)
{
	int i;

	for (i = 0; i < zone_count; i++)
		if (zones[i].dt_id == dt_id)
			return &zones[i];
	return NULL;
}

static void reset_model(void)
{
	free_all_allocs();
	memset(mmio_regs, 0, sizeof(mmio_regs));
	memset(rck_samples, 0, sizeof(rck_samples));
	memset(rck_selected, 0, sizeof(rck_selected));
	memset(rck_kicks, 0, sizeof(rck_kicks));
	memset(calscale_writes, 0, sizeof(calscale_writes));
	memset(low_power_single_writes, 0, sizeof(low_power_single_writes));
	memset(reset_all_writes, 0, sizeof(reset_all_writes));
	memset(dev_cmd_count, 0, sizeof(dev_cmd_count));
	memset(dev_cmd_log, 0, sizeof(dev_cmd_log));
	memset(efuse_buf, 0, sizeof(efuse_buf));
	memset(&model_clk, 0, sizeof(model_clk));
	memset(&model_rst, 0, sizeof(model_rst));
	memset(zones, 0, sizeof(zones));
	zone_count = 0;
	model_udelay_total = 0;
	registered_irq_fn = NULL;
	registered_irq_data = NULL;
	force_rck_timeout = 0;
	force_reset_fail = 0;
	devm_count = 0;
	teardown_zones_before_stop = 0;
	teardown_irq_before_stop = 0;
	teardown_hw_stopped = 0;
}

static const void *find_mt6789_data(struct platform_driver *drv)
{
	const struct of_device_id *id = drv->driver.of_match_table;

	while (id && id->compatible) {
		if (strcmp(id->compatible, "mediatek,mt6789-lvts") == 0)
			return id->data;
		id++;
	}
	return NULL;
}

static int expected_raw(int gt, int temp)
{
	s64 off = (s64)gt * 500 + 250460;
	s64 num = (off - temp) << 14;

	return (int)(num / 250460);
}

static int expected_temp(int gt, u32 raw)
{
	s64 off = (s64)gt * 500 + 250460;

	return (int)((((s64)(raw & 0xFFFF) * (-250460)) >> 14) + off);
}

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", (msg)); \
		return 1; \
	} \
} while (0)

static int case_cold_boot_and_rck(void)
{
	struct platform_driver *drv = lvts_test_driver();
	struct platform_device pdev = { 0 };
	struct device_node np = { 0 };
	int c, s, temp;
	const u32 count_r[13] = {
		34100, 34200, 34300, 34400,
		35100, 35200, 35300, 35400,
		36100, 36200, 36300, 36400,
		37100,
	};
	const u32 count_rc[4] = { 2700, 2750, 2800, 2850 };
	const u8 doms[4] = { 1, 2, 0, 0 };
	const u32 masks[4] = { 0x20f, 0x20f, 0x20f, 0x201 };

	reset_model();
	put_u32(efuse_buf, 0, 63U << 24);
	put_u32(efuse_buf, 1, count_r[0] | (count_r[1] << 16));
	put_u32(efuse_buf, 2, count_r[2] | (count_r[3] << 16));
	put_u32(efuse_buf, 3, count_r[4] | (count_r[5] << 16));
	put_u32(efuse_buf, 4, count_r[6] | (count_r[7] << 16));
	put_u32(efuse_buf, 5, count_r[8] | (count_r[9] << 16));
	put_u32(efuse_buf, 6, count_r[10] | (count_r[11] << 16));
	put_u32(efuse_buf, 7, count_r[12]);
	for (c = 0; c < 4; c++)
		put_u32(efuse_buf, 8 + c, count_rc[c]);

	for (c = 0; c < 4; c++)
		for (s = 0; s < 4; s++)
			rck_samples[c][s] = count_rc[c] + 30;
	rck_samples[0][2] = count_rc[0] + 500;
	rck_samples[1][1] = 0;
	force_rck_timeout = 1;

	pdev.dev.of_node = &np;
	pdev.dev.match_data = find_mt6789_data(drv);
	pdev.dev.name = "1100b000.lvts";
	CHECK(pdev.dev.match_data != NULL, "missing mediatek,mt6789-lvts match");
	CHECK(drv->probe(&pdev) == 0, "probe failed");
	CHECK(model_rst.assert_calls == 1 && model_rst.deassert_calls == 1, "domain reset missing");
	CHECK(zone_count == 14, "expected 14 thermal zones (soc_max + 13 sensors)");
	CHECK(find_zone(0) != NULL, "missing soc_max dt_id 0 zone");

	for (c = 0; c < 4; c++) {
		u32 base = (u32)(c * 0x100 / 4);
		int n = c == 3 ? 1 : 4;

		CHECK(mmio_regs[base + 0xE4 / 4] == 1, "LVTS_CLKEN not enabled");
		CHECK(calscale_writes[c] == 1 && mmio_regs[base + 0x48 / 4] == 0x300,
		      "LVTS_CALSCALE not written to 0x300");
		CHECK(mmio_regs[base + 0x40 / 4] == 0x13121110, "LVTS_TSSEL wrong");
		CHECK(mmio_regs[base + 0x38 / 4] == 0, "LVTS_MSRCTL0 not 0");
		CHECK(mmio_regs[base + 0x04 / 4] == 0x0010000C, "LVTS_MONCTL1 wrong");
		CHECK(mmio_regs[base + 0x08 / 4] == 0x00010001, "LVTS_MONCTL2 wrong");
		CHECK(mmio_regs[base + 0x00 / 4] == masks[c], "LVTS_MONCTL0 sensor mask wrong");
		CHECK(low_power_single_writes[c] == 1, "missing 0xC10306B8 low-power single mode");
		CHECK(rck_kicks[c] == n, "unexpected RCK kick count");
		CHECK(mmio_regs[base + 0xC0 / 4] == ((((u32)doms[c] << 2) + 2U) << 16),
		      "LVTS_PROTCTL dominator or offset wrong");
		CHECK(mmio_regs[base + 0xCC / 4] == (u32)expected_raw(63, 113500),
		      "LVTS_PROTTC wrong for gt=63 113.5C");
		CHECK((mmio_regs[base + 0x0C / 4] & BIT(31)) != 0, "Stage-3 IRQ not enabled");

		for (s = 0; s < n; s++) {
			int idx = c * 4 + s;
			u32 rc_exp = count_rc[c] + 30;
			u32 edata_exp;

			if ((c == 0 && s == 2) || (c == 1 && s == 1) || (c == 2 && s == 0))
				rc_exp = count_rc[c];
			edata_exp = (u32)(((u64)rc_exp * count_r[idx]) >> 14);
			CHECK(mmio_regs[base + (0x54 + s * 4) / 4] == edata_exp,
			      "LVTS_EDATA calibration mismatch");
		}
	}

	for (c = 0; c < 4; c++) {
		int n = c == 3 ? 1 : 4;
		for (s = 0; s < n; s++) {
			u32 raw = 15500 - (u32)(c * 100 + s * 20);
			mmio_regs[(c * 0x100 + 0x90 + s * 4) / 4] = BIT(16) | raw;
		}
	}
	CHECK(find_zone(1)->ops->get_temp(find_zone(1), &temp) == 0, "sensor 1 get_temp failed");
	CHECK(temp == expected_temp(63, 15500), "sensor 1 temp formula mismatch (gt=63)");
	CHECK(find_zone(0)->ops->get_temp(find_zone(0), &temp) == 0, "soc_max get_temp failed");
	CHECK(temp == expected_temp(63, 15200), "soc_max did not return max across all 13 sensors");

	run_devm_teardown();
	CHECK(teardown_hw_stopped == 1, "devm hw_stop action missing");
	CHECK(teardown_irq_before_stop == 1, "IRQ not freed before LVTS hw_stop");
	CHECK(teardown_zones_before_stop == 14, "thermal zones not unregistered before LVTS hw_stop");
	for (c = 0; c < 4; c++)
		CHECK(mmio_regs[(c * 0x100 + 0xE4) / 4] == 0, "LVTS_CLKEN not disabled on teardown");
	free_all_allocs();
	printf("PASS cold-boot-and-rck\n");
	return 0;
}

static int case_fake_efuse_and_trips(void)
{
	struct platform_driver *drv = lvts_test_driver();
	struct platform_device pdev = { 0 };
	struct device_node np = { 0 };
	struct thermal_zone_device *tz1;
	struct thermal_zone_device *tz0;
	struct thermal_trip trip = { .type = THERMAL_TRIP_CRITICAL, .temperature = 110000 };
	u32 edata_exp;
	int c;

	reset_model();
	for (c = 0; c < 4; c++)
		for (int s = 0; s < 4; s++)
			rck_samples[c][s] = 2750;

	pdev.dev.of_node = &np;
	pdev.dev.match_data = find_mt6789_data(drv);
	CHECK(drv->probe(&pdev) == 0, "fake efuse probe failed");
	edata_exp = (u32)(((u64)2750 * 35000) >> 14);
	CHECK(mmio_regs[0x54 / 4] == edata_exp, "fake efuse EDATA mismatch");
	CHECK(mmio_regs[0xCC / 4] == (u32)expected_raw(60, 113500), "fake efuse golden_temp != 60");

	tz1 = find_zone(1);
	CHECK(tz1->ops->set_trips(tz1, 10000, 85000) == 0, "set_trips 10C/85C failed");
	CHECK(mmio_regs[0x34 / 4] == (u32)expected_raw(60, 10000),
	      "LVTS_OFFSETL overflowed on low temp 10C");
	CHECK(mmio_regs[0x30 / 4] == (u32)expected_raw(60, 85000), "LVTS_OFFSETH wrong for 85C");
	CHECK((mmio_regs[0x0C / 4] & (BIT(31) | BIT(3) | BIT(2))) == (BIT(31) | BIT(3) | BIT(2)),
	      "LVTS_MONINT missing Stage-3 or sensor0 offset bits");

	CHECK(tz1->ops->set_trips(tz1, -INT_MAX, INT_MAX) == 0, "set_trips INT_MAX failed");
	CHECK(mmio_regs[0x30 / 4] == 0, "LVTS_OFFSETH not clamped to 0 for INT_MAX");
	CHECK((mmio_regs[0x0C / 4] & (BIT(3) | BIT(2))) == 0, "INT_MAX left offset IRQs armed");
	CHECK((mmio_regs[0x0C / 4] & BIT(31)) != 0, "set_trips cleared Stage-3 bit");

	tz0 = find_zone(0);
	CHECK(tz0->ops->set_trip_temp(tz0, &trip, 110000) == 0, "soc_max set_trip_temp failed");
	for (c = 0; c < 4; c++)
		CHECK(mmio_regs[(c * 0x100 + 0xCC) / 4] == (u32)expected_raw(60, 110000),
		      "soc_max critical update did not update LVTS_PROTTC");
	CHECK(tz0->ops->set_trip_temp(tz0, &trip, -300000) == 0, "soc_max low critical failed");
	for (c = 0; c < 4; c++)
		CHECK(mmio_regs[(c * 0x100 + 0xCC) / 4] == 0x7FFF,
		      "LVTS_PROTTC not clamped to 0x7FFF");

	run_devm_teardown();
	free_all_allocs();
	printf("PASS fake-efuse-and-trips\n");
	return 0;
}

static int case_irq_and_suspend_resume(void)
{
	struct platform_driver *drv = lvts_test_driver();
	struct platform_device pdev = { 0 };
	struct device_node np = { 0 };
	struct thermal_zone_device *tz0, *tz1;
	struct thermal_trip trip = { .type = THERMAL_TRIP_CRITICAL, .temperature = 110000 };
	u32 saved_edata[4][4];
	int c, s;

	reset_model();
	put_u32(efuse_buf, 0, 58U << 24);
	for (c = 1; c <= 7; c++)
		put_u32(efuse_buf, (size_t)c, 35000U | (35000U << 16));
	for (c = 0; c < 4; c++) {
		put_u32(efuse_buf, (size_t)(8 + c), 2750U);
		for (s = 0; s < 4; s++)
			rck_samples[c][s] = 2760;
	}

	pdev.dev.of_node = &np;
	pdev.dev.match_data = find_mt6789_data(drv);
	CHECK(drv->probe(&pdev) == 0, "probe failed");

	tz0 = find_zone(0);
	tz1 = find_zone(1);
	CHECK(tz1->ops->set_trips(tz1, 80000, 105000) == 0, "set_trips failed");
	CHECK(tz0->ops->set_trip_temp(tz0, &trip, 110000) == 0, "set_trip_temp failed");

	for (c = 0; c < 4; c++)
		for (s = 0; s < 4; s++)
			saved_edata[c][s] = mmio_regs[(c * 0x100 + 0x54 + s * 4) / 4];

	mmio_regs[(3 * 0x100 + 0x10) / 4] = 0x00247C00;
	CHECK(registered_irq_fn(201, registered_irq_data) == IRQ_HANDLED,
	      "IRQ on CTRL3 unused slot failed");
	CHECK(mmio_regs[(3 * 0x100 + 0x10) / 4] == 0, "CTRL3 MONINTSTS not cleared");

	mmio_regs[0x10 / 4] = BIT(31) | 0x0009001F;
	CHECK(registered_irq_fn(201, registered_irq_data) == IRQ_HANDLED, "Stage-3 IRQ failed");
	CHECK(tz0->updates == 1 && tz1->updates == 1, "Stage-3 or sensor0 tz not updated");

	memset(rck_kicks, 0, sizeof(rck_kicks));
	memset(calscale_writes, 0, sizeof(calscale_writes));
	memset(low_power_single_writes, 0, sizeof(low_power_single_writes));
	CHECK(drv->driver.pm->suspend_noirq(&pdev.dev) == 0, "suspend failed");
	CHECK(model_clk.enabled == 0, "clk not disabled on suspend");
	for (c = 0; c < 4; c++) {
		CHECK(mmio_regs[(c * 0x100 + 0xE4) / 4] == 0, "LVTS_CLKEN not disabled on suspend");
		CHECK(reset_all_writes[c] >= 2, "LVTS_DEV_RESET_ALL not sent on suspend");
	}

	for (c = 0; c < LVTS_REG_SPACE / 4; c++)
		mmio_regs[c] = 0xDEADBEEF;

	CHECK(drv->driver.pm->resume_noirq(&pdev.dev) == 0, "resume failed");
	CHECK(model_clk.enabled == 1, "clk not re-enabled on resume");
	CHECK(model_rst.assert_calls == 2 && model_rst.deassert_calls == 2,
	      "domain reset not pulsed on resume");

	for (c = 0; c < 4; c++) {
		u32 base = (u32)(c * 0x100 / 4);

		CHECK(rck_kicks[c] == 0, "resume re-ran cold RCK measurement instead of cached");
		CHECK(calscale_writes[c] == 1 && mmio_regs[base + 0x48 / 4] == 0x300,
		      "resume did not restore LVTS_CALSCALE");
		CHECK(low_power_single_writes[c] == 1, "resume did not restore 0xC10306B8");
		CHECK(mmio_regs[base + 0x04 / 4] == 0x0010000C, "resume did not restore MONCTL1");
		CHECK(mmio_regs[base + 0x08 / 4] == 0x00010001, "resume did not restore MONCTL2");
		CHECK(mmio_regs[base + 0xCC / 4] == (u32)expected_raw(58, 110000),
		      "resume did not restore updated Stage-3 LVTS_PROTTC");
		CHECK((mmio_regs[base + 0x0C / 4] & BIT(31)) != 0,
		      "resume did not restore Stage-3 MONINT bit");
		for (s = 0; s < 4; s++)
			CHECK(mmio_regs[base + (0x54 + s * 4) / 4] == saved_edata[c][s],
			      "resume did not restore cached LVTS_EDATA");
	}
	CHECK(mmio_regs[0x34 / 4] == (u32)expected_raw(58, 80000),
	      "resume did not restore LVTS_OFFSETL");
	CHECK(mmio_regs[0x30 / 4] == (u32)expected_raw(58, 105000),
	      "resume did not restore LVTS_OFFSETH");
	CHECK((mmio_regs[0x0C / 4] & (BIT(3) | BIT(2))) == (BIT(3) | BIT(2)),
	      "resume did not restore sensor0 offset IRQ bits");

	CHECK(drv->driver.pm->suspend_noirq(&pdev.dev) == 0, "second suspend failed");
	force_reset_fail = 1;
	CHECK(drv->driver.pm->resume_noirq(&pdev.dev) == -EIO, "resume did not propagate reset failure");
	CHECK(model_clk.enabled == 0, "resume leaked enabled clk on hw_init error");
	force_reset_fail = 0;

	run_devm_teardown();
	free_all_allocs();
	printf("PASS irq-and-suspend-resume\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "cold-boot-and-rck") == 0)
		return case_cold_boot_and_rck();
	if (strcmp(argv[1], "fake-efuse-and-trips") == 0)
		return case_fake_efuse_and_trips();
	if (strcmp(argv[1], "irq-and-suspend-resume") == 0)
		return case_irq_and_suspend_resume();
	return 2;
}
