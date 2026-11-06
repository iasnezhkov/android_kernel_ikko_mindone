// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>

#include <musb_core.h>
#include "usb20.h"

void mt_usb_clock_prepare(void)
{
	usb_prepare_clock(true);
}

void mt_usb_clock_unprepare(void)
{
	usb_prepare_clock(false);
}

void usb_phy_context_save(void) { }
void usb_phy_context_restore(void) { }

bool in_uart_mode;
EXPORT_SYMBOL(in_uart_mode);

bool usb_phy_check_in_uart_mode(void)
{
	return false;
}

bool apple;
EXPORT_SYMBOL(apple);

int polling_vbus_value(void *data)
{
	return 0;
}

int mtk_audio_request_sram(dma_addr_t *phys_addr, unsigned char **virt_addr,
			   unsigned int length, void *user)
{
	return -ENOMEM;
}

void mtk_audio_free_sram(void *user) { }

int mt_usb_dual_role_changed(struct musb *musb)
{
	return 0;
}
