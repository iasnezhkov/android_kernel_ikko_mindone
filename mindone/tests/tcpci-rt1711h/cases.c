/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
struct scenario {
	u16 alert;
	u8 power_mask;
	u8 rx_cnt;
	u16 inject;
	unsigned int inject_at;
	int vsafe0v;
	u8 ext_status;
	int fail_read;
	int refill;
	u8 next_cnt;
};

struct outcome {
	char text[1 << 16];
	size_t len;
	int ret;
	int warns;
	u16 alert_after;
};

static struct tcpm_port the_port = { 7 };

static void load(struct regmap *map, const struct scenario *s)
{
	unsigned int i;

	memset(map, 0, sizeof(*map));
	for (i = 0; i < 256; i++)
		map->reg[i] = (u8)(i * 7 + 3);
	set_alert_word(map, s->alert);
	map->reg[0x14] = s->power_mask;
	map->reg[0x30] = s->rx_cnt;
	map->reg[0x32] = 0xa1;
	map->reg[0x33] = 0x13;
	map->reg[0x20] = s->ext_status;
	map->inject_at = s->inject_at;
	map->inject_bits = s->inject;
	map->fail_alert_read = s->fail_read;
	map->rx_refill = s->refill;
	map->rx_next_cnt = s->next_cnt;
}

static void begin(void)
{
	trace_len = 0;
	trace[0] = 0;
	warn_count = 0;
}

static void finish(struct outcome *o, int ret, struct regmap *map)
{
	memcpy(o->text, trace, trace_len + 1);
	o->len = trace_len;
	o->ret = ret;
	o->warns = warn_count;
	o->alert_after = alert_word(map);
}

static void run_ack(const struct scenario *s, struct outcome *o)
{
	static struct regmap setup_map;
	static struct regmap map;
	static struct tcpci_data data;
	static struct tcpci tcpci;

	memset(&data, 0, sizeof(data));
	memset(&tcpci, 0, sizeof(tcpci));
	data.vbus_vsafe0v = s->vsafe0v;
	tcpci.data = &data;
	tcpci.port = &the_port;
	load(&setup_map, s);
	tcpci.regmap = &setup_map;
	ack_parse_config(&tcpci);
	if (ack_alert_mask(&tcpci) < 0) {
		fprintf(stderr, "ack alert mask setup failed\n");
		exit(4);
	}
	load(&map, s);
	tcpci.regmap = &map;
	data.regmap = &map;
	current_map = &map;
	begin();
	finish(o, ack_tcpci_irq(&tcpci), &map);
}

static void run_module(const struct scenario *s, struct outcome *o)
{
	static struct regmap map;
	static struct tcpci tcpci;
	static struct rt1711h_chip chip;

	memset(&tcpci, 0, sizeof(tcpci));
	memset(&chip, 0, sizeof(chip));
	load(&map, s);
	chip.data.regmap = &map;
	chip.data.vbus_vsafe0v = s->vsafe0v;
	chip.tcpci = &tcpci;
	tcpci.port = &the_port;
	tcpci.regmap = NULL;
	tcpci.data = &chip.data;
	current_map = &map;
	begin();
	finish(o, rt1711h_alert(&chip), &map);
}

static struct outcome ack_out, mod_out;
static unsigned long compared;

static int compare(const struct scenario *s)
{
	run_ack(s, &ack_out);
	run_module(s, &mod_out);
	compared++;
	if (ack_out.ret != mod_out.ret || ack_out.warns != mod_out.warns ||
	    ack_out.alert_after != mod_out.alert_after || ack_out.len != mod_out.len ||
	    memcmp(ack_out.text, mod_out.text, ack_out.len)) {
		printf("MISMATCH alert=%04x power_mask=%02x cnt=%u inject=%04x@%u vsafe0v=%d ext=%02x fail=%d refill=%d/%u\n",
		       s->alert, s->power_mask, s->rx_cnt, s->inject, s->inject_at, s->vsafe0v, s->ext_status,
		       s->fail_read, s->refill, s->next_cnt);
		printf("ack ret=%d warns=%d alert_after=%04x\n%s", ack_out.ret, ack_out.warns, ack_out.alert_after,
		       ack_out.text);
		printf("module ret=%d warns=%d alert_after=%04x\n%s", mod_out.ret, mod_out.warns,
		       mod_out.alert_after, mod_out.text);
		return 1;
	}
	return 0;
}

static const u16 bits[] = { BIT(0), BIT(1), BIT(2), BIT(3), BIT(4), BIT(5), BIT(6), BIT(9), BIT(10), BIT(13) };
static const u8 counts[] = { 0, 3, 4, 7, 31, 32, 40 };

static int exhaustive(void)
{
	unsigned int combo, i, c, v, p;
	struct scenario s;

	for (combo = 0; combo < (1u << 10); combo++) {
		memset(&s, 0, sizeof(s));
		for (i = 0; i < 10; i++)
			if (combo & (1u << i))
				s.alert |= bits[i];
		for (v = 0; v < 2; v++) {
			for (p = 0; p < 2; p++) {
				for (c = 0; c < sizeof(counts); c++) {
					s.vsafe0v = v;
					s.power_mask = p ? 0xff : 0x0c;
					s.ext_status = v ? 1 : 0;
					s.rx_cnt = counts[c];
					if (compare(&s))
						return 1;
				}
			}
		}
	}
	return 0;
}

static int injected(void)
{
	unsigned int a, b, at;
	struct scenario s;

	for (a = 0; a < 10; a++) {
		for (b = 0; b < 10; b++) {
			for (at = 1; at <= 3; at++) {
				memset(&s, 0, sizeof(s));
				s.alert = bits[a];
				s.inject = bits[b];
				s.inject_at = at;
				s.rx_cnt = 10;
				s.power_mask = 0x0c;
				if (compare(&s))
					return 1;
				s.vsafe0v = 1;
				s.ext_status = 1;
				if (compare(&s))
					return 1;
			}
		}
	}
	memset(&s, 0, sizeof(s));
	s.alert = BIT(2) | BIT(6);
	s.rx_cnt = 12;
	s.refill = 2;
	s.next_cnt = 20;
	if (compare(&s))
		return 1;
	memset(&s, 0, sizeof(s));
	s.alert = BIT(0) | BIT(2);
	s.rx_cnt = 9;
	s.fail_read = 2;
	return compare(&s);
}

static int quiet(void)
{
	struct scenario s;

	memset(&s, 0, sizeof(s));
	if (compare(&s))
		return 1;
	if (ack_out.ret != IRQ_NONE) {
		printf("an empty ALERT register is not IRQ_NONE\n");
		return 1;
	}
	s.alert = BIT(9) | BIT(10);
	if (compare(&s))
		return 1;
	if (ack_out.ret != IRQ_NONE) {
		printf("FAULT and RX_BUF_OVF alone are handled although outside the tcpci alert mask\n");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int failed;

	if (argc != 2)
		return 2;
	if (!strcmp(argv[1], "exhaustive"))
		failed = exhaustive();
	else if (!strcmp(argv[1], "injected"))
		failed = injected();
	else if (!strcmp(argv[1], "quiet"))
		failed = quiet();
	else
		return 2;
	if (failed)
		return 1;
	printf("PASS %s: %lu scenarios, identical register accesses, tcpm calls and return values\n", argv[1], compared);
	return 0;
}
