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

static int run_irq_case(u16 alert, u8 cc_status, u8 role_ctrl, bool toggling,
			unsigned int fail_write, unsigned int fail_read,
			struct regmap *out_map)
{
	static struct tcpci tcpci;
	static struct rt1711h_chip chip;
	static const struct rt1711h_chip_info info = { .rxdz_sel = RT1711H_BMCIO_RXDZSEL };

	memset(out_map, 0, sizeof(*out_map));
	memset(&tcpci, 0, sizeof(tcpci));
	memset(&chip, 0, sizeof(chip));
	set_alert_word(out_map, alert);
	out_map->reg[TCPC_ROLE_CTRL] = role_ctrl;
	out_map->reg[TCPC_CC_STATUS] = cc_status;
	out_map->reg[TCPC_POWER_STATUS_MASK] = 0x04;
	out_map->fail_write_at = fail_write;
	out_map->fail_read_at = fail_read;
	the_port.toggling = toggling;
	chip.info = &info;
	chip.data.regmap = out_map;
	chip.tcpci = &tcpci;
	tcpci.port = &the_port;
	tcpci.data = &chip.data;
	current_map = out_map;
	begin();
	return rt1711h_irq(0, &chip);
}

static int retoggle(void)
{
	static struct regmap map;
	unsigned int hw_tog, term, c1, c2, ptog, drp, icc, rpv, w;
	unsigned long checked = 0;

	for (hw_tog = 0; hw_tog < 2; hw_tog++) {
		for (term = 0; term < 2; term++) {
			for (c1 = 0; c1 < 4; c1++) {
				for (c2 = 0; c2 < 4; c2++) {
					for (ptog = 0; ptog < 2; ptog++) {
						for (drp = 0; drp < 2; drp++) {
							for (icc = 1; icc <= 2; icc++) {
								for (rpv = 0; rpv < 3; rpv++) {
									u8 cc_status = (hw_tog << 5) | (term << 4) | (c2 << 2) | c1;
									u8 role = (drp << 6) | (rpv << 4) | (icc << 2) | icc;
									enum typec_cc_status cc1 = tcpci_to_typec_cc(c1, term || tcpc_presenting_rd(role, CC1));
									enum typec_cc_status cc2 = tcpci_to_typec_cc(c2, term || tcpc_presenting_rd(role, CC2));
									bool want_retoggle = !hw_tog && ptog && drp &&
										((cc1 == TYPEC_CC_OPEN && cc2 == TYPEC_CC_OPEN) ||
										 (cc1 == TYPEC_CC_RA && cc2 == TYPEC_CC_OPEN) ||
										 (cc1 == TYPEC_CC_OPEN && cc2 == TYPEC_CC_RA));
									u8 base = (rpv << 4) | (2 << 2) | 2;
									char expected_seq[128];
									int ret = run_irq_case(TCPC_ALERT_CC_STATUS, cc_status, role, ptog, 0, 0, &map);

									checked++;
									if (want_retoggle) {
										snprintf(expected_seq, sizeof(expected_seq),
											 "raw_write 10 1 1\nraw_write 1a 1 %x\nsleep\nraw_write 1a 1 %x\nraw_write 23 1 99\n",
											 base, base | (u8)TCPC_ROLE_CTRL_DRP);
										if (ret != IRQ_HANDLED || alert_word(&map) != 0 ||
										    map.reg[TCPC_ROLE_CTRL] != (base | TCPC_ROLE_CTRL_DRP) ||
										    map.reg[TCPC_COMMAND] != TCPC_CMD_LOOK4CONNECTION ||
										    map.sleeps != 1 || strstr(trace, "tcpm_cc_change") ||
										    !strstr(trace, expected_seq)) {
											printf("retoggle fail cc=%02x role=%02x ret=%d alert=%04x rc=%02x cmd=%02x\n%s",
											       cc_status, role, ret, alert_word(&map), map.reg[TCPC_ROLE_CTRL],
											       map.reg[TCPC_COMMAND], trace);
											return 1;
										}
									} else if (hw_tog) {
										if (alert_word(&map) != 0 || map.reg[TCPC_COMMAND] == TCPC_CMD_LOOK4CONNECTION ||
										    strstr(trace, "tcpm_cc_change")) {
											printf("hw_tog fail cc=%02x role=%02x\n%s", cc_status, role, trace);
											return 1;
										}
									} else {
										if (ret != IRQ_HANDLED || map.reg[TCPC_COMMAND] == TCPC_CMD_LOOK4CONNECTION ||
										    !strstr(trace, "tcpm_cc_change 7\n")) {
											printf("attach fail cc=%02x role=%02x ptog=%u drp=%u ret=%d\n%s",
											       cc_status, role, ptog, drp, ret, trace);
											return 1;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (run_irq_case(TCPC_ALERT_CC_STATUS | TCPC_ALERT_POWER_STATUS, 0x01, 0x4a, true, 0, 0, &map) != IRQ_HANDLED ||
	    alert_word(&map) != TCPC_ALERT_POWER_STATUS || map.reg[TCPC_COMMAND] != TCPC_CMD_LOOK4CONNECTION ||
	    strstr(trace, "tcpm_cc_change")) {
		printf("co-occurring power alert lost or mishandled: alert=%04x\n%s", alert_word(&map), trace);
		return 1;
	}
	checked++;

	for (w = 1; w <= 4; w++) {
		run_irq_case(TCPC_ALERT_CC_STATUS, 0x01, 0x4a, true, w, 0, &map);
		if (map.reg[TCPC_COMMAND] == TCPC_CMD_LOOK4CONNECTION) {
			printf("write failure %u unexpectedly issued LOOK4CONNECTION\n%s", w, trace);
			return 1;
		}
		if (w == 1 && !strstr(trace, "tcpm_cc_change 7\n")) {
			printf("write failure 1 did not fall back to rt1711h_alert\n%s", trace);
			return 1;
		}
		checked++;
	}

	if (run_irq_case(TCPC_ALERT_CC_STATUS, 0x01, 0x4a, true, 0, 3, &map) != IRQ_HANDLED ||
	    map.reg[TCPC_COMMAND] == TCPC_CMD_LOOK4CONNECTION || !strstr(trace, "tcpm_cc_change 7\n")) {
		printf("role read failure did not fall back to rt1711h_alert\n%s", trace);
		return 1;
	}
	checked++;

	compared = checked;
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
	else if (!strcmp(argv[1], "retoggle"))
		failed = retoggle();
	else
		return 2;
	if (failed)
		return 1;
	printf("PASS %s: %lu scenarios, identical register accesses, tcpm calls and return values\n", argv[1], compared);
	return 0;
}
