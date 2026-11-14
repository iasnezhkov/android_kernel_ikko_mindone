/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include "oracle.h"

#define MAX_REGIONS 64
#define REGION_WORDS 1024
#define MAX_CLKS 1024
#define MAX_DRIVERS 64
#define MAX_PARENTS 32
#define TRACE_LEN 65536

enum { KIND_CUSTOM, KIND_FACTOR, KIND_FIXED, KIND_COMPOSITE, KIND_DIVIDER };

struct region {
	const char *compatible;
	u32 mem[REGION_WORDS];
	struct device_node node;
	struct regmap *regmap;
	void *provider;
	int provider_kind;
	int probed;
	int probe_ret;
	bool mapped;
};

struct regmap {
	int region;
};

struct hclk {
	char name[64];
	char parents[MAX_PARENTS][64];
	int nparents;
	unsigned long flags;
	const struct clk_ops *ops;
	struct clk_hw *hw;
	int kind;
	unsigned int mult;
	unsigned int div;
	unsigned long rate;
	int region;
	struct clk_hw *mux_hw;
	struct clk_hw *rate_hw;
	struct clk_hw *gate_hw;
	const struct clk_ops *mux_ops;
	const struct clk_ops *rate_ops;
	const struct clk_ops *gate_ops;
	int div_shift;
	int div_width;
	int div_flags;
	void *div_reg;
};

const struct clk_ops clk_mux_ops;
const struct clk_ops clk_gate_ops;
const struct clk_ops clk_divider_ops;

static struct region regions[MAX_REGIONS];
static int nregions;
static struct hclk clks[MAX_CLKS];
static int nclks;
static struct platform_driver *drivers[MAX_DRIVERS];
static int ndrivers;
static int current_region = -1;
static bool spm_present = true;
static bool tracing;
static char trace[TRACE_LEN];
static size_t trace_len;
static int log_errors;
static const char *muxdet;

static void tr(const char *fmt, ...)
{
	va_list args;

	if (!tracing || trace_len >= TRACE_LEN - 64)
		return;
	va_start(args, fmt);
	trace_len += vsnprintf(trace + trace_len, TRACE_LEN - trace_len, fmt, args);
	va_end(args);
}

static int snap_region = -1;
static u32 snapshot[REGION_WORDS];

static void trace_start(void)
{
	trace_len = 0;
	trace[0] = 0;
	tracing = true;
	if (snap_region >= 0)
		memcpy(snapshot, regions[snap_region].mem, sizeof(snapshot));
}

static const char *trace_stop(void)
{
	int i;
	bool first = true;

	if (snap_region >= 0)
		for (i = 0; i < REGION_WORDS; i++)
			if (regions[snap_region].mem[i] != snapshot[i]) {
				tr("%s %x:%x", first ? " =>" : "", i * 4, regions[snap_region].mem[i]);
				first = false;
			}
	tracing = false;
	return trace_len ? trace : "-";
}

void harness_log(int level, const char *fmt, ...)
{
	va_list args;

	if (level <= 3)
		log_errors++;
	if (getenv("CLK_UPSTREAM_VERBOSE")) {
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf, 4096, fmt, args);
	va_end(args);
	return n;
}

int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf + at, 4096 - at, fmt, args);
	va_end(args);
	return n;
}

void harness_udelay(unsigned long us)
{
	tr(" D%lu", us);
}

extern bool (*mtk_fh_set_rate)(const char *name, unsigned long dds, int postdiv);

static bool harness_hop(const char *name, unsigned long dds, int postdiv)
{
	tr(" H%s:%lx:%d", name, dds, postdiv);
	return true;
}

static int region_of(const char *compatible)
{
	int i;

	for (i = 0; i < nregions; i++)
		if (!strcmp(regions[i].compatible, compatible))
			return i;
	if (nregions == MAX_REGIONS) {
		fprintf(stderr, "too many regions\n");
		exit(2);
	}
	regions[nregions].compatible = compatible;
	regions[nregions].node.compatible = compatible;
	regions[nregions].node.full_name = compatible;
	regions[nregions].node.region = nregions;
	return nregions++;
}

