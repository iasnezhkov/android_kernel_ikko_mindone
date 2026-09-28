// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/atomic.h>
#include <linux/list.h>
#include <mali_kbase_fence_defs.h>
#include <mali_kbase.h>

static const char *
kbase_fence_get_driver_name(struct dma_fence *fence)
{
	return kbase_drv_name;
}

static const char *
kbase_fence_get_timeline_name(struct dma_fence *fence)
{
	return kbase_timeline_name;
}

static bool
kbase_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void
kbase_fence_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	snprintf(str, size, "%llu", fence->seqno);
}

const struct dma_fence_ops kbase_fence_ops = {
	.wait = dma_fence_default_wait,
	.get_driver_name = kbase_fence_get_driver_name,
	.get_timeline_name = kbase_fence_get_timeline_name,
	.enable_signaling = kbase_fence_enable_signaling,
	.fence_value_str = kbase_fence_fence_value_str
};

