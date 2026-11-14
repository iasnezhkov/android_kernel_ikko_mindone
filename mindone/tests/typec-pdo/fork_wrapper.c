/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
int fork_request(const u32 *snk, int nsnk, const u32 *src, int nsrc, struct request *out)
{
	static struct pd_port port;
	struct pd_port_power_caps source;
	struct dpm_rdo_info_t req;

	memset(&port, 0, sizeof(port));
	memset(&source, 0, sizeof(source));
	memset(&req, 0, sizeof(req));
	port.local_snk_cap.nr = nsnk;
	memcpy(port.local_snk_cap.pdos, snk, nsnk * sizeof(u32));
	source.nr = nsrc;
	memcpy(source.pdos, src, nsrc * sizeof(u32));
	if (!dpm_build_request_info_pdo(&port, &req, &source, FORK_POLICY))
		return -1;
	out->pos = req.pos;
	out->mv = req.vmax;
	out->ma = req.oper_ma;
	out->max_ma = req.max_ma;
	out->mismatch = req.mismatch;
	return 0;
}