static int addr_region(const volatile void *addr, unsigned int *offset)
{
	int i;

	for (i = 0; i < nregions; i++) {
		const char *base = (const char *)regions[i].mem;

		if ((const char *)addr >= base && (const char *)addr < base + sizeof(regions[i].mem)) {
			*offset = (const char *)addr - base;
			return i;
		}
	}
	fprintf(stderr, "access outside every region: %p\n", (void *)addr);
	exit(2);
}

static const char *short_name(int region)
{
	return strcmp(regions[region].compatible, ORACLE_SPM_COMPATIBLE) ? "" : "spm:";
}

u32 harness_readl(const volatile void *addr)
{
	unsigned int offset;
	int r = addr_region(addr, &offset);
	u32 val = regions[r].mem[offset / 4];

	tr(" R%s%x=%x", short_name(r), offset, val);
	return val;
}

void harness_writel(u32 val, volatile void *addr)
{
	unsigned int offset;
	int r = addr_region(addr, &offset);

	tr(" W%s%x=%x", short_name(r), offset, val);
	regions[r].mem[offset / 4] = val;
}

void *harness_iomap(struct device_node *np)
{
	if (!np)
		return NULL;
	regions[np->region].mapped = true;
	return regions[np->region].mem;
}

void harness_iounmap(volatile void *addr)
{
	(void)addr;
}

void *devm_kzalloc(struct device *dev, size_t size, int flags)
{
	(void)dev;
	return kzalloc(size, flags);
}

static struct regmap *regmap_of(struct device_node *np)
{
	struct region *r = &regions[np->region];

	if (!r->regmap) {
		r->regmap = calloc(1, sizeof(*r->regmap));
		r->regmap->region = np->region;
	}
	return r->regmap;
}

struct regmap *syscon_node_to_regmap(struct device_node *np)
{
	return regmap_of(np);
}

struct regmap *device_node_to_regmap(struct device_node *np)
{
	return regmap_of(np);
}

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np, const char *property)
{
	(void)np;
	(void)property;
	return ERR_PTR(-ENODEV);
}

struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index)
{
	(void)np;
	(void)name;
	(void)index;
	return NULL;
}

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	*val = regions[map->region].mem[reg / 4];
	tr(" R%s%x=%x", short_name(map->region), reg, *val);
	return 0;
}

int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	tr(" W%s%x=%x", short_name(map->region), reg, val);
	regions[map->region].mem[reg / 4] = val;
	return 0;
}

int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int old;

	regmap_read(map, reg, &old);
	if (((old & ~mask) | (val & mask)) != old)
		regmap_write(map, reg, (old & ~mask) | (val & mask));
	return 0;
}

int of_device_is_compatible(const struct device_node *np, const char *compatible)
{
	return np && !strcmp(np->compatible, compatible);
}

struct device_node *of_find_compatible_node(struct device_node *from, const char *type,
					    const char *compatible)
{
	(void)from;
	(void)type;
	if (!strcmp(compatible, ORACLE_SPM_COMPATIBLE) && !spm_present)
		return NULL;
	return &regions[region_of(compatible)].node;
}

static struct resource the_resource = { 0x10000000, 0x10000fff, IORESOURCE_MEM };

struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num)
{
	(void)pdev;
	(void)type;
	(void)num;
	return &the_resource;
}

void *devm_ioremap_resource(struct device *dev, struct resource *res)
{
	(void)res;
	return harness_iomap(dev->of_node);
}

void *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index)
{
	(void)index;
	return harness_iomap(pdev->dev.of_node);
}

int devm_pm_runtime_enable(struct device *dev)
{
	dev->pm_enabled = 1;
	return 0;
}

int pm_runtime_resume_and_get(struct device *dev)
{
	dev->pm_usage++;
	return 0;
}

int pm_runtime_put(struct device *dev)
{
	dev->pm_usage--;
	return 0;
}

int mtk_register_reset_controller_with_dev(struct device *dev, const struct mtk_clk_rst_desc *desc)
{
	(void)dev;
	(void)desc;
	fprintf(stderr, "unexpected reset controller\n");
	exit(2);
}

void harness_add_driver(struct platform_driver *drv)
{
	if (ndrivers == MAX_DRIVERS)
		exit(2);
	drivers[ndrivers++] = drv;
}

