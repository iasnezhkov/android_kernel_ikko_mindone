/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef int irqreturn_t;

#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (64 - 1 - (h))))
#define FIELD_GET(mask, reg) (((reg) & (mask)) >> __builtin_ctzl(mask))
#define FIELD_PREP(mask, val) ((((unsigned long)(val)) << __builtin_ctzl(mask)) & (mask))
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define IRQ_RETVAL(x) ((x) ? IRQ_HANDLED : IRQ_NONE)
#define cpu_to_le16(x) ((u16)(x))
#define __packed __attribute__((packed))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define RT1711H_RTCTRL4		0x93
#define RT1711H_BMCIO_RXDZSEL	BIT(0)
#define RT1711H_RTCTRL18	0xAF
#define BMCIO_RXDZEN		BIT(0)

enum typec_cc_status {
	TYPEC_CC_OPEN,
	TYPEC_CC_RA,
	TYPEC_CC_RD,
	TYPEC_CC_RP_DEF,
	TYPEC_CC_RP_1_5,
	TYPEC_CC_RP_3_0,
};

#define tcpc_presenting_rd(reg, cc) \
	(!(TCPC_ROLE_CTRL_DRP & (reg)) && \
	 FIELD_GET(TCPC_ROLE_CTRL_## cc, reg) == TCPC_ROLE_CTRL_CC_RD)

static inline enum typec_cc_status tcpci_to_typec_cc(unsigned int cc, bool sink)
{
	switch (cc) {
	case 0x1:
		return sink ? TYPEC_CC_RP_DEF : TYPEC_CC_RA;
	case 0x2:
		return sink ? TYPEC_CC_RP_1_5 : TYPEC_CC_RD;
	case 0x3:
		return sink ? TYPEC_CC_RP_3_0 : TYPEC_CC_OPEN;
	case 0x0:
	default:
		return TYPEC_CC_OPEN;
	}
}

#define PD_MAX_PAYLOAD 7

struct pd_chunked_ext_message_data {
	__le16 header;
	u8 data[26];
} __packed;

struct pd_message {
	__le16 header;
	union {
		__le32 payload[PD_MAX_PAYLOAD];
		struct pd_chunked_ext_message_data ext_msg;
	};
} __packed;

enum tcpm_transmit_status {
	TCPC_TX_SUCCESS = 0,
	TCPC_TX_DISCARDED = 1,
	TCPC_TX_FAILED = 2,
};

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
};

struct regmap {
	u8 reg[256];
	unsigned int alert_reads;
	unsigned int inject_at;
	u16 inject_bits;
	int fail_alert_read;
	int rx_refill;
	u8 rx_next_cnt;
	u8 last_cnt;
	unsigned int raw_writes;
	unsigned int fail_write_at;
	unsigned int raw_reads;
	unsigned int fail_read_at;
	unsigned int sleeps;
};

struct tcpm_port {
	int id;
	bool toggling;
};

struct device;
struct regulator;
struct tcpc_dev {
	int unused;
};

static char trace[1 << 16];
static size_t trace_len;
static int warn_count;
static struct regmap *current_map;

static void note(const char *fmt, unsigned long a, unsigned long b, unsigned long c)
{
	int n = snprintf(trace + trace_len, sizeof(trace) - trace_len, fmt, a, b, c);

	if (n < 0 || (size_t)n >= sizeof(trace) - trace_len) {
		fprintf(stderr, "trace overflow\n");
		exit(3);
	}
	trace_len += n;
}

#define WARN_ON(cond) ({ int __c = !!(cond); if (__c) { warn_count++; note("WARN\n", 0, 0, 0); } __c; })

static u16 alert_word(struct regmap *map)
{
	return map->reg[0x10] | (map->reg[0x11] << 8);
}

static void set_alert_word(struct regmap *map, u16 value)
{
	map->reg[0x10] = value & 0xff;
	map->reg[0x11] = value >> 8;
}

static int regmap_raw_read(struct regmap *map, unsigned int reg, void *val, size_t len)
{
	u8 *out = val;
	size_t i;

	map->raw_reads++;
	if (map->fail_read_at && map->raw_reads == map->fail_read_at) {
		note("raw_read %lx %lu fail\n", reg, len, 0);
		return -5;
	}
	if (reg == 0x10) {
		map->alert_reads++;
		if (map->inject_at && map->alert_reads == map->inject_at)
			set_alert_word(map, alert_word(map) | map->inject_bits);
		if (map->fail_alert_read && map->alert_reads == (unsigned int)map->fail_alert_read) {
			note("raw_read %lx %lu fail\n", reg, len, 0);
			return -5;
		}
	}
	for (i = 0; i < len; i++)
		out[i] = map->reg[(reg + i) & 0xff];
	note("raw_read %lx %lu %lx\n", reg, len, len == 2 ? (unsigned long)(out[0] | (out[1] << 8)) : out[0]);
	return 0;
}

static int regmap_raw_write(struct regmap *map, unsigned int reg, const void *val, size_t len)
{
	const u8 *in = val;
	size_t i;

	map->raw_writes++;
	if (map->fail_write_at && map->raw_writes == map->fail_write_at) {
		note("raw_write %lx %lu fail\n", reg, len, 0);
		return -5;
	}
	note("raw_write %lx %lu %lx\n", reg, len, len == 2 ? (unsigned long)(in[0] | (in[1] << 8)) : in[0]);
	if (reg == 0x10 && len == 1) {
		map->reg[0x10] &= ~in[0];
		return 0;
	}
	if (reg == 0x10 && len == 2) {
		u16 clear = in[0] | (in[1] << 8);

		set_alert_word(map, alert_word(map) & ~clear);
		if ((clear & BIT(2)) && map->rx_refill) {
			map->rx_refill--;
			map->reg[0x30] = map->rx_next_cnt;
			map->reg[0x32]++;
			set_alert_word(map, alert_word(map) | BIT(2));
		}
		return 0;
	}
	for (i = 0; i < len; i++)
		map->reg[(reg + i) & 0xff] = in[i];
	return 0;
}

static int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val)
{
	u8 next = (map->reg[reg & 0xff] & ~mask) | (val & mask);

	note("update %lx %lx %lx\n", reg, mask, val);
	map->reg[reg & 0xff] = next;
	return 0;
}

