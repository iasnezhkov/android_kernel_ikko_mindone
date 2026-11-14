/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MD_COOLING_TRACE_H
#define _MD_COOLING_TRACE_H

#include "md_cooling.h"

static inline void trace_md_mutt_limit(struct md_cooling_device *md_cdev,
				       enum md_cooling_status status)
{
}

static inline void trace_md_tx_pwr_limit(struct md_cooling_device *md_cdev,
					 enum md_cooling_status status)
{
}

static inline void trace_md_scg_off(struct md_cooling_device *md_cdev,
				    enum md_cooling_status status)
{
}

#endif
