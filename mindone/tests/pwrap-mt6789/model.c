// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include "stubs.h"
#include "oracle.h"

extern struct platform_driver *const harness_driver;

static u32 window[0x1000 / 4];
static u16 pmic[0x10000 / 2];
static u32 fsm;
static u32 rdata;
static u32 init_done2;
static unsigned long ticks;
static struct resource pwrap_resource = { 0x10026000, 0x10026fff, "pwrap" };
static const struct regmap_config *regmap_config;
static void *regmap_context;
static irq_handler_t irq_handler;
static void *irq_context;
static int violations;

struct regmap {
	int unused;
};

static struct regmap the_regmap;

struct clk {
	const char *id;
	int enables;
};

static struct clk clks[ORACLE_CLOCKS];

struct reset_control {
	int unused;
};

static struct reset_control the_reset;

void harness_log(int level, const char *fmt, ...)
{
	va_list ap;

	if (level > 5)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "log%d: ", level);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void trace(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void violation(const char *fmt, ...)
{
	va_list ap;

	violations++;
	printf("VIOLATION ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return err;
}

int __ratelimit(struct ratelimit_state *rs)
{
	(void)rs;
	return 0;
}

unsigned long harness_jiffies(void)
{
	return ticks++;
}

static bool compatible(const struct device_node *np, const char *name)
{
	int i;

	for (i = 0; np && i < np->ncompatible; i++)
		if (!strcmp(np->compatible[i], name))
			return true;
	return false;
}

const struct of_device_id *of_match_node(const struct of_device_id *matches, const struct device_node *node)
{
	const struct of_device_id *best = NULL;
	int i, score = -1;

	for (; matches->compatible[0]; matches++)
		for (i = 0; i < node->ncompatible; i++)
			if (!strcmp(node->compatible[i], matches->compatible) && (score < 0 || i < score)) {
				best = matches;
				score = i;
			}
	return best;
}

const void *of_device_get_match_data(const struct device *dev)
{
	const struct of_device_id *id = of_match_node(harness_driver->driver.of_match_table, dev->of_node);

	return id ? id->data : NULL;
}

int platform_driver_register(struct platform_driver *drv)
{
	(void)drv;
	return 0;
}

struct resource *platform_get_resource_byname(struct platform_device *pdev, unsigned int type, const char *name)
{
	(void)pdev;
	if (type != IORESOURCE_MEM || strcmp(name, ORACLE_REG_NAME))
		return NULL;
	return &pwrap_resource;
}

void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res)
{
	(void)dev;
	if (res != &pwrap_resource)
		return ERR_PTR(-EINVAL);
	return window;
}

void __iomem *devm_platform_ioremap_resource_byname(struct platform_device *pdev, const char *name)
{
	return devm_ioremap_resource(&pdev->dev, platform_get_resource_byname(pdev, IORESOURCE_MEM, name));
}

int platform_get_irq(struct platform_device *pdev, unsigned int num)
{
	(void)pdev;
	return num ? -ENXIO : ORACLE_IRQ;
}

int devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler, unsigned long flags,
		     const char *name, void *dev_id)
{
	(void)dev;
	trace("IRQ %u flags=0x%lx name=%s", irq, flags, name);
	irq_handler = handler;
	irq_context = dev_id;
	return 0;
}

void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id)
{
	(void)dev;
	(void)dev_id;
	trace("IRQFREE %u", irq);
}

int of_platform_populate(struct device_node *root, const void *matches, const void *lookup, struct device *parent)
{
	(void)matches;
	(void)lookup;
	(void)parent;
	trace("POPULATE %s", root->name);
	return 0;
}

void of_platform_depopulate(struct device *parent)
{
	(void)parent;
	trace("DEPOPULATE");
}

void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	(void)dev;
	(void)gfp;
	return calloc(1, size);
}

struct reset_control *devm_reset_control_get(struct device *dev, const char *id)
{
	(void)dev;
	trace("RESETGET %s", id);
	return &the_reset;
}

int reset_control_reset(struct reset_control *rstc)
{
	(void)rstc;
	trace("RESET");
	return 0;
}

static struct clk *clock_named(const char *name)
{
	int i;

	for (i = 0; i < ORACLE_CLOCKS; i++)
		if (!strcmp(oracle_clock_names[i], name))
			return &clks[i];
	return NULL;
}

struct clk *devm_clk_get(struct device *dev, const char *id)
{
	struct clk *clk = clock_named(id);

	(void)dev;
	return clk ? clk : ERR_PTR(-ENOENT);
}

int clk_prepare_enable(struct clk *clk)
{
	clk->enables++;
	trace("CLK+ %s", clk->id);
	return 0;
}

void clk_disable_unprepare(struct clk *clk)
{
	clk->enables--;
	trace("CLK- %s", clk->id);
}

