// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include "stubs.h"
#include "oracle.h"

#define PWR_RST_B BIT(0)
#define PWR_ISO BIT(1)
#define PWR_ON BIT(2)
#define PWR_ON_2ND BIT(3)
#define PWR_CLK_DIS BIT(4)
#define PWR_ACK_SETTLE_US 100
#define MAX_NODES 128
#define MAX_PROPS 512
#define MAX_CLKS 128
#define MAX_TRIPLES 16

extern struct platform_driver *const harness_driver;
void harness_build_tree(void);
struct device_node *harness_probe_node(void);

static struct device_node nodes[MAX_NODES];
static int node_count;
static struct property props[MAX_PROPS];
static int prop_count;
static const char *phase = "init";
static int violations;
static int timeouts;
static unsigned long now_us = 1000000;
static unsigned long powered_at[ORACLE_DOMAINS];
static u32 spm[0x400];
static u8 spm_window[0x1000];
static struct resource spm_resource = { 0x10006000, 0x10006fff };

struct regmap {
	int kind;
};

static struct regmap spm_map = { 1 };
static struct regmap infra_map = { 2 };

struct triple {
	u32 set, clr, sta, en;
};

static struct triple triples[MAX_TRIPLES];
static int triple_count;

struct clk {
	char id[96];
	int domain;
	int enables;
};

static struct clk clks[MAX_CLKS];
static int clk_count;

struct regulator {
	int unused;
};

static struct generic_pm_domain *genpds[64];
static bool genpd_off[64];
static int genpd_count;
static struct generic_pm_domain *links[64][2];
static int link_count;
static struct genpd_onecell_data *provider;
static struct blocking_notifier_head *notifier_heads;

void harness_log(int level, const char *fmt, ...)
{
	va_list ap;

	if (level > 5)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "[%s] log%d: ", phase, level);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void trace(const char *fmt, ...)
{
	va_list ap;

	printf("%s|", phase);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void violation(const char *fmt, ...)
{
	va_list ap;

	violations++;
	printf("%s|VIOLATION ", phase);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

int harness_warn_on(int cond, const char *text)
{
	if (cond)
		trace("WARN %s", text);
	return cond;
}

void harness_poll_done(void)
{
	trace("WAIT");
}

void harness_poll_timeout(int ret, const char *what)
{
	timeouts++;
	trace("POLLTIMEOUT %d", ret);
	(void)what;
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return err;
}

void *dev_err_cast_probe(const struct device *dev, const void *err_ptr, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return (void *)err_ptr;
}

struct device_node *harness_node(struct device_node *parent, const char *name, const char *label)
{
	struct device_node *np = &nodes[node_count];
	struct device_node **tail;

	if (node_count + 1 >= MAX_NODES)
		abort();
	np->name = name;
	np->full_name = name;
	np->label = label;
	np->parent = parent;
	np->index = node_count;
	if (node_count)
		nodes[node_count - 1].allnext = np;
	node_count++;
	if (parent) {
		for (tail = &parent->child; *tail; tail = &(*tail)->sibling)
			;
		*tail = np;
	}
	return np;
}

static struct property *new_prop(struct device_node *np, const char *name, int count)
{
	struct property *p = &props[prop_count++];
	struct property **tail;

	if (prop_count >= MAX_PROPS)
		abort();
	p->name = name;
	p->count = count;
	for (tail = &np->properties; *tail; tail = &(*tail)->next)
		;
	*tail = p;
	return p;
}

void harness_prop_strings(struct device_node *np, const char *name, int count, const char *const *strings)
{
	new_prop(np, name, count)->strings = strings;
}

void harness_prop_u32(struct device_node *np, const char *name, int count, const u32 *cells)
{
	new_prop(np, name, count)->cells = cells;
}

static struct device_node *find_label(const char *label)
{
	int i;

	for (i = 0; i < node_count; i++)
		if (nodes[i].label && !strcmp(nodes[i].label, label))
			return &nodes[i];
	return NULL;
}

static bool node_compatible(const struct device_node *np, const char *compatible)
{
	struct property *p = of_find_property(np, "compatible", NULL);
	int i;

	for (i = 0; p && i < p->count; i++)
		if (!strcmp(p->strings[i], compatible))
			return true;
	return false;
}

struct device_node *of_node_get(struct device_node *np)
{
	return np;
}

void of_node_put(struct device_node *np)
{
	(void)np;
}

struct property *of_find_property(const struct device_node *np, const char *name, int *lenp)
{
	struct property *p;

	for (p = np ? np->properties : NULL; p; p = p->next)
		if (!strcmp(p->name, name)) {
			if (lenp)
				*lenp = p->count * 4;
			return p;
		}
	return NULL;
}

int of_property_read_u32(const struct device_node *np, const char *name, u32 *value)
{
	struct property *p = of_find_property(np, name, NULL);

	if (!p)
		return -EINVAL;
	if (!p->cells || p->count < 1)
		return -EOVERFLOW;
	*value = p->cells[0];
	return 0;
}

struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index)
{
	struct property *p = of_find_property(np, name, NULL);

	if (!p || !p->strings || index >= p->count)
		return NULL;
	return find_label(p->strings[index]);
}

int of_count_phandle_with_args(const struct device_node *np, const char *list, const char *cells)
{
	struct property *p = of_find_property(np, list, NULL);

	(void)cells;
	return p ? p->count : -ENOENT;
}

struct device_node *of_find_node_with_property(struct device_node *from, const char *name)
{
	struct device_node *np = from ? from->allnext : &nodes[0];

	for (; np; np = np->allnext)
		if (of_find_property(np, name, NULL))
			return np;
	return NULL;
}

struct device_node *of_get_next_child(const struct device_node *np, struct device_node *prev)
{
	return prev ? prev->sibling : np->child;
}

struct device_node *of_get_next_available_child(const struct device_node *np, struct device_node *prev)
{
	struct device_node *next = of_get_next_child(np, prev);

	while (next && next->disabled)
		next = next->sibling;
	return next;
}

const char *of_prop_next_string(struct property *prop, const char *cur)
{
	int i;

	if (!prop || !prop->strings)
		return NULL;
	if (!cur)
		return prop->count ? prop->strings[0] : NULL;
	for (i = 0; i < prop->count; i++)
		if (prop->strings[i] == cur)
			return i + 1 < prop->count ? prop->strings[i + 1] : NULL;
	return NULL;
}

const void *of_device_get_match_data(const struct device *dev)
{
	const struct of_device_id *id;

	for (id = dev->driver->of_match_table; id->compatible[0]; id++)
		if (node_compatible(dev->of_node, id->compatible))
			return id->data;
	return NULL;
}

struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num)
{
	if (type != IORESOURCE_MEM || num || !of_find_property(pdev->dev.of_node, "reg", NULL))
		return NULL;
	return &spm_resource;
}

