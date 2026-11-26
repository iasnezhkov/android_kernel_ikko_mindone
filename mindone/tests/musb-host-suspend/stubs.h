/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define BIT(n) (1U << (n))
#define __iomem
#define EBUSY 16
#define HZ 250
#define USB_PORT_STAT_CONNECTION 0x0001
#define USB_PORT_STAT_SUSPEND 0x0004
#define MUSB_PORT_STAT_RESUME (1U << 31)
#define MUSB_POWER 0x01
#define MUSB_POWER_RESUME 0x04
#define MUSB_POWER_SUSPENDM 0x01
#define MUSB_DEVCTL 0x60
#define MUSB_DEVCTL_VBUS 0x18
#define OTG_TIME_A_AIDL_BDIS 200
#define RESREG 0x80
#define HSTPWRDWN_OPT BIT(0)
#define USB_DPIDLE_TIMER 1
#define DEFINE_RATELIMIT_STATE(name, i, b) int name = 0
#define __ratelimit(r) (*(r) == 0)
#define pr_debug(...) do { } while (0)
#define IS_ERR_OR_NULL(p) ((p) == NULL)
#define time_after(a, b) ((long)((b) - (a)) < 0)

enum usb_otg_state {
	OTG_STATE_UNDEFINED = 0,
	OTG_STATE_B_IDLE,
	OTG_STATE_B_PERIPHERAL,
	OTG_STATE_B_WAIT_ACON,
	OTG_STATE_B_HOST,
	OTG_STATE_A_IDLE,
	OTG_STATE_A_WAIT_VRISE,
	OTG_STATE_A_WAIT_BCON,
	OTG_STATE_A_HOST,
	OTG_STATE_A_SUSPEND,
};

struct timer_list {
	int pending;
	unsigned long expires;
};

struct delayed_work {
	int pending;
};

struct device {
	struct musb *musb;
};

struct usb_bus {
	int b_hnp_enable;
};

struct usb_otg {
	enum usb_otg_state state;
	struct usb_bus *host;
};

struct usb_phy {
	struct usb_otg *otg;
};

struct musb {
	void __iomem *mregs;
	struct usb_phy *xceiv;
	struct timer_list idle_timer;
	struct timer_list otg_timer;
	unsigned long rh_timer;
	u32 port1_status;
	u8 is_active;
	u8 is_host;
	int a_wait_bcon;
};

struct usb_hcd {
	struct musb *musb;
};

struct regmap {
	u32 regs[0x1000 / 4];
	u32 history[32];
	int history_count;
};

static unsigned long jiffies = 1000;
static struct delayed_work idle_work;
static int dbg0_count;
static int dbg2_count;
static int timer_delete_calls;
static int timer_delete_sync_calls;
static int mod_timer_calls;
static int cancel_delayed_work_sync_calls;
static int udelay_total;
static int dpidle_last;
static int dpidle_calls;
static int power_read_calls;
static int power_clear_after;
static u8 mmio_power;
static u8 mmio_devctl;
static u32 mmio_resreg;
static bool cd_clr_seen_high;
static bool cd_clr_seen_udelay;

#define DBG(level, fmt, ...) do { \
	if ((level) == 0) \
		dbg0_count++; \
	else if ((level) >= 2) \
		dbg2_count++; \
} while (0)

static unsigned long msecs_to_jiffies(unsigned int m)
{
	return m;
}

static unsigned int jiffies_to_msecs(unsigned long j)
{
	return (unsigned int)j;
}

static const char *otg_state_string(enum usb_otg_state state)
{
	switch (state) {
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_WAIT_BCON:
		return "a_wait_bcon";
	case OTG_STATE_A_WAIT_VRISE:
		return "a_wait_vrise";
	case OTG_STATE_B_HOST:
		return "b_host";
	case OTG_STATE_B_WAIT_ACON:
		return "b_wait_acon";
	default:
		return "other";
	}
}

static struct musb *dev_to_musb(struct device *dev)
{
	return dev->musb;
}

static struct musb *hcd_to_musb(struct usb_hcd *hcd)
{
	return hcd->musb;
}

static bool is_host_active(struct musb *musb)
{
	return musb->is_host != 0;
}

static int timer_pending(const struct timer_list *t)
{
	return t->pending;
}

static int timer_delete(struct timer_list *t)
{
	int was = t->pending;

	t->pending = 0;
	timer_delete_calls++;
	return was;
}

static int timer_delete_sync(struct timer_list *t)
{
	int was = t->pending;

	t->pending = 0;
	timer_delete_sync_calls++;
	return was;
}

static int mod_timer(struct timer_list *t, unsigned long expires)
{
	t->pending = 1;
	t->expires = expires;
	mod_timer_calls++;
	return 0;
}

static bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	int was = dwork->pending;

	dwork->pending = 0;
	cancel_delayed_work_sync_calls++;
	return was != 0;
}

static void udelay(unsigned long usecs)
{
	udelay_total += (int)usecs;
	if (cd_clr_seen_high)
		cd_clr_seen_udelay = true;
}

static void mdelay(unsigned long msecs)
{
	udelay_total += (int)(msecs * 1000);
}

static int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	*val = map->regs[reg / 4];
	return 0;
}

static int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	map->regs[reg / 4] = val;
	if (map->history_count < 32)
		map->history[map->history_count++] = val;
	if (reg == 0xf08 && (val & BIT(7)))
		cd_clr_seen_high = true;
	return 0;
}

static u8 musb_readb(void __iomem *addr, unsigned int offset)
{
	(void)addr;
	if (offset == MUSB_POWER) {
		power_read_calls++;
		if (power_clear_after > 0) {
			power_clear_after--;
			if (power_clear_after == 0)
				mmio_power &= ~MUSB_POWER_SUSPENDM;
		}
		return mmio_power;
	}
	if (offset == MUSB_DEVCTL)
		return mmio_devctl;
	return 0;
}

static void musb_writeb(void __iomem *addr, unsigned int offset, u8 data)
{
	(void)addr;
	if (offset == MUSB_POWER)
		mmio_power = data;
}

static u32 musb_readl(void __iomem *addr, unsigned int offset)
{
	(void)addr;
	if (offset == RESREG)
		return mmio_resreg;
	return 0;
}

static void musb_writel(void __iomem *addr, unsigned int offset, u32 data)
{
	(void)addr;
	if (offset == RESREG)
		mmio_resreg = data;
}

static void usb_hal_dpidle_request(int type)
{
	dpidle_last = type;
	dpidle_calls++;
}

static void mt_usb_try_idle(struct musb *musb, unsigned long timeout);

static void musb_platform_try_idle(struct musb *musb, unsigned long timeout)
{
	mt_usb_try_idle(musb, timeout);
}

static int musb_port_suspend(struct musb *musb, bool do_suspend);