int platform_register_drivers(struct platform_driver * const *list, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		harness_add_driver(list[i]);
	return 0;
}

void platform_unregister_drivers(struct platform_driver * const *list, unsigned int count)
{
	(void)list;
	(void)count;
}

static struct hclk *new_clk(const char *name, int kind)
{
	struct hclk *c;
	int i;

	for (i = 0; i < nclks; i++)
		if (!strcmp(clks[i].name, name)) {
			fprintf(stderr, "duplicate clock name %s\n", name);
			exit(2);
		}
	if (nclks == MAX_CLKS)
		exit(2);
	c = &clks[nclks++];
	snprintf(c->name, sizeof(c->name), "%s", name);
	c->kind = kind;
	c->region = current_region;
	return c;
}

static void set_parents(struct hclk *c, const char * const *names, int n)
{
	int i;

	if (n > MAX_PARENTS)
		exit(2);
	c->nparents = n;
	for (i = 0; i < n; i++)
		snprintf(c->parents[i], sizeof(c->parents[i]), "%s", names[i]);
}

static struct clk *clk_of(struct clk_hw *hw, struct hclk *c)
{
	struct clk *clk = calloc(1, sizeof(*clk));

	clk->hw = hw;
	clk->index = c - clks;
	hw->clk = clk;
	c->hw = hw;
	return clk;
}

static void record_init(struct clk_hw *hw)
{
	const struct clk_init_data *init = hw->init;
	struct hclk *c = new_clk(init->name, KIND_CUSTOM);

	set_parents(c, init->parent_names, init->num_parents);
	c->flags = init->flags;
	c->ops = init->ops;
	clk_of(hw, c);
}

struct clk *clk_register(struct device *dev, struct clk_hw *hw)
{
	(void)dev;
	record_init(hw);
	return hw->clk;
}

int clk_hw_register(struct device *dev, struct clk_hw *hw)
{
	(void)dev;
	record_init(hw);
	return 0;
}

void clk_hw_unregister(struct clk_hw *hw)
{
	(void)hw;
	fprintf(stderr, "unexpected unregister\n");
	exit(2);
}

static struct clk_hw *fixed(const char *name, const char *parent, unsigned long flags, unsigned long rate)
{
	struct hclk *c = new_clk(name, KIND_FIXED);
	struct clk_hw *hw = calloc(1, sizeof(*hw));

	if (parent)
		set_parents(c, &parent, 1);
	c->flags = flags;
	c->rate = rate;
	clk_of(hw, c);
	return hw;
}

struct clk *clk_register_fixed_rate(struct device *dev, const char *name, const char *parent,
				    unsigned long flags, unsigned long rate)
{
	(void)dev;
	return fixed(name, parent, flags, rate)->clk;
}

struct clk_hw *clk_hw_register_fixed_rate(struct device *dev, const char *name, const char *parent,
					  unsigned long flags, unsigned long rate)
{
	(void)dev;
	return fixed(name, parent, flags, rate);
}

void clk_hw_unregister_fixed_rate(struct clk_hw *hw)
{
	clk_hw_unregister(hw);
}

static struct clk_hw *factor(const char *name, const char *parent, unsigned long flags, unsigned int mult,
			     unsigned int div)
{
	struct hclk *c = new_clk(name, KIND_FACTOR);
	struct clk_hw *hw = calloc(1, sizeof(*hw));

	set_parents(c, &parent, 1);
	c->flags = flags;
	c->mult = mult;
	c->div = div;
	clk_of(hw, c);
	return hw;
}

struct clk *clk_register_fixed_factor(struct device *dev, const char *name, const char *parent,
				      unsigned long flags, unsigned int mult, unsigned int div)
{
	(void)dev;
	return factor(name, parent, flags, mult, div)->clk;
}

struct clk_hw *clk_hw_register_fixed_factor(struct device *dev, const char *name, const char *parent,
					    unsigned long flags, unsigned int mult, unsigned int div)
{
	(void)dev;
	return factor(name, parent, flags, mult, div);
}

void clk_hw_unregister_fixed_factor(struct clk_hw *hw)
{
	clk_hw_unregister(hw);
}

