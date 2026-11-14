// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include "kernel.h"
#include HARNESS_DRIVER
#include HARNESS_COMMON
#include "paris-part.c"
#include "stubs-gen.h"

#define HARNESS_BLOCKS 16
#define HARNESS_WORDS 1024

static u32 harness_regs[HARNESS_BLOCKS][HARNESS_WORDS];
static void __iomem *harness_bases[HARNESS_BLOCKS];
static struct device harness_dev;
static struct mtk_pinctrl harness_hw;

struct harness_field {
	const char *name;
	int id;
};

static const struct harness_field harness_fields[] = {
	{ "MODE", PINCTRL_PIN_REG_MODE },
	{ "DIR", PINCTRL_PIN_REG_DIR },
	{ "DI", PINCTRL_PIN_REG_DI },
	{ "DO", PINCTRL_PIN_REG_DO },
	{ "SR", PINCTRL_PIN_REG_SR },
	{ "SMT", PINCTRL_PIN_REG_SMT },
	{ "PD", PINCTRL_PIN_REG_PD },
	{ "PU", PINCTRL_PIN_REG_PU },
	{ "E4", PINCTRL_PIN_REG_E4 },
	{ "E8", PINCTRL_PIN_REG_E8 },
	{ "TDSEL", PINCTRL_PIN_REG_TDSEL },
	{ "RDSEL", PINCTRL_PIN_REG_RDSEL },
	{ "DRV", PINCTRL_PIN_REG_DRV },
	{ "PUPD", PINCTRL_PIN_REG_PUPD },
	{ "R0", PINCTRL_PIN_REG_R0 },
	{ "R1", PINCTRL_PIN_REG_R1 },
	{ "IES", PINCTRL_PIN_REG_IES },
	{ "PULLEN", PINCTRL_PIN_REG_PULLEN },
	{ "PULLSEL", PINCTRL_PIN_REG_PULLSEL },
	{ "DRV_EN", PINCTRL_PIN_REG_DRV_EN },
	{ "DRV_E0", PINCTRL_PIN_REG_DRV_E0 },
	{ "DRV_E1", PINCTRL_PIN_REG_DRV_E1 },
	{ "RSEL", PINCTRL_PIN_REG_RSEL },
#ifdef HARNESS_ACK
	{ "DRV_ADV", PINCTRL_PIN_REG_DRV_ADV },
#else
	{ "DRV_EH", PINCTRL_PIN_REG_DRV_EH },
#endif
};

#define HARNESS_FN(f) { #f, (const void *)f }

static const struct {
	const char *name;
	const void *fn;
} harness_fns[] = {
	HARNESS_FN(mtk_pinconf_bias_disable_set),
	HARNESS_FN(mtk_pinconf_bias_disable_get),
	HARNESS_FN(mtk_pinconf_bias_set),
	HARNESS_FN(mtk_pinconf_bias_get),
	HARNESS_FN(mtk_pinconf_bias_disable_set_rev1),
	HARNESS_FN(mtk_pinconf_bias_disable_get_rev1),
	HARNESS_FN(mtk_pinconf_bias_set_rev1),
	HARNESS_FN(mtk_pinconf_bias_get_rev1),
	HARNESS_FN(mtk_pinconf_bias_set_combo),
	HARNESS_FN(mtk_pinconf_bias_get_combo),
	HARNESS_FN(mtk_pinconf_drive_set),
	HARNESS_FN(mtk_pinconf_drive_get),
	HARNESS_FN(mtk_pinconf_drive_set_rev1),
	HARNESS_FN(mtk_pinconf_drive_get_rev1),
	HARNESS_FN(mtk_pinconf_adv_pull_set),
	HARNESS_FN(mtk_pinconf_adv_pull_get),
	HARNESS_FN(mtk_pinconf_adv_drive_set),
	HARNESS_FN(mtk_pinconf_adv_drive_get),
	HARNESS_FN(mtk_paris_pinctrl_probe),
#ifdef HARNESS_ACK
	HARNESS_FN(mtk_pinconf_drive_set_raw),
	HARNESS_FN(mtk_pinconf_drive_get_raw),
	HARNESS_FN(mtk_pinconf_adv_drive_set_raw),
	HARNESS_FN(mtk_pinconf_adv_drive_get_raw),
#endif
};

void *harness_drvdata(struct pinctrl_dev *pctldev)
{
	(void)pctldev;
	return &harness_hw;
}

