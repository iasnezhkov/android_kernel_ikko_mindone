int main(void)
{
	struct platform_device pdev = { 0 };
	struct mtk_gauge gauge = { .pdev = &pdev };
	const int errors[] = { -EIO, -ETIMEDOUT, -EACCES };
	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		int value = 444;
		reg_error = errors[i];
		assert(fgauge_get_info(&gauge, GAUGE_PROP_CON0_SOC, &value) == errors[i]);
		assert(value == 444);
		assert(battery_exist_get(&gauge, NULL, &value) == errors[i]);
		assert(value == 444);
		reg_error = 0;
		cell_get_error = errors[i];
		assert(rtc_ui_soc_get(&gauge, NULL, &value) == errors[i]);
		assert(value == 444 && cell_refs == 0 && buffers == 0);
		cell_get_error = 0;
		cell_read_error = errors[i];
		assert(rtc_ui_soc_get(&gauge, NULL, &value) == errors[i]);
		assert(value == 444 && cell_refs == 0 && buffers == 0);
		cell_read_error = 0;
	}
	puts("PASS native PMIC and RTC errno propagation with unchanged output and balanced ownership");
	int value = 444;
	cell_length = 0;
	assert(rtc_ui_soc_get(&gauge, NULL, &value) == -ENODATA);
	assert(value == 444 && cell_refs == 0 && buffers == 0);
	cell_length = 1;
	cell_value = 0;
	assert(rtc_ui_soc_get(&gauge, NULL, &value) == 0 && value == 0);
	cell_value = 0xc1;
	assert(rtc_ui_soc_get(&gauge, NULL, &value) == 0 && value == 65);
	cell_length = 2;
	assert(rtc_ui_soc_get(&gauge, NULL, &value) == 0 && value == 65);
	assert(cell_refs == 0 && buffers == 0);
	puts("PASS empty RTC rejection and legal zero/masked/multibyte RTC samples");
	reg_value = 65U << 9;
	assert(fgauge_get_info(&gauge, GAUGE_PROP_CON0_SOC, &value) == 0 && value == 65);
	reg_value = 0;
	assert(fgauge_get_info(&gauge, GAUGE_PROP_CON0_SOC, &value) == 0 && value == 0);
	assert(battery_exist_get(&gauge, NULL, &value) == 0 && value == 1);
	reg_value = AD_BATON_UNDET_MASK << AD_BATON_UNDET_SHIFT;
	assert(battery_exist_get(&gauge, NULL, &value) == 0 && value == 0);
	puts("PASS native CON0 and battery-presence register masks");
	return 0;
}