void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res)
{
	(void)dev;
	if (res != &spm_resource)
		return ERR_PTR(-EINVAL);
	return spm_window;
}

void *devm_kzalloc(struct device *dev, size_t size, int gfp)
{
	(void)dev;
	(void)gfp;
	return calloc(1, size ? size : 1);
}

void *devm_kcalloc(struct device *dev, size_t n, size_t size, int gfp)
{
	(void)dev;
	(void)gfp;
	return calloc(n ? n : 1, size ? size : 1);
}

void *devm_kmalloc_array(struct device *dev, size_t n, size_t size, int gfp)
{
	return devm_kcalloc(dev, n, size, gfp);
}

static int domain_of_ctl(u32 ofs)
{
	int i;

	for (i = 0; i < ORACLE_DOMAINS; i++)
		if (oracle_domains[i].ctl == ofs)
			return i;
	return -1;
}

static bool domain_up(int d)
{
	u32 v = spm[oracle_domains[d].ctl / 4];

	if (oracle_domains[d].modem)
		return (v & (PWR_ON | PWR_RST_B)) == (PWR_ON | PWR_RST_B);
	return (v & (PWR_ON | PWR_ON_2ND | PWR_RST_B | PWR_ISO | PWR_CLK_DIS)) ==
	       (PWR_ON | PWR_ON_2ND | PWR_RST_B);
}

static struct triple *triple_of(u32 ofs, int which)
{
	int i;

	for (i = 0; i < triple_count; i++)
		if ((which == 0 && triples[i].set == ofs) || (which == 1 && triples[i].clr == ofs) ||
		    (which == 2 && triples[i].sta == ofs))
			return &triples[i];
	return NULL;
}

static bool bus_protected(int d)
{
	const struct oracle_domain *o = &oracle_domains[d];
	int i;

	for (i = 0; i < o->nbp; i++) {
		struct triple *t = triple_of(o->bp[i].set, 0);

		if ((t->en & o->bp[i].mask) != o->bp[i].mask)
			return false;
	}
	return true;
}

