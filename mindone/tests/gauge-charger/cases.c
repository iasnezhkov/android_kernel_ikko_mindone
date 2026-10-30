// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static int reply(struct mtk_battery *gm, int command)
{
	struct response response;
	int value;

	memset(&response, 0xa5, sizeof(response));
	response.fgd_data_len = 0;
	dispatch(gm, command, &response);
	assert(response.fgd_data_len == sizeof(value));
	memcpy(&value, response.fgd_data, sizeof(value));
	for (size_t i = sizeof(value); i < sizeof(response.fgd_data); i++)
		assert(response.fgd_data[i] == 0xa5);
	return value;
}

int main(void)
{
	struct power_supply supply = {0};
	struct mtk_battery gm = { .bs_data.chg_psy = &supply };
	const int failures[] = { -EIO, -ETIMEDOUT, -ENODATA, -ENODEV };

	gm.bs_data.bat_status = POWER_SUPPLY_STATUS_FULL;
	supply.online = 1;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 1);
	puts("PASS: Full retains physical charger presence for GM30 tracking");
	for (int status = POWER_SUPPLY_STATUS_UNKNOWN;
		status <= POWER_SUPPLY_STATUS_FULL; status++) {
		gm.bs_data.bat_status = status;
		for (int online = 0; online <= 1; online++) {
			supply.online = online;
			read_count = 0;
			assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == online);
			assert(read_count == 1);
			assert(get_charger_exist(&gm) == online);
			assert(read_count == 2);
			assert(reply(&gm, FG_DAEMON_CMD_GET_CHARGER_STATUS) ==
				(status == POWER_SUPPLY_STATUS_NOT_CHARGING ? -1 : 0));
		}
	}
	puts("PASS: physical ONLINE is independent of all five STATUS values");
	supply.online = 2;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 1);
	assert(get_charger_exist(&gm) == 1);
	puts("PASS: nonzero ONLINE values become one in the boolean protocol");
	gm.bs_data.bat_status = POWER_SUPPLY_STATUS_FULL;
	supply.online = 1;
	for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
		read_error = failures[i];
		assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 0);
		assert(get_charger_exist(&gm) == 0);
	}
	read_error = 0;
	puts("PASS: supplier read errors do not fabricate online presence");
	read_count = 0;
	gm.bs_data.chg_psy = NULL;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 0);
	assert(get_charger_exist(&gm) == 0);
	gm.bs_data.chg_psy = (struct power_supply *)(intptr_t)-ENODEV;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 0);
	assert(get_charger_exist(&gm) == 0);
	assert(read_count == 0);
	puts("PASS: invalid suppliers are not dereferenced");
	gm.bs_data.chg_psy = &supply;
	gm.bs_data.bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 1);
	assert(reply(&gm, FG_DAEMON_CMD_GET_CHARGER_STATUS) == -1);
	gm.bs_data.bat_status = POWER_SUPPLY_STATUS_FULL;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 1);
	assert(reply(&gm, FG_DAEMON_CMD_GET_CHARGER_STATUS) == 0);
	supply.online = 0;
	assert(reply(&gm, FG_DAEMON_CMD_IS_CHARGER_EXIST) == 0);
	puts("PASS: policy-blocked charging and physical unplug remain distinct from Full");
	return 0;
}