static struct clk_hw *divider(const char *name, const char *parent, unsigned long flags, void *reg, u8 shift,
			      u8 width, u8 divider_flags)
{
	struct hclk *c = new_clk(name, KIND_DIVIDER);
	struct clk_hw *hw = calloc(1, sizeof(*hw));

	set_parents(c, &parent, 1);
	c->flags = flags;
	c->div_reg = reg;
	c->div_shift = shift;
	c->div_width = width;
	c->div_flags = divider_flags;
	clk_of(hw, c);
	return hw;
}

struct clk *clk_register_divider(struct device *dev, const char *name, const char *parent,
				 unsigned long flags, void __iomem *reg, u8 shift, u8 width,
				 u8 divider_flags, spinlock_t *lock)
{
	(void)dev;
	(void)lock;
	return divider(name, parent, flags, reg, shift, width, divider_flags)->clk;
}

struct clk_hw *clk_hw_register_divider(struct device *dev, const char *name, const char *parent,
				       unsigned long flags, void __iomem *reg, u8 shift, u8 width,
				       u8 divider_flags, spinlock_t *lock)
{
	(void)dev;
	(void)lock;
	return divider(name, parent, flags, reg, shift, width, divider_flags);
}

void clk_hw_unregister_divider(struct clk_hw *hw)
{
	clk_hw_unregister(hw);
}

static struct clk_hw *composite(const char *name, const char * const *parent_names, int num_parents,
				struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
				struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
				struct clk_hw *gate_hw, const struct clk_ops *gate_ops, unsigned long flags)
{
	struct hclk *c = new_clk(name, KIND_COMPOSITE);
	struct clk_composite *comp = calloc(1, sizeof(*comp));

	set_parents(c, parent_names, num_parents);
	c->flags = flags;
	c->mux_hw = mux_hw;
	c->rate_hw = rate_hw;
	c->gate_hw = gate_hw;
	c->mux_ops = mux_ops;
	c->rate_ops = rate_ops;
	c->gate_ops = gate_ops;
	comp->mux_hw = mux_hw;
	comp->rate_hw = rate_hw;
	comp->gate_hw = gate_hw;
	clk_of(&comp->hw, c);
	return &comp->hw;
}

struct clk *clk_register_composite(struct device *dev, const char *name,
				   const char * const *parent_names, int num_parents,
				   struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
				   struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
				   struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
				   unsigned long flags)
{
	(void)dev;
	return composite(name, parent_names, num_parents, mux_hw, mux_ops, rate_hw, rate_ops, gate_hw, gate_ops,
			 flags)->clk;
}

struct clk_hw *clk_hw_register_composite(struct device *dev, const char *name,
					 const char * const *parent_names, int num_parents,
					 struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
					 struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
					 struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
					 unsigned long flags)
{
	(void)dev;
	return composite(name, parent_names, num_parents, mux_hw, mux_ops, rate_hw, rate_ops, gate_hw, gate_ops,
			 flags);
}

void clk_hw_unregister_composite(struct clk_hw *hw)
{
	clk_hw_unregister(hw);
}

struct clk *of_clk_src_onecell_get(struct of_phandle_args *args, void *data)
{
	(void)args;
	(void)data;
	return NULL;
}

struct clk_hw *of_clk_hw_onecell_get(struct of_phandle_args *args, void *data)
{
	(void)args;
	(void)data;
	return NULL;
}

int of_clk_add_provider(struct device_node *np,
			struct clk *(*get)(struct of_phandle_args *args, void *data), void *data)
{
	(void)get;
	regions[np->region].provider = data;
	regions[np->region].provider_kind = 1;
	return 0;
}

int of_clk_add_hw_provider(struct device_node *np,
			   struct clk_hw *(*get)(struct of_phandle_args *args, void *data), void *data)
{
	(void)get;
	regions[np->region].provider = data;
	regions[np->region].provider_kind = 2;
	return 0;
}

void of_clk_del_provider(struct device_node *np)
{
	regions[np->region].provider = NULL;
}

const char *clk_hw_get_name(const struct clk_hw *hw)
{
	return clks[hw->clk->index].name;
}

struct clk_hw *__clk_get_hw(struct clk *clk)
{
	return clk ? clk->hw : NULL;
}

int __clk_mux_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	(void)hw;
	(void)req;
	muxdet = "clk_mux_determine_rate_flags(0)";
	return 0;
}