static u32 *harness_word(const volatile void *addr, unsigned int *offset)
{
	const char *a = (const char *)addr;
	int i;

	for (i = 0; i < HARNESS_BLOCKS; i++) {
		const char *base = (const char *)harness_regs[i];

		if (a >= base && a < base + sizeof(harness_regs[i])) {
			*offset = (unsigned int)(a - base);
			return &harness_regs[i][*offset / 4];
		}
	}
	fprintf(stderr, "access outside the register file\n");
	exit(3);
}

u32 harness_readl(const volatile void *addr)
{
	unsigned int offset;

	return *harness_word(addr, &offset);
}

void harness_writel(u32 value, volatile void *addr)
{
	unsigned int offset, k;
	u32 *word = harness_word(addr, &offset);

	switch (offset & 0xf) {
	case 0x4:
		word[-1] |= value;
		break;
	case 0x8:
		word[-2] &= ~value;
		break;
	case 0xc:
		for (k = 0; k < 8; k++) {
			u32 nibble = (value >> (4 * k)) & 0xf;

			if (nibble & 0x8)
				word[-3] = (word[-3] & ~(0xfu << (4 * k))) | ((nibble & 0x7) << (4 * k));
		}
		break;
	default:
		*word = value;
	}
}

static const char *harness_fn_name(const void *fn)
{
	size_t i;

	if (!fn)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(harness_fns); i++)
		if (harness_fns[i].fn == fn)
			return harness_fns[i].name;
	return "?";
}

static void harness_str(const char *s)
{
	if (!s) {
		printf("null");
		return;
	}
	putchar('"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			putchar('\\');
		putchar(*s);
	}
	putchar('"');
}

static void harness_ranges(const struct mtk_pin_reg_calc *rc)
{
	unsigned int i;

	if (!rc->range) {
		printf("null");
		return;
	}
	printf("[");
	for (i = 0; i < rc->nranges; i++) {
		const struct mtk_pin_field_calc *c = &rc->range[i];

		printf("%s[%u,%u,%u,%u,%u,%u,%u,%u,%u]", i ? "," : "", c->s_pin, c->e_pin, c->i_base,
		       c->s_addr, c->x_addrs, c->s_bit, c->x_bits, c->sz_reg, c->fixed);
	}
	printf("]");
}

