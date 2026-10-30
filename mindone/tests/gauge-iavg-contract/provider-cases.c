// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static void reset(struct mtk_gauge *gauge, struct mtk_battery *battery)
{
	memset(gauge, 0, sizeof(*gauge));
	memset(battery, 0, sizeof(*battery));
	memset(counts, 0, sizeof(counts));
	gauge->gm = battery;
	gauge->hw_status.r_fg_value = 100;
	gauge->fg_hw_info.current_avg = -777;
	gauge->fg_hw_info.current_1 = -777;
	battery->fg_cust_data.car_tune_value = 1010;
	fault = OPERATION_COUNT;
	fault_number = 1;
	fault_errno = -EIO;
	pre_timeout = post_timeout = clearing = latched = false;
	average_reads = 0;
	hardware_valid = 1;
	raw_average = 1000;
	raw_instant = 100;
}

int main(int argc, char **argv)
{
	struct mtk_gauge gauge;
	struct mtk_battery battery;
	int output = 987654;
	int result;

	assert(argc == 2);
	reset(&gauge, &battery);
	if (!strcmp(argv[1], "signed-negative"))
		raw_average = 0x10000000 - 1000;
	else if (!strcmp(argv[1], "valid-zero-fallback") || !strcmp(argv[1], "fallback-read-error")) {
		hardware_valid = 0;
		if (!strcmp(argv[1], "fallback-read-error"))
			fault = INSTANT_READ;
	} else if (!strcmp(argv[1], "pre-write-error"))
		fault = PRE_WRITE;
	else if (!strcmp(argv[1], "pre-read-error"))
		fault = PRE_READ;
	else if (!strcmp(argv[1], "pre-timeout"))
		pre_timeout = true;
	else if (!strcmp(argv[1], "valid-read-error"))
		fault = VALID_READ;
	else if (!strcmp(argv[1], "upper-read-error")) {
		fault = UPPER_READ;
		raw_average = 0x10000 + 1000;
	}
	else if (!strcmp(argv[1], "lower-read-error"))
		fault = LOWER_READ;
	else if (!strcmp(argv[1], "clear-write-error"))
		fault = CLEAR_WRITE;
	else if (!strcmp(argv[1], "clear-read-error"))
		fault = CLEAR_READ;
	else if (!strcmp(argv[1], "clear-timeout"))
		post_timeout = true;
	else if (!strcmp(argv[1], "final-clear-error"))
		fault = FINAL_CLEAR;
	else
		assert(!strcmp(argv[1], "signed-positive"));

	result = average_current_get(&gauge, NULL, &output);
	assert(result == 0 && counts[FINAL_CLEAR] >= 1);
	assert(counts[PRE_READ] <= 1000 && counts[CLEAR_READ] <= 1001);
	if (fault != OPERATION_COUNT)
		assert(counts[fault] >= (unsigned int)fault_number);
	if (!strcmp(argv[1], "signed-positive"))
		assert(output == 1926 && gauge.fg_hw_info.current_avg_valid == 1 && gauge.fg_hw_info.current_avg_sign == 0);
	else if (!strcmp(argv[1], "signed-negative"))
		assert(output == -1926 && gauge.fg_hw_info.current_avg_valid == 1 && gauge.fg_hw_info.current_avg_sign == 1);
	else if (!strcmp(argv[1], "valid-zero-fallback"))
		assert(output == 384 && gauge.fg_hw_info.current_1 == 384 && gauge.fg_hw_info.current_avg_valid == 0);
	else if (!strcmp(argv[1], "fallback-read-error"))
		assert(output == -777 && gauge.fg_hw_info.current_1 == -777 && gauge.fg_hw_info.current_avg_valid == 0);
	else if (!strcmp(argv[1], "lower-read-error"))
		assert(output == 0 && gauge.fg_hw_info.current_avg_valid == 1);
	else if (!strcmp(argv[1], "pre-timeout"))
		assert(counts[PRE_READ] == 1000 && output == 1926);
	else if (!strcmp(argv[1], "clear-timeout"))
		assert(counts[CLEAR_READ] == 1000 && output == 1926);
	else if (strcmp(argv[1], "valid-read-error"))
		assert(output == 1926 && gauge.fg_hw_info.current_avg_valid == 1);
	printf("%s rc=%d output=%d valid=%d final_clear=%u\n", argv[1], result, output, gauge.fg_hw_info.current_avg_valid, counts[FINAL_CLEAR]);
	return 0;
}
