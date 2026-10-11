#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define BMLOG_DEBUG_LEVEL 7
#define READ_ONCE(value) (value)
struct mtk_battery { int log_level; };
struct mtk_gauge { struct mtk_battery *gm; };
struct power_supply { struct mtk_gauge *gauge; int refs; };
static struct power_supply supply;
static bool registered;
static struct power_supply *power_supply_get_by_name(const char *name)
{
	if (!registered)
		return NULL;
	supply.refs++;
	return &supply;
}
static void *power_supply_get_drvdata(struct power_supply *psy)
{
	assert(psy == &supply && psy->refs == 1);
	return psy->gauge;
}
static void power_supply_put(struct power_supply *psy)
{
	assert(psy == &supply && psy->refs == 1);
	psy->refs--;
}
