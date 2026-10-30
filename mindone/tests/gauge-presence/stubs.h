/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define bm_err(...) mock_log(__VA_ARGS__)
#define bm_debug(...) mock_log(__VA_ARGS__)
#define IRQ_HANDLED 1
#define POWER_SUPPLY_STATUS_UNKNOWN 0
#define POWER_SUPPLY_TECHNOLOGY_LION 1
#define POWER_SUPPLY_HEALTH_GOOD 2
#define GAUGE_PROP_BATTERY_EXIST 3
#define BAT_PROP_DISABLE 4
#define FG_INTR_BAT_PLUGOUT 5
#define EVT_INT_BAT_PLUGOUT 6
#define BAT_PLUGOUT_IRQ 7
#define IS_ERR_OR_NULL(pointer) (!(pointer))
typedef int irqreturn_t;
struct power_supply { int unused; };
union power_supply_propval { int intval; };
struct battery_data {
	struct power_supply *psy;
	struct power_supply *chg_psy;
	int bat_technology, bat_health, bat_present, bat_capacity, bat_status;
};
struct mtk_battery {
	struct battery_data bs_data;
	bool is_probe_done, disableGM30, disable_plug_int;
	struct { bool active; } algo;
	int ui_soc, plug_miss_count;
	void *gauge;
};
struct message { int fgd_data_len; char fgd_data[sizeof(int)]; };
static int error, present, publications, poweroffs, daemon_events, kernel_events;
static int reports, disabled_irqs, reads;
static bool irq_allowed = true;
static struct power_supply supply;
static void mock_log(const char *format, ...) {}
static int gauge_get_property(int property, int *value)
{
	assert(property == GAUGE_PROP_BATTERY_EXIST);
	reads++;
	if (error)
		return error;
	*value = present;
	return 1;
}
int gauge_get_int_property(int property)
{
	int value = 0;
	gauge_get_property(property, &value);
	return value;
}
static void battery_update_psd(struct mtk_battery *gm) {}
static int battery_get_int_property(int property)
{
	assert(property == BAT_PROP_DISABLE);
	return 0;
}
static bool is_algo_active(struct mtk_battery *gm) { return gm->algo.active; }
static int mtk_battery_get_charge_now_uah(struct mtk_battery *gm) { return 0; }
static int mtk_battery_status_from_chg(struct power_supply *supply,
	union power_supply_propval *online, union power_supply_propval *status)
{
	return 1;
}
static void power_supply_changed(struct power_supply *supply) { publications++; }
static void kernel_power_off(void) { poweroffs++; }
static void wakeup_fg_algo(struct mtk_battery *gm, int event)
{
	assert(event == FG_INTR_BAT_PLUGOUT);
	if (gm->algo.active)
		kernel_events++;
	else
		daemon_events++;
}
static bool fg_interrupt_check(struct mtk_battery *gm) { return irq_allowed; }
static void fg_int_event(struct mtk_battery *gm, int event)
{
	assert(event == EVT_INT_BAT_PLUGOUT);
	reports++;
}
static void disable_gauge_irq(void *gauge, int irq)
{
	assert(irq == BAT_PLUGOUT_IRQ);
	disabled_irqs++;
}
