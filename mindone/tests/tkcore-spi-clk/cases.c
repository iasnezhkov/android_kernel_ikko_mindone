// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
int main(void)
{
	struct clk spi0 = { .id = 19 };
	void (*en_fn)(const void *) = tkcore_spi_clk_enable;
	void (*dis_fn)(const void *) = tkcore_spi_clk_disable;

	assert(tee_clkmgr_register1("spi", 0, en_fn, dis_fn, &spi0) == 0);
	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 0), TEE_CLKMGR_OP_ENABLE) == 0);
	assert(enable_calls == 1 && last_enabled_clk == &spi0 && disable_calls == 0);

	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 0), 0) == 0);
	assert(disable_calls == 1 && last_disabled_clk == &spi0);

	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 1), TEE_CLKMGR_OP_ENABLE) == TEEC_ERROR_ITEM_NOT_FOUND);

	enable_fail_rc = -EIO;
	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 0), TEE_CLKMGR_OP_ENABLE) == 0);
	assert(enable_calls == 2 && last_enabled_clk == &spi0);

	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 0), 0) == 0);
	assert(disable_calls == 1);

	assert(tee_clkmgr_unregister("spi", 0) == 0);
	assert(tee_clkmgr_unregister("spi", 0) == -ENOENT);
	assert(tee_clkmgr_handle(TEE_CLKMGR_TOKEN(0, 0), TEE_CLKMGR_OP_ENABLE) == TEEC_ERROR_ITEM_NOT_FOUND);
	assert(clk_list.next == &clk_list);
	printf("PASS tkcore-spi-clk dispatch\n");
	return 0;
}
