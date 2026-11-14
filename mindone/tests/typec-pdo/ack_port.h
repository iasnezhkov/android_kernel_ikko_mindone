/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
enum { POWER_SUPPLY_USB_TYPE_C = 5, POWER_SUPPLY_USB_TYPE_PD, POWER_SUPPLY_USB_TYPE_PD_DRP, POWER_SUPPLY_USB_TYPE_PD_PPS,
       POWER_SUPPLY_USB_TYPE_PD_SPR_AVS, POWER_SUPPLY_USB_TYPE_PD_PPS_SPR_AVS };
enum { TYPEC_SINK, TYPEC_SOURCE };
struct tcpm_port {
	struct { bool supported; } pps_data;
	struct { int port_partner_src_status; int port_partner_src_pdo_index; int port_snk_status;
		 int port_snk_pdo_index; } spr_avs_data;
	int usb_type;
	void *psy;
	unsigned int nr_source_caps;
	u32 source_caps[PDO_MAX_OBJECTS];
	unsigned int nr_snk_pdo;
	u32 snk_pdo[PDO_MAX_OBJECTS];
	unsigned int operating_snk_mw;
	int cc_req, cc1, cc2, vbus_source, vconn_role, polarity;
	u32 req_current_limit, req_supply_voltage;
};
#define tcpm_log(port, ...) ((void)(port))
#define power_supply_changed(psy) ((void)(psy))