int clk_mux_determine_rate_flags(struct clk_hw *hw, struct clk_rate_request *req, unsigned long flags)
{
	(void)hw;
	(void)req;
	muxdet = flags ? "clk_mux_determine_rate_flags(nonzero)" : "clk_mux_determine_rate_flags(0)";
	return 0;
}

static void flags_text(unsigned long flags, char *out, size_t len)
{
	static const struct {
		unsigned long bit;
		const char *name;
	} names[] = {
		{ CLK_SET_RATE_GATE, "CLK_SET_RATE_GATE" },
		{ CLK_SET_PARENT_GATE, "CLK_SET_PARENT_GATE" },
		{ CLK_SET_RATE_PARENT, "CLK_SET_RATE_PARENT" },
		{ CLK_IGNORE_UNUSED, "CLK_IGNORE_UNUSED" },
		{ CLK_GET_RATE_NOCACHE, "CLK_GET_RATE_NOCACHE" },
		{ CLK_SET_RATE_NO_REPARENT, "CLK_SET_RATE_NO_REPARENT" },
		{ CLK_GET_ACCURACY_NOCACHE, "CLK_GET_ACCURACY_NOCACHE" },
		{ CLK_RECALC_NEW_RATES, "CLK_RECALC_NEW_RATES" },
		{ CLK_SET_RATE_UNGATE, "CLK_SET_RATE_UNGATE" },
		{ CLK_IS_CRITICAL, "CLK_IS_CRITICAL" },
		{ CLK_OPS_PARENT_ENABLE, "CLK_OPS_PARENT_ENABLE" },
		{ CLK_DUTY_CYCLE_PARENT, "CLK_DUTY_CYCLE_PARENT" },
	};
	size_t i, n = 0;

	out[0] = 0;
	for (i = 0; i < ARRAY_SIZE(names); i++)
		if (flags & names[i].bit) {
			n += snprintf(out + n, len - n, "%s|", names[i].name);
			flags &= ~names[i].bit;
		}
	if (flags)
		snprintf(out + n, len - n, "0x%lx|", flags);
}

static unsigned int reg_offset(void *reg)
{
	unsigned int offset;

	addr_region(reg, &offset);
	return offset;
}

static void fill(int region, int pattern)
{
	int i;

	for (i = 0; i < REGION_WORDS; i++) {
		u32 v;

		switch (pattern) {
		case 0:
			v = 0;
			break;
		case 1:
			v = 0xffffffff;
			break;
		case 2:
			v = 0x9e3779b9u * (u32)(i + 1);
			break;
		default:
			v = 0x85ebca6bu * (u32)(i + 7) ^ 0xc2b2ae35u;
			break;
		}
		regions[region].mem[i] = v;
	}
}

static int spm_region(void)
{
	return region_of(ORACLE_SPM_COMPATIBLE);
}

enum { F_STA, F_STA2, F_ON, F_ON2, F_ISO, F_RSTB, F_CLKDIS, NFAULTS };

#define CON_RST_B BIT(0)
#define CON_ISO BIT(1)
#define CON_ON BIT(2)
#define CON_ON_2ND BIT(3)
#define CON_CLK_DIS BIT(4)
#define CON_UP (CON_RST_B | CON_ON | CON_ON_2ND)

static void spm_all_up(void)
{
	int s = spm_region();
	size_t i;

	memset(regions[s].mem, 0, sizeof(regions[s].mem));
	for (i = 0; i < ARRAY_SIZE(oracle_domains); i++) {
		regions[s].mem[ORACLE_PWR_STATUS / 4] |= oracle_domains[i].sta_mask;
		regions[s].mem[ORACLE_PWR_STATUS_2ND / 4] |= oracle_domains[i].sta_mask;
		regions[s].mem[oracle_domains[i].con_ofs / 4] = CON_UP;
	}
}