int devm_clk_bulk_get_all_enabled(struct device *dev, struct clk_bulk_data **out)
{
	static struct clk_bulk_data bulk[ORACLE_CLOCKS];
	int i;

	(void)dev;
	for (i = 0; i < ORACLE_CLOCKS; i++) {
		bulk[i].id = oracle_clock_names[i];
		bulk[i].clk = &clks[i];
		clk_prepare_enable(&clks[i]);
	}
	*out = bulk;
	return ORACLE_CLOCKS;
}

static u32 offset_of(const volatile void *addr)
{
	const volatile u8 *p = addr;
	const u8 *base = (const u8 *)window;

	if (p < base || p >= base + sizeof(window) || ((p - base) & 3)) {
		violation("access outside the pwrap window");
		return 0;
	}
	return (u32)(p - base);
}

u32 readl(const volatile void __iomem *addr)
{
	u32 ofs = offset_of(addr);
	u32 val;

	if (ofs == ORACLE_WACS2_RDATA)
		val = (fsm << 1) | BIT(15) | (fsm ? 0 : BIT(20));
	else if (ofs == ORACLE_SWINF_2_RDATA_31_0)
		val = rdata;
	else if (ofs == ORACLE_INIT_DONE2)
		val = init_done2;
	else
		val = window[ofs / 4];
	trace("R 0x%03x", ofs);
	return val;
}

void writel(u32 value, volatile void __iomem *addr)
{
	u32 ofs = offset_of(addr);

	trace("W 0x%03x 0x%08x", ofs, value);
	if (ofs == ORACLE_WACS2_CMD) {
		if (fsm)
			violation("command while the FSM waits for VLDCLR");
		if (value & BIT(29)) {
			pmic[(value & 0xffff) / 2] = window[ORACLE_SWINF_2_WDATA_31_0 / 4] & 0xffff;
			trace("PMIC write 0x%04x=0x%04x", value & 0xffff, window[ORACLE_SWINF_2_WDATA_31_0 / 4] & 0xffff);
		} else {
			rdata = pmic[(value & 0xffff) / 2];
			fsm = 6;
			trace("PMIC read 0x%04x", value & 0xffff);
		}
	} else if (ofs == ORACLE_WACS2_VLDCLR) {
		fsm = 0;
	}
	window[ofs / 4] = value;
}

void udelay(unsigned long us)
{
	trace("DELAY %lu", us);
}

void msleep(unsigned int ms)
{
	trace("SLEEP %u", ms);
}

void usleep_range(unsigned long min, unsigned long max)
{
	trace("SLEEP %lu-%lu", min, max);
}

struct regmap *devm_regmap_init(struct device *dev, const void *bus, void *bus_context,
				const struct regmap_config *config)
{
	(void)dev;
	(void)bus;
	trace("REGMAP reg_bits=%d val_bits=%d stride=%d max=0x%x", config->reg_bits, config->val_bits,
	      config->reg_stride, config->max_register);
	regmap_config = config;
	regmap_context = bus_context;
	return &the_regmap;
}

int main(int argc, char **argv)
{
	static struct device_node child = { .compatible = oracle_pmic_compatible,
					    .ncompatible = ARRAY_SIZE(oracle_pmic_compatible) };
	static struct device_node node = { .compatible = oracle_pwrap_compatible,
					   .ncompatible = ARRAY_SIZE(oracle_pwrap_compatible), .child = &child };
	static struct platform_device pdev;
	unsigned int val = 0;
	int i, ret;

	(void)argc;
	init_done2 = strcmp(argv[1], "cold") ? 1 : 0;
	child.name = "pmic";
	node.name = "pwrap";
	for (i = 0; i < ORACLE_CLOCKS; i++)
		clks[i].id = oracle_clocks[i];
	pmic[0x0008 / 2] = 0x6620;
	pmic[0x040e / 2] = 0x5aa5;
	pdev.dev.of_node = &node;
	ret = harness_driver->probe(&pdev);
	trace("RESULT %d", ret);
	if (ret)
		return 0;
	if (!regmap_config || !irq_handler) {
		violation("no regmap or interrupt");
		return 0;
	}
	trace("PHASE regmap");
	ret = regmap_config->reg_read(regmap_context, 0x0008, &val);
	trace("READ 0x0008 -> %d 0x%04x", ret, val);
	ret = regmap_config->reg_write(regmap_context, 0x0ec2, 0x1234);
	trace("WRITE 0x0ec2 -> %d", ret);
	ret = regmap_config->reg_read(regmap_context, 0x0ec2, &val);
	trace("READ 0x0ec2 -> %d 0x%04x", ret, val);
	trace("PHASE irq");
	trace("IRQRET %d", irq_handler(ORACLE_IRQ, irq_context));
	for (i = 0; i < ORACLE_CLOCKS; i++)
		trace("CLKSTATE %s=%d", clks[i].id, clks[i].enables);
	trace("VIOLATIONS %d", violations);
	return 0;
}