static void spm_update_status(void)
{
	u32 sta = 0, sta2 = 0;
	int i;

	for (i = 0; i < ORACLE_DOMAINS; i++) {
		const struct oracle_domain *o = &oracle_domains[i];
		u32 *con = &spm[o->ctl / 4];

		if (*con & PWR_ON)
			sta |= o->sta;
		if (*con & PWR_ON_2ND)
			sta2 |= o->sta;
		if (o->sram) {
			if (!(*con & PWR_ON) || (*con & o->sram))
				*con |= o->ack;
			else
				*con &= ~o->ack;
		}
	}
	spm[ORACLE_PWR_STATUS / 4] = sta;
	spm[ORACLE_PWR_STATUS_2ND / 4] = sta2;
}

static void spm_check(int d, u32 old, u32 new)
{
	const struct oracle_domain *o = &oracle_domains[d];
	int i;

	if (!(old & PWR_ON) && (new & PWR_ON) && o->parent >= 0 && !domain_up(o->parent))
		violation("%s powered while parent %s is down", o->name, oracle_domains[o->parent].name);
	if ((old & PWR_ON) && !(new & PWR_ON)) {
		for (i = 0; i < ORACLE_DOMAINS; i++)
			if (oracle_domains[i].parent == d && (spm[oracle_domains[i].ctl / 4] & PWR_ON))
				violation("%s powered down while child %s is up", o->name, oracle_domains[i].name);
		if (!bus_protected(d))
			violation("%s powered down with its bus unprotected", o->name);
		if (o->sram && !(old & o->sram))
			violation("%s powered down with its SRAM up", o->name);
		if (!o->modem && !(old & PWR_ISO))
			violation("%s powered down without isolation", o->name);
	}
	if (o->modem)
		return;
	if ((new & (PWR_ON | PWR_ON_2ND)) == (PWR_ON | PWR_ON_2ND) &&
	    (old & (PWR_ON | PWR_ON_2ND)) != (PWR_ON | PWR_ON_2ND))
		powered_at[d] = now_us;
	if (((old & PWR_ISO) && !(new & PWR_ISO)) || ((old & PWR_CLK_DIS) && !(new & PWR_CLK_DIS))) {
		if (now_us - powered_at[d] < PWR_ACK_SETTLE_US)
			violation("%s isolation or clock released %lu us after power-on, settle is %d us", o->name,
				  now_us - powered_at[d], PWR_ACK_SETTLE_US);
	}
	if (!(old & PWR_RST_B) && (new & PWR_RST_B) && (new & (PWR_ON | PWR_ON_2ND)) != (PWR_ON | PWR_ON_2ND))
		violation("%s reset released before power", o->name);
	if ((old & PWR_ISO) && !(new & PWR_ISO) && (new & (PWR_ON | PWR_ON_2ND)) != (PWR_ON | PWR_ON_2ND))
		violation("%s isolation released before power", o->name);
	if (o->sram && (old & o->sram) && !(new & o->sram) && !domain_up(d))
		violation("%s SRAM released while the domain is down", o->name);
}

static u32 spm_read(u32 ofs)
{
	if (ofs >= sizeof(spm_window))
		violation("SPM read outside the window 0x%x", ofs);
	spm_update_status();
	return spm[ofs / 4];
}

static void spm_write(u32 ofs, u32 val)
{
	u32 old;
	int d;
	int i;

	if (ofs == ORACLE_PWR_STATUS || ofs == ORACLE_PWR_STATUS_2ND || ofs >= sizeof(spm_window)) {
		violation("SPM write to 0x%x", ofs);
		return;
	}
	old = spm[ofs / 4];
	d = domain_of_ctl(ofs);
	if (d < 0) {
		for (i = 0; i < ORACLE_DOMAINS; i++)
			if (oracle_domains[i].ext_ofs == ofs)
				break;
		if (i == ORACLE_DOMAINS) {
			violation("SPM write to unknown register 0x%x", ofs);
			return;
		}
	} else {
		spm_check(d, old, val);
	}
	spm[ofs / 4] = val;
	spm_update_status();
	if (spm[ofs / 4] != old)
		trace("SPM 0x%03x 0x%08x->0x%08x", ofs, old, spm[ofs / 4]);
}

u32 readl(const volatile void __iomem *addr)
{
	const volatile u8 *p = addr;

	if (p < spm_window || p >= spm_window + sizeof(spm_window)) {
		violation("readl outside SPM");
		return 0;
	}
	return spm_read((u32)(p - spm_window));
}

