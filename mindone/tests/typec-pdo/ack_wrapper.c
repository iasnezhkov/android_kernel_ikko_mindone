/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
int ack_request(const u32 *snk, int nsnk, u32 op_mw, const u32 *src, int nsrc, struct request *out)
{
	static struct tcpm_port port;
	u32 rdo;
	int ret;

	memset(&port, 0, sizeof(port));
	memcpy(port.snk_pdo, snk, nsnk * sizeof(u32));
	port.nr_snk_pdo = nsnk;
	memcpy(port.source_caps, src, nsrc * sizeof(u32));
	port.nr_source_caps = nsrc;
	port.operating_snk_mw = op_mw;
	ret = tcpm_pd_build_request(&port, &rdo);
	if (ret)
		return ret;
	out->pos = (rdo >> RDO_OBJ_POS_SHIFT) & RDO_OBJ_POS_MASK;
	out->mv = port.req_supply_voltage;
	out->ma = port.req_current_limit;
	out->max_ma = ((rdo >> RDO_FIXED_MAX_CURR_SHIFT) & RDO_CURR_MASK) * 10;
	out->mismatch = !!(rdo & RDO_CAP_MISMATCH);
	out->usb_comm = !!(rdo & RDO_USB_COMM);
	return 0;
}