static void spm_fault(size_t d, int fault)
{
	u32 *m = regions[spm_region()].mem;
	u32 con = oracle_domains[d].con_ofs / 4;

	switch (fault) {
	case F_STA:
		m[ORACLE_PWR_STATUS / 4] &= ~oracle_domains[d].sta_mask;
		break;
	case F_STA2:
		m[ORACLE_PWR_STATUS_2ND / 4] &= ~oracle_domains[d].sta_mask;
		break;
	case F_ON:
		m[con] &= ~CON_ON;
		break;
	case F_ON2:
		m[con] &= ~CON_ON_2ND;
		break;
	case F_ISO:
		m[con] |= CON_ISO;
		break;
	case F_RSTB:
		m[con] &= ~CON_RST_B;
		break;
	default:
		m[con] |= CON_CLK_DIS;
		break;
	}
}

static bool cg_read_happens(struct hclk *c, int *result)
{
	char needle[32];
	const char *t;

	snprintf(needle, sizeof(needle), " R");
	trace_start();
	*result = c->ops->is_enabled(c->hw);
	t = trace_stop();
	while ((t = strstr(t, needle))) {
		if (strncmp(t, " Rspm:", 6))
			return true;
		t += 2;
	}
	return false;
}

static const unsigned long rates[] = {
	26000000, 22579200, 24576000, 180633600, 196608000, 208000000, 286000000, 390000000,
	416000000, 594000000, 1000000000, 1200000000, 1340000000, 1500000000, 2000000000,
	2184000000UL, 2290000000UL, 2496000000UL, 2750000000UL, 3800000000UL, 4500000000UL,
};

static int cmp_clk(const void *a, const void *b)
{
	return strcmp(((const struct hclk *)a)->name, ((const struct hclk *)b)->name);
}

static bool print_rate_op(struct hclk *c, unsigned long rate)
{
	if (c->ops->determine_rate) {
		struct clk_rate_request req = { .rate = rate, .best_parent_rate = 26000000 };

		muxdet = NULL;
		c->ops->determine_rate(c->hw, &req);
		if (muxdet) {
			printf("%s determine %s\n", c->name, muxdet);
			return false;
		}
		printf("%s round %lu -> %lu\n", c->name, rate, req.rate);
	} else if (c->ops->round_rate) {
		unsigned long prate = 26000000;

		printf("%s round %lu -> %ld\n", c->name, rate, c->ops->round_rate(c->hw, rate, &prate));
	}
	return true;
}