static void harness_tables(void)
{
	const struct mtk_pin_soc *soc = &mt6789_data;
	unsigned int i;
	size_t f;

	printf("{\"npins\":%u,\"ngrps\":%u,\"nfuncs\":%u,\"gpio_m\":%u,\"ies_present\":%d,", soc->npins, soc->ngrps,
	       soc->nfuncs, soc->gpio_m, soc->ies_present);
	printf("\"grps\":%d,\"funcs\":%d,\"driver_data\":%d,", soc->grps != NULL, soc->funcs != NULL,
	       soc->driver_data != NULL);
	printf("\"nbase_names\":%u,\"base_names\":", soc->nbase_names);
	if (soc->base_names) {
		printf("[");
		for (i = 0; i < soc->nbase_names; i++) {
			printf("%s", i ? "," : "");
			harness_str(soc->base_names[i]);
		}
		printf("]");
	} else {
		printf("null");
	}
	printf(",\"fields\":{");
	for (f = 0; f < ARRAY_SIZE(harness_fields); f++) {
		printf("%s\"%s\":", f ? "," : "", harness_fields[f].name);
		harness_ranges(&soc->reg_cal[harness_fields[f].id]);
	}
	printf("},\"pull_type\":");
	if (soc->pull_type) {
		printf("[");
		for (i = 0; i < soc->npins; i++)
			printf("%s%u", i ? "," : "", soc->pull_type[i]);
		printf("]");
	} else {
		printf("null");
	}
	printf(",\"pins\":[");
	for (i = 0; i < soc->npins; i++) {
		const struct mtk_pin_desc *p = &soc->pins[i];

		printf("%s[%u,", i ? "," : "", p->number);
		harness_str(p->name);
		printf(",%u,%u,%u,", p->eint.eint_m, p->eint.eint_n, p->drv_n);
		if (p->funcs) {
			const struct mtk_func_desc *fd;

			printf("[");
			for (fd = p->funcs; fd->name || fd->muxval; fd++) {
				printf("%s[%u,", fd == p->funcs ? "" : ",", fd->muxval);
				harness_str(fd->name);
				printf("]");
				if (!fd->name)
					break;
			}
			printf("]");
		} else {
			printf("null");
		}
		printf("]");
	}
	printf("],\"callbacks\":{");
	printf("\"bias_disable_set\":"); harness_str(harness_fn_name((const void *)soc->bias_disable_set));
	printf(",\"bias_disable_get\":"); harness_str(harness_fn_name((const void *)soc->bias_disable_get));
	printf(",\"bias_set\":"); harness_str(harness_fn_name((const void *)soc->bias_set));
	printf(",\"bias_get\":"); harness_str(harness_fn_name((const void *)soc->bias_get));
	printf(",\"bias_set_combo\":"); harness_str(harness_fn_name((const void *)soc->bias_set_combo));
	printf(",\"bias_get_combo\":"); harness_str(harness_fn_name((const void *)soc->bias_get_combo));
	printf(",\"drive_set\":"); harness_str(harness_fn_name((const void *)soc->drive_set));
	printf(",\"drive_get\":"); harness_str(harness_fn_name((const void *)soc->drive_get));
	printf(",\"adv_pull_set\":"); harness_str(harness_fn_name((const void *)soc->adv_pull_set));
	printf(",\"adv_pull_get\":"); harness_str(harness_fn_name((const void *)soc->adv_pull_get));
	printf(",\"adv_drive_set\":"); harness_str(harness_fn_name((const void *)soc->adv_drive_set));
	printf(",\"adv_drive_get\":"); harness_str(harness_fn_name((const void *)soc->adv_drive_get));
	printf("},\"driver\":{\"name\":");
	harness_str(mt6789_pinctrl_driver.driver.name);
	printf(",\"probe\":");
	harness_str(harness_fn_name((const void *)mt6789_pinctrl_driver.probe));
	printf(",\"pm\":%d,\"compatible\":[", mt6789_pinctrl_driver.driver.pm == &mtk_paris_pinctrl_pm_ops);
	for (i = 0; mt6789_pinctrl_of_match[i].compatible[0]; i++) {
		printf("%s[", i ? "," : "");
		harness_str(mt6789_pinctrl_of_match[i].compatible);
		printf(",%d]", mt6789_pinctrl_of_match[i].data == (const void *)&mt6789_data);
	}
	printf("]}");
#ifdef HARNESS_ACK
	printf(",\"eint_regs\":%d,\"eint_pin\":%d,\"npin_rsel\":%u,\"pin_rsel\":%d,\"eint_hw\":", soc->eint_regs != NULL,
	       soc->eint_pin != NULL, soc->npin_rsel, soc->pin_rsel != NULL);
	if (soc->eint_hw) {
		printf("{\"port_mask\":%u,\"ports\":%u,\"ap_num\":%u,\"db_cnt\":%u,\"db_time\":", soc->eint_hw->port_mask,
		       soc->eint_hw->ports, soc->eint_hw->ap_num, soc->eint_hw->db_cnt);
		if (soc->eint_hw->db_time) {
			printf("[");
			for (i = 0; i < 16; i++) {
				printf("%s%u", i ? "," : "", soc->eint_hw->db_time[i]);
				if (!soc->eint_hw->db_time[i])
					break;
			}
			printf("]");
		} else {
			printf("null");
		}
		printf("}");
	} else {
		printf("null");
	}
#else
	printf(",\"capability_flags\":%u,\"eh\":[", soc->capability_flags);
	for (i = 0; i < soc->neh_pins; i++)
		printf("%s[%u,%u]", i ? "," : "", soc->eh_pin_pinmux[i].pin, soc->eh_pin_pinmux[i].pinmux);
	printf("]");
#endif
	printf("}\n");
}

static void harness_lookup(void)
{
	const struct mtk_pin_soc *soc = &mt6789_data;
	unsigned int i;
	size_t f;

	printf("{");
	for (f = 0; f < ARRAY_SIZE(harness_fields); f++) {
		printf("%s\"%s\":[", f ? "," : "", harness_fields[f].name);
		for (i = 0; i < soc->npins; i++) {
			struct mtk_pin_field pf;
			int err;

			memset(&pf, 0, sizeof(pf));
			err = mtk_hw_pin_field_get(&harness_hw, &soc->pins[i], harness_fields[f].id, &pf);
			if (err)
				printf("%s[%d]", i ? "," : "", err);
			else
				printf("%s[0,%u,%u,%u,%u,%u]", i ? "," : "", pf.index, pf.offset, pf.bitpos, pf.mask, pf.next);
		}
		printf("]");
	}
	printf("}\n");
}