void writel(u32 value, volatile void __iomem *addr)
{
	volatile u8 *p = addr;

	if (p < spm_window || p >= spm_window + sizeof(spm_window)) {
		violation("writel outside SPM");
		return;
	}
	spm_write((u32)(p - spm_window), value);
}

void udelay(unsigned long us)
{
	now_us += us;
	trace("DELAY %lu", us);
}

void usleep_range(unsigned long min, unsigned long max)
{
	now_us += min;
	trace("SLEEP %lu-%lu", min, max);
}

static void infra_release_check(struct triple *t, u32 mask)
{
	int d, i;

	for (d = 0; d < ORACLE_DOMAINS; d++)
		for (i = 0; i < oracle_domains[d].nbp; i++)
			if (oracle_domains[d].bp[i].set == t->set && (oracle_domains[d].bp[i].mask & mask) &&
			    !domain_up(d))
				violation("bus of %s released while it is down", oracle_domains[d].name);
}

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	struct triple *t;

	if (map == &spm_map) {
		*val = spm_read(reg);
		return 0;
	}
	if (map != &infra_map) {
		violation("read from an unknown regmap");
		return -EINVAL;
	}
	t = triple_of(reg, 2);
	if (!t) {
		violation("infracfg read of 0x%x", reg);
		*val = 0;
		return 0;
	}
	*val = t->en;
	return 0;
}

int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	struct triple *t;

	if (map == &spm_map) {
		spm_write(reg, val);
		return 0;
	}
	if (map != &infra_map) {
		violation("write to an unknown regmap");
		return -EINVAL;
	}
	if ((t = triple_of(reg, 0))) {
		t->en |= val;
		trace("INFRA SET 0x%03x 0x%08x", reg, val);
		return 0;
	}
	if ((t = triple_of(reg, 1))) {
		infra_release_check(t, val);
		t->en &= ~val;
		trace("INFRA CLR 0x%03x 0x%08x", reg, val);
		return 0;
	}
	violation("infracfg write of 0x%x", reg);
	return 0;
}

int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int orig, tmp;

	if (map == &infra_map) {
		violation("read-modify-write of infracfg 0x%x", reg);
		return -EINVAL;
	}
	regmap_read(map, reg, &orig);
	tmp = (orig & ~mask) | (val & mask);
	if (tmp != orig)
		regmap_write(map, reg, tmp);
	return 0;
}

struct regmap *syscon_node_to_regmap(struct device_node *np)
{
	if (np && node_compatible(np, "mediatek,mt6789-scpsys") && node_compatible(np, "syscon"))
		return &spm_map;
	if (np && np->label && !strcmp(np->label, "infracfg_ao_clk"))
		return &infra_map;
	violation("syscon regmap for %s", np ? np->name : "(null)");
	return ERR_PTR(-EINVAL);
}

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np, const char *property)
{
	struct device_node *target = of_parse_phandle(np, property, 0);

	if (!target)
		return ERR_PTR(-ENODEV);
	return syscon_node_to_regmap(target);
}

struct regmap *device_node_to_regmap(struct device_node *np)
{
	return syscon_node_to_regmap(np);
}

static struct clk *clk_handle(const char *id)
{
	int i;

	for (i = 0; i < clk_count; i++)
		if (!strcmp(clks[i].id, id))
			return &clks[i];
	if (clk_count >= MAX_CLKS)
		abort();
	snprintf(clks[clk_count].id, sizeof(clks[clk_count].id), "%s", id);
	clks[clk_count].domain = -1;
	for (i = 0; i < (int)ARRAY_SIZE(oracle_blocks); i++)
		if (!strncmp(id, oracle_blocks[i].label, strlen(oracle_blocks[i].label)) &&
		    id[strlen(oracle_blocks[i].label)] == ':')
			clks[clk_count].domain = oracle_blocks[i].domain;
	return &clks[clk_count++];
}

struct clk *devm_clk_get(struct device *dev, const char *id)
{
	struct property *names = of_find_property(dev->of_node, "clock-names", NULL);
	struct property *list = of_find_property(dev->of_node, "clocks", NULL);
	int i;

	for (i = 0; names && list && i < names->count; i++)
		if (!strcmp(names->strings[i], id))
			return clk_handle(list->strings[i]);
	return ERR_PTR(-ENOENT);
}

struct clk *of_clk_get(struct device_node *np, int index)
{
	struct property *list = of_find_property(np, "clocks", NULL);

