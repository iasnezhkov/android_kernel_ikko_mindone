// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(void)
{
	memset(&gauge, 0, sizeof(gauge));
	memset(&battery, 0, sizeof(battery));
	memset(steps, 0, sizeof(steps));
	battery.gauge = &gauge;
	battery.sw_iavg = -300;
	version = GAUGE_HW_V2000;
	next_step = 0;
}

static int request(int command)
{
	struct reply reply = { 0 };
	int value;
	dispatch(&battery, command, &reply);
	assert(reply.fgd_data_len == 4);
	memcpy(&value, reply.fgd_data, sizeof(value));
	return value;
}

int main(int argc, char **argv)
{
	_Static_assert(sizeof(int) == 4, "GM30 scalar replies require four bytes");
	assert(argc == 2);
	reset();
	if (!strcmp(argv[1], "valid-to-invalid")) {
		steps[0] = (struct step){ 0, -200, 1 };
		steps[1] = (struct step){ 0, -900, 0 };
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == -200);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 1);
		assert(gauge.fg_hw_info.current_avg_valid == 0);
	} else if (!strcmp(argv[1], "invalid-to-valid")) {
		steps[0] = (struct step){ 0, -200, 0 };
		steps[1] = (struct step){ 0, -900, 1 };
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == -200);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 0);
		assert(gauge.fg_hw_info.current_avg_valid == 1);
	} else if (!strcmp(argv[1], "io-error") || !strcmp(argv[1], "no-data")) {
		gauge.fg_hw_info.current_avg_valid = 1;
		gauge.fg_hw_info.current_avg = -600;
		steps[0] = (struct step){ !strcmp(argv[1], "io-error") ? -5 : -61, -200, 0 };
		steps[1] = (struct step){ 0, -900, 1 };
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == 0);
		assert(gauge.fg_hw_info.current_avg == -600);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 1);
	} else if (!strcmp(argv[1], "status-read-error")) {
		steps[0] = (struct step){ 0, -200, 1 };
		steps[1] = (struct step){ -5, -900, 0 };
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == -200);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 1);
	} else if (!strcmp(argv[1], "intervening-reader")) {
		steps[0] = (struct step){ 0, -200, 1 };
		steps[1] = (struct step){ 0, -900, 1 };
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == -200);
		gauge.fg_hw_info.current_avg_valid = 0;
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 0);
	} else if (!strcmp(argv[1], "software-version")) {
		version = GAUGE_HW_V1000;
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == -300);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 1);
		assert(next_step == 0);
	} else {
		assert(!strcmp(argv[1], "disabled"));
		battery.disableGM30 = true;
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_AVG) == 0);
		assert(request(FG_DAEMON_CMD_GET_FG_CURRENT_IAVG_VALID) == 0);
		assert(next_step == 0);
	}
	puts(argv[1]);
	return 0;
}
