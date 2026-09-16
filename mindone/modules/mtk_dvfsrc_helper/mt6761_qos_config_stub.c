// SPDX-License-Identifier: GPL-2.0
/*
 * Stub for mt6761_qos_config, a foreign-SoC DVFSRC QoS table entry that
 * dvfsrc-helper.c references unconditionally via a static const table
 * covering every MediaTek chip variant (see mt6779/mt6761/mt6768/mt6765
 * entries in the vendor's real dvfsrc-ipi.c). This device is mt6789, whose
 * dispatch entries point elsewhere; the mt6761 row is never selected at
 * runtime. The real dvfsrc-ipi.c additionally needs sspm_ipi.h/
 * sspm_ipi_pin.h (a different IPI channel-table context not wired into
 * this module), so rather than pull that whole file in for one unused
 * table row, provide a zeroed placeholder -- same approach as
 * mt5725_stub.c in mtk_charger_framework for hardware that is not present
 * on this device.
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "dvfsrc-helper.h"

const struct dvfsrc_qos_config mt6761_qos_config = {
	.ipi_pin = NULL,
	.qos_dvfsrc_init = NULL,
};