static void harness_eintmap(unsigned int count)
{
	unsigned int e;

	printf("[");
	for (e = 0; e < count; e++) {
		struct gpio_chip *chip = NULL;
		unsigned int gpio = 0;
		int err = mtk_eint_xt.get_gpio_n(&harness_hw, e, &gpio, &chip);

		printf("%s[%u,%d,%u,%d]", e ? "," : "", e, err, err ? 0 : gpio,
		       err ? 0 : (int)mtk_is_virt_gpio(&harness_hw, gpio));
	}
	printf("]\n");
}

static void harness_virt(void)
{
	unsigned int i;

	printf("[");
	for (i = 0; i < mt6789_data.npins; i++)
		printf("%s%d", i ? "," : "", (int)mtk_is_virt_gpio(&harness_hw, i));
	printf("]\n");
}

static void harness_dump(void)
{
	int b, w, first = 1;

	printf("[");
	for (b = 0; b < HARNESS_BLOCKS; b++)
		for (w = 0; w < HARNESS_WORDS; w++)
			if (harness_regs[b][w]) {
				printf("%s[%d,%d,%u]", first ? "" : ",", b, w * 4, harness_regs[b][w]);
				first = 0;
			}
	printf("]\n");
}

static void harness_peek(unsigned int pin)
{
	size_t f;

	printf("{");
	for (f = 0; f < ARRAY_SIZE(harness_fields); f++) {
		int value = 0;
		int err = mtk_hw_get_value(&harness_hw, &mt6789_data.pins[pin], harness_fields[f].id, &value);

		printf("%s\"%s\":[%d,%d]", f ? "," : "", harness_fields[f].name, err, err ? 0 : value);
	}
	printf("}\n");
}

static int harness_poke(unsigned int pin, const char *name, int value)
{
	size_t f;

	for (f = 0; f < ARRAY_SIZE(harness_fields); f++) {
		struct mtk_pin_field pf;
		u32 *word;

		if (strcmp(harness_fields[f].name, name))
			continue;
		if (mtk_hw_pin_field_get(&harness_hw, &mt6789_data.pins[pin], harness_fields[f].id, &pf))
			return -1;
		if (pf.next)
			return -2;
		word = &harness_regs[pf.index][pf.offset / 4];
		*word = (*word & ~(pf.mask << pf.bitpos)) | (((u32)value & pf.mask) << pf.bitpos);
		return 0;
	}
	return -3;
}

static void harness_pinconf(unsigned int pin)
{
	int sep = 0;
	int pass;

	printf("\"");
	for (pass = 0; pass < 2; pass++) {
		const struct pin_config_item *items = pass ? mtk_conf_items : harness_conf_items;
		size_t n = pass ? ARRAY_SIZE(mtk_conf_items) : ARRAY_SIZE(harness_conf_items);
		size_t i;

		for (i = 0; i < n; i++) {
			unsigned long config = pinconf_to_config_packed(items[i].param, 0);
			int ret = mtk_pinconf_get(NULL, pin, &config);

			if (ret == -EINVAL || ret == -ENOTSUPP)
				continue;
			if (ret) {
				printf("ERROR READING CONFIG SETTING %zu ", i);
				continue;
			}
			if (sep)
				printf(", ");
			sep = 1;
			printf("%s", items[i].display);
			if (items[i].has_arg) {
				u32 val = pinconf_to_config_argument(config);

				if (items[i].format)
					printf(" (%u %s)", val, items[i].format);
				else
					printf(" (0x%x)", val);
			}
		}
	}
	printf("\"\n");
}

static void harness_show(unsigned int pin)
{
	char buf[96];
	ssize_t len;

	memset(buf, 0, sizeof(buf));
	len = mtk_pctrl_show_one_pin(&harness_hw, pin, buf, sizeof(buf));
	printf("[%zd,", len);
	harness_str(buf);
	printf("]\n");
}