	if (!list || index >= list->count)
		return ERR_PTR(-ENOENT);
	return clk_handle(list->strings[index]);
}

unsigned int of_clk_get_parent_count(const struct device_node *np)
{
	struct property *list = of_find_property(np, "clocks", NULL);

	return list ? list->count : 0;
}

int clk_prepare_enable(struct clk *clk)
{
	if (!clk)
		return 0;
	if (clk->domain >= 0 && !domain_up(clk->domain))
		violation("clock %s enabled while %s is down", clk->id, oracle_domains[clk->domain].name);
	clk->enables++;
	trace("CLK+ %s", clk->id);
	return 0;
}

void clk_disable_unprepare(struct clk *clk)
{
	if (!clk)
		return;
	if (clk->domain >= 0 && !domain_up(clk->domain))
		violation("clock %s disabled while %s is down", clk->id, oracle_domains[clk->domain].name);
	if (--clk->enables < 0)
		violation("clock %s disabled more often than enabled", clk->id);
	trace("CLK- %s", clk->id);
}

int clk_bulk_prepare_enable(int num, const struct clk_bulk_data *clks_in)
{
	int i;

	for (i = 0; i < num; i++)
		clk_prepare_enable(clks_in[i].clk);
	return 0;
}

void clk_bulk_disable_unprepare(int num, const struct clk_bulk_data *clks_in)
{
	while (--num >= 0)
		clk_disable_unprepare(clks_in[num].clk);
}

void clk_bulk_put(int num, struct clk_bulk_data *clks_in)
{
	(void)num;
	(void)clks_in;
}

struct regulator *devm_regulator_get_optional(struct device *dev, const char *id)
{
	(void)dev;
	(void)id;
	return ERR_PTR(-ENODEV);
}

struct regulator *devm_of_regulator_get_optional(struct device *dev, struct device_node *node, const char *id)
{
	(void)dev;
	(void)node;
	(void)id;
	return ERR_PTR(-ENODEV);
}

int regulator_enable(struct regulator *r)
{
	(void)r;
	violation("regulator enabled");
	return 0;
}

int regulator_disable(struct regulator *r)
{
	(void)r;
	violation("regulator disabled");
	return 0;
}

int regulator_is_enabled(struct regulator *r)
{
	(void)r;
	return 0;
}

static int genpd_index(struct generic_pm_domain *genpd)
{
	int i;

	for (i = 0; i < genpd_count; i++)
		if (genpds[i] == genpd)
			return i;
	return -1;
}

int pm_genpd_init(struct generic_pm_domain *genpd, struct dev_power_governor *gov, bool is_off)
{
	(void)gov;
	if (genpd_index(genpd) >= 0) {
		violation("genpd %s initialised twice", genpd->name);
		return -EINVAL;
	}
	genpds[genpd_count] = genpd;
	genpd_off[genpd_count++] = is_off;
	return 0;
}

int pm_genpd_add_subdomain(struct generic_pm_domain *genpd, struct generic_pm_domain *subdomain)
{
	links[link_count][0] = genpd;
	links[link_count++][1] = subdomain;
	return 0;
}

int pm_genpd_remove(struct generic_pm_domain *genpd)
{
	violation("genpd %s removed", genpd->name);
	return 0;
}

int of_genpd_add_provider_onecell(struct device_node *np, struct genpd_onecell_data *data)
{
	(void)np;
	provider = data;
	return 0;
}

int blocking_notifier_chain_register(struct blocking_notifier_head *nh, struct notifier_block *nb)
{
	(void)nb;
	notifier_heads = nh;
	return 0;
}

int blocking_notifier_chain_unregister(struct blocking_notifier_head *nh, struct notifier_block *nb)
{
	(void)nh;
	(void)nb;
	return 0;
}

int blocking_notifier_call_chain(struct blocking_notifier_head *nh, unsigned long val, void *v)
{
	(void)nh;
	(void)val;
	(void)v;
	return 0;
}

static struct generic_pm_domain *parent_of(struct generic_pm_domain *genpd)
{
	int i;

	for (i = 0; i < link_count; i++)
		if (links[i][1] == genpd)
			return links[i][0];
	return NULL;
}

static int call(struct generic_pm_domain *genpd, bool on)
{
	int i = genpd_index(genpd);
	int ret = on ? genpd->power_on(genpd) : genpd->power_off(genpd);

	trace("%s %s %d", on ? "ON" : "OFF", genpd->name, ret);
	if (!ret)
		genpd_off[i] = !on;
	return ret;
}

