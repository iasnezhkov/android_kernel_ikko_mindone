static void error_case(struct mtk_gauge *gauge, bool voltage)
{
	const int errors[] = { -EIO, -ETIMEDOUT, -EACCES };
	for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		int value = 987654;
		register_error = errors[i];
		register_calls = 0;
		expected_register = voltage ? RG_AUXADC_ADC_OUT_FGADC_PCHR : RG_FGADC_ZCV_CON1;
		int ret = voltage ? zcv_get(gauge, NULL, &value) : zcv_current_get(gauge, NULL, &value);
		assert(ret == register_error && register_calls == 1);
		assert(value == 987654);
	}
}

static void valid_cases(struct mtk_gauge *gauge)
{
	int value;
	register_error = 0;
	expected_register = RG_AUXADC_ADC_OUT_FGADC_PCHR;
	register_value = 24000;
	assert(zcv_get(gauge, NULL, &value) == 0 && value == 39550);
	register_value |= 0x8000;
	assert(zcv_get(gauge, NULL, &value) == 0 && value == 39550);
	register_value = 0;
	assert(zcv_get(gauge, NULL, &value) == 0 && value == 0);
	expected_register = RG_FGADC_ZCV_CON1;
	assert(zcv_current_get(gauge, NULL, &value) == 0 && value == 0);
	register_value = 1000;
	assert(zcv_current_get(gauge, NULL, &value) == 0 && value == 3852);
	register_value = 64535;
	assert(zcv_current_get(gauge, NULL, &value) == 0 && value == 3852);
	register_value = 65535;
	assert(zcv_current_get(gauge, NULL, &value) == 0 && value == 0);
	register_value = 1000;
	gauge->gm->fg_cust_data.r_fg_value = 200;
	assert(zcv_current_get(gauge, NULL, &value) == 0 && value == 1926);
}

int main(int argc, char **argv)
{
	struct mtk_battery gm = { .fg_cust_data = { .r_fg_value = 100, .car_tune_value = 1010 } };
	struct mtk_gauge gauge = { .gm = &gm };
	assert(argc == 2);
	if (strcmp(argv[1], "voltage-error") == 0)
		error_case(&gauge, true);
	else if (strcmp(argv[1], "current-error") == 0)
		error_case(&gauge, false);
	else if (strcmp(argv[1], "valid") == 0)
		valid_cases(&gauge);
	else
		assert(false);
	printf("PASS %s\n", argv[1]);
	return 0;
}
