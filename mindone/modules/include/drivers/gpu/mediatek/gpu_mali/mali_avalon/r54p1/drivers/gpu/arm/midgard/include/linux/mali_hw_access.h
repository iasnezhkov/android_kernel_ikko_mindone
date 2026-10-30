/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_HW_ACCESS_H_
#define _MALI_HW_ACCESS_H_

#include <asm/arch_timer.h>
#include <linux/io.h>

#define mali_readl(addr) readl(addr)

#define mali_writel(val, addr) writel(val, addr)

#define mali_readq(addr) ((u64)mali_readl(addr) | ((u64)mali_readl(addr + 4) << 32))

static inline u64 mali_readq_coherent(const void __iomem *addr)
{
	u32 hi1, hi2, lo;

	do {
		hi1 = mali_readl(addr + 4);
		lo = mali_readl(addr);
		hi2 = mali_readl(addr + 4);
	} while (hi1 != hi2);

	return lo | (((u64)hi1) << 32);
}

#define mali_writeq(val, addr)                       \
	do {                                         \
		mali_writel(val & 0xFFFFFFFF, addr); \
		mali_writel(val >> 32, addr + 4);    \
	} while (0)

#define mali_ioremap(addr, size) ioremap(addr, size)

#define mali_iounmap(addr) iounmap(addr)

#define mali_arch_timer_get_cntfrq() arch_timer_get_cntfrq()

#endif