static void harness_setup(void)
{
	const struct mtk_pin_soc *soc = &mt6789_data;
	unsigned int i;

	for (i = 0; i < HARNESS_BLOCKS; i++)
		harness_bases[i] = (void __iomem *)harness_regs[i];
	harness_hw.soc = soc;
	harness_hw.dev = &harness_dev;
	harness_hw.base = harness_bases;
#ifdef HARNESS_ACK
	harness_hw.nbase = soc->nbase_names;
#else
	harness_hw.nbase = HARNESS_NBASE;
#endif
	harness_hw.groups = calloc(soc->ngrps, sizeof(*harness_hw.groups));
	harness_hw.grp_names = calloc(soc->ngrps, sizeof(*harness_hw.grp_names));
	for (i = 0; i < soc->npins; i++) {
		harness_hw.groups[i].name = soc->pins[i].name;
		harness_hw.groups[i].pin = soc->pins[i].number;
		harness_hw.grp_names[i] = soc->pins[i].name;
	}
}

int main(void)
{
	char line[8192];

	harness_setup();
	while (fgets(line, sizeof(line), stdin)) {
		char cmd[32] = "";
		char *rest;
		int n = 0;

		if (sscanf(line, "%31s %n", cmd, &n) < 1)
			continue;
		rest = line + n;
		if (!strcmp(cmd, "tables")) {
			harness_tables();
		} else if (!strcmp(cmd, "lookup")) {
			harness_lookup();
		} else if (!strcmp(cmd, "eintmap")) {
			harness_eintmap((unsigned int)strtoul(rest, NULL, 0));
		} else if (!strcmp(cmd, "virt")) {
			harness_virt();
		} else if (!strcmp(cmd, "zero")) {
			memset(harness_regs, 0, sizeof(harness_regs));
			printf("0\n");
		} else if (!strcmp(cmd, "set")) {
			int b, off;
			unsigned int v;

			while (sscanf(rest, "%d:%d:%u %n", &b, &off, &v, &n) == 3) {
				harness_regs[b][off / 4] = v;
				rest += n;
			}
			printf("0\n");
		} else if (!strcmp(cmd, "dump")) {
			harness_dump();
		} else if (!strcmp(cmd, "peek")) {
			harness_peek((unsigned int)strtoul(rest, NULL, 0));
		} else if (!strcmp(cmd, "poke")) {
			unsigned int pin;
			char name[32];
			int value;

			if (sscanf(rest, "%u %31s %d", &pin, name, &value) != 3)
				return 4;
			printf("%d\n", harness_poke(pin, name, value));
		} else if (!strcmp(cmd, "mux")) {
			unsigned int pin, func;

			if (sscanf(rest, "%u %u", &pin, &func) != 2)
				return 4;
			printf("%d\n", mtk_pmx_set_mux(NULL, func, pin));
		} else if (!strcmp(cmd, "conf")) {
			unsigned long configs[64];
			unsigned int pin, count = 0, param, arg;

			if (sscanf(rest, "%u %n", &pin, &n) != 1)
				return 4;
			rest += n;
			while (count < 64 && sscanf(rest, "%u:%u %n", &param, &arg, &n) == 2) {
				configs[count++] = pinconf_to_config_packed(param, arg);
				rest += n;
			}
			printf("%d\n", mtk_pconf_group_set(NULL, pin, configs, count));
		} else if (!strcmp(cmd, "gpioreq")) {
			printf("%d\n", mtk_pinmux_gpio_request_enable(NULL, NULL, (unsigned int)strtoul(rest, NULL, 0)));
		} else if (!strcmp(cmd, "gpiodir")) {
			unsigned int pin, input;

			if (sscanf(rest, "%u %u", &pin, &input) != 2)
				return 4;
			printf("%d\n", mtk_pinmux_gpio_set_direction(NULL, NULL, pin, input));
		} else if (!strcmp(cmd, "gpioset")) {
			unsigned int pin, value;

			if (sscanf(rest, "%u %u", &pin, &value) != 2)
				return 4;
			printf("%d\n", mtk_hw_set_value(&harness_hw, &mt6789_data.pins[pin], PINCTRL_PIN_REG_DO, !!value));
		} else if (!strcmp(cmd, "eintreq")) {
			printf("%d\n", mtk_eint_xt.set_gpio_as_eint(&harness_hw, strtoul(rest, NULL, 0)));
		} else if (!strcmp(cmd, "show")) {
			harness_show((unsigned int)strtoul(rest, NULL, 0));
		} else if (!strcmp(cmd, "pinconf")) {
			harness_pinconf((unsigned int)strtoul(rest, NULL, 0));
		} else {
			return 5;
		}
		fflush(stdout);
	}
	return 0;
}
