/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
int main(void)
{
	struct device dev = {};
	struct mtk_btcvsd_snd_stream tx = {};
	struct mtk_btcvsd_snd_stream rx = {};
	u8 sram_bank2[65536] = {};
	u32 reg_pkt_w = 0;
	u32 reg_pkt_r = 0;
	u32 reg_ctl = 0;
	struct mtk_btcvsd_snd bt = {
		.dev = &dev,
		.bt_sram_bank2_base = sram_bank2,
		.bt_reg_pkt_w = &reg_pkt_w,
		.bt_reg_pkt_r = &reg_pkt_r,
		.bt_reg_ctl = &reg_ctl,
		.tx = &tx,
		.rx = &rx,
		.band = BT_SCO_NB,
	};

	tx.buffer_info.packet_length = 60;
	tx.buffer_info.packet_num = 3;
	tx.buffer_info.num_valid_addr = 1;

	reg_pkt_w = 0xdeadfeedU;
	transfer_calls = 0;
	assert(btcvsd_tx_clean_buffer(&bt) == -EIO);
	assert(transfer_calls == 0);
	assert(bt.write_tx == 0);

	reg_pkt_w = 0x18080100U;
	assert(btcvsd_tx_clean_buffer(&bt) == 0);
	assert(transfer_calls == 1);
	assert(last_transfer_dst == (void *)(sram_bank2 + 0x0100));
	assert(bt.write_tx == 1);

	bt.bypass_bt_access = 1;
	bt.write_tx = 0;
	transfer_calls = 0;
	assert(btcvsd_tx_clean_buffer(&bt) == -EIO);
	assert(transfer_calls == 0);
	bt.bypass_bt_access = 0;

	reg_ctl = BT_CVSD_TX_NREADY | BT_CVSD_RX_READY | BT_CVSD_INTERRUPT;
	assert((BT_CVSD_CLEAR & BT_CVSD_TX_UNDERFLOW) == 0);

	printf("PASS btcvsd\n");
	return 0;
}