static void ensure_on(struct generic_pm_domain *genpd)
{
	struct generic_pm_domain *parent = parent_of(genpd);

	if (parent)
		ensure_on(parent);
	if (genpd_off[genpd_index(genpd)])
		call(genpd, true);
}

static void ensure_off(struct generic_pm_domain *genpd)
{
	int i;

	for (i = 0; i < link_count; i++)
		if (links[i][0] == genpd)
			ensure_off(links[i][1]);
	if (!genpd_off[genpd_index(genpd)])
		call(genpd, false);
}

static void dump_state(void)
{
	int i;

	spm_update_status();
	for (i = 0; i < ORACLE_DOMAINS; i++)
		trace("STATE %s con=0x%08x", oracle_domains[i].name, spm[oracle_domains[i].ctl / 4]);
	for (i = 0; i < ORACLE_DOMAINS; i++)
		if (oracle_domains[i].ext_ofs)
			trace("STATE ext 0x%03x=0x%08x", oracle_domains[i].ext_ofs, spm[oracle_domains[i].ext_ofs / 4]);
	trace("STATE status=0x%08x status2=0x%08x", spm[ORACLE_PWR_STATUS / 4], spm[ORACLE_PWR_STATUS_2ND / 4]);
	for (i = 0; i < triple_count; i++)
		trace("STATE infra 0x%03x=0x%08x", triples[i].sta, triples[i].en);
	for (i = 0; i < clk_count; i++)
		if (clks[i].enables)
			trace("STATE clk %s=%d", clks[i].id, clks[i].enables);
}

static void reset_hardware(void)
{
	int i, j;

	memset(spm, 0, sizeof(spm));
	for (i = 0; i < ORACLE_DOMAINS; i++) {
		const struct oracle_domain *o = &oracle_domains[i];

		for (j = 0; j < o->nbp; j++) {
			struct triple *t = triple_of(o->bp[j].set, 0);

			if (!t) {
				t = &triples[triple_count++];
				t->set = o->bp[j].set;
				t->clr = o->bp[j].clr;
				t->sta = o->bp[j].sta;
			}
			if (!o->boot_on)
				t->en |= o->bp[j].mask;
		}
		if (o->boot_on)
			spm[o->ctl / 4] = o->modem ? (PWR_ON | PWR_RST_B) : (PWR_ON | PWR_ON_2ND | PWR_RST_B);
		else
			spm[o->ctl / 4] = o->modem ? 0 : (PWR_ISO | PWR_CLK_DIS | o->sram);
		if (o->ext_ofs && !o->boot_on)
			spm[o->ext_ofs / 4] |= o->ext_mask;
	}
	spm_update_status();
}

int main(void)
{
	static struct platform_device pdev;
	static struct device parent;
	struct device_node *np;
	unsigned int i;
	int ret;

	reset_hardware();
	harness_build_tree();
	np = harness_probe_node();
	parent.of_node = np->parent;
	pdev.dev.of_node = np;
	pdev.dev.parent = &parent;
	pdev.dev.driver = &harness_driver->driver;
	phase = "probe";
	ret = harness_driver->probe(&pdev);
	trace("RESULT %d", ret);
	dump_state();
	phase = "genpd";
	for (i = 0; (int)i < genpd_count; i++)
		trace("GENPD %s off=%d flags=0x%x", genpds[i]->name, genpd_off[i], genpds[i]->flags);
	for (i = 0; (int)i < link_count; i++)
		trace("LINK %s %s", links[i][0]->name, links[i][1]->name);
	if (!provider) {
		violation("no provider");
		return 0;
	}
	for (i = 0; i < provider->num_domains; i++)
		trace("PROVIDER %u %s", i, provider->domains[i] ? provider->domains[i]->name : "-");
	for (i = 0; i < provider->num_domains; i++) {
		struct generic_pm_domain *genpd = provider->domains[i];
		static char name[64];

		if (!genpd)
			continue;
		snprintf(name, sizeof(name), "cycle-%s", genpd->name);
		phase = name;
		if (genpd_off[genpd_index(genpd)]) {
			ensure_on(genpd);
			ensure_off(genpd);
		} else {
			ensure_off(genpd);
			ensure_on(genpd);
		}
		dump_state();
	}
	phase = "end";
	trace("TIMEOUTS %d", timeouts);
	trace("VIOLATIONS %d", violations);
	return 0;
}
