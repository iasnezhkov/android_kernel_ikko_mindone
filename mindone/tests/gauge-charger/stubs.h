#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	POWER_SUPPLY_STATUS_UNKNOWN,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
	POWER_SUPPLY_PROP_ONLINE,
	FG_DAEMON_CMD_IS_CHARGER_EXIST,
	FG_DAEMON_CMD_GET_CHARGER_STATUS,
};

struct power_supply { int online; };
union power_supply_propval { int intval; };
struct mtk_battery {
	struct {
		struct power_supply *chg_psy;
		int bat_status;
	} bs_data;
};
struct response {
	unsigned int fgd_data_len;
	unsigned char fgd_data[8];
};

static int read_error;
static int read_count;

#define IS_ERR_OR_NULL(pointer) (!(pointer) || (uintptr_t)(pointer) >= UINTPTR_MAX - 4095)
#define bm_debug(...) ((void)0)

static int power_supply_get_property(struct power_supply *psy, int property,
	union power_supply_propval *value)
{
	assert(!IS_ERR_OR_NULL(psy));
	assert(property == POWER_SUPPLY_PROP_ONLINE);
	read_count++;
	if (read_error)
		return read_error;
	value->intval = psy->online;
	return 0;
}