static void usleep_range(unsigned long min, unsigned long max)
{
	(void)min;
	(void)max;
	if (current_map)
		current_map->sleeps++;
	note("sleep\n", 0, 0, 0);
}

static bool tcpm_port_is_toggling(struct tcpm_port *port)
{
	return port->toggling;
}

static int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	*val = map->reg[reg & 0xff];
	if (reg == 0x30)
		map->last_cnt = *val;
	note("read %lx %lx\n", reg, *val, 0);
	return 0;
}

static int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	note("write %lx %lx\n", reg, val, 0);
	map->reg[reg & 0xff] = val;
	return 0;
}

static void tcpm_cc_change(struct tcpm_port *port)
{
	note("tcpm_cc_change %lu\n", port->id, 0, 0);
}

static void tcpm_vbus_change(struct tcpm_port *port)
{
	note("tcpm_vbus_change %lu\n", port->id, 0, 0);
}

static void tcpm_tcpc_reset(struct tcpm_port *port)
{
	note("tcpm_tcpc_reset %lu\n", port->id, 0, 0);
}

static void tcpm_pd_hard_reset(struct tcpm_port *port)
{
	note("tcpm_pd_hard_reset %lu\n", port->id, 0, 0);
}

static void tcpm_pd_transmit_complete(struct tcpm_port *port, enum tcpm_transmit_status status)
{
	note("tcpm_pd_transmit_complete %lu %lu\n", port->id, status, 0);
}

static void tcpm_pd_receive(struct tcpm_port *port, const struct pd_message *msg, enum tcpm_transmit_type type)
{
	const u8 *bytes = (const u8 *)msg->payload;
	unsigned int cnt = current_map->last_cnt;
	unsigned int receive_len = cnt > 3 ? cnt - 3 : 0;
	unsigned int i;

	note("tcpm_pd_receive %lu %lx %lu\n", port->id, msg->header, type);
	for (i = 0; i < receive_len && i < sizeof(msg->payload); i++)
		note(" %lx", bytes[i], 0, 0);
	note("\n", 0, 0, 0);
}
