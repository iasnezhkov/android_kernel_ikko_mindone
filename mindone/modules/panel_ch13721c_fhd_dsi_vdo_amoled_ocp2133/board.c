// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "adapter.h"

const struct mindone_panel_step mindone_panel_init_steps[] = {
	PANEL_STEP_GPIO(0, 1),
	PANEL_STEP_SLEEP(20),
	PANEL_STEP_GPIO(0, 0),
	PANEL_STEP_SLEEP(20),
	PANEL_STEP_GPIO(0, 1),
	PANEL_STEP_SLEEP(20),
	PANEL_STEP_GPIO(1, 1),
	PANEL_STEP_SLEEP(10),
	PANEL_STEP_GPIO(2, 1),
	PANEL_STEP_WRITE(0xf0, 0x51),
	PANEL_STEP_WRITE(0xc0, 0x2d),
	PANEL_STEP_WRITE(0xc1, 0x18),
	PANEL_STEP_WRITE(0x35, 0x00),
	PANEL_STEP_BRIGHTNESS,
	PANEL_STEP_WRITE(0x11, 0x00),
	PANEL_STEP_SLEEP(120),
	PANEL_STEP_WRITE(0x29, 0x00),
	PANEL_STEP_SLEEP(50),
};

const size_t mindone_panel_init_count = ARRAY_SIZE(mindone_panel_init_steps);

static int __init mindone_panel_module_init(void)
{
	int status = mindone_panel_driver_register();

	return status > 0 ? -EIO : status;
}

static void __exit mindone_panel_module_exit(void)
{
	mindone_panel_driver_unregister();
}

module_init(mindone_panel_module_init);
module_exit(mindone_panel_module_exit);

MODULE_AUTHOR("MindOne Contributors");
MODULE_DESCRIPTION("CH13721C FHD DSI video AMOLED panel");
MODULE_LICENSE("GPL v2");
