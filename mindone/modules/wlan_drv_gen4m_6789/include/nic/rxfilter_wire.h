/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef MINDONE_RXFILTER_WIRE_H
#define MINDONE_RXFILTER_WIRE_H

#include "rxfilter_control.h"

static inline int rxfilter_wire_reply(const u8 *packet, unsigned int length,
		u8 *sequence, const u8 **payload)
{
	if (!packet || length < 8 || packet[4] != 0xfd)
		return 0;
	if (length < 20 || packet[0] != 20 || packet[1] ||
	    packet[2] || packet[3] != 0xe0 || packet[6] || packet[7] ||
	    packet[9] != 0xc1 || packet[10] != 1 || packet[11] || packet[19])
		return -EPROTO;
	*sequence = packet[5];
	*payload = packet + 8;
	return 1;
}

#endif