static void exercise(struct hclk *c)
{
	const struct clk_ops *o = c->ops;
	int r = c->region;
	int p;
	unsigned int i;
	const char *pn[] = { "zero", "ones", "mix1", "mix2" };

	snap_region = r;
	spm_all_up();
	if (o->is_prepared)
		for (p = 0; p < 4; p++) {
			int v;

			fill(r, p);
			trace_start();
			v = o->is_prepared(c->hw);
			printf("%s is_prepared %s -> %d |%s\n", c->name, pn[p], v, trace_stop());
		}
	if (o->is_enabled)
		for (p = 0; p < 4; p++) {
			int v;

			fill(r, p);
			trace_start();
			v = o->is_enabled(c->hw);
			printf("%s is_enabled %s -> %d |%s\n", c->name, pn[p], v, trace_stop());
		}
	for (p = 0; p < 3; p++) {
		if (o->prepare) {
			fill(r, p);
			trace_start();
			o->prepare(c->hw);
			printf("%s prepare %s |%s\n", c->name, pn[p], trace_stop());
		}
		if (o->unprepare) {
			fill(r, p);
			trace_start();
			o->unprepare(c->hw);
			printf("%s unprepare %s |%s\n", c->name, pn[p], trace_stop());
		}
		if (o->enable) {
			fill(r, p);
			trace_start();
			o->enable(c->hw);
			printf("%s enable %s |%s\n", c->name, pn[p], trace_stop());
		}
		if (o->disable) {
			fill(r, p);
			trace_start();
			o->disable(c->hw);
			printf("%s disable %s |%s\n", c->name, pn[p], trace_stop());
		}
		if (o->disable_unused || o->disable) {
			fill(r, p);
			trace_start();
			if (o->disable_unused)
				o->disable_unused(c->hw);
			else
				o->disable(c->hw);
			printf("%s disable_unused %s |%s\n", c->name, pn[p], trace_stop());
		}
	}
	if (o->get_parent)
		for (p = 0; p < 4; p++) {
			int v;

			fill(r, p);
			trace_start();
			v = o->get_parent(c->hw);
			printf("%s get_parent %s -> %d |%s\n", c->name, pn[p], v, trace_stop());
		}
	if (o->set_parent)
		for (p = 0; p < 3; p += 2)
			for (i = 0; i < (unsigned int)c->nparents; i++) {
				fill(r, p);
				trace_start();
				o->set_parent(c->hw, i);
				printf("%s set_parent %s %u |%s\n", c->name, pn[p], i, trace_stop());
			}
	if (o->recalc_rate)
		for (p = 0; p < 4; p++) {
			unsigned long v;

			fill(r, p);
			trace_start();
			v = o->recalc_rate(c->hw, 26000000);
			printf("%s recalc %s -> %lu |%s\n", c->name, pn[p], v, trace_stop());
		}
	if (o->determine_rate || o->round_rate)
		for (i = 0; i < ARRAY_SIZE(rates); i++) {
			fill(r, 2);
			if (!print_rate_op(c, rates[i]))
				break;
		}
	if (o->set_rate)
		for (i = 0; i < ARRAY_SIZE(rates); i++) {
			fill(r, 0);
			trace_start();
			o->set_rate(c->hw, rates[i], 26000000);
			printf("%s set_rate %lu |%s\n", c->name, rates[i], trace_stop());
		}
	if (o->set_rate && !strcmp(regions[r].compatible, "mediatek,mt6789-apmixedsys")) {
		mtk_fh_set_rate = harness_hop;
		for (i = 0; i < ARRAY_SIZE(rates); i++) {
			fill(r, 0);
			trace_start();
			o->set_rate(c->hw, rates[i], 26000000);
			printf("%s set_rate_hop %lu |%s\n", c->name, rates[i], trace_stop());
		}
		mtk_fh_set_rate = NULL;
	}
	if (o->is_enabled && strcmp(regions[r].compatible, "mediatek,mt6789-topckgen")) {
		char line[1024];
		size_t n = 0;
		int v, base_read;

		fill(r, 1);
		spm_all_up();
		base_read = cg_read_happens(c, &v);
		n += snprintf(line + n, sizeof(line) - n, "%s guard up:%s", c->name, base_read ? "read" : "skip");
		for (i = 0; i < ARRAY_SIZE(oracle_domains); i++) {
			int f, skipped = 0;

			for (f = 0; f < NFAULTS; f++) {
				spm_all_up();
				spm_fault(i, f);
				if (!cg_read_happens(c, &v)) {
					skipped |= 1 << f;
					if (v)
						n += snprintf(line + n, sizeof(line) - n, " LEAK");
				}
			}
			if (skipped)
				n += snprintf(line + n, sizeof(line) - n, " %s:%x", oracle_domains[i].name, skipped);
		}
		printf("%s\n", line);
	}
	snap_region = -1;
}

static void describe(struct hclk *c)
{
	char flags[256];
	int i;

	flags_text(c->flags, flags, sizeof(flags));
	printf("%s clk provider=%s flags=%s parents=", c->name, regions[c->region].compatible, flags);
	for (i = 0; i < c->nparents; i++)
		printf("%s%s", i ? "," : "", c->parents[i]);
	switch (c->kind) {
	case KIND_FACTOR:
		printf(" factor mult=%u div=%u\n", c->mult, c->div);
		break;
	case KIND_FIXED:
		printf(" fixed rate=%lu\n", c->rate);
		break;
	case KIND_DIVIDER:
		printf(" divider reg=%x shift=%d width=%d flags=%x\n", reg_offset(c->div_reg), c->div_shift,
		       c->div_width, c->div_flags);
		break;
	case KIND_COMPOSITE:
		printf(" composite");
		if (c->mux_hw) {
			struct clk_mux *m = to_clk_mux(c->mux_hw);

			printf(" mux(reg=%x shift=%u mask=%x flags=%x ops=%s)", reg_offset(m->reg), m->shift, m->mask,
			       m->flags, c->mux_ops == &clk_mux_ops ? "clk_mux_ops" : "other");
		}
		if (c->rate_hw) {
			struct clk_divider *d = to_clk_divider(c->rate_hw);

			printf(" div(reg=%x shift=%u width=%u flags=%x table=%d ops=%s)", reg_offset(d->reg), d->shift,
			       d->width, d->flags, d->table != NULL,
			       c->rate_ops == &clk_divider_ops ? "clk_divider_ops" : "other");
		}
		if (c->gate_hw) {
			struct clk_gate *g = to_clk_gate(c->gate_hw);

			printf(" gate(reg=%x bit=%u flags=%x ops=%s)", reg_offset(g->reg), g->bit_idx, g->flags,
			       c->gate_ops == &clk_gate_ops ? "clk_gate_ops" : "other");
		}
		printf("\n");
		break;
	default:
		printf(" ops=");
		printf("%s%s%s%s%s%s%s%s%s%s%s\n", c->ops->prepare ? "P" : "", c->ops->unprepare ? "U" : "",
		       c->ops->is_prepared ? "p" : "", c->ops->enable ? "E" : "", c->ops->disable ? "D" : "",
		       c->ops->is_enabled ? "e" : "", c->ops->get_parent ? "g" : "", c->ops->set_parent ? "s" : "",
		       c->ops->recalc_rate ? "r" : "", c->ops->determine_rate || c->ops->round_rate ? "d" : "",
		       c->ops->set_rate ? "R" : "");
		break;
	}
}

