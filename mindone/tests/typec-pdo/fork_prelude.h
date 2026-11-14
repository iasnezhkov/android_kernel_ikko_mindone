/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define CONFIG_USB_PD_REV30_PPS_SINK 0
#define CONFIG_USB_PD_ALT_MODE_RTDC 0
#define CONFIG_USB_PD_REV30 0
#define TCPC_VBUS_SINK_5V 5000
#define DPM_DBG(...) ((void)0)
#define PDO_MAX_NR 7
struct pd_port_power_caps { uint8_t nr; uint32_t pdos[PDO_MAX_NR]; };
struct tcpc_device { int unused; };
struct pd_port {
	struct pd_port_power_caps local_snk_cap;
	struct { uint8_t local_selected_cap; } pe_data;
	struct tcpc_device *tcpc;
};