static const char *index_name(struct clk_hw *hw)
{
	if (IS_ERR_OR_NULL(hw) || !hw->clk)
		return "-";
	return clks[hw->clk->index].name;
}

static void provider_map(int r)
{
	struct region *g = &regions[r];
	unsigned int i;

	if (!g->provider) {
		printf("provider %s none\n", g->compatible);
		return;
	}
	if (g->provider_kind == 1) {
		struct clk_onecell_data *d = g->provider;

		printf("provider %s cells=%u\n", g->compatible, d->clk_num);
		for (i = 0; i < d->clk_num; i++)
			printf("provider %s %u %s\n", g->compatible, i,
			       IS_ERR_OR_NULL(d->clks[i]) ? "-" : index_name(d->clks[i]->hw));
	} else {
		struct clk_hw_onecell_data *d = g->provider;

		printf("provider %s cells=%u\n", g->compatible, d->num);
		for (i = 0; i < d->num; i++)
			printf("provider %s %u %s\n", g->compatible, i, index_name(d->hws[i]));
	}
}

static void probe_all(void)
{
	int i;

	for (i = 0; i < ndrivers; i++) {
		const struct of_device_id *id;

		for (id = drivers[i]->driver.of_match_table; id->compatible[0]; id++) {
			struct platform_device *pdev = calloc(1, sizeof(*pdev));
			int r = region_of(id->compatible);

			pdev->name = id->compatible;
			pdev->dev.of_node = &regions[r].node;
			pdev->dev.match_data = id->data;
			current_region = r;
			regions[r].probe_ret = drivers[i]->probe(pdev);
			regions[r].probed = 1;
			current_region = -1;
			printf("probe %s driver=%s ret=%d\n", id->compatible, drivers[i]->driver.name ? "set" : "-",
			       regions[r].probe_ret);
		}
	}
}

int main(int argc, char **argv)
{
	int i;
	bool nospm = argc > 1 && !strcmp(argv[1], "nospm");

	spm_present = !nospm;
	spm_region();
	probe_all();
	qsort(clks, nclks, sizeof(clks[0]), cmp_clk);
	for (i = 0; i < nclks; i++)
		clks[i].hw->clk->index = i;
	if (nospm) {
		for (i = 0; i < nclks; i++) {
			int v;

			if (clks[i].kind != KIND_CUSTOM || !clks[i].ops->is_enabled ||
			    !strcmp(regions[clks[i].region].compatible, "mediatek,mt6789-topckgen"))
				continue;
			fill(clks[i].region, 1);
			printf("%s nospm %s\n", clks[i].name, cg_read_happens(&clks[i], &v) ? "read" : "skip");
		}
		return 0;
	}
	for (i = 0; i < nregions; i++)
		if (regions[i].probed)
			provider_map(i);
	for (i = 0; i < nclks; i++) {
		describe(&clks[i]);
		if (clks[i].kind == KIND_CUSTOM)
			exercise(&clks[i]);
	}
	printf("summary clocks=%d errors=%d\n", nclks, log_errors);
	return 0;
}
